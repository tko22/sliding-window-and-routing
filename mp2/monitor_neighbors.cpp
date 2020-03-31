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
#include <sys/types.h>
#include <unistd.h>

#define LOG_LINE_LENGTH 100

extern int globalMyID;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];

// Indices of nodes with active links
extern bool connections[256];
// Costs for each node
extern unsigned int costs[256];

// log path for node
extern char *theLogFile;

// write to log function
// you need to provide buffer
void writeToLog(char *logLine)
{
    FILE *fp = fopen(theLogFile, "a");
    fwrite(logLine, 1, strlen(logLine), fp);
    fflush(fp);
    fclose(fp);
}

void writeSendLog(int dest, int nexthop, char *message)
{
    char logLine[LOG_LINE_LENGTH];
    memset(logLine, '\0', LOG_LINE_LENGTH);
    sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", dest, nexthop, message);
    writeToLog(logLine);
}

void writeForwardLog(int dest, int nexthop, char *message)
{
    char logLine[LOG_LINE_LENGTH];
    memset(logLine, '\0', LOG_LINE_LENGTH);
    sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", dest, nexthop, message);
    writeToLog(logLine);
}

void writeReceiveLog(char *message)
{
    char logLine[LOG_LINE_LENGTH];
    memset(logLine, '\0', LOG_LINE_LENGTH);
    sprintf(logLine, "receive packet message %s\n", message);
    writeToLog(logLine);
}

void writeReceiveLog(int dest)
{
    char logLine[LOG_LINE_LENGTH];
    memset(logLine, '\0', LOG_LINE_LENGTH);
    sprintf(logLine, "unreachable dest %d\n", dest);
    writeToLog(logLine);
}

void updateCost(uint16_t nodeID, uint32_t cost)
{
    if (nodeID >= 0 && nodeID < 256)
    {
        costs[nodeID] = cost;
    }
    else
    {
        fprintf(stderr, "Invalid Node ID: %d", nodeID);
    }
}

void updateConnection(int nodeID, bool connect)
{
    if (nodeID >= 0 && nodeID < 256)
    {
        connections[nodeID] = connect;
    }
    else
    {
        fprintf(stderr, "Invalid Node ID: %d", nodeID);
    }
}

//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char *buf, int length)
{
    int i;
    for (i = 0; i < 256; i++)
        if (i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
            sendto(globalSocketUDP, buf, length, 0,
                   (struct sockaddr *)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void *announceToNeighbors(void *unusedParam)
{
    struct timespec sleepFor;
    sleepFor.tv_sec = 0;
    sleepFor.tv_nsec = 300 * 1000 * 1000; //300 ms
    while (1)
    {
        hackyBroadcast("HEREIAM", 7);
        nanosleep(&sleepFor, 0);
    }
}

void listenForNeighbors()
{
    char fromAddr[100];
    struct sockaddr_in theirAddr;
    socklen_t theirAddrLen;
    char recvBuf[1000];

    int bytesRecvd;
    while (1)
    {
        theirAddrLen = sizeof(theirAddr);
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000, 0,
                                   (struct sockaddr *)&theirAddr, &theirAddrLen)) == -1)
        {
            perror("connectivity listener: recvfrom failed");
            exit(1);
        }

        inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);

        short int heardFrom = -1;
        if (strstr(fromAddr, "10.1.1."))
        {
            heardFrom = atoi(
                strchr(strchr(strchr(fromAddr, '.') + 1, '.') + 1, '.') + 1);

            //TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.
            updateConnection(heardFrom, true);

            //record that we heard from heardFrom just now.
            gettimeofday(&globalLastHeartbeat[heardFrom], 0);
        }

        //Is it a packet from the manager? (see mp2 specification for more details)
        //send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
        if (!strncmp(recvBuf, "send", 4))
        {
            //TODO send the requested message to the requested destination node
        }
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
        else if (!strncmp(recvBuf, "cost", 4))
        {
            //TODO record the cost change (remember, the link might currently be down! in that case,
            //this is the new cost you should treat it as having once it comes back up.)

            updateCost(ntohs(recvBuf[4]), ntohl(recvBuf[6]));
        }

        //TODO now check for the various types of packets you use in your own protocol
        //else if(!strncmp(recvBuf, "your other message types", ))
        // ...
    }
    //(should never reach here)
    close(globalSocketUDP);
}
