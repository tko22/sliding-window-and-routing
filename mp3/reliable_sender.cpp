#include <stdio.h>
#include <stdlib.h>
#include <iomanip>
#include <chrono>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

#include "utils.cpp"

#define SWS 8         // sender
#define MAX_SEQ_NO 16 // max sequence number (15) + 1

#define FRAME_SIZE 1472 // max framesize

struct sockaddr_in recv_addr, sender_addr;
int globalSocketUDP;

void reliablyTransfer(char *hostname, unsigned short int hostUDPport, char *filename, unsigned long long int bytesToTransfer)
{
    // receiver info
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(hostUDPport);
    inet_pton(AF_INET, hostname, &recv_addr.sin_addr);

    // socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
    if ((globalSocketUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    int enable = 1;
    // set reuseaddr
    if (setsockopt(globalSocketUDP, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    // set resuseport
    if (setsockopt(globalSocketUDP, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    // NOT NEEDED BECAUSE USING SENDTO
    // connect to receiver (server)
    // if (connect(globalSocketUDP, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
    // {
    //     printf("\n Error : Connect Failed \n");
    //     exit(0);
    // }

    // read data from file
    FILE *f;
    f = fopen(filename, "rb");

    char data[bytesToTransfer]; //  number of bytes from the specified file to be sent to the receiver
    size_t newLen = fread(data, sizeof(char), bytesToTransfer, f);

    // send data - sliding window algorithm begins
    bool readDone = false; // to know when to stop reading, first step to ending program

    // sending data structures
    char frame[FRAME_SIZE];

    // DATA SIZE - 9 bytes in frame before data
    int maxDataSize = FRAME_SIZE - 9; //
    char data[maxDataSize];
    int dataSize;

    // receiving data structures
    socklen_t recvAddrLen = sizeof(recv_addr);
    char recvBuf[FRAME_SIZE];
    int bytesRecvd;

    // TODO: algo

    while (!readDone)
    {
        // read a frame
        dataSize = fread(frame, 1, maxDataSize, f);
        if (dataSize == maxDataSize)
        {
            char temp[1];
            int next_buffer_size = fread(temp, 1, 1, f);

            // if that's the end of the file, then its the last frame
            if (next_buffer_size == 0)
            {
                readDone = true;
            }
        }
        else if (dataSize < maxDataSize)
        {
            readDone = true;
        }

        bool sendDone = false;
        while (!sendDone)
        {

            sendDone = true;
        }
    }

    // send data
    if (sendto(globalSocketUDP, frame, sizeof(frame), 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
    {
        perror("sending: sendto failed");
    }

    // waiting for response, from the server
    if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000, 0,
                               (struct sockaddr *)&recv_addr, &recvAddrLen)) == -1)
    {
        perror("listener: recvfrom failed");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5)
    {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int)atoi(argv[2]);
    numBytes = atoll(argv[4]);

    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);
}
