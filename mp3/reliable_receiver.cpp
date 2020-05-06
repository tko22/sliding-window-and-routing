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
#define ACK_SIZE 5      // end (1 byte) + seq no (4 bytes)

char buf[RWS][FRAME_SIZE]; // RWS frame buffers
int present[RWS];          // are frame buffers full, initialized to 0
int NFE = 0;               // next frame expected

struct sockaddr_in recv_addr, sender_addr;
int globalSocketUDP;

void writeToFile(char *destinationFile)
{

    FILE *fd;
    fd = fopen(destinationFile, "w");
    // fwrite(buf, sizeof(char), numbytes, fd);
}

void handleRecvFrame(char *data, int seq_no)
{

    int idx;
    char ack_frame[4];

    if (((seq_no + (MAX_SEQ_NO - NFE)) % MAX_SEQ_NO) < RWS)
    {
        // ignore frame (seq_no)
        // send ack though
        create_ack_frame(ack_frame, seq_no);
        sendto(globalSocketUDP, ack_frame, sizeof(ack_frame), 0, (const struct sockaddr *)&sender_addr, sizeof(sender_addr));
        return;
    }

    // calculate index into data structure (present + buf)
    idx = (seq_no % RWS);

    // check if we already received it
    // if so mark received and copy to buffer
    if (!present[idx])
    {
        present[idx] = 1;                   // mark received
        memcpy(buf[idx], data, FRAME_SIZE); // copy data over to buffer
    }

    // figure out what acknowledgement to send
    int i;
    for (i = 0; i < RWS; i++)
    {
        // get the actual index from seq no for data structure
        idx = (i + NFE) % RWS;

        // terminate loop if first missing
        if (!present[idx])
            break;

        //TODO: pass to the app (buf[idx]) since it exists
        present[idx] = 0; // mark buffer empty
    }

    // remember to wrap
    // advance NFE to first missing frame
    NFE = (NFE + i) % MAX_SEQ_NO;

    // send ack for ((NFE + MAX_SEQ_NO - 1) % MAX_SEQ_NO)

    int ack_seq = ((NFE + MAX_SEQ_NO - 1) % MAX_SEQ_NO);

    create_ack_frame(ack_frame, ack_seq);
    sendto(globalSocketUDP, ack_frame, sizeof(ack_frame), 0, (const struct sockaddr *)&sender_addr, sizeof(sender_addr));
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
        int data_size = FRAME_SIZE; // sizeof(data)?
        int end;
        read_send_frame(recvBuf, &seq_no, data, &data_size, &end);

        // handle frame, decide what ack to send
        handleRecvFrame(data, seq_no);

        // already have data, write to file
        writeToFile(destinationFile);
    }
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
