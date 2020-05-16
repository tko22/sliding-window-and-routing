#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <deque>

#include <chrono>
#include <iomanip>
#include <mutex>
#include <thread>

#include "utils.cpp"

#define SWS 170          // sender (RTT=20ms*Bandwidth=100Mbps)
#define MAX_SEQ_NO 5000  // max sequence number (15) + 1

#define FRAME_SIZE 1472  // max framesize
#define MAX_DATA_SIZE FRAME_SIZE - 9

#define ACK_SIZE 4 // seq no (4 bytes)
#define SEC_2_USEC 1000000

struct Frame {
    char data[MAX_DATA_SIZE];
    int size;
    int seqNum;
    //   bool sent = false;
    // bool acked = false;
    int isEnd = 0;
    timeval sendTime;
} ;

struct sockaddr_in recv_addr, sender_addr;
int globalSocketUDP;

std::mutex window_mutex;

// DATA SIZE - 9 bytes in frame before data
std::deque<Frame*> windowBuf;

int congestionWindow = SWS / 2;

//** ALL USED WITHIN MUTEX **//
// int hasSent[SWS];                    // frame in window has been sent
// int acked[MAX_SEQ_NO];                      // frame ack received
// struct timeval windowSendTime[MAX_SEQ_NO];  // tracking the send times of each
                                            // frame in window
int lastFrameSeqNo = -1;
// bool sendDone = false;

long long RTO = 2 * SEC_2_USEC;  // retransmission timeout, set to 2s initially

int LAR = -1;  // last ack received
int LFS = 0;   // last frame sent
// ** ** ** ** ** ** ** **  **//

long long RTTs = -1.0;  // smoothed RTTs
long long RTTd = -1.0;  // RTT deviation

long long timeDiff(timeval time1, timeval time2) {
    long long t1 = (time1.tv_sec * SEC_2_USEC) + time1.tv_usec;
    long long t2 = (time2.tv_sec * SEC_2_USEC) + time2.tv_usec;
    return t2 - t1;
}

void reliablyTransfer(char *hostname, unsigned short int hostUDPport,
                      char *filename, unsigned long long int bytesToTransfer) {
    // receiver info
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(hostUDPport);
    inet_pton(AF_INET, hostname, &recv_addr.sin_addr);

    // socket() and bind() our socket. We will do all sendto()ing and
    // recvfrom()ing on this one.
    if ((globalSocketUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int enable = 1;
    
    // set reuseaddr
    if (setsockopt(globalSocketUDP, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    // set resuseport
    if (setsockopt(globalSocketUDP, SOL_SOCKET, SO_REUSEPORT, &enable,
                   sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    if (setsockopt(globalSocketUDP, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("Error");
    }

    // NOT NEEDED BECAUSE USING SENDTO
    // connect to receiver (server)
    if (connect(globalSocketUDP, (struct sockaddr *)&recv_addr,
                sizeof(recv_addr)) < 0) {
        printf("\n Error : Connect Failed \n");
        exit(0);
    }

    // read data from file
    FILE *f;
    f = fopen(filename, "rb");

    // read file to buffer, only send amount told to send
    // than file size
    // char buffer[bytesToTransfer];  //  number of bytes from the specified file
                                   //  to be sent to the receiver
    // size_t newLen = fread(buffer, sizeof(char), bytesToTransfer, f);
    // fclose(f);

    // sending data structures
    // char data[MAX_DATA_SIZE];
    // int dataSize;

    // int nextBufIdx = 0;
    int newLen = 0;

    // ** receive usage ** //
    char ack[ACK_SIZE];
    socklen_t recvAddrLen = sizeof(recv_addr);
    int bytesRecvd;
    int ack_seq_no;
    // ** ** ** ** ** //

    int cpySize;

    bool updateTO = true;

    // send data - sliding window algorithm begins

    while (true) {
        int idx;
        int seq_no;

        // std::cout << "RTO: " << RTO << std::endl;

        // *** Send all frames in window ***/
        for (int i = 0; i < congestionWindow; i++) {
            // get time
            timeval now;
            gettimeofday(&now, 0);

            // window_mutex.lock();
            // get the actual index from seq no for window data structures
            // LAR = -1 or 7, i = 0 -> idx = 0 (for initial set up)
            // LAR = -1 or 7, i = 7 -> idx = 7
            // LAR = 15, i = 0 -> idx = 0
            // idx = (i + LAR + 1) % SWS;

            // if LAR = 7, i = 0 -> seq_no = 8
            // if LAR = 15, i = 0 -> seq_no = 0
            // if LAR = 0, i = 1 (2nd frame in window) -> seq_no = 2
            seq_no = (LAR + i + 1) % MAX_SEQ_NO;

            // copy to variable so we can unlock mutex quickly
            // int frameHasSent = hasSent[seq_no];
            // int frameHasAcked = acked[seq_no];
            // int frameSentTimeSec = windowSendTime[seq_no].tv_sec;
            // window_mutex.unlock();
            // if packet timed out, frame hasn't been sent, send it
            // else don't send
            int isEnd = 0;
            if (i >= windowBuf.size()) {
                unsigned long long int bytesRemaining = bytesToTransfer - ftell(f);
                // std::cout << "Remaining: " << bytesRemaining << std::endl;
                // break out of loop if there isn't any more to be
                // copied so we won't send a packet with 0 byte data
                // this will occur when bytesToTransfer / MAX_DATA_SIZE
                // is an integer means we are able to cut
                // bytesToTransfer into a perfect amount of fully filled
                // packets
                if (bytesRemaining == 0) {
                    break;
                } else if (bytesRemaining <= MAX_DATA_SIZE) {
                    // sending last frame, or last frame already sent
                    cpySize = bytesRemaining;

                    // window_mutex.lock();
                    isEnd = 1;
                    lastFrameSeqNo = seq_no;
                    // nextBufIncrement = (bytesToTransfer - nextBufIdx);

                    // window_mutex.unlock();

                    std::cout << "LAST PACKET TO SEND.... lastFrameSeqNo "
                                << lastFrameSeqNo
                                << " with cpSize: " << cpySize << std::endl;
                } else {
                    // normal copy size
                    cpySize = MAX_DATA_SIZE;
                    // shift nextBufIdx by MAX_DATA_SIZE (since
                    // MAX_DATA_SIZE is in bytes/sizeof(char))
                    // nextBufIncrement = MAX_DATA_SIZE;
                }

                // std::cout << "cpySize: " << cpySize << std::endl;

                Frame *newFrame = new Frame;
                // copy over data to windowBuf, which temporarily stores the
                // the data for each window frame
                memset(newFrame->data, '\0', MAX_DATA_SIZE);
                newLen = fread(newFrame->data, 1, cpySize, f);
                // memcpy(windowBuf[seq_no], buffer + nextBufIdx, cpySize);
                // keep track of how much data is in the windowBuf
                newFrame->size = newLen;
                newFrame->seqNum = seq_no;
                newFrame->isEnd = isEnd;

                windowBuf.push_back(newFrame);

                if (newLen < cpySize) {
                    std::cout << "Full frame not sent: Expected = " << cpySize << " Sent = " << newLen << std::endl;
                }
            } else {
                if (timeDiff(windowBuf[i]->sendTime, now) >= RTO) {
                    if (updateTO && congestionWindow > 1) {
                        updateTO = false;
                        congestionWindow /= 2;
                        // std::cout << "Congestion Decrease: " << congestionWindow << std::endl;
                    }
                } else {
                    continue;
                }
            }

            // *** SEND PACKET  ***//
            char frame[FRAME_SIZE];
            memset(&frame, '\0', sizeof(frame));

            int sendSize =
                create_send_frame(frame, seq_no, windowBuf[i]->data,
                                    windowBuf[i]->size, windowBuf[i]->isEnd);
            if (sendto(globalSocketUDP, frame, sendSize, 0,
                        (struct sockaddr *)&recv_addr,
                        sizeof(recv_addr)) < 0) {
                perror("sending: sendto failed");
                exit(1);
            }

            // window_mutex.lock();
            // mark frame sent and time sent
            // std::cout << seq_no << ", ";
            
            gettimeofday(&(windowBuf[i]->sendTime), 0);
            // LFS = seq_no; TODO: get most recent seq_no (seq_no can be a
            // retransmitted one)

            // window_mutex.unlock();

            // break out of loop if it is the last frame of entire program
            // that is sent
            if (windowBuf[i]->isEnd == 1) {
                break;
            }
        }

        updateTO = true;
        // window_mutex.lock();
        // if (sendDone == true)
        // {
        // unlock mutex is about to break out of loop, ending program...
        //     // window_mutex.unlock();
        //     break;
        // }
        // unlock mutex if not end of program
        // window_mutex.unlock();

        // ********************** //

        // ** WAIT FOR RESPONSE **/

        // ********************** //

        // waiting for response, from the server
        if ((bytesRecvd =
            recvfrom(globalSocketUDP, (char *)ack, ACK_SIZE, 0,
                    (struct sockaddr *)&recv_addr, &recvAddrLen)) == -1) {
            // perror("listener: recvfrom failed");
            if (errno == ECONNREFUSED) {
                exit(1);
            }
        }

        // get receive time
        timeval receiveTime;
        gettimeofday(&receiveTime, 0);

        read_ack(ack, &ack_seq_no);
        // std::cout << "\n---------------------------------------------------"
                //   << std::endl;
        // std::cout << "\nRECEIVED ACK seq_no " << ack_seq_no << " LAR: " << LAR << std::endl;

        if (congestionWindow < SWS) {
            congestionWindow++;
            // std::cout << "Congestion Increase: " << congestionWindow << std::endl;
        }

        int ackIdx = (ack_seq_no - (LAR + 1)) % MAX_SEQ_NO;

        // if inside window
        // LAR = 15, 0-7 are in window, 15 is not included
        // TODO: do we need make sure its within < LFS
        if (ackIdx >= 0 && ackIdx < windowBuf.size()) {
            // int idx = seq_no % SWS;
            // std::cout << "inside window - seq _no: " << ack_seq_no << std::endl;
            // std::cout << "processing... we have sent this ack seq_no: "
            //           << ack_seq_no << std::endl;
            // break out of loop if end reached and last seq no is acked
            // std::cout << "Last Frame Num: " << lastFrameSeqNo << " Ack Num: " << ack_seq_no << std::endl;
            if (lastFrameSeqNo == ack_seq_no) {
                std::cout << "ENDING PROGRAM lastframeSeqNo acked: "
                            << lastFrameSeqNo << std::endl;
                break;
            }

            // get estimated RTT for RTO (retransmission timeout)
            // https://www.geeksforgeeks.org/tcp-timers/
            long long RTTm =
                timeDiff(windowBuf[ackIdx]->sendTime, receiveTime);

            // get smoothed RTT and deviated RTTs
            if (RTTs == -1) {
                RTTs = RTTm;
            } else {
                // 7/8 is from 1-t, where t = 1/8
                RTTs = 7 * (RTTs / 8) +  RTTm / 8;  
            }
            if (RTTd == -1) {
                RTTd = RTTm / 2;
            } else {
                // 3/4 is from 1-k, where k = 1/4
                RTTd = 3 * (RTTd / 4) + (RTTm - RTTs) / 4;
            }

            // set RTO
            RTO = RTTs + 4 * RTTd;

            for (int i = 0; i <= ackIdx; i++) {
                delete windowBuf.front();
                windowBuf.pop_front();
            }

            // if (windowBuf.front()->seqNum != ack_seq_no + 1) {
            //     std::cout << "Mismatched Seq Num! Expected: " << ack_seq_no + 1 << " Found: " << windowBuf.front()->seqNum << std::endl;
            // }
            
            LAR = ack_seq_no;

            // if (RTTs < 0) {
            //     std::cout << "\nRECEIVED ACK seq_no " << ack_seq_no << " LAR: " << LAR << std::endl;

            //     std::cout << "RTMs " << RTTs << std::endl;
            // }
            // std::cout << "RTMd " << RTTd << std::endl;
            // std::cout << "RTO::: " << RTO << ".... RTTm:  " << RTTm
            //           << std::endl;
            // std::cout << "new LAR: " << LAR << std::endl;
        }
        // std::cout << "----------------------------------\n" << std::endl;
    }

    fclose(f);

    close(globalSocketUDP);
}

int main(int argc, char **argv) {
    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr,
                "usage: %s receiver_hostname receiver_port filename_to_xfer "
                "bytes_to_xfer\n\n",
                argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int)atoi(argv[2]);
    numBytes = atoll(argv[4]);

    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);
}
