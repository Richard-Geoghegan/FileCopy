# FileCopy: A Reliable Client-Server File Transfer Program in C++

## Overview
**FileCopy** is a robust client-server file transfer program designed to ensure 100% file reconstruction success over a network prone to frequent packet drops, reordering, and corruption. Written in C++, the program implements strict end-to-end checks, with features designed to maintain file integrity even under harsh network conditions.

This project demonstrates my ability to develop networked applications that prioritize data integrity and reliability, using concepts like acknowledgment-based communication, sequential packet handling, and error recovery.

## Problem Context
The assignment simulates a highly unreliable network environment using the `C150NastySockets` library, which introduces variable levels of packet loss, corruption, and reordering. The goal is to create a file transfer system that reliably copies all files from a source directory on the client side to a target directory on the server side, handling a wide range of potential network and file errors.

## How It Works
The program consists of two main components:
- **fileclient.cpp**: The client-side code responsible for sending files.
- **fileserver.cpp**: The server-side code that receives and reconstructs files.

### Client-Side Algorithm
1. **Directory Scanning**: The client iterates through each file in the source directory and divides the file data into packets of 512 bytes or less.
2. **Sequential Packet Sending**: The client sends each packet in order, waiting for an acknowledgment (ACK) from the server before proceeding to the next packet. If no ACK is received within a specified time, the client retries sending the current packet, up to a maximum number of attempts.
3. **Retries and Error Handling**: If an end-to-end check fails (i.e., the reconstructed file is incomplete or corrupted), the client resends the entire file.
4. **File Naming**: During transfer, files are renamed with a `.TMP` suffix in the destination directory. Once the end-to-end check succeeds, the `.TMP` suffix is removed, allowing users to easily identify files that may still be incomplete or faulty.

### Server-Side Algorithm
1. **Packet Reception and Verification**: The server receives packets in sequence and validates them. If a packet is out of order, the server waits for the correct packet before sending an ACK.
2. **ACK Mechanism**: The server only sends an ACK when it receives the expected packet number, ensuring both sides stay in sync. It ignores or discards packets that arrive out of order.
3. **File Reconstruction**: Once all packets for a file are received and verified, the server reassembles the file and performs an end-to-end check to validate its integrity.

## Packet Structure
The `Packet` struct used in the program is defined as follows:
```cpp
struct Packet {
    bool isFile;            // Indicates whether the packet contains file data or a message
    uint32_t packetNum;     // Global sequence number for the packet
    uint16_t totalPackets;  // Total number of packets for the current file or operation
    uint16_t dataSize;      // Size of valid data within packetData
    char packetData[498];   // Contains file data, filename, or message
};
