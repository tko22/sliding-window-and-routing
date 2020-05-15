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

#include <chrono>
#include <iomanip>
#include <mutex>
#include <thread>

#include "utils.cpp"

#define SWS 50          // sender (RTT=20ms*Bandwidth=100Mbps)
#define MAX_SEQ_NO 350  // max sequence number (15) + 1

#define FRAME_SIZE 1472  // max framesize
#define MAX_DATA_SIZE FRAME_SIZE - 9

#define ACK_SIZE 5  // end (1 byte) + seq no (4 bytes)
#define SEC_2_USEC 1000000

struct sockaddr_in recv_addr, sender_addr;
int globalSocketUDP;

std::mutex window_mutex;

// DATA SIZE - 9 bytes in frame before data
char windowBuf[MAX_SEQ_NO][MAX_DATA_SIZE];
int windowBufSize[MAX_SEQ_NO];

//** ALL USED WITHIN MUTEX **//
int hasSent[MAX_SEQ_NO];                    // frame in window has been sent
int acked[MAX_SEQ_NO];                      // frame ack received
struct timeval windowSendTime[MAX_SEQ_NO];  // tracking the send times of each
                                            // frame in window
int isEnd;                                  // has sent last frame
int lastFrameSeqNo;
bool sendDone = false;

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

    // send data - sliding window algorithm begins

    while (true) {
        int idx;
        int seq_no;

        // *** Send all frames in window ***/
        for (int i = 0; i < SWS; i++) {
            // get time
            timeval now;
            gettimeofday(&now, 0);

            // window_mutex.lock();
            // get the actual index from seq no for window data structures
            // LAR = -1 or 7, i = 0 -> idx = 0 (for initial set up)
            // LAR = -1 or 7, i = 7 -> idx = 7
            // LAR = 15, i = 0 -> idx = 0
            idx = (i + LAR + 1) % SWS;

            // if LAR = 7, i = 0 -> seq_no = 8
            // if LAR = 15, i = 0 -> seq_no = 0
            // if LAR = 0, i = 1 (2nd frame in window) -> seq_no = 2
            seq_no =
                (LAR + i + 1) % MAX_SEQ_NO;  // TODO: DOUBLE CHECK if correct

            // copy to variable so we can unlock mutex quickly
            int frameHasSent = hasSent[seq_no];
            int frameHasAcked = acked[seq_no];
            // int frameSentTimeSec = windowSendTime[seq_no].tv_sec;
            // window_mutex.unlock();
            // if packet timed out, frame hasn't been sent, send it
            // else don't send
            if ((frameHasSent == 0 && isEnd != 1) ||
                (frameHasSent == 1 && frameHasAcked == 0 &&
                 timeDiff(windowSendTime[seq_no], now) >= RTO)) {
                std::cout << "NEED TO SEND seq_no: " << seq_no
                          << " with seq no: " << seq_no << std::endl;
                if (frameHasSent == 0 && isEnd != 1) {
                    std::cout << "hasn't been sent: " << std::endl;
                } else {
                    std::cout << "    TIMEOUT   " << std::endl;
                }
                std::cout << "RTO::: " << RTO << std::endl;
                // frame hasnt been sent and not the end
                if (frameHasSent == 0) {
                    std::cout << "frame hasn't been sent before:   nextBufIdx: "
                              << ftell(f) << std::endl;
                    // TODO: double check if cpy size needs sizeof(char)
                    // checks whether its the end of the file
                    unsigned long long int bytesRemaining = bytesToTransfer - ftell(f);
                    if (bytesRemaining <= MAX_DATA_SIZE) {
                        // sending last frame, or last frame already sent
                        cpySize = bytesRemaining * sizeof(char);

                        // window_mutex.lock();
                        isEnd = 1;
                        lastFrameSeqNo = seq_no;
                        // nextBufIncrement = (bytesToTransfer - nextBufIdx);

                        // window_mutex.unlock();

                        
                        // break out of loop if there isn't any more to be
                        // copied so we won't send a packet with 0 byte data
                        // this will occur when bytesToTransfer / MAX_DATA_SIZE
                        // is an integer means we are able to cut
                        // bytesToTransfer into a perfect amount of fully filled
                        // packets
                        // if (cpySize == 0) {
                        //     // window_mutex.lock();
                        //     // isEnd = 1;
                        //     // seq no was the last seq no before this seq_no
                        //     lastFrameSeqNo =
                        //         (seq_no - 1 + MAX_SEQ_NO) % MAX_SEQ_NO;
                        //     // window_mutex.unlock();

                        //     break;
                        // }

                        std::cout << "LAST PACKET TO SEND.... lastFrameSeqNo "
                                  << lastFrameSeqNo
                                  << " Sequence Number: " << seq_no 
                                  << " with cpSize: " << cpySize << std::endl;
                    } else {
                        // normal copy size
                        cpySize = MAX_DATA_SIZE * sizeof(char);
                        // shift nextBufIdx by MAX_DATA_SIZE (since
                        // MAX_DATA_SIZE is in bytes/sizeof(char))
                        // nextBufIncrement = MAX_DATA_SIZE;
                    }

                    // std::cout << "cpySize: " << cpySize << std::endl;

                    // copy over data to windowBuf, which temporarily stores the
                    // the data for each window frame
                    memset(windowBuf[seq_no], '\0', sizeof(windowBuf[seq_no]));
                    newLen = fread(windowBuf[seq_no], sizeof(char), cpySize, f);
                    // memcpy(windowBuf[seq_no], buffer + nextBufIdx, cpySize);
                    // keep track of how much data is in the windowBuf
                    windowBufSize[seq_no] = newLen;

                    if (newLen < cpySize) {
                        std::cout << "Full frame not sent: Expected = " << cpySize << " Sent = " << newLen << std::endl;
                    }

                    // update nextBufIdx
                    // nextBufIdx += nextBufIncrement;
                }

                // *** SEND PACKET  ***//
                char frame[FRAME_SIZE];
                memset(&frame, '\0', sizeof(frame));

                int sendSize =
                    create_send_frame(frame, seq_no, windowBuf[seq_no],
                                      windowBufSize[seq_no], isEnd);
                if (sendto(globalSocketUDP, frame, sendSize, 0,
                           (struct sockaddr *)&recv_addr,
                           sizeof(recv_addr)) < 0) {
                    perror("sending: sendto failed");
                    exit(1);
                }

                // window_mutex.lock();
                // mark frame sent and time sent
                hasSent[seq_no] = 1;
                std::cout << "SENT seq_no " << seq_no << " - marking hasSent \n"
                          << std::endl;
                gettimeofday(&windowSendTime[seq_no], 0);
                // LFS = seq_no; TODO: get most recent seq_no (seq_no can be a
                // retransmitted one)

                // mark ack sent 0, so we know to check for timeout
                acked[seq_no] = 0;
                // window_mutex.unlock();

                // break out of loop if it is the last frame of entire program
                // that is sent
                if (isEnd == 1) {
                    break;
                }
            }
        }
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
            perror("listener: recvfrom failed");
            exit(1);
        }

        // get receive time
        timeval receiveTime;
        gettimeofday(&receiveTime, 0);

        read_ack(ack, &ack_seq_no);
        std::cout << "\n---------------------------------------------------"
                  << std::endl;
        std::cout << "RECEIVED ACK seq_no " << ack_seq_no << std::endl;

        // if inside window
        // LAR = 15, 0-7 are in window, 15 is not included
        // TODO: do we need make sure its within < LFS
        if (((ack_seq_no + (MAX_SEQ_NO - LAR - 1)) % MAX_SEQ_NO) < SWS) {
            // int idx = seq_no % SWS;
            std::cout << "inside window - seq _no: " << ack_seq_no << std::endl;
            if (hasSent[ack_seq_no]) {
                std::cout << "processing... we have sent this ack seq_no: "
                          << ack_seq_no << std::endl;
                // break out of loop if end reached and last seq no is acked
                if (lastFrameSeqNo == ack_seq_no && isEnd == 1) {
                    std::cout << "ENDING PROGRAM lastframeSeqNo acked: "
                              << lastFrameSeqNo << std::endl;
                    break;
                }
                // if received ack for 4th seq, and LAR is 2nd seq no
                // | 1 | 1 | 0 | 0 | 0 | 0 |
                // then we assume 3rd is received too | 1 | 1 | 1 | 1 | 0 | 0 |
                // TODO: make sure server code does that, but im pretty sure it
                // does
                int larToSeqNo =
                    (ack_seq_no + MAX_SEQ_NO - LAR - 1) % MAX_SEQ_NO;
                int new_seq;
                for (int i = 0; i < larToSeqNo; i++) {
                    // LAR = 15 & seq_no = 2
                    // you want to ack 0,1, & 2
                    // larToSeqNo would be 2 so we iterate twice
                    // new_seq = 0 for i=0 => mark acked
                    // new_seq = 1 for i=1 => mark acked
                    new_seq = (i + LAR + 1) % MAX_SEQ_NO;

                    // set hasSent back to 0, moving the window..
                    hasSent[ack_seq_no] = 0;
                    acked[ack_seq_no] = 1;
                }

                // get estimated RTT for RTO (retransmission timeout)
                // https://www.geeksforgeeks.org/tcp-timers/
                long long RTTm =
                    timeDiff(windowSendTime[ack_seq_no], receiveTime);

                // get smoothed RTT and deviated RTTs
                if (RTTs == -1) {
                    RTTs = RTTm;
                } else {
                    RTTs = (7 / 8) * RTTs +
                           (1 / 8) * RTTm;  // 7/8 is from 1-t, where t = 1/8
                }
                std::cout << "RTMs " << RTTs << std::endl;
                if (RTTd == -1) {
                    RTTd = RTTm / 2;
                } else {
                    RTTd = (3 / 4) * RTTd +
                           1 / 4 *
                               (RTTm - RTTs);  // 3/4 is from 1-k, where k = 1/4
                }

                std::cout << "RTMd " << RTTd << std::endl;

                // set RTO
                RTO = RTTs + 4 * RTTd;
                std::cout << "RTO::: " << RTO << ".... RTTm:  " << RTTm
                          << std::endl;

                // mark hasSent
                hasSent[ack_seq_no] = 0;
                acked[ack_seq_no] = 1;
                // no need to change windowSendTime because we don't look at it
                // unless the idx isn't acked - here we change acked to acked
                // once we set acked[idx] to 0, then we would update
                // windowSendTime[idx]

                // packet acked is within window => greater than LAR
                // acks are cumulative but check just in case
                // check if any in front are acked e.g. LAR = 3, received ack
                // 5,6,7,8, just received ack 4 => make LAR = 8

                // if LAR = 15, seq_no received is 2, 0,1,2 are acked
                // larToSeqNo = 2
                // check 3,4,5,6,7 => SWS -1 - larToSeqNo = 5
                LAR = ack_seq_no;
                int x;
                for (x = 0; x < SWS - 1 - larToSeqNo; x++) {
                    new_seq = (x + LAR + 1) % MAX_SEQ_NO;
                    if (acked[new_seq]) {
                        LAR = new_seq;
                    }
                }
                std::cout << "new LAR: " << LAR << std::endl;
            }
        }
        std::cout << "----------------------------------\n" << std::endl;
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
