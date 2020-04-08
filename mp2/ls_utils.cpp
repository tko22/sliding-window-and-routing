#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>

//memset(ret, '\0', sizeof ret);

typedef struct
{
    int dest;
    int cost;
    int nexthop;
} Entry;

struct packetArg
{
    int exceptID;
    char *buf;
};

void setupAdjMatrix(int matrix[256][256], bool *connections, unsigned int *costs, int globalMyID)
{
    int i;
    for (i = 0; i < 256; i++)
    {
        if (connections[i] == true)
        {
            // symmetric graph
            matrix[globalMyID][i] = costs[i];
            matrix[i][globalMyID] = costs[i];
        }
    }
}

void sendPacketToNeighbor(int exceptID, char *buf)
{
    int i;
    for (i = 0; i < 256; i++)
        // send to neighbors except self and exceptID (used for not sending an update packet)
        if (i != globalMyID && connections[i] == true && i != exceptID)
            sendto(globalSocketUDP, buf, sizeof(buf), 0,
                   (struct sockaddr *)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

// 'ls'<2 ascii bytes> node 1<net order 2 byte signed> node 2<netorder 2byte signed> cost<net order 4 byte signed> seq_num<net order 4 byte signed> ttl<netorder 4 byte signed>
void floodLSP(bool *connections, int sequenceNum)
{
    int i;
    for (i = 0; i < 256; i++)

        if (i != globalMyID && connections[i] == true)
        {
            // neighbor - broadcast
            short int node1 = htons(globalMyID);
            short int node2 = htons(i);
            int cost = htonl(costs[i]);
            int seqNum = htonl(sequenceNum);
            int ttl = htonl(50);
            char sendBuf[2 + sizeof(short int) + sizeof(short int) + 3 * sizeof(int)];
            strcpy(sendBuf, "ls");
            memcpy(sendBuf + 2, &node1, sizeof(short int));
            memcpy(sendBuf + 2 + sizeof(short int), &node2, sizeof(short int));
            memcpy(sendBuf + 2 + 2 * sizeof(short int), &cost, sizeof(int));
            memcpy(sendBuf + 2 + 2 * sizeof(short int) + sizeof(int), &seqNum, sizeof(int));
            memcpy(sendBuf + 2 + 2 * sizeof(short int) + 2 * sizeof(int), &ttl, sizeof(int));
            sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
                   (struct sockaddr *)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
        }
}

//forward format: 'forward'<7 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
void sendForwardPacket(int nextHop, int dest, char *message)
{
    char sendBuf[7 + sizeof(short int) + strlen(message)];
    short int no_destID = htons(dest);
    strcpy(sendBuf, "forward");
    memcpy(sendBuf + 7, &no_destID, sizeof(short int));
    memcpy(sendBuf + 7 + sizeof(short int), message, strlen(message));
    sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
           (struct sockaddr *)&globalNodeAddrs[nextHop], sizeof(globalNodeAddrs[nextHop]));
}

void dijkstras(std::map<int, Entry> &confirmedMap, bool connections[256], int adjMatrix[256][256], int costs[255])
{
    std::vector<Entry> tentativeTable;
    std::vector<int> neighborList;
    // add all neighbors to tentative table with costs
    int i;
    for (i = 0; i < 256; i++)
    {
        if (connections[i] == true)
        {
            Entry e{i, costs[i], i}; // neighbor 23, cost 1 => {23,1,23}
            tentativeTable.push_back(e);
            neighborList.push_back(i); // add to neighborlist (list of nextHops)
        }
    }

    // if there are no neighbors
    if (tentativeTable.size() <= 0)
    {
        return;
    }

    // pick lowest cost tentative neighbor
    int lowest_cost = tentativeTable[0].cost;
    int lowest_neighbor_idx = 0;

    for (i = 1; i < tentativeTable.size(); i++)
    {
        if (tentativeTable[i].cost < lowest_cost)
        {
            lowest_cost = tentativeTable[i].cost;
            lowest_neighbor_idx = i;
        }
    }

    // add it's neighbors
    for (i = 0; i < 256; i++)
    {
        if (adjMatrix[lowest_neighbor_idx][i] > 0)
        {
            // i is a neighbor, nextHop is the lowest cost neighbor
            Entry e{i, adjMatrix[lowest_neighbor_idx][i], lowest_neighbor_idx};
        }
    }

    //TODO:
    // go through tentative table to update cost values (i think you need to run dikstras)

    // keep going until tentative table is empty
    // for adding node A's neighbors in the loop, nextHop would be the node A's nextHop. so if node A had a next Hop of 3 then it's neighbors will have a nexthop of 3.
}