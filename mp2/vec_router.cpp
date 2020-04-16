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
#define HEARTBEAT_TIMEOUT 3
#define MESSAGE_BUF_SIZE 120
// TODO: Verify this limit (check with line topology)
#define INFINITY_LIMIT 1000
#define UPDATE_FREQ 1

typedef struct Entry {
    int32_t dist = -1;
    int16_t nextHop = -1;
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

time_t updateTime = 0;

char *logFile;

void printDVTable() {
    fprintf(stderr, "DV Table:\n");
    for (int i = 0; i < NUM_NODES; ++i) {
        if (dvTable[i].dist != -1) {
            fprintf(stderr, "\t(%d, %d, %d)\n", i, dvTable[i].dist,
                    dvTable[i].nextHop);
        }
    }
}

// SEND FUNCTIONS
// Send message directly to connected host
void sendMessage(int16_t nodeID, char *msg, int length) {
    if (nodeID >= 0 && nodeID < NUM_NODES && connections[nodeID]) {
        if (sendto(globalSocketUDP, msg, length, 0,
                   (struct sockaddr *)&globalNodeAddrs[nodeID],
                   sizeof(globalNodeAddrs[nodeID])) < 0) {
            fprintf(stderr, "Node %d - Error sending to node: %d - %s\n",
                    globalMyID, nodeID, msg);
        }
    } else {
        fprintf(stderr,
                "Node %d - There is no direct connection to this node: %d\n",
                globalMyID, nodeID);
    }
}

// Send Distance Vector update message to neighbors
void sendDistVecUpdate(int16_t nodeID, int32_t dist) {
    char sendBuf[MESSAGE_BUF_SIZE];
    char poisonedReverseBuf[MESSAGE_BUF_SIZE];
    int32_t poisonDist = -1;
    int num_bytes = 4 + sizeof(int16_t) + sizeof(int32_t);

    // fprintf(stderr, "Node %d - Sending a dist update %d - %d\n", globalMyID,
    // nodeID, dist);

    strcpy(sendBuf, "dist");
    memcpy(sendBuf + 4, &nodeID, sizeof(int16_t));
    memcpy(sendBuf + 4 + sizeof(int16_t), &dist, sizeof(int32_t));

    strcpy(poisonedReverseBuf, "dist");
    memcpy(poisonedReverseBuf + 4, &nodeID, sizeof(int16_t));
    memcpy(poisonedReverseBuf + 4 + sizeof(int16_t), &poisonDist,
           sizeof(int32_t));

    for (int i = 0; i < NUM_NODES; ++i) {
        if (connections[i]) {
            if (i == dvTable[nodeID].nextHop) {
                sendMessage(i, poisonedReverseBuf, num_bytes);
            } else {
                sendMessage(i, sendBuf, num_bytes);
            }
        }
    }
}

void checkUpdateTime() {
    timeval now;
    gettimeofday(&now, 0);

    if (now.tv_sec >= updateTime) {
        updateTime = now.tv_sec + UPDATE_FREQ;
        // fprintf(stderr, "Node %d - Sending dist updates...\n", globalMyID);
        for (int i = 0; i < NUM_NODES; ++i) {
            if (dvTable[i].dist != -1) {
                sendDistVecUpdate(i, dvTable[i].dist);
            }
        }
    }
}

// void *updateNeighborsDV(void *unusedParam) {
//     struct timespec sleepFor;
//     sleepFor.tv_sec = UPDATE_FREQ;
//     // sleepFor.tv_nsec = 300 * 1000 * 1000;  // 300 ms
//     while (1) {
//         for (int i = 0; i < NUM_NODES; ++i) {
//             if (dvTable[i].dist != -1) {
//                 sendDistVecUpdate(i, dvTable[i].dist);
//             }
//         }
//         nanosleep(&sleepFor, 0);
//     }
// }

// Forward message to any host using Distance Vector Routing
void forwardMessage(int16_t nodeID, char *msg, int length, bool originate) {
    char sendBuf[MESSAGE_BUF_SIZE];
    int num_bytes = 4 + sizeof(int16_t) + sizeof(int32_t);

    strcpy(sendBuf, "mesg");
    memcpy(sendBuf + 4, &nodeID, sizeof(int16_t));
    strcpy(sendBuf + 4 + sizeof(int16_t), msg);

    if (dvTable[nodeID].nextHop != -1) {
        // fprintf(stderr, "Node %d - Forwarding message to %d (%d) - %s
        // (%d)\n", globalMyID, nodeID, dvTable[nodeID].nextHop, msg, length);
        sendMessage(dvTable[nodeID].nextHop, sendBuf, length + 6);

        if (originate) {
            writeSendLog(logFile, nodeID, dvTable[nodeID].nextHop, msg);
        } else {
            writeForwardLog(logFile, nodeID, dvTable[nodeID].nextHop, msg);
        }
    } else {
        fprintf(stderr, "Node %d - Could not reach node %d", globalMyID, nodeID);
        writeUnreachableLog(logFile, nodeID);
    }
}

// LOCAL HELPER FUNCTIONS
// Update local Distance Vector data and send messages to neighbors if necessary
void updateDistVec(int16_t nodeID, int32_t dist, int16_t nextHop,
                   int callerID) {
    if (nodeID >= 0 && nodeID < NUM_NODES) {
        // If distance is over limit, set to infinity
        if (dist > INFINITY_LIMIT) {
            dist = -1;
        }

        // Check to make sure that the update is necessary
        // Update if found lower cost path
        bool lowerDist = dist != -1 && dist < dvTable[nodeID].dist;
        // Update if found same cost path with lower next hop ID
        bool lowerID = nextHop < dvTable[nodeID].nextHop && dvTable[nodeID].dist == dist;
        // Update if connection to next hop is lost
        bool connectionLost = dvTable[nodeID].nextHop != -1 && !connections[dvTable[nodeID].nextHop] && nextHop == -1;
        // Update if dist has changed for the existing next hop or if dist is infinity
        bool distChanged = dvTable[nodeID].dist != dist && (dvTable[nodeID].nextHop == nextHop || dvTable[nodeID].dist == -1);

        // If distance is infinity, no next hop
        if (dist == -1) {
            nextHop = -1;
        }

        // Cost to reach self is always 0
        if (nodeID != globalMyID && (lowerDist || lowerID || connectionLost || distChanged)) {
            if (dvTable[nodeID].dist != -1 && dist > dvTable[nodeID].dist) {
                fprintf(
                    stderr,
                    "Node %d - Update dist %d[%d, %d, %d, %d] (%d, %d, %d) -> (%d, %d, %d)\n",
                    globalMyID, lowerDist, lowerID, connectionLost, distChanged, callerID, nodeID, dvTable[nodeID].dist,
                    dvTable[nodeID].nextHop, nodeID, dist, nextHop);
            }
            // Update Table
            dvTable[nodeID].nextHop = nextHop;
            dvTable[nodeID].dist = dist;

            if (globalMyID == 1) {
                printDVTable();
            }

            // Tell neighbors about update
            sendDistVecUpdate(nodeID, dist);
        }
    } else {
        fprintf(stderr, "Node %d - Invalid nodeID: %d\n", globalMyID, nodeID);
    }
}

// Check heartbeats for expired connections
void checkHeartbeats() {
    timeval now;
    gettimeofday(&now, 0);
    for (int i = 0; i < NUM_NODES; ++i) {
        if (connections[i] &&
            now.tv_sec - globalLastHeartbeat[i].tv_sec >= HEARTBEAT_TIMEOUT) {
            connections[i] = false;

            fprintf(stderr, "Node %d - Connection to Node %d lost\n", globalMyID,
                    i);
            for (int j = 0; j < NUM_NODES; ++j) {
                // Bring down connections to all nodes routed through i
                if (dvTable[j].nextHop == i) {
                    updateDistVec(j, -1, -1, 0);
                }
            }
        }
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
            fprintf(stderr,
                    "Node %d - connectivity listener: recvfrom failed: %d\n",
                    globalMyID, errno);
            exit(1);
        }

        // fprintf(stderr, "Node %d - Received %s\n", globalMyID, recvBuf);
        inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);

        short int heardFrom = -1;
        if (strstr(fromAddr, "10.1.1.")) {
            heardFrom = atoi(
                strchr(strchr(strchr(fromAddr, '.') + 1, '.') + 1, '.') + 1);

            if (heardFrom != -1) {
                updateConnection(heardFrom, true);
                updateDistVec(heardFrom, costs[heardFrom], heardFrom, 1);

                // record that we heard from heardFrom just now.
                gettimeofday(&globalLastHeartbeat[heardFrom], 0);
            }
        }

        checkUpdateTime();
        checkHeartbeats();

        // Is it a packet from the manager? (see mp2 specification for more
        // details) send format: 'send'<4 ASCII bytes>, destID<net order 2 byte
        // signed>, <some ASCII message>
        if (!strncmp(recvBuf, "send", 4)) {
            int16_t destNode = ntohs(*((int16_t *)&recvBuf[4]));
            recvBuf[bytesRecvd] = '\0';

            // fprintf(stderr, "Node %d - Received send command: %d - %s\n",
            // globalMyID, destNode, recvBuf + 6);

            forwardMessage(destNode, recvBuf + 6, bytesRecvd - 5, true);
        }
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net
        // order 4 byte signed>
        else if (!strncmp(recvBuf, "cost", 4)) {
            int16_t nodeID = ntohs(*((int16_t *)&recvBuf[4]));
            int32_t cost = ntohl(*((int32_t *)&recvBuf[6]));

            // fprintf(stderr, "Node %d - Received cost update command: %d -
            // %d\n", globalMyID, nodeID, cost);

            updateCost(nodeID, cost);

            if (connections[nodeID]) {
                updateDistVec(nodeID, cost, nodeID, 2);
            }
        }
        //'mesg'<4 ASCII bytes>, destID<host order 2 bytes signed>, msg<ASCII
        // message>
        else if (!strncmp(recvBuf, "mesg", 4)) {
            int16_t destNode = *((int16_t *)&recvBuf[4]);

            fprintf(stderr, "Node %d - Received message from %d: %s\n",
                    globalMyID, heardFrom, recvBuf + 6);

            if (destNode == globalMyID) {
                writeReceiveLog(logFile, recvBuf + 6);
            } else {
                forwardMessage(destNode, recvBuf + 6, bytesRecvd - 6, false);
            }
        }
        //'dist'<4 ASCII bytes>, nodeID<host order 2 byte signed>, newDist<host
        // order 4 byte signed>
        else if (!strncmp(recvBuf, "dist", 4)) {
            if (heardFrom != -1) {
                int16_t nodeID = *((int16_t *)&recvBuf[4]);
                int32_t cost = *((int32_t *)&recvBuf[6]);

                // fprintf(stderr, "Node %d - Received dist %d - %d\n",
                // globalMyID, nodeID, cost);

                if (dvTable[heardFrom].dist >= 0) {
                    if (cost != -1) {
                        cost += costs[heardFrom];
                    }

                    updateDistVec(nodeID, cost, heardFrom, 3);
                } else {
                    fprintf(stderr,
                            "Node %d - Received dist %d - %d from unconnected "
                            "node: %d (%d (%d))\n",
                            globalMyID, nodeID, cost, heardFrom,
                            costs[heardFrom], connections[heardFrom]);
                }
            }
        }
    }

    //(should never reach here)
    close(globalSocketUDP);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,
                "Node %d - Usage: %s mynodeid initialcostsfile logfile\n\n",
                globalMyID, argv[0]);
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

    for (int i = 0; i < NUM_NODES; ++i) {
        connections[i] = false;
        costs[i] = 1;
    }

    FILE *fp;
    int buffLen = 50;
    char buff[buffLen];
    char *costPtr;
    int nodeID;

    fp = fopen(argv[2], "r");
    if (fp == NULL) {
        fprintf(stderr, "Node %d - Invalid initial costs file %s\n", globalMyID,
                argv[2]);
    } else {
        while (fgets(buff, buffLen, fp)) {
            costPtr = strchr(buff, ' ');
            *costPtr = '\0';
            costPtr++;

            updateCost(atoi(buff), atoi(costPtr));
        }

        costs[globalMyID] = 0;
        dvTable[globalMyID].nextHop = globalMyID;
        dvTable[globalMyID].dist = 0;

        fclose(fp);
    }

    // socket() and bind() our socket. We will do all sendto()ing and
    // recvfrom()ing on this one.
    if ((globalSocketUDP = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        fprintf(stderr, "Node %d - socket\n", globalMyID);
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
        fprintf(stderr, "Node %d - bind\n", globalMyID);
        close(globalSocketUDP);
        exit(1);
    }

    logFile = argv[3];
    remove(logFile);
    fp = fopen(logFile, "a");
    fclose(fp);

    // start threads... feel free to add your own, and to remove the
    // provided ones.
    pthread_t announcerThread;
    pthread_create(&announcerThread, 0, announceToNeighbors, (void *)0);

    // pthread_t updateThread;
    // pthread_create(&updateThread, 0, updateNeighborsDV, (void *)0);

    // good luck, have fun!
    vecListenForNeighbors();
}