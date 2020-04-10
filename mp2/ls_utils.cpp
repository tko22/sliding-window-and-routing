#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>

#define V 256 // number of nodes

//memset(ret, '\0', sizeof ret);

typedef struct
{
    int dest; // ID of itself
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

/** ------------------- SEND PACKETS -------------------- **/

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

/** -------------- DIJKSTRAS ---------------- **/

int minDistance(int dist[], bool sptSet[])
{
    // Initialize min value
    int min = INT_MAX, min_index;

    for (int v = 0; v < V; v++)
        if (sptSet[v] == false && dist[v] <= min)
            min = dist[v], min_index = v;

    return min_index;
}

void dijkstra(int **graph, int src, int *dist, int *parent)
{

    // sptSet[i] will true if vertex i is included/in
    // shortest path tree or shortest distance from src to i is finalized
    bool sptSet[V];

    // Initialize all distances as INFINITE and stpSet[] as false
    for (int i = 0; i < V; i++)
    {
        parent[0] = -1;
        dist[i] = INT_MAX;
        sptSet[i] = false;
    }

    // Distance of source vertex from itself is always 0
    dist[src] = 0;

    // Find shortest path for all vertices
    for (int count = 0; count < V - 1; count++)
    {
        // Pick the minimum distance vertex from the set of
        // vertices not yet processed. u is always equal to src in first iteration.
        int u = minDistance(dist, sptSet);

        // Mark the picked vertex as processed
        sptSet[u] = true;

        // Update dist value of the adjacent vertices of the picked vertex.
        for (int v = 0; v < V; v++)
        {

            // Update dist[v] only if is
            // not in sptSet, there is
            // an edge from u to v, and
            // total weight of path from
            // src to v through u is smaller
            // than current value of
            // dist[v]
            if (!sptSet[v] && graph[u][v] && dist[u] + graph[u][v] < dist[v])
            {
                parent[v] = u;
                dist[v] = dist[u] + graph[u][v];
            }
        }
    }
    // print the constructed
    // distance array
    // printSolution(dist, V, parent);
}

// https://cseweb.ucsd.edu/classes/fa10/cse123/lectures/disc.123-fa10-l8.pdf
void updateFwdTable(std::map<int, Entry> &confirmedMap, bool connections[256], int adjMatrix[256][256], unsigned int costs[256])
{
    std::vector<Entry> tentativeTable;
    std::vector<int> neighborList; // list of nextHops
    int dist[V];                   // The output array. dist[i] will hold the shortest distance from src to i
    int parent[V];                 // Parent array to store shortest path tree

    // Algorithm Start
    // wipe confirmedMap to start new
    confirmedMap.clear();

    // Add self to Confirmed
    Entry s{globalMyID, 0, -1}; // { myid, 0, -}
    confirmedMap[globalMyID] = s;

    /** NEIGHBOR STUFF **/
    // add all neighbors to tentative table with costs
    int i;
    for (i = 0; i < 256; i++)
    {
        if (connections[i] == true)
        {
            // hopefully they dont give us numbers bigger than MAXINT
            Entry e{i, static_cast<int>(costs[i]), i}; // neighbor 23, cost 1 => {23,1,23}
            tentativeTable.push_back(e);
            neighborList.push_back(i); // add to neighborlist (list of nextHops)
        }
    }

    // keep going until tentative table is empty, else
    // 1) pick lowest cost in tentative and put into confirmed - Next
    // 2) get NextHop from Next
    // 3) For each neighbor,
    //      Cost(me, Neighbor) = Cost(me, Next) + Cost(Next, Neighbor)
    //      If Neighbor is on Tentative
    //          if cost is smaller, update cost and nextHop
    //      If Neighbor not on Tentative or Confirmed:
    //          add to tentative with cost, nextHop
    while (tentativeTable.size() > 0)
    {
        // 1) pick lowest cost in tentative
        int lowest_cost = tentativeTable[0].cost;
        int next_idx = 0;

        for (i = 1; i < tentativeTable.size(); i++)
        {
            if (tentativeTable[i].cost < lowest_cost)
            {
                lowest_cost = tentativeTable[i].cost;
                next_idx = i;
            }
        }

        // add Next to ConfirmedMap & remove from tentativeTable
        Entry nextEntry = tentativeTable[i];
        tentativeTable.erase(tentativeTable.begin() + next_idx);
        confirmedMap[nextEntry.dest] = nextEntry;

        // 2) get next hop from Next
        int nextHop = nextEntry.nexthop;
        int nextID = nextEntry.dest;

        // add it's neighbors
        for (i = 0; i < 256; i++)
        {
            if (adjMatrix[nextID][i] > 0)
            {
                // i is neighbor to Next

                //Cost(me, Neighbor - i) = Cost(me, Next) + Cost(Next, Neighbor - i)
                int cost = nextEntry.cost + adjMatrix[nextID][i];

                bool in_tent = false;
                // if neighbor(i) is in tentative, update cost and nextHop

                for (int x = 0; x < tentativeTable.size(); x++)
                {
                    if (tentativeTable[x].dest == i)
                    {
                        in_tent = true; // for next check, if not in tentative and confirmed
                        // if new cost is lower than current tentative cost
                        if (tentativeTable[x].cost > cost)
                        {

                            tentativeTable[x].cost = cost;
                            tentativeTable[x].nexthop = nextHop;
                        }
                        break;
                    }
                }

                // if neighbor(i) not in tentative or confirmed, add to tentative
                if (in_tent == false && confirmedMap.find(i) == confirmedMap.end())
                {
                    // key doesnt exist - can't reach, not in tentative
                    // Add to tentative
                    // i is a neighbor
                    Entry e{
                        i,
                        cost,
                        nextHop};
                    tentativeTable.push_back(e);
                }
            }
        }
    }
}