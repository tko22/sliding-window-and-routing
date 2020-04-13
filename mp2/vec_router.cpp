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

#include "monitor_neighbors.cpp"

#define LOG_LINE_LENGTH 100
#define NUM_NODES 256
#define HEARTBEAT_TIMEOUT 1
#define MESSAGE_BUF_SIZE 120
// TODO: Verify this limit (check with line topology)
#define INFINITY_LIMIT = 512

typedef struct Entry {
    int32_t dist;
    int16_t nextHop;
} Entry;

int globalMyID = 0;
// last time you heard from each node. TODO: you will want to monitor this
// in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[NUM_NODES];

// our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
// pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[NUM_NODES];

// Indices of nodes with active links
bool connections[NUM_NODES];
// Costs for each node
int32_t costs[NUM_NODES];

Entry dvTable[NUM_NODES];

char *logFile;

// LOCAL HELPER FUNCTIONS
// Check heartbeats for expired connections
void checkHeartbeats() {
    timeval now;
    gettimeofday(&now, 0);

    for (int i = 0; i < NUM_NODES; ++i) {
        if (now.tv_sec - globalLastHeartbeat[i].tv_sec >= HEARTBEAT_TIMEOUT) {
            connections[i] = false;

            updateDistVec(i, -1, -1);
        }
    }
}

// Update local Distance Vector data and send messages to neighbors if necessary
void updateDistVec(int16_t nodeID, int32_t dist, int_16_t nextHop) {
    if (nodeID >= 0 && nodeID < NUM_NODES) {
        // Check to make sure that the update is necessary
        if (dvTable[nodeID].dist < dist ||
            (nextHop < dvTable[nodeID].nextHop &&
             dvTable[nodeID].dist == dist) ||
            dvTable[nodeID].dist != dist &&
                (dvTable[nodeID].nextHop == nextHop ||
                 dvTable[nodeID].dist == -1)) {
            if (dist > INFINITY_LIMIT) {
                dist = -1;
            }
            // Update Table
            dvTable[nodeID].nextHop = nextHop;
            dvTable[nodeID].dist = dist;

            // Tell neighbors about update
            sendDistVecUpdate(nodeID, dist);
        }
    } else {
        fprintf(stderr, "Invalid nodeID: %d", nodeID);
    }
}

// SEND FUNCTIONS
// Send Distance Vector update message to neighbors
void sendDistVecUpdate(int16_t nodeID, int32_t dist) {
    char sendBuf[MESSAGE_BUF_SIZE];
    char poisonedReverseBuf[MESSAGE_BUF_SIZE];
    int32_t poisonDist = -1;
    int num_bytes = 4 + sizeof(int16_t) + sizeof(int32_t);

    strcpy(sendBuf, "dist");
    memcpy(sendBuf + 4, &nodeID, sizeof(int16_t));
    memcpy(sendBuf + 4 + sizeof(int16_t), &dist, sizeof(int32_t));

    strcpy(poisonedReverseBuf, "dist");
    memcpy(poisonedReverseBuf + 4, &nodeID, sizeof(int16_t));
    memcpy(poisonedReverseBuf + 4 + sizeof(int16_t), &poisonDist,
           sizeof(int32_t));

    for (int i = 0; i < NUM_NODES; ++i) {
        if (connections[nodeID]) {
            if (i == dvTable[nodeID].nextHop) {
                sendMessage(i, poisonedReverseBuf, num_bytes)
            } else {
                sendMessage(i, sendBuf, num_bytes)
            }
        }
    }
}

// Forward message to any host using Distance Vector Routing
void forwardMessage(int16_t nodeID, char *msg, int length, bool originate) {
    char sendBuf[MESSAGE_BUF_SIZE];
    int num_bytes = 4 + sizeof(int16_t) + sizeof(int32_t);

    strcpy(sendBuf, "dist");
    memcpy(sendBuf + 4, &nodeID, sizeof(int16_t));
    strcpy(sendBuf + 4 + sizeof(int16_t), msg);

    if (dvTable[nodeID].nextHop != -1) {
        sendMessage(dvTable[nodeID].nextHop, msg, length + 6);

        if (originate) {
            writeSendLog(logFile, nodeID, dvTable[nodeID].nextHop, msg);
        } else {
            writeForwardLog(logFile, nodeID, dvTable[nodeID].nextHop, msg);
        }
    } else {
        writeUnreachableLog(logFile, nodeID)
    }
}

// Send message directly to connected host
void sendMessage(int16_t nodeID, char *msg, int length) {
    if (nodeID >= 0 && nodeID < NUM_NODES && connections[nodeID]) {
        if (sendto(globalSocketUDP, msg, length, 0,
                   (struct sockaddr *)&globalNodeAddrs[nodeID],
                   sizeof(globalNodeAddrs[nodeID])) < 0) {
            fprintf(stderr, "Error sending to node: %d", nodeID);
        }
    } else {
        fprintf(stderr, "There is no direct connection to this node: %d",
                nodeID);
    }
}

// Monitor Neighbors
void vecListenForNeighbors() {
    char fromAddr[100];
    struct sockaddr_in theirAddr;
    socklen_t theirAddrLen;
    char recvBuf[1000];

    int bytesRecvd;
    while (1) {
        theirAddrLen = sizeof(theirAddr);
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000, 0,
                                   (struct sockaddr *)&theirAddr,
                                   &theirAddrLen)) == -1) {
            perror("connectivity listener: recvfrom failed");
            exit(1);
        }

        inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);

        short int heardFrom = -1;
        if (strstr(fromAddr, "10.1.1.")) {
            heardFrom = atoi(
                strchr(strchr(strchr(fromAddr, '.') + 1, '.') + 1, '.') + 1);

            updateConnection(heardFrom, true);
            updateDistVec(heardFrom, costs[heardFrom], heardFrom);

            // record that we heard from heardFrom just now.
            gettimeofday(&globalLastHeartbeat[heardFrom], 0);
        }

        checkHeartbeats();

        // Is it a packet from the manager? (see mp2 specification for more
        // details) send format: 'send'<4 ASCII bytes>, destID<net order 2 byte
        // signed>, <some ASCII message>
        if (!strncmp(recvBuf, "send", 4)) {
            int16_t destNode = ntohs(*((int16_t *)recvBuf + 4));
            recvBuf[bytesRecvd] = '\0';

            forwardMessage(destNode, recvBuf + 6, bytesRecvd - 5, true)
        }
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net
        // order 4 byte signed>
        else if (!strncmp(recvBuf, "cost", 4)) {
            int16_t nodeID = ntohs(*((int16_t *)recvBuf + 4));
            int32_t cost = ntohl(*((int32_t *)recvBuf + 6));

            updateCost(nodeID, cost);

            if (connections[nodeID]) {
                updateDistVec(nodeID, cost, nodeID);
            }
        }
        //'mesg'<4 ASCII bytes>, destID<host order 2 bytes signed>, msg<ASCII
        // message>
        else if (!strncmp(recvBuf, "mesg", 4)) {
            int16_t destNode = ntohs(*((int16_t *)recvBuf + 4));

            if (destNode == globalMyID) {
                writeReceiveLog(logFile, recvBuf + 6)
            } else {
                forwardMessage(destNode, recvBuf + 6, bytesRecvd - 6, false)
            }
        }
        //'dist'<4 ASCII bytes>, nodeID<host order 2 byte signed>, newDist<host
        // order 4 byte signed>
        else if (!strncmp(recvBuf, "dist", 4)) {
            if (heardFrom != -1) {
                int16_t nodeID = ntohs(*((int16_t *)recvBuf + 4));
                int32_t cost = ntohl(*((int32_t *)recvBuf + 6));

                updateDistVec(nodeID, cost, heardFrom);
            }
        }

        //(should never reach here)
        close(globalSocketUDP);
    }

    int main(int argc, char **argv) {
        if (argc != 4) {
            fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n",
                    argv[0]);
            exit(1);
        }

        // initialization: get this process's node ID, record what time it is,
        // and set up our sockaddr_in's for sending to the other nodes.
        globalMyID = atoi(argv[1]);
        int i;
        for (i = 0; i < NUM_NODES; ++i) {
            gettimeofday(&globalLastHeartbeat[i], 0);

            char tempaddr[100];
            sprintf(tempaddr, "10.1.1.%d", i);
            memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
            globalNodeAddrs[i].sin_family = AF_INET;
            globalNodeAddrs[i].sin_port = htons(7777);
            inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
        }

        FILE *fp;
        int buffLen = 50;
        char buff[buffLen];
        char *costPtr;
        int nodeID;

        fp = fopen(argv[2], "r");
        if (fp == NULL) {
            perror("Invalid initial costs file");
            return -1;
        }

        while (fgets(buff, buffLen, fp)) {
            costPtr = strchr(buff, ' ');
            *costPtr = '\0';
            costPtr++;

            updateCost(atoi(buff), atoi(costPtr));
        }

        for (int i = 0; i < NUM_NODES; ++i) {
            if (costs[i] == 0) {
                costs[i] = 1;
            }
        }

        costs[globalMyID] = 0;

        fclose(fp);

        // socket() and bind() our socket. We will do all sendto()ing and
        // recvfrom()ing on this one.
        if ((globalSocketUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
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
        if (bind(globalSocketUDP, (struct sockaddr *)&bindAddr,
                 sizeof(struct sockaddr_in)) < 0) {
            perror("bind");
            close(globalSocketUDP);
            exit(1);
        }

        logFile = argv[3];

        // start threads... feel free to add your own, and to remove the
        // provided ones.
        pthread_t announcerThread;
        pthread_create(&announcerThread, 0, announceToNeighbors, (void *)0);

        // good luck, have fun!
        vecListenForNeighbors();
    }