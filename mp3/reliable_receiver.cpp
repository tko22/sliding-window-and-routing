#include <stdio.h>
#include <stdlib.h>

void reliablyReceive(unsigned short int myUDPport, char *destinationFile)
{

    // already have data, write to file
    FILE *fd;
    fd = fopen(destinationFile, "w");
    // fwrite(buf, sizeof(char), numbytes, fd);
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
