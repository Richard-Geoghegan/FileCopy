#include "c150nastydgmsocket.h"
#include "c150grading.h"
#include <fstream>
#include <cstdlib> 
#include "fileutils.h"
#include <unordered_set>
#include <cstdio>

using namespace C150NETWORK;  // for all the comp150 utilities 

void parseCommandLineArguments(int argc, char *argv[], int &fileNastiness, int &networkNastiness);

void receiveFilename(int &packetsWrittenToFile, 
                     uint32_t &currentFileNameCounter,
                     Packet &incomingPacket, 
                     string &currentFileName,
                     string &targetName,
                     string &targetDir,
                     unordered_set<string> &logResult,
                     unordered_set<string> &logStart,
                     NASTYFILE& outputFile);

void writeDataToFile(int &packetsWrittenToFile,
                     NASTYFILE& outputFile, 
                     Packet &incomingPacket, 
                     string &targetName);

void acknowledgePacket(C150DgmSocket *sock, Packet &incomingPacket);

void handleFilePacket(C150DgmSocket *sock,
                      int &packetsWrittenToFile, 
                      uint32_t &currentFileNameCounter,
                      uint32_t &currentPacketNumber,
                      Packet &incomingPacket, 
                      string &currentFileName,
                      string &targetName,
                      string &targetDir,
                      unordered_set<string> &logResult,
                      unordered_set<string> &logStart,
                      NASTYFILE& outputFile);

void handleCheck(C150DgmSocket *sock,
                 string &fileName,
                 string &currentFileName,
                 string &targetName,
                 unordered_set<string> &logResult, 
                 int &fileNastiness);

void handleResult(C150DgmSocket *sock, 
                  string &response, 
                  string &currentFileName, 
                  unordered_set<string> &logStart, 
                  string &fileName, 
                  string &targetName, 
                  string &targetDir);

void handleMessagePacket(C150DgmSocket *sock,  
                         string &currentFileName, 
                         unordered_set<string> &logStart,
                         unordered_set<string> &logResult,
                         string &targetName, 
                         string &targetDir,
                         Packet &incomingPacket,
                         int &fileNastiness,
                         uint32_t &currentPacketNumber,
                         uint32_t &currentFileNameCounter,
                         int &packetsWrittenToFile);

const int networkNastinessArg = 1;
const int fileNastinessArg = 2;
const int destArg = 3;

// USAGE: fileserver <networknastiness> <filenastiness> <targetdir>

/* Main Loop: process incoming packets, determine type, and process accordingly */
int main(int argc, char *argv[]) {
    GRADEME(argc, argv);
    
    int fileNastiness;
    int networkNastiness;
    parseCommandLineArguments(argc, argv, fileNastiness, networkNastiness);
    
    try {
        // Create the socket
        C150DgmSocket *sock = new C150NastyDgmSocket(networkNastiness);

        unordered_set<string> logResult;
        unordered_set<string> logStart;

        uint32_t currentPacketNumber = 0; // Track state of the current 'global' packet number exapected (corresponds to packetNum struct in Packet)
        uint32_t currentFileNameCounter = 0; // Next first FILE packet for a file (will contain filename and no data)

        int packetsWrittenToFile = 0;
        
        string targetDir = argv[destArg];

        string currentFileName = "";
        string targetName = "";

        NASTYFILE outputFile(fileNastiness);

        while(1) { 
            Packet incomingPacket = readPacket(sock);
            
            if (incomingPacket.isFile) {
                handleFilePacket(sock, packetsWrittenToFile, currentFileNameCounter,
                                 currentPacketNumber, incomingPacket, currentFileName,
                                 targetName, targetDir, logResult, logStart, outputFile);
            }

            else {
                handleMessagePacket(sock, currentFileName, logStart, logResult,
                                    targetName, targetDir, incomingPacket,
                                    fileNastiness, currentPacketNumber,
                                    currentFileNameCounter, packetsWrittenToFile);
            }

        }
    }

    catch (C150NetworkException& e) {
        cerr << argv[0] << ": caught C150NetworkException: " << e.formattedExplanation() << endl;
    }

    // This only executes if there was an error caught above
    return 4;
}

/* Ensure Command Line Arguments are within expected bounds and values */
void parseCommandLineArguments(int argc, char *argv[], int &fileNastiness, int &networkNastiness) {
    if (argc != 4)  {
        fprintf(stderr,"Correct syntxt is: %s <networknastiness> <filenastiness> <targetdir>\n", argv[0]);
        exit(1);
    }

    if (strspn(argv[networkNastinessArg], "0123456789") != strlen(argv[networkNastinessArg])) {
        fprintf(stderr,"Nastiness %s is not numeric\n", argv[networkNastinessArg]);     
        fprintf(stderr,"Correct syntxt is: %s <networknastiness> <filenastiness> <targetdir>\n", argv[0]);     
        exit(4);
    }

    if (strspn(argv[fileNastinessArg], "0123456789") != strlen(argv[fileNastinessArg])) {
        fprintf(stderr,"Nastiness %s is not numeric\n", argv[fileNastinessArg]);     
        fprintf(stderr,"Correct syntxt is: %s <networknastiness> <filenastiness> <targetdir>\n", argv[0]);     
        exit(4);
    }
    networkNastiness = atoi(argv[networkNastinessArg]);   // convert command line string to integer
    fileNastiness = atoi(argv[fileNastinessArg]);   // convert command line string to integer
}

/* Process the filename packet, the 1st packet send for transmission of a given file */
void receiveFilename(int &packetsWrittenToFile, 
                     uint32_t &currentFileNameCounter,
                     Packet &incomingPacket, 
                     string &currentFileName,
                     string &targetName,
                     string &targetDir,
                     unordered_set<string> &logResult,
                     unordered_set<string> &logStart,
                     NASTYFILE& outputFile) 
{
    packetsWrittenToFile = 0;

    currentFileNameCounter = incomingPacket.packetNum + incomingPacket.totalPackets;
    currentFileName.assign(incomingPacket.packetData, incomingPacket.dataSize);
    *GRADING << "File: " << currentFileName << " starting to receive file" << endl;
    cout << "File: " << currentFileName << " starting to receive file" << endl;

    targetName = makeFileName(targetDir, (currentFileName + ".TMP"));

    logResult.erase(currentFileName);
    logStart.erase(currentFileName);

    void *fopenretval;
    fopenretval = outputFile.fopen(targetName.c_str(), "wb");

    
    if (fopenretval == NULL) {
        cerr << "Error opening input file " << targetName << " errno=" << strerror(errno) << endl;
        exit(12);
    }
}

/* Take in packet and write selected portion into file */
void writeDataToFile(int &packetsWrittenToFile,
                     NASTYFILE& outputFile, 
                     Packet &incomingPacket, 
                     string &targetName)
{
    ssize_t len = outputFile.fwrite(incomingPacket.packetData, 1, incomingPacket.dataSize);
    if (len != incomingPacket.dataSize) {
        cerr << "Error writing file " << targetName << " errno=" << strerror(errno) << endl;
        exit(16);
    }
                        
    packetsWrittenToFile++;
}

/* Send ACK packet */
void acknowledgePacket(C150DgmSocket *sock, Packet &incomingPacket) {
    Packet ackPacket = createDataPacket(true, incomingPacket.packetNum, 0, "", 0);
    writePacket(sock, ackPacket);
}

/* Process incoming FILE packet and send ACK packet */
void handleFilePacket(C150DgmSocket *sock,
                      int &packetsWrittenToFile, 
                      uint32_t &currentFileNameCounter,
                      uint32_t &currentPacketNumber,
                      Packet &incomingPacket, 
                      string &currentFileName,
                      string &targetName,
                      string &targetDir,
                      unordered_set<string> &logResult,
                      unordered_set<string> &logStart,
                      NASTYFILE& outputFile)
{
    if (currentPacketNumber == incomingPacket.packetNum) {      
        // Handle packet containing filename
        if (currentFileNameCounter == incomingPacket.packetNum) {

            receiveFilename(packetsWrittenToFile, currentFileNameCounter, incomingPacket, 
                            currentFileName, targetName, targetDir, logResult, logStart,
                            outputFile);
    
        } else {       
            writeDataToFile(packetsWrittenToFile, outputFile, incomingPacket,targetName);
        };
        
        acknowledgePacket(sock, incomingPacket);

        currentPacketNumber++; // Increment current packet

        if (currentPacketNumber == currentFileNameCounter) {
            if (outputFile.fclose() != 0 ) {
                cerr << "Error closing output file " << targetName << 
                    " errno=" << strerror(errno) << endl;
                exit(16);
            }
        }

    /* Send ACK Packet for previous packet if that ACK was never received */
    } else if (incomingPacket.packetNum == currentPacketNumber - 1 ) {
        acknowledgePacket(sock, incomingPacket);
    }
}

/* Process incoming CHECK packet and send the HASH message containing the hash code
    of the server file*/ 
void handleCheck(C150DgmSocket *sock,
                 string &fileName,
                 string &currentFileName,
                 string &targetName,
                 unordered_set<string> &logResult, 
                 int &fileNastiness)
{
    if (logResult.count(fileName) == 0) {
        *GRADING << "File: " << currentFileName << " received, beginning end-to-end check" << endl;
        cout << "File: " << currentFileName << " received, beginning end-to-end check" << endl;
        logResult.insert(fileName);
    }

    string messageHash = "HASH:" + fileName + "," + computeHash(targetName, fileNastiness);

    Packet messagePacket = createMessagePacket(messageHash);

    writePacket(sock, messagePacket);
}

/* Process incoming RESULT packet and send the LOG confirmation message */
void handleResult(C150DgmSocket *sock, 
                  string &response, 
                  string &currentFileName, 
                  unordered_set<string> &logStart, 
                  string &fileName, 
                  string &targetName, 
                  string &targetDir)
{
    string result;
    if (!parseResponse(response, "RESULT" , currentFileName, result)) {
        cerr << "Invalid RESULT response or filename mismatch." << endl;
        cout << "Wrong file " << endl;
    } else {
        if (result == "PASS") {
            // On a PASS, remove .TMP extension
            if (rename(targetName.c_str(), makeFileName(targetDir, currentFileName).c_str()) != 0) {
                cout << "ERROR with RENAME" << endl;
            }
            targetName = makeFileName(targetDir, currentFileName);
        }
        
        if (logStart.count(fileName) == 0) {
            if (result == "PASS") {
                *GRADING << "File: " << fileName << " end-to-end check succeeded" << endl;
                cout << "File: " << fileName << " end-to-end check succeeded" << endl;
            } else if (result == "FAIL") {
                *GRADING << "File: " << fileName << " end-to-end check failed" << endl;
                cout << "File: " << fileName << " end-to-end check failed" << endl;
            }
            logStart.insert(fileName);
        }

        string logConfirmation = "LOG:" + fileName + "," + result;           
        Packet logPacket = createMessagePacket(logConfirmation);
        writePacket(sock, logPacket);
    }
}

/* Process an incoming Message Packet */
void handleMessagePacket(C150DgmSocket *sock,  
                         string &currentFileName, 
                         unordered_set<string> &logStart,
                         unordered_set<string> &logResult,
                         string &targetName, 
                         string &targetDir,
                         Packet &incomingPacket,
                         int &fileNastiness,
                         uint32_t &currentPacketNumber,
                         uint32_t &currentFileNameCounter,
                         int &packetsWrittenToFile)
{
    string response;
    response.assign(incomingPacket.packetData, incomingPacket.dataSize);
    size_t posColon = response.find(":");
    size_t posComma = response.find(",");
    string msgCommand = response.substr(0, posColon);
    string fileName = response.substr(posColon + 1, posComma - posColon - 1);

    if (fileName == currentFileName) {
        if (msgCommand == "CHECK") {
            handleCheck(sock, fileName, currentFileName, targetName, logResult, fileNastiness);
        }  
        else if (msgCommand == "RESULT") {
            handleResult(sock, response, currentFileName, logStart, 
                            fileName, targetName, targetDir);
        }

    } else if (msgCommand == "FINISHED") {
        currentPacketNumber = 0;
        currentFileNameCounter = 0;
        packetsWrittenToFile = 0;
        string finalMessage = "FINISHED:";

        Packet finalPacket = createMessagePacket(finalMessage);               
        writePacket(sock, finalPacket);
    }
}