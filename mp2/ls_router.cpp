#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "monitor_neighbors.cpp"

void updateCost(uint16_t nodeID, uint32_t cost);
void listenForNeighbors();
void *announceToNeighbors(void *unusedParam);

int globalMyID = 0;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];

// Indices of nodes with active links
bool connections[256];
// Costs for each node
unsigned int costs[256];

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
        exit(1);
    }

    theLogFile = argv[3];

    //initialization: get this process's node ID, record what time it is,
    //and set up our sockaddr_in's for sending to the other nodes.
    globalMyID = atoi(argv[1]);
    int i;
    for (i = 0; i < 256; i++)
    {
        gettimeofday(&globalLastHeartbeat[i], 0);

        char tempaddr[100];
        sprintf(tempaddr, "10.1.1.%d", i);
        memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
        globalNodeAddrs[i].sin_family = AF_INET;
        globalNodeAddrs[i].sin_port = htons(7777);
        inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
    }

    // read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.
    FILE *fp;
    int buffLen = 50;
    char buff[buffLen];
    char *costPtr;
    int nodeID;

    fp = fopen(argv[2], "r");
    if (fp == NULL)
    {
        perror("Invalid initial costs file");
        return -1;
    }

    while (fgets(buff, buffLen, fp))
    {
        costPtr = strchr(buff, ' ');
        *costPtr = '\0';
        costPtr++;

        updateCost(atoi(buff), atoi(costPtr));
    }
    // fill in rest of the costs
    for (int i = 0; i < 256; ++i)
    {
        if (costs[i] == 0)
        {
            costs[i] = 1;
        }
    }
    costs[globalMyID] = 0; // own node cost is the same
    fclose(fp);

    // -------------------------------------------------- //

    //socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
    if ((globalSocketUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }
    char myAddr[100];
    struct sockaddr_in bindAddr;
    sprintf(myAddr, "10.1.1.%d", globalMyID);
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(7777);
    inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
    if (bind(globalSocketUDP, (struct sockaddr *)&bindAddr, sizeof(struct sockaddr_in)) < 0)
    {
        perror("bind");
        close(globalSocketUDP);
        exit(1);
    }

    //start threads... feel free to add your own, and to remove the provided ones.
    pthread_t announcerThread;
    pthread_create(&announcerThread, 0, announceToNeighbors, (void *)0);

    //good luck, have fun!
    listenForNeighbors();
}