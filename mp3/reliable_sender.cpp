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
#include <chrono>
#include <time.h>
#include <sys/time.h>

#include "utils.cpp"

#define SWS 8         // sender
#define MAX_SEQ_NO 16 // max sequence number (15) + 1

#define FRAME_SIZE 1472 // max framesize
#define TIMEOUT 10
#define MAX_DATA_SIZE FRAME_SIZE - 9

struct sockaddr_in recv_addr, sender_addr;
int globalSocketUDP;

// DATA SIZE - 9 bytes in frame before data
char windowBuf[SWS][MAX_DATA_SIZE];
int hasSent[SWS];                   // frame in window has been sent
int acked[SWS];                     // frame ack received
struct timeval windowSendTime[SWS]; // tracking the send times of each frame in window

int LAR = -1; // last ack received
int LFS = 0;  // last frame sent

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

    // read file to buffer, only send amount told to send
    // TODO: check if we need to handle wehther bytesToTransfer is bigger
    // than file size
    char buffer[bytesToTransfer]; //  number of bytes from the specified file to be sent to the receiver
    size_t newLen = fread(buffer, sizeof(char), bytesToTransfer, f);

    // sending data structures
    char data[MAX_DATA_SIZE];
    int dataSize;

    // receiving data structures
    socklen_t recvAddrLen = sizeof(recv_addr);
    char recvBuf[FRAME_SIZE];
    int bytesRecvd;

    int nextBufIdx = 0;

    // send data - sliding window algorithm begins
    bool sendDone = false;
    while (!sendDone)
    {
        // TODO: algo
        int idx;
        int isEnd;

        // *** Send all frames in window ***/
        for (int i = 0; i < SWS; i++)
        {
            // get the actual index from seq no for window data structures
            // LAR = -1 or 7, i = 0 -> idx = 0
            // LAR = -1 or 7, i = 7 -> idx = 7
            idx = (i + LAR + 1) % SWS;

            char frame[FRAME_SIZE];
            memset(&frame, 0, sizeof(frame));

            // TODO: double check if cpy size needs sizeof(char)
            // checks whether its the end of the
            int cpySize;
            if (nextBufIdx + MAX_DATA_SIZE < bytesToTransfer)
            {
                cpySize = (bytesToTransfer - nextBufIdx) * sizeof(char);
                isEnd = 1;

                // break out of loop if there isn't any more to be copied
                // so we won't send a packet with 0 byte data
                if (cpySize == 0)
                {
                    break;
                }
            }
            else
            {
                // normal copy size
                cpySize = MAX_DATA_SIZE * sizeof(char);
                // shift nextBufIdx by MAX_DATA_SIZE (since MAX_DATA_SIZE is in bytes/sizeof(char))
                nextBufIdx += MAX_DATA_SIZE;
            }

            // copy over data to windowBuf, which temporarily stores the
            // the data for each window frame
            memcpy(buffer + (nextBufIdx * sizeof(char)), windowBuf[idx], cpySize);

            // *** SEND PACKET  ***//
            // if LAR = 7, i = 0 -> seq_no = 8
            // if LAR = 15, i = 0 -> seq_no = 0
            // if LAR = 0, i = 1 (2nd frame in window) -> seq_no = 2
            int seq_no = (LAR + i + 1) % MAX_SEQ_NO; // TODO: DOUBLE CHECK if correct
            int sendSize = create_send_frame(frame, seq_no, windowBuf[idx], cpySize, isEnd);
            // send data
            if (sendto(globalSocketUDP, frame, sendSize, 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
            {
                perror("sending: sendto failed");
                exit(1);
            }

            // mark frame sent and time sent
            hasSent[idx] = 1;
            gettimeofday(&windowSendTime[idx], 0);

            // break out of loop if it is the last frame of entire program that is sent
            if (isEnd == 1)
            {
                break;
            }
        }

        // TODO: handle ACKS

        // waiting for response, from the server
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000, 0,
                                   (struct sockaddr *)&recv_addr, &recvAddrLen)) == -1)
        {
            perror("listener: recvfrom failed");
            exit(1);
        }

        sendDone = true;
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
