#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <vector>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <time.h>

using namespace std;

#include "monitor_neighbors.cpp"
#include "ls_utils.cpp"

void updateCost(uint16_t nodeID, uint32_t cost);
void listenForNeighbors();
void *announceToNeighbors(void *unusedParam);

int globalMyID = 0;
//last time you heard from each node. you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];

// Indices of nodes with active links
bool connections[256];
// Costs for each node
int costs[256];

// sequence number for flooding
int sequenceNum = 0;

int seqNumMatrix[256][256] = {{0}}; // [source][neighbor]

// adjacent matrix for graph to use with Dijkstra’s
int adjMatrix[256][256] = {{0}}; // initialize all elements to zero, if nonzero, there is a connection

std::chrono::steady_clock::time_point lastTimeHeardFrom[256];
// log file name
char *theLogFile;

std::map<int, Entry> confirmedMap;

struct timeval floodInterval;

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
                cout << "\n"
                     << globalMyID << "!!! NEW CONNECTION with  " << heardFrom << "\n"
                     << endl;
                //this node can consider heardFrom to be directly connected to it; do any such logic now.
                updateConnection(heardFrom, true);

                // update adjacent matrix - undirected graph
                adjMatrix[globalMyID][heardFrom] = costs[heardFrom];
                adjMatrix[heardFrom][globalMyID] = costs[heardFrom];

                updateFwdTable(confirmedMap, adjMatrix);
                // flood to all neighbors - including the neighbor you just got a message from
                floodLSP(connections, seqNumMatrix, adjMatrix);
            }

            //record that we heard from heardFrom just now.
            gettimeofday(&globalLastHeartbeat[heardFrom], 0);
        }

        timeval now;
        gettimeofday(&now, 0);
        // if any connections are different by 3 pings then it is broken, send ls
        for (int x = 0; x < 256; x++)
        {
            // last time you heard from node x (which you already had a connection with) is more than 1 seconds
            if ((now.tv_sec - globalLastHeartbeat[x].tv_sec) >= 3 && connections[x] == true)
            {
                std::cout << "\n"
                          << "DOWN NODE ~~~~~~ link with " << x << " is down..." << endl;
                // set connection with x to false, no longer neighbor
                connections[x] = false;
                adjMatrix[globalMyID][x] = -1;
                adjMatrix[x][globalMyID] = -1;
                // send LS flood of 0 - link doesn't not exist
                updateFwdTable(confirmedMap, adjMatrix);
                sendDownLSP(seqNumMatrix, x);
            }
        }

        // if (now.tv_sec - floodInterval.tv_sec > 15)
        // {
        //     // flood periodically
        //     std::cout << "flooding periodically" << endl;
        //     gettimeofday(&floodInterval, 0);
        //     floodLSP(connections, seqNumMatrix, adjMatrix);
        // }

        //Is it a packet from the manager? (see mp2 specification for more details)
        //send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
        if (!strncmp(recvBuf, "send", 4))
        {
            std::cout << "\n"
                      << globalMyID << " -- Receive send -- " << endl;
            //send the requested message to the requested destination node
            short int newdest;
            memcpy(&newdest, recvBuf + 4, sizeof(short int));
            int dest = (int)ntohs(newdest);

            // copy message over
            char message[bytesRecvd - 6 + 1]; // 100 is max size for message
            memcpy(message, recvBuf + 6, bytesRecvd - 6);
            message[bytesRecvd - 6] = '\0';

            std::cout << "message: " << message << endl;
            cout << "send dest: " << dest << endl;

            // decide whether to send
            if (dest == globalMyID)
            {
                // if dest is itself, write to log, receive
                lswriteReceiveLog(theLogFile, message);
            }
            else
            {
                std::cout << "decide to send out packet" << endl;
                // check if you can reach node
                if (confirmedMap.find(dest) == confirmedMap.end())
                {
                    std::cout << "dest: " << dest << " unreachable. write to log" << endl;
                    // key doesnt exist - can't reach
                    lswriteUnreachableLog(theLogFile, dest);
                }
                else
                {
                    int nextHop = confirmedMap[dest].nexthop;
                    std::cout << "nexthop for dest: " << dest << " is " << nextHop << endl;
                    std::cout << "writing to log and sending..." << endl;
                    // write send log message
                    lswriteSendLog(theLogFile, dest, nextHop, message);

                    std::cout << "Forwarding Packet to " << nextHop << " with dest: " << dest << endl;
                    char sendBuf[7 + sizeof(short int) + strlen(message)];
                    short int no_destID = htons(dest);
                    strcpy(sendBuf, "forward");
                    memcpy(sendBuf + 7, &no_destID, sizeof(short int));
                    memcpy(sendBuf + 7 + sizeof(short int), message, strlen(message));
                    sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
                           (struct sockaddr *)&globalNodeAddrs[nextHop], sizeof(globalNodeAddrs[nextHop]));
                }
            }
            std::cout << globalMyID << " -- Confirmed table";
            printMap(confirmedMap);
            std::cout << " -- exit send receive -- \n"
                      << endl;
        }

        /* ---------- COST CHANGE ------------ */
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
        else if (!strncmp(recvBuf, "cost", 4))
        {
            cout << "\n"
                 << globalMyID << "-- Receive cost -- " << endl;
            //record the cost change (remember, the link might currently be down! in that case,
            //this is the new cost you should treat it as having once it comes back up.)

            // dest
            short int newdest;
            memcpy(&newdest, recvBuf + 4, sizeof(short int));
            int dest = (int)ntohs(newdest);

            // cost
            int cost;
            memcpy(&cost, recvBuf + 6, sizeof(int));
            cost = (int)ntohl(cost);

            updateCost(dest, cost);

            cout << "receive cost - dest: " << dest << endl;
            cout << "receive cost - costs: " << cost << endl;

            // if neighbor (if connection is there), flood LSP next cost
            if (connections[dest] == true)
            {
                std::cout << "going to flood because cost change for neighbor" << endl;
                // update adjMatrix, since link exists
                adjMatrix[globalMyID][dest] = cost;
                adjMatrix[dest][globalMyID] = cost;

                // convert to netorder
                short int selfID = htons(globalMyID);
                short int hdest = htons(dest);
                int ttl = htonl(13);
                int hCost = htonl(cost);

                // increment sequence number
                seqNumMatrix[globalMyID][dest]++;
                int seqNum = htonl(seqNumMatrix[globalMyID][dest]);

                // LSP flood it to neighbors
                char sendBuf[2 + sizeof(short int) + sizeof(short int) + 3 * sizeof(int)];
                strcpy(sendBuf, "ls");
                memcpy(sendBuf + 2, &selfID, sizeof(short int));
                memcpy(sendBuf + 2 + sizeof(short int), &hdest, sizeof(short int));
                memcpy(sendBuf + 2 + sizeof(short int) + sizeof(short int), &hCost, sizeof(int));
                memcpy(sendBuf + 2 + sizeof(short int) + sizeof(short int) + sizeof(int), &seqNum, sizeof(int));
                memcpy(sendBuf + 2 + sizeof(short int) + sizeof(short int) + sizeof(int) + sizeof(int), &ttl, sizeof(int));
                for (int i = 0; i < 256; i++)
                    // send to neighbors except self and exceptID (used for not sending an update packet)
                    if (i != globalMyID && connections[i] == true)
                        sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
                               (struct sockaddr *)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));

                updateFwdTable(confirmedMap, adjMatrix);
            }
            std::cout << globalMyID << " -- Confirmed table";
            printMap(confirmedMap);
            std::cout << " -- exit cost receive -- \n"
                      << endl;
        }

        /* ---------- LINK STATE FLOODING ------------ */
        // 'ls'<2 ascii bytes> node 1<net order 2 byte signed> node 2<netorder 2byte signed> cost<net order 4 byte signed> seq_num<net order 4 byte signed> ttl<netorder 4 byte signed>
        // When routers propagate a new link (or send their own neighbors out)
        // link state packet flood
        else if (!strncmp(recvBuf, "ls", 2))
        {
            cout << "\n"
                 << globalMyID << " -- Recieve LSP flood from " << heardFrom << " -- " << endl;
            short int node1c; // always the source
            memcpy(&node1c, recvBuf + 2, sizeof(short int));
            int node1 = (int)ntohs(node1c);

            short int node2c;
            memcpy(&node2c, recvBuf + 2 + sizeof(short int), sizeof(short int));
            int node2 = (int)ntohs(node2c);

            // cost
            int wcost;
            memcpy(&wcost, recvBuf + 6, sizeof(int));
            int cost = (int)ntohl(wcost);

            // sequence Number
            int seqNum;
            memcpy(&seqNum, recvBuf + 10, sizeof(int));
            seqNum = (int)ntohl(seqNum);

            // ttl
            int ttl;
            memcpy(&ttl, recvBuf + 14, sizeof(int));
            ttl = (int)ntohl(ttl);
            ttl = ttl - 1; // subtract ttl

            std::cout << "node1 " << node1 << "-> node2 " << node2 << " -- with ttl " << ttl << endl;
            std::cout << "cost " << cost << endl;
            std::cout << "ls seqNum " << seqNum << endl;

            // if (haven't seen or cost change ) and ttl is still ok
            if (seqNum > seqNumMatrix[node1][node2] && ttl > 0)
            {
                // update sequence matrix
                seqNumMatrix[node1][node2] = seqNum;

                adjMatrix[node1][node2] = cost; // set costs
                adjMatrix[node2][node1] = cost; // set costs

                // update cost in cost table if it's my id
                if (node2 == globalMyID && costs[node1] != cost && cost > 0)
                {
                    cout << "node 2 is self... updating cost to node1 " << node1 << " with cost" << cost << endl;
                    updateCost(node1, cost);
                }
                else if (node1 == globalMyID && costs[node2] != cost && cost > 0)
                {
                    cout << "node 1 is self... updating cost to node2 " << node2 << "with cost " << cost << endl;
                    updateCost(node2, cost);
                }

                // convert to netorder 
                short int hNode1 = htons(node1);
                short int hNode2 = htons(node2);
                int hSeq = htonl(seqNum);
                int hCost = htonl(cost);
                ttl = htonl(ttl);

                // forward it to neighbors else besides heardFrom
                char sendBuf[2 + sizeof(short int) + sizeof(short int) + 3 * sizeof(int)];
                strcpy(sendBuf, "ls");
                memcpy(sendBuf + 2, &hNode1, sizeof(short int));
                memcpy(sendBuf + 2 + sizeof(short int), &hNode2, sizeof(short int));
                memcpy(sendBuf + 2 + 2 * sizeof(short int), &hCost, sizeof(int));
                memcpy(sendBuf + 2 + 2 * sizeof(short int) + sizeof(int), &hSeq, sizeof(int));
                memcpy(sendBuf + 2 + 2 * sizeof(short int) + 2 * sizeof(int), &ttl, sizeof(int));
                for (int i = 0; i < 256; i++)
                    // send to neighbors except self and exceptID (used for not sending an update packet)
                    if (i != globalMyID && connections[i] == true && i != heardFrom)
                        sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
                               (struct sockaddr *)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));

                updateFwdTable(confirmedMap, adjMatrix);
            }
            else
            {
                std::cout << "... DISCARDING lsp... current seq: " << seqNumMatrix[node1][node2] << endl;
            }
            std::cout << globalMyID << " -- Confirmed table";
            printMap(confirmedMap);
            std::cout << "-- exit ls receive -- \n"
                      << endl;
            // don't flood if already seen
        }

        /* ---------- FORWARD PACKET ------------ */
        // got a forwarding packet
        //forward format: 'forward'<7 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
        else if (!strncmp(recvBuf, "forward", 7))
        {
            std::cout << "\n"
                      << globalMyID << " -- Receive forward -- " << endl;

            // dest
            short int newdest;
            memcpy(&newdest, recvBuf + 7, sizeof(short int));
            int dest = (int)ntohs(newdest);

            // copy message over
            char message[bytesRecvd - 9 + 1]; // 100 is max size for message
            int i;
            for (i = 9; i < bytesRecvd; i++)
            {
                message[i - 9] = recvBuf[i];
            }
            message[bytesRecvd - 9] = '\0';
            cout << "message for " << dest << " : " << message << endl;

            // decide whether to forward or not
            if (dest == globalMyID)
            {
                std::cout << "GOT PACKET: packet is MINE - message: " << message << endl;
                // if dest is itself, write to log, receive
                lswriteReceiveLog(theLogFile, message);
            }
            else
            {
                std::cout << "need to forward packet for " << dest << endl;
                // need to forward
                // look at forward table and send
                // check if unreachable (for debugging maybe print it out to see whether you did it wrong or not)
                if (confirmedMap.find(dest) == confirmedMap.end())
                {
                    std::cout << "dest: " << dest << " unreachable. write to log" << endl;
                    // key doesnt exist - can't reach
                    lswriteUnreachableLog(theLogFile, dest);
                }
                else
                {
                    int nextHop = confirmedMap[dest].nexthop;
                    std::cout << "nexthop for dest: " << dest << "is " << nextHop << endl;
                    std::cout << "writing to log and sending..." << endl;
                    // write send log message
                    lswriteForwardLog(theLogFile, dest, nextHop, message);

                    // remove null terminator
                    std::cout << "Forwarding Packet to " << nextHop << " with dest: " << dest << endl;
                    char sendBuf[7 + sizeof(short int) + strlen(message)];
                    short int no_destID = htons(dest);
                    strcpy(sendBuf, "forward");
                    memcpy(sendBuf + 7, &no_destID, sizeof(short int));
                    memcpy(sendBuf + 7 + sizeof(short int), message, strlen(message));
                    sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
                           (struct sockaddr *)&globalNodeAddrs[nextHop], sizeof(globalNodeAddrs[nextHop]));
                }
            }
            std::cout << globalMyID << " -- Confirmed table";
            printMap(confirmedMap);
            std::cout << "-- exit forward receive -- \n"
                      << endl;
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
    FILE *f;
    f = fopen(theLogFile, "a");
    fclose(f);

    //initialization: get this process's node ID, record what time it is,
    //and set up our sockaddr_in's for sending to the other nodes.
    globalMyID = atoi(argv[1]);

    std::cout << "Starting " << globalMyID << endl;
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

    // initialize floodInterval for periodic flooding
    gettimeofday(&floodInterval, 0);

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
    // set resuseport
    if (setsockopt(globalSocketUDP, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    char myAddr[100];
    struct sockaddr_in bindAddr;
    sprintf(myAddr, "10.1.1.%d", globalMyID);
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(7777);
    inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
    if (::bind(globalSocketUDP, (struct sockaddr *)&bindAddr, sizeof(struct sockaddr_in)) < 0)
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
