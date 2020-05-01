#include <stdio.h>
#include <stdlib.h>

void reliablyTransfer(char *hostname, unsigned short int hostUDPport, char *filename, unsigned long long int bytesToTransfer)
{
    // read data from file
    FILE *f;
    f = fopen(filename, "rb");

    char data[bytesToTransfer]; //  number of bytes from the specified file to be sent to the receiver
    size_t newLen = fread(data, sizeof(char), bytesToTransfer, f);
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
