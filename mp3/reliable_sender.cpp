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
#include <pthread.h>
#include <stdlib.h>
#include <mutex>
#include <thread>

#include "utils.cpp"

#define SWS 8         // sender
#define MAX_SEQ_NO 16 // max sequence number (15) + 1

#define FRAME_SIZE 1472 // max framesize
#define MAX_DATA_SIZE FRAME_SIZE - 9
#define ACK_SIZE 5 // end (1 byte) + seq no (4 bytes)

struct sockaddr_in recv_addr, sender_addr;
int globalSocketUDP;

std::mutex window_mutex;

char windowBuf[SWS][MAX_DATA_SIZE]; // DATA SIZE - 9 bytes in frame before data

// ** ALL USED WITHIN MUTEX **//
int hasSent[SWS];                   // frame in window has been sent
int acked[SWS];                     // frame ack received
struct timeval windowSendTime[SWS]; // tracking the send times of each frame in window
int isEnd;                          // has sent last frame
int lastFrameSeqNo;
bool sendDone = false;

float RTO = 2.0; // retransmission timeout, set to 2s initially

int LAR = -1; // last ack received
int LFS = 0;  // last frame sent
// ** ** ** ** ** ** ** **  **//

float RTTs = -1.0; // smoothed RTTs
float RTTd = -1.0; // RTT deviation

void handleAckThread()
{
    char ack[ACK_SIZE];
    socklen_t recvAddrLen = sizeof(recv_addr);
    int bytesRecvd;
    int seq_no;

    while (true)
    {
        // waiting for response, from the server
        if ((bytesRecvd = recvfrom(globalSocketUDP, (char *)ack, ACK_SIZE, 0,
                                   (struct sockaddr *)&recv_addr, &recvAddrLen)) == -1)
        {
            perror("listener: recvfrom failed");
            exit(1);
        }
        // get receive time
        timeval receiveTime;
        gettimeofday(&receiveTime, 0);

        read_ack(ack, &seq_no);

        window_mutex.lock();

        // if inside window
        // LAR = 15, 0-7 are in window, 15 is not included
        // TODO: do we need make sure its within < LFS
        if (((seq_no + (MAX_SEQ_NO - LAR - 1)) % MAX_SEQ_NO) < SWS)
        {
            int idx = seq_no % SWS;
            if (hasSent[idx])
            {
                // if received ack for 4th index, and LAR is 2nd index
                // | 1 | 1 | 0 | 0 | 0 | 0 |
                // then we assume 3rd is received too | 1 | 1 | 1 | 1 | 0 | 0 |
                // TODO: make sure server code does that, but im pretty sure it does
                int new_idx;
                for (int i = 0; i < idx; i++)
                {
                    new_idx = (i + LAR + 1) % SWS;

                    // set hasSent back to 0, moving the window..
                    hasSent[new_idx] = 0;
                    acked[new_idx] = 1;
                }

                // get estimated RTT for RTO (retransmission timeout)
                //https://www.geeksforgeeks.org/tcp-timers/
                float t1 = windowSendTime[idx].tv_sec + (windowSendTime[idx].tv_usec / 1000000.0);
                float t2 = receiveTime.tv_sec + (receiveTime.tv_usec / 1000000.0);
                float RTTm = t2 - t1;

                // get smoothed RTT and deviated RTTs
                if (RTTs == -1)
                    RTTs = RTTm;
                else
                    RTTs = (7 / 8) * RTTs + (1 / 8) * RTTm; // 7/8 is from 1-t, where t = 1/8

                if (RTTd == -1)
                    RTTd = RTTm / 2;
                else
                    RTTd = (3 / 4) * RTTd + 1 / 4 * (RTTm - RTTs); // 3/4 is from 1-k, where k = 1/4

                // set RTO
                RTO = RTTs + 4 * RTTd;

                // mark hasSent
                hasSent[idx] = 0;
                acked[idx] = 1;
                // no need to change windowSendTime because we don't look at it unless
                // the idx isn't acked - here we change acked to acked
                // once we set acked[idx] to 0, then we would update windowSendTime[idx]

                // packet acked is within window => > LAR
                // make LAR to it then...
                LAR = seq_no;
            }
        }

        window_mutex.unlock();
    }
}

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
    fclose(f);

    // sending data structures
    char data[MAX_DATA_SIZE];
    int dataSize;

    // receiving data structures
    socklen_t recvAddrLen = sizeof(recv_addr);
    char recvBuf[FRAME_SIZE];
    int bytesRecvd;

    int nextBufIdx = 0;

    // send data - sliding window algorithm begins

    // setup thread to handle acks
    std::thread recv_ack_thread(handleAckThread);

    while (true)
    {
        int idx;

        // *** Send all frames in window ***/
        for (int i = 0; i < SWS; i++)
        {
            // get time
            timeval now;
            gettimeofday(&now, 0);

            window_mutex.lock();
            // get the actual index from seq no for window data structures
            // LAR = -1 or 7, i = 0 -> idx = 0 (for initial set up)
            // LAR = -1 or 7, i = 7 -> idx = 7
            // LAR = 15, i = 0 -> idx = 0
            idx = (i + LAR + 1) % SWS;

            // if LAR = 7, i = 0 -> seq_no = 8
            // if LAR = 15, i = 0 -> seq_no = 0
            // if LAR = 0, i = 1 (2nd frame in window) -> seq_no = 2
            int seq_no = (LAR + i + 1) % MAX_SEQ_NO; // TODO: DOUBLE CHECK if correct

            // copy to variable so we can unlock mutex quickly
            int frameHasSent = hasSent[idx];
            int frameHasAcked = acked[idx];
            int frameSentTime = windowSendTime[idx].tv_sec;
            window_mutex.unlock();
            // if packet timed out, frame hasn't been sent, send it
            // else don't send
            if (frameHasSent == 0 || (frameHasAcked == 0 && now.tv_sec - frameSentTime >= RTO))
            {

                // TODO: double check if cpy size needs sizeof(char)
                // checks whether its the end of the
                int cpySize;
                if (bytesToTransfer - nextBufIdx < MAX_DATA_SIZE)
                {
                    // sending last frame, or last frame already sent
                    cpySize = (bytesToTransfer - nextBufIdx) * sizeof(char);

                    window_mutex.lock();
                    isEnd = 1;
                    lastFrameSeqNo = seq_no;
                    window_mutex.unlock();

                    // break out of loop if there isn't any more to be copied
                    // so we won't send a packet with 0 byte data
                    // this will occur when bytesToTransfer / MAX_DATA_SIZE is an integer
                    // means we are able to cut bytesToTransfer into
                    // a perfect amount of fully filled packets
                    if (cpySize == 0)
                    {
                        window_mutex.lock();
                        isEnd = 1;
                        // seq no was the last seq no before this seq_no
                        lastFrameSeqNo = (seq_no - 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                        window_mutex.unlock();

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
                memset(windowBuf[idx], '\0', sizeof(windowBuf[idx]));
                memcpy(buffer + (nextBufIdx * sizeof(char)), windowBuf[idx], cpySize);

                // *** SEND PACKET  ***//
                char frame[FRAME_SIZE];
                memset(&frame, '\0', sizeof(frame));

                int sendSize = create_send_frame(frame, seq_no, windowBuf[idx], cpySize, isEnd);
                if (sendto(globalSocketUDP, frame, sendSize, 0, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
                {
                    perror("sending: sendto failed");
                    exit(1);
                }

                window_mutex.lock();
                // mark frame sent and time sent
                hasSent[idx] = 1;
                gettimeofday(&windowSendTime[idx], 0);
                LFS = seq_no;

                // mark ack sent 0, so we know to check for timeout
                acked[idx] = 0;
                window_mutex.unlock();

                // break out of loop if it is the last frame of entire program that is sent
                if (isEnd == 1)
                {
                    break;
                }
            }
        }
        window_mutex.lock();
        if (sendDone == true)
        {
            // unlock mutex is about to break out of loop, ending program...
            window_mutex.unlock();
            break;
        }
        // unlock mutex if not end of program
        window_mutex.unlock();
    }

    // close thread
    recv_ack_thread.detach();
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
