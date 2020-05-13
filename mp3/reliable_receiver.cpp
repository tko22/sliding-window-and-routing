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
#include <sys/types.h>

#include "utils.cpp"

//TODO: Get better values
#define RWS 8           //
#define MAX_SEQ_NO 16   // max sequence number (15) + 1
#define FRAME_SIZE 1472 // MTU in network, constant for simplicity
#define ACK_SIZE 4      // end (1 byte) + seq no (4 bytes)

char buf[RWS][FRAME_SIZE]; // RWS frame buffers
int buf_data_size[RWS];
int present[RWS]; // are frame buffers full, initialized to 0
int NFE = 0;      // next frame expected
int endSeqNum = -1;

struct sockaddr_in recv_addr, sender_addr;
int globalSocketUDP;

void writeToFile(FILE *fd, char *buf, int size)
{
    fwrite(buf, sizeof(char), size, fd);
}

bool handleRecvFrame(char *data, int seq_no, int data_size, int end, FILE *fd)
{
    std::cout << "\n"
              << "receive frame: seq_no: " << seq_no << " with NFE: " << NFE << std::endl;
    int idx;
    char ack_frame[ACK_SIZE];
    bool reachedEnd = false;

    // set end sequence number
    // receiving last frame doesn't mean you should end program
    // need to account for lost previous frames
    if (end == 1)
    {
        endSeqNum = seq_no;
    }

    if (((seq_no + (MAX_SEQ_NO - NFE)) % MAX_SEQ_NO) >= RWS)
    {
        std::cout << "outside window seq: " << seq_no << "::: Current NFE: " << NFE << std::endl;
        // ignore frame (seq_no)
        // send ack though since it may be because an ack was lost and sender didnt know you received it
        create_ack_frame(ack_frame, seq_no);
        sendto(globalSocketUDP, ack_frame, sizeof(ack_frame), 0, (const struct sockaddr *)&sender_addr, sizeof(sender_addr));
        return false;
    }

    // calculate index into data structure (present + buf)
    idx = (seq_no % RWS);
    std::cout << "idx:" << idx << std::endl;
    // check if we already received it
    // if so mark received and copy to buffer
    if (!present[idx])
    {
        std::cout << "marking present: idx:" << idx << std::endl;
        present[idx] = 1;                  // mark received
        memcpy(buf[idx], data, data_size); // copy data over to buffer

        // write data size to know how much to copy over
        // because frame includes other data seqNum, data_size, etc
        // so its not exactly FRAME_SIZE big
        buf_data_size[idx] = data_size;
    }

    // figure out what acknowledgement to send
    int i;
    for (i = 0; i < RWS; i++)
    {
        // get the actual index from seq no for data structure
        idx = (i + NFE) % RWS;

        // terminate loop if first missing
        if (present[idx] == 0) {
            std::cout << "idx: " << idx << "is not present" << std::endl;
            break;
        }

        std::cout << "writing to file -- i + NFE: " << (NFE + i)%MAX_SEQ_NO << std::endl;
        std::cout << "datasize: " << buf_data_size[idx] << std::endl;
        // pass to the app (buf[idx]) since it exists
        writeToFile(fd, buf[idx], buf_data_size[idx]);
        present[idx] = 0;       // mark buffer empty
        buf_data_size[idx] = 0; // reset datasize array index
        memset(buf[idx], '\0', sizeof(buf[idx]));
    }

    // remember to wrap
    // advance NFE to first missing frame
    NFE = (NFE + i) % MAX_SEQ_NO;

    // send ack for for predecessor of NFE
    // ((NFE + MAX_SEQ_NO - 1) % MAX_SEQ_NO)
    // if NFE=0, then it goes to 15...
    int ack_seq = ((NFE + MAX_SEQ_NO - 1) % MAX_SEQ_NO);
    std::cout << "SENDING ack:" << ack_seq << std::endl;
    std::cout << "new NFE: " << NFE << std::endl;

    create_ack_frame(ack_frame, ack_seq);
    sendto(globalSocketUDP, ack_frame, sizeof(ack_frame), 0, (const struct sockaddr *)&sender_addr, sizeof(sender_addr));

    // if the last frame written (ack_seq, predecessor of NFE) has the same seq num
    // as end seq num, we know we can close the program
    if (endSeqNum >= 0 && ack_seq == endSeqNum)
    {
        reachedEnd = true;
    }

    return reachedEnd;
}

void reliablyReceive(unsigned short int myUDPport, char *destinationFile)
{
    memset(&recv_addr, 0, sizeof recv_addr);

    // accept all connections to machine
    recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    recv_addr.sin_port = htons(myUDPport);
    recv_addr.sin_family = AF_INET;

    // create UDP socket
    if ((globalSocketUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    // bind receiver address to socket descriptor
    if (::bind(globalSocketUDP, (struct sockaddr *)&recv_addr, sizeof(struct sockaddr_in)) < 0)
    {
        perror("bind");
        close(globalSocketUDP);
        exit(1);
    }

    FILE *fd;
    fd = fopen(destinationFile, "w");

    int end_seq_no;
    while (1)
    {
        char fromAddrStr[100];
        socklen_t senderAddrLen;
        char recvBuf[FRAME_SIZE];

        int bytesRecvd;
        senderAddrLen = sizeof(sender_addr);

        // recv frame
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, FRAME_SIZE, 0,
                                   (struct sockaddr *)&sender_addr, &senderAddrLen)) == -1)
        {
            perror("connectivity listener: recvfrom failed");
            exit(1);
        }

        inet_ntop(AF_INET, &sender_addr.sin_addr, fromAddrStr, 100);

        // Received Frame
        int seq_no;
        char data[FRAME_SIZE];
        int data_size; // sizeof(data)?
        int end;
        read_send_frame(recvBuf, &seq_no, data, &data_size, &end);

        // handle frame, decide what ack to send
        bool is_end = handleRecvFrame(data, seq_no, data_size, end, fd);

        // end of transport, when is_end == true
        // break out of loop, closing program
        if (is_end == true)
        {
            end_seq_no = ((NFE + MAX_SEQ_NO - 1) % MAX_SEQ_NO);
            std::cout << "\nIT IS THE END... end_seq_no: " << end_seq_no << std::endl;
            break;
        }
    }

    // TODO: run until you don't receive a message for 10 seconds

    // currently, just sending the last ack for last seq no 3 times just in case it gets lost lol
    char ack_frame[ACK_SIZE];
    create_ack_frame(ack_frame, end_seq_no);
    sendto(globalSocketUDP, ack_frame, sizeof(ack_frame), 0, (const struct sockaddr *)&sender_addr, sizeof(sender_addr));
    sendto(globalSocketUDP, ack_frame, sizeof(ack_frame), 0, (const struct sockaddr *)&sender_addr, sizeof(sender_addr));
    sendto(globalSocketUDP, ack_frame, sizeof(ack_frame), 0, (const struct sockaddr *)&sender_addr, sizeof(sender_addr));

    fflush(fd);
    fclose(fd);
    close(globalSocketUDP);
}

int main(int argc, char **argv)
{
    unsigned short int udpPort;

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int)atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}
