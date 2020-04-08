#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>

#include "monitor_neighbors.cpp"
#include "ls_utils.cpp"

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

// sequence number for flooding
int sequenceNum = 0;

// adjacent matrix for graph to use with Dijkstra’s
int adjMatrix[256][256] = {{0}}; // initialize all elements to zero

// log file name
char *theLogFile;

std::map<int, Entry> confirmedMap;
Entry tentativeTable[256];

void lslistenForNeighbors()
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

            // if new connection
            if (connections[heardFrom] == false)
            {
                //this node can consider heardFrom to be directly connected to it; do any such logic now.
                updateConnection(heardFrom, true);

                // update adjacent matrix - undirected graph
                adjMatrix[globalMyID][heardFrom] = costs[heardFrom];
                adjMatrix[heardFrom][globalMyID] = costs[heardFrom];

                // flood to all neighbors - including the neighbor you just got a message from
                sequenceNum++; // increment sequence Number
                floodLSP(connections, sequenceNum);
            }

            //record that we heard from heardFrom just now.
            gettimeofday(&globalLastHeartbeat[heardFrom], 0);
        }

        //Is it a packet from the manager? (see mp2 specification for more details)
        //send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
        if (!strncmp(recvBuf, "send", 4))
        {
            //send the requested message to the requested destination node
            int dest = (int)ntohs(recvBuf[7]);
            // copy message over
            // TODO: make sure length is correct
            char message[bytesRecvd - 6 + 1]; // 100 is max size for message
            int i;
            for (i = 6; i < bytesRecvd; i++)
            {
                message[i - 6] = recvBuf[i];
            }

            // decide whether to send
            if (dest == globalMyID)
            {
                // if dest is itself, write to log, receive
                writeReceiveLog(theLogFile, message);
            }
            else
            {
                // check if you can reach node
                if (confirmedMap.find(dest) == confirmedMap.end())
                {
                    // key doesnt exist - can't reach
                    writeUnreachableLog(theLogFile, dest);
                }
                else
                {
                    int nextHop = confirmedMap[dest].nexthop;
                    // write send log message
                    writeSendLog(theLogFile, dest, nextHop, message);
                    sendForwardPacket(nextHop, dest, message);
                }
            }
        }
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
        else if (!strncmp(recvBuf, "cost", 4))
        {
            //TODO: record the cost change (remember, the link might currently be down! in that case,
            //this is the new cost you should treat it as having once it comes back up.)

            updateCost(ntohs(recvBuf[4]), ntohl(recvBuf[6]));
        }

        // 'ls'<2 ascii bytes> node 1<net order 2 byte signed> node 2<netorder 2byte signed> cost<net order 4 byte signed> seq_num<net order 4 byte signed> ttl<netorder 4 byte signed>
        // When routers propagate a new link (or send their own neighbors out)
        // link state packet
        else if (!strncmp(recvBuf, "ls", 2))
        {
            int node1 = (int)ntohs(recvBuf[2]);
            int node2 = (int)ntohs(recvBuf[4]);
            int seqNum = ntohl(recvBuf[10]);
            int ttl = ntohl(recvBuf[14]);
            ttl = ttl - 1; // subtract ttl

            // if haven't seen and ttl is still ok
            // TODO: check sequence number too, need to keep track it first
            if (adjMatrix[node1][node2] == 0 && ttl > 0)
            {

                adjMatrix[node1][node2] = ntohl(recvBuf[6]); // set costs
                adjMatrix[node2][node1] = ntohl(recvBuf[6]); // set costs

                // convert to netorder
                short int hNode1 = htons(node1);
                short int hNode2 = htons(node2);
                ttl = htonl(ttl);

                // forward it to neighbors else besides heardFrom
                char sendBuf[2 + sizeof(short int) + sizeof(short int) + 3 * sizeof(int)];
                strcpy(sendBuf, "ls");
                memcpy(sendBuf + 2, &hNode1, sizeof(short int));
                memcpy(sendBuf + 2 + sizeof(short int), &hNode2, sizeof(short int));
                memcpy(sendBuf + 2 + 2 * sizeof(short int), &recvBuf[6], sizeof(int)); // hopefully this works (&recvBuf? // TODO:
                memcpy(sendBuf + 2 + 2 * sizeof(short int) + sizeof(int), &recvBuf[10], sizeof(int));
                memcpy(sendBuf + 2 + 2 * sizeof(short int) + 2 * sizeof(int), &ttl, sizeof(int));
                sendPacketToNeighbor(heardFrom, sendBuf);

                // use threads if too slow
                // pthread_t updateThread;
                // pthread_create(&updateThread, 0, sendPacketToNeighbor, packetArgs);

                // TODO: do something with fwdTable
            }
            // don't flood if already seen
        }

        // got a forwarding packet
        //forward format: 'forward'<7 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
        else if (!strncmp(recvBuf, "forward", 7))
        {
            int dest = (int)ntohs(recvBuf[7]);
            // copy message over
            char message[bytesRecvd - 9 + 1]; // 100 is max size for message
            int i;
            for (i = 9; i < bytesRecvd; i++)
            {
                message[i - 9] = recvBuf[i];
            }

            // decide whether to forward or not
            if (dest == globalMyID)
            {
                // if dest is itself, write to log, receive
                writeReceiveLog(theLogFile, message);
            }
            else
            {

                // need to forward
                // look at forward table and send
                // check if unreachable (for debugging maybe print it out to see whether you did it wrong or not)
                if (confirmedMap.find(dest) == confirmedMap.end())
                {
                    // key doesnt exist - can't reach
                    writeUnreachableLog(theLogFile, dest);
                }
                else
                {
                    int nextHop = confirmedMap[dest].nexthop;
                    // write send log message
                    writeForwardLog(theLogFile, dest, nextHop, message);
                    sendForwardPacket(nextHop, dest, message);
                }
            }
        }
    }
    //(should never reach here)
    close(globalSocketUDP);
}

/** --------------- MAIN -------------------- **/

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

    int enable = 1;
    // set reuseaddr
    if (setsockopt(globalSocketUDP, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

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
    pthread_create(&announcerThread, 0, announceToNeighbors, (void *)0); // ping everyone that you can ping... your neighbors

    //good luck, have fun!
    lslistenForNeighbors();
}
