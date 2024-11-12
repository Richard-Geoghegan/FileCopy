#include "c150nastyfile.h" 
#include "c150grading.h"
#include "c150dgmsocket.h"
#include "c150debug.h"

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>          
#include <cerrno>
#include <cstring>          
#include <iostream>            
#include <fstream>        
#include <openssl/sha.h>
#include <iomanip>
#include <string>
#include <vector>
#include <unordered_map>

using namespace C150NETWORK;
struct Packet {
    bool isFile;            /* Whether packet is a FILE or MESSAGE */
    uint32_t packetNum;     /* Global number of each packets */
    uint16_t totalPackets;  /* Count of the total packets being sent for operation (multiple for FILE, 1 for MESSAGE) */
    uint16_t dataSize;      /* Size of valid data written to packetData */
    char packetData[498];   /* Data field of packet: filename for 1st file packet of group, file content, or message */
};

const int maxHashAttempts = 50; /* Compute the hash this many times*/

void copyFile(string sourceDir, string fileName, string targetDir, int nastiness);
bool isFile(string fname);
void checkDirectory(char *dirname);
string makeFileName(string dir, string name);
string computeHash(const string& filepath);
void checkAndPrintMessage(ssize_t readlen, char *msg, ssize_t bufferlen);
Packet createMessagePacket(const string &message);

Packet createDataPacket(bool isFile, 
                        size_t packetNum, 
                        size_t totalPackets, 
                        const char *data, 
                        size_t dataSize);

bool parseResponse(const string &response,
                   const string &expectedCommand,
                   const string &expectedFileName,
                   string &extractedData);

/* Deconstruct a message packet into it's fields */
bool parseResponse(const string &response,
                   const string &expectedCommand,
                   const string &expectedFileName,
                   string &extractedData) {

    size_t posColon = response.find(":");
    size_t posComma = response.find(",");

    if (posColon == string::npos || posComma == string::npos) {
        return false;
    }

    string command = response.substr(0, posColon);
    string fileName = response.substr(posColon + 1, posComma - posColon - 1);
    extractedData = response.substr(posComma + 1);
 
    return (command == expectedCommand && fileName == expectedFileName);
}

/* Create a message packet that is used for end-to-end check */
Packet createMessagePacket(const string &message) {
    Packet packet;
    packet.isFile = false;
    packet.packetNum = 0;
    packet.totalPackets = 0;
    packet.dataSize = message.size();

    if (packet.dataSize > sizeof(packet.packetData)) {
        throw std::runtime_error("Message size exceeds packetData buffer size");
    }

    memset(packet.packetData, 0, sizeof(packet.packetData));
    memcpy(packet.packetData, message.c_str(), packet.dataSize);

    return packet;
}

/* Create a data packet used for sending file */
Packet createDataPacket(bool isFile, size_t packetNum, size_t totalPackets, const char *data, size_t dataSize) {
    Packet packet;
    packet.isFile = isFile;
    packet.packetNum = packetNum;
    packet.totalPackets = totalPackets;
    packet.dataSize = dataSize;

    if (dataSize > sizeof(packet.packetData)) {
        throw runtime_error("Data size exceeds packetData buffer size");
    }

    memcpy(packet.packetData, data, dataSize);
    return packet;
}

/* Write instance of Packet struct over C150DgmSocket */
/* Uses network byte order to send the struct as a buffer */
void writePacket(C150DgmSocket *sock, const Packet &packet) {
    char buffer[512] = {0};
    size_t offset = 0;

    uint8_t isFileByte = packet.isFile ? 1 : 0;
    memcpy(buffer + offset, &isFileByte, sizeof(isFileByte));
    offset += sizeof(isFileByte);

    uint32_t net_packetNum = htonl(packet.packetNum);
    memcpy(buffer + offset, &net_packetNum, sizeof(net_packetNum));
    offset += sizeof(net_packetNum);

    uint16_t net_totalPackets = htons(packet.totalPackets);
    memcpy(buffer + offset, &net_totalPackets, sizeof(net_totalPackets));
    offset += sizeof(net_totalPackets);

    uint16_t net_dataSize = htons(packet.dataSize);
    memcpy(buffer + offset, &net_dataSize, sizeof(net_dataSize));
    offset += sizeof(net_dataSize);

    memcpy(buffer + offset, packet.packetData, packet.dataSize);
    offset += packet.dataSize;

    sock->write(buffer, offset);
}

/* Read from the socket and return a Packet struct */
/* Deserializes buffer and constructs a Packet struct from it */
Packet readPacket(C150DgmSocket *sock) {

    char buffer[512] = {0};
    ssize_t readlen_ssize = sock->read(buffer, sizeof(buffer));
    if (readlen_ssize <= 0) {
        if (sock->timedout()) {
            throw C150NetworkException("Read timed out");
        } else {
            cout << "PACKET READ FAIL" << endl;
            throw C150Exception("Failed to read packet");
        }
    }

    size_t readlen = static_cast<size_t>(readlen_ssize);

    Packet packet;
    size_t offset = 0;
    
    /* Deserialize isFile */
    if (readlen < offset + sizeof(uint8_t)) {
        throw C150Exception("Incomplete packet received (isFile)");
    }
    uint8_t isFileByte;
    memcpy(&isFileByte, buffer + offset, sizeof(isFileByte));
    packet.isFile = (isFileByte != 0);
    offset += sizeof(isFileByte);

    /* Deserialize packetNum */
    if (readlen < offset + sizeof(uint32_t)) {
        throw C150Exception("Incomplete packet received (packetNum)");
    }
    uint32_t net_packetNum;
    memcpy(&net_packetNum, buffer + offset, sizeof(net_packetNum));
    packet.packetNum = ntohl(net_packetNum);
    offset += sizeof(net_packetNum);

    /* Deserialize totalPackets */
    if (readlen < offset + sizeof(uint16_t)) {
        throw C150Exception("Incomplete packet received (totalPackets)");
    }
    uint16_t net_totalPackets;
    memcpy(&net_totalPackets, buffer + offset, sizeof(net_totalPackets));
    packet.totalPackets = ntohs(net_totalPackets);
    offset += sizeof(net_totalPackets);

    /* Deserialise dataSize */
    if (readlen < offset + sizeof(uint16_t)) {
        throw C150Exception("Incomplete packet received (dataSize)");
    }
    uint16_t net_dataSize;
    memcpy(&net_dataSize, buffer + offset, sizeof(net_dataSize));
    packet.dataSize = ntohs(net_dataSize);
    offset += sizeof(net_dataSize);

    /* Check for a buffer overflow */
    if (packet.dataSize > sizeof(packet.packetData)) {
        throw C150Exception("Received packet dataSize exceeds buffer size");
    }

    /* Ensure packet is complete */
    if (readlen < offset + packet.dataSize) {
        throw C150Exception("Incomplete packet received (packetData)");
    }

    memcpy(packet.packetData, buffer + offset, packet.dataSize);
    offset += packet.dataSize;

    memset(packet.packetData + packet.dataSize, 0, sizeof(packet.packetData) - packet.dataSize);

    return packet;
}

/* Read in a file and compute the hash a single time */
string computeHashHelper(const string& filepath, int fileNastiness) {
    NASTYFILE inputFile(fileNastiness);
    unsigned char obuf[20];  // Buffer to hold the hash output
    void* fopenretval;
    struct stat statbuf;
    size_t sourceSize;
    char* buffer;

    /* NEEDSWORK: with sufficiently high file nastiness (4+), the E-E check may be 
    marked as succeeding despite the files not matching due to file nastiness 
    'cancelling out' at the client and server, resulting in the E-E check passing 
    despite actual file contents not matching */

    // Open the file in read binary mode using NASTYFILE
    fopenretval = inputFile.fopen(filepath.c_str(), "rb");
    if (fopenretval == NULL) {
        cerr << "Error opening input file " << filepath
             << " errno=" << strerror(errno) << endl;
        exit(12);
    }

    // Get the file size
    if (lstat(filepath.c_str(), &statbuf) != 0) {
        cerr << "computeHash: Error stating supplied file " << filepath << endl;
        exit(20);
    }

    // Allocate a buffer large enough to hold the entire file
    sourceSize = statbuf.st_size;
    buffer = (char*)malloc(sourceSize);
    if (buffer == nullptr) {
        cerr << "Failed to allocate memory for file reading" << endl;
        exit(1);
    }

    // Read the entire file into the buffer
    size_t bytesRead = inputFile.fread(buffer, 1, sourceSize);
    if (bytesRead != sourceSize) {
        cerr << "Error reading file " << filepath
             << " errno=" << strerror(errno) << endl;
        free(buffer);
        exit(16);
    }

    // Close the file after reading
    if (inputFile.fclose() != 0) {
        cerr << "Error closing input file " << filepath
             << " errno=" << strerror(errno) << endl;
        free(buffer);
        exit(16);
    }

    // Compute the SHA-1 hash using the single call version of SHA1
    SHA1((const unsigned char*)buffer, sourceSize, obuf);

    // Convert the hash to a hexadecimal string
    stringstream hexStream;
    for (int i = 0; i < 20; i++) {
        hexStream << hex << setw(2) << setfill('0') << (int)obuf[i];
    }

    // Free the allocated buffer memory
    free(buffer);

    // Return the hash as a hexadecimal string
    return hexStream.str();
}

/* Compute a file's hash by repeatedly calling computeHashHelper and taking 
    the most common hash code (a measure to account for file nastiness during
    the computation of a single hash code) */string computeHash(const string& filepath, int fileNastiness) {
    unordered_map<string, int> hashMap;

    for (int i = 0; i < maxHashAttempts; i++) {
        string currHash = computeHashHelper(filepath, fileNastiness);
        hashMap[currHash]++;
        // if (hashMap.count(currHash) == 0) { // currHash not yet 
        //     hashMap
        // }
    }

    string mostCommonHash;
    int maxCount = 0;
    for (auto& pair : hashMap) {
        if (pair.second > maxCount) {
            mostCommonHash = pair.first;
            maxCount = pair.second;
        }
    }
    
    return mostCommonHash;
}

// ------------------------------------------------------
//
//                   makeFileName
//
// Put together a directory and a file name, making
// sure there's a / in between
//
// ------------------------------------------------------
string
makeFileName(string dir, string name) {
  stringstream ss;

  ss << dir;
  // make sure dir name ends in /
  if (dir.substr(dir.length()-1,1) != "/")
    ss << '/';
  ss << name;     // append file name to dir
  return ss.str();  // return dir/name
  
}


// ------------------------------------------------------
//
//                   checkDirectory
//
//  Make sure directory exists
//     
// ------------------------------------------------------
void
checkDirectory(char *dirname) {
  struct stat statbuf;  
  if (lstat(dirname, &statbuf) != 0) {
    fprintf(stderr,"Error stating supplied source directory %s\n", dirname);
    exit(8);
  }

  if (!S_ISDIR(statbuf.st_mode)) {
    fprintf(stderr,"File %s exists but is not a directory\n", dirname);
    exit(8);
  }
}


// ------------------------------------------------------
//
//                   isFile
//
//  Make sure the supplied file is not a directory or
//  other non-regular file.
//     
// ------------------------------------------------------
bool
isFile(string fname) {
  const char *filename = fname.c_str();
  struct stat statbuf;  
  if (lstat(filename, &statbuf) != 0) {
    fprintf(stderr,"isFile: Error stating supplied source file %s\n", filename);
    return false;
  }

  if (!S_ISREG(statbuf.st_mode)) {
    fprintf(stderr,"isFile: %s exists but is not a regular file\n", filename);
    return false;
  }
  return true;
}