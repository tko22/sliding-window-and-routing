#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>

//TODO: Get better values
#define RWS 8           //
#define MAX_SEQ_NO 16   // max sequence number (15) + 1
#define FRAME_SIZE 1500 // MTU in network, constant for simplicity

char buf[RWS][FRAME_SIZE]; // RWS frame buffers
int present[RWS];          // are frame buffers full, initialized to 0
int NFE = 0;               // next frame expected

void writeToFile(char *destinationFile)
{

    FILE *fd;
    fd = fopen(destinationFile, "w");
    // fwrite(buf, sizeof(char), numbytes, fd);
}

void handleRecvFrame(char *data, int seq_no)
{
    int idx;

    if (((seq_no + (MAX_SEQ_NO - NFE)) % MAX_SEQ_NO) < RWS)
    {
        // ignore frame
        // send ack though
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
    //TODO: send ack for ((NFE + MAX_SEQ_NO - 1) % MAX_SEQ_NO)
}

void reliablyReceive(unsigned short int myUDPport, char *destinationFile)
{

    // some udp recvfrom stuff
    // setup listener and stuff

    // Received Frame
    // handleRecvFrame(data, seq_no)

    // already have data, write to file
    writeToFile(destinationFile);
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
