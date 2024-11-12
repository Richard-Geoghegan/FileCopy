#include "fileutils.h"
#include "c150nastydgmsocket.h"
#include "c150dgmsocket.h"
#include "c150grading.h"
#include <iostream>
#include <fstream>

// Always use namespace C150NETWORK with COMP 150 IDS framework!
using namespace C150NETWORK;

/* Perform end-to-end check on a given file */
bool checkFile(C150DgmSocket *sock, 
              dirent *sourceFile,
              char *sourceDir,
              int attemptNumber,
              int fileNastiness);

/* Send a file from source to destination */
int sendFile(C150DgmSocket *sock,
             const dirent *sourceFile,
             char *targetDir,
             int fileNastiness,
             size_t &packetCount,
             int attemptNumber);

/* Send a message packet until correct response from server is received */
bool sendMessageWithResponse(C150DgmSocket *sock,
                             const string &message,
                             const string &expectedCommand,
                             const string &expectedFileName,
                             string &responseMessage);


/* Return a buffer containing the entire file passed in */
char* readEntireFile(const string &filePath,
                     size_t &fileSize, 
                     int fileNastiness);

/* Continue sending a packet until it is acknowledged */
bool sendPacketWithAck(C150DgmSocket *sock, Packet &packet);

/* Check that correct command line arguments are used */
void parseCommandLineArguments(int argc,
                               char *argv[],
                               int &fileNastiness,
                               int &networkNastiness);

/* Sends a message to the server confirming all files were sent */
void sendFinalMessage(C150DgmSocket *sock);

/* Send a file and perform an end-to-end check */
void processFile(C150DgmSocket *sock, 
                 dirent *sourceFile, 
                 char *sourceDir, 
                 int fileNastiness, 
                 size_t &packetCount);

const int maxPacketDataLength = 498;
const int serverArg = 1;
const int sourceArg = 4;
const int networkNastinessArg = 2;
const int fileNastinessArg = 3;
const int timeOut = 20;
const int maxAttempts = 1000000; /* High number to account for computeHash() time */
const int maxFileSendRetries = 15;

int main(int argc, char *argv[]) {
    GRADEME(argc, argv);
    
    int fileNastiness;
    int networkNastiness;
    parseCommandLineArguments(argc, argv, fileNastiness, networkNastiness);

    DIR *SRC;                 
    struct dirent *sourceFile;
    checkDirectory(argv[sourceArg]);
    SRC = opendir(argv[sourceArg]);

    if (SRC == NULL) {
        fprintf(stderr,"Error opening source directory %s\n", argv[sourceArg]);     
        exit(8);
    }

    try {
        C150DgmSocket *sock = new C150NastyDgmSocket(networkNastiness);
        
        sock->setServerName(argv[serverArg]);  
        sock->turnOnTimeouts(timeOut); 

        size_t packetCount = 0;

        /* Process each file that is not a directory */
        while ((sourceFile = readdir(SRC)) != NULL) { 
            if ((strcmp(sourceFile->d_name, ".") == 0) || (strcmp(sourceFile->d_name, "..")  == 0 )) 
                continue;

            string sourceName = makeFileName(argv[sourceArg], sourceFile->d_name);
            if (!isFile(sourceName)) {
                continue;
            }
            processFile(sock, sourceFile, argv[sourceArg], fileNastiness, packetCount);
        }

        sendFinalMessage(sock); /* Tell server file sends are complete */
    }

    catch (C150NetworkException& e) {
        cerr << argv[0] << ": caught C150NetworkException: " << e.formattedExplanation() << endl;
    }

    closedir(SRC);
}

int sendFile(C150DgmSocket *sock, const dirent *sourceFile, char *sourceDir, int fileNastiness, size_t &packetCount, int attemptNumber) {
    cout << endl;
    *GRADING << "File: " <<  sourceFile->d_name << ", beginning transmission, attempt " << attemptNumber << endl;
    cout << "File: " <<  sourceFile->d_name << ", beginning transmission, attempt " << attemptNumber << endl;

    char *buffer = nullptr;

    string fileName = sourceFile->d_name;
    string sourceDirName = sourceDir;
    string sourceName = makeFileName(sourceDirName, fileName);
    size_t fileSize = 0;

    try {

        buffer = readEntireFile(sourceName, fileSize, fileNastiness);
        int numPackets = ((fileSize + maxPacketDataLength - 1) / maxPacketDataLength) + 1;

        size_t bytesRead = 0;
        size_t bytesRemaining = fileSize;

        /* Attempt to send fileName to server */
        Packet filenamePacket = createDataPacket(true, packetCount, numPackets, fileName.c_str(), fileName.size());
        if (!sendPacketWithAck(sock, filenamePacket)) {
            cerr << "Failed to send filename packet after maximum retries." << endl;
            free(buffer);
            return -1;
        }

        packetCount++;

        /* Break the file down into packets, and send them to the server */
        for (int i = 1; i < numPackets; i++) {

            size_t packetBytes = (bytesRemaining >= maxPacketDataLength) ? maxPacketDataLength : bytesRemaining;
            if (packetBytes > 498) {
                throw new runtime_error("Oversized packetBytes");
            }

            Packet dataPacket = createDataPacket(true, packetCount, numPackets, buffer + bytesRead, packetBytes);

            bytesRead += packetBytes;
            bytesRemaining = fileSize - bytesRead;
            
            if (!sendPacketWithAck(sock, dataPacket)) {
                cerr << "Failed to send data packet " << packetCount << " after maximum retries." << endl;
                free(buffer);
                return -1;
            }

            packetCount++;
        }
    
        free(buffer);
        
    } catch (C150Exception& e) {
        cerr << "nastyfiletest:copyfile(): Caught C150Exception: " << e.formattedExplanation() << endl;
        if (buffer != nullptr) {
            free(buffer);
        }
    }

    *GRADING << "File: " << sourceFile->d_name << " transmission complete, waiting for end-to-end check, attempt " << attemptNumber << endl;
    cout << "File: " << sourceFile->d_name << " transmission complete, waiting for end-to-end check, attempt " << attemptNumber << endl;
    return 1;
}

bool checkFile(C150DgmSocket *sock, dirent *sourceFile, char *sourceDir, int attemptNumber, int fileNastiness)
{
    string fileName = sourceFile->d_name;
    string msgCheck = "CHECK:" + fileName + ",";

    /* Send CHECK message and await for the server HASH */
    string serverHashResponse;
    if (!sendMessageWithResponse(sock, msgCheck, "HASH", fileName ,serverHashResponse)) {
        cerr << "Failed to receive HASH response after maximum attempts." << endl;
        return false;
    }

    /* Break down the server HASH response */
    string serverHash;
    if (!parseResponse(serverHashResponse, "HASH" ,fileName, serverHash)) {
        cerr << "Invalid HASH response or filename mismatch." << endl;
        return false;
    }

    /*Compute client hash and compare with server */
    string clientHash = computeHash(makeFileName(sourceDir, fileName), fileNastiness);

    bool filesMatch = (serverHash == clientHash);
    string resultMessage = filesMatch ? ("RESULT:" + fileName + ",PASS")
                                      : ("RESULT:" + fileName + ",FAIL");

    /* Send RESULT message and wait for LOG response */
    string serverLogResponse;
    if (!sendMessageWithResponse(sock, resultMessage, "LOG", fileName ,serverLogResponse)) {
        cerr << "Failed to receive LOG response after maximum attempts." << endl;
        return false;
    }

    *GRADING << "File: " << fileName << " end-to-end check "
             << (filesMatch ? "succeeded" : "failed") << ", attempt " << attemptNumber << endl;

    cout << "File: " << fileName << " end-to-end check "
            << (filesMatch ? "succeeded" : "failed") << ", attempt " << attemptNumber << endl;

    return filesMatch;
}

bool sendMessageWithResponse(C150DgmSocket *sock,
                             const string &message,
                             const string &expectedCommand,
                             const string &expectedFileName,
                             string &responseMessage)
{
    for (int attempts = 0; attempts < maxAttempts; ++attempts) {
        Packet messagePacket = createMessagePacket(message);
        writePacket(sock, messagePacket);

        try {
            Packet responsePacket = readPacket(sock);
            responseMessage.assign(responsePacket.packetData, responsePacket.dataSize);

            size_t posColon = responseMessage.find(":");
            if (posColon != string::npos) {
                string command = responseMessage.substr(0, posColon);
                if (command == expectedCommand) {
                    string payload = responseMessage.substr(posColon + 1);
                    size_t posComma = payload.find(",");
                    string receivedFileName = (posComma != string::npos)
                        ? payload.substr(0, posComma)
                        : payload;

                    if (receivedFileName == expectedFileName) {
                        return true;
                    }
                }
            }
        } catch (C150NetworkException&) {
            // Timeout occurred, retry
        } catch (C150Exception& e) {
            cerr << "Error: " << e.formattedExplanation() << endl;
            break;
        }
    }
    return false;
}

char* readEntireFile(const string &filePath, size_t &fileSize, int fileNastiness) {
    struct stat statbuf;
    if (lstat(filePath.c_str(), &statbuf) != 0) {
        throw runtime_error("Error stating source file: " + filePath);
    }

    fileSize = statbuf.st_size;
    char *buffer = (char *)malloc(fileSize);
    if (buffer == nullptr) {
        throw runtime_error("Memory allocation failed for file: " + filePath);
    }

    NASTYFILE inputFile(fileNastiness);
    if (inputFile.fopen(filePath.c_str(), "rb") == nullptr) {
        free(buffer);
        throw runtime_error("Error opening input file: " + filePath);
    }

    size_t len = inputFile.fread(buffer, 1, fileSize);
    if (len != fileSize) {
        free(buffer);
        throw runtime_error("Error reading file: " + filePath);
    }

    inputFile.fclose();
    return buffer;
}

bool sendPacketWithAck(C150DgmSocket *sock, Packet &packet) {
    int retries = 0;
    while (retries < maxAttempts) {
        writePacket(sock, packet);
        try {
            Packet response = readPacket(sock);
            if (response.packetNum == packet.packetNum) {
                return true;
            } else {
                retries++;
            }
        } catch (C150NetworkException&) {
            retries++;
        } catch (C150Exception& e) {
            cerr << "Error: " << e.formattedExplanation() << endl;
            break;
        }
    }
    return false;
}

void parseCommandLineArguments(int argc, char *argv[], int &fileNastiness, int &networkNastiness) {
    if (argc != 5) {
        fprintf(stderr, "Correct syntax is: %s <server> <networknastiness> <filenastiness> <srcdir>\n", argv[0]);
        exit(1);
    }

    if (strspn(argv[networkNastinessArg], "0123456789") != strlen(argv[networkNastinessArg])) {
        fprintf(stderr, "Nastiness %s is not numeric\n", argv[networkNastinessArg]);
        fprintf(stderr, "Correct syntax is: %s <server> <networknastiness> <filenastiness> <srcdir>\n", argv[0]);
        exit(4);
    }

    if (strspn(argv[fileNastinessArg], "0123456789") != strlen(argv[fileNastinessArg])) {
        fprintf(stderr, "Nastiness %s is not numeric\n", argv[fileNastinessArg]);
        fprintf(stderr, "Correct syntax is: %s <server> <networknastiness> <filenastiness> <srcdir>\n", argv[0]);
        exit(4);
    }

    fileNastiness = atoi(argv[fileNastinessArg]);
    networkNastiness = atoi(argv[networkNastinessArg]);
}

void sendFinalMessage(C150DgmSocket *sock) {
    string finalMessage = "FINISHED:";
    string responseMessage;
    if (!sendMessageWithResponse(sock, finalMessage, "FINISHED", "", responseMessage)) {
        cerr << "Failed to receive FINISHED acknowledgment after maximum attempts." << endl;
        exit(-1);
    } else {
        cout << endl;
        cout << "Successfully finished sending all files to server." << endl;
    }
}

void processFile(C150DgmSocket *sock, 
                 dirent *sourceFile, 
                 char *sourceDir, 
                 int fileNastiness, 
                 size_t &packetCount)
{
    int fileTransferAttempt = 1;

    sendFile(sock, sourceFile, sourceDir, fileNastiness, packetCount, fileTransferAttempt);

    /* Attempt to send file maxFileSendRetries until end-to-end check succeeds */
    for (int i = 0; i < maxFileSendRetries; i++) {
        if (checkFile(sock, sourceFile, sourceDir, fileTransferAttempt, fileNastiness)) {
            break;
        } else {
            fileTransferAttempt++;
            sendFile(sock, sourceFile, sourceDir, fileNastiness, packetCount, fileTransferAttempt);
        }
    }
}