#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{

    FILE *fd;
    fd = fopen(argv[2], "w");

    fclose(fd);

    return 0;
}