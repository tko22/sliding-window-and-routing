#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <iostream>

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

// flood
void sendPacketToNeighbor(int exceptID, char *buf)
{
    std::cout << "sending LSP flood packet to neighbors" << endl;
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
    std::cout << "floodlsp Called" << endl;
    int i;
    for (i = 0; i < 256; i++)

        if (i != globalMyID && connections[i] == true)
        {
            std::cout << "floodlsp to node: " << i << "-- lsp: " << globalMyID << " -> " << i << endl;
            // neighbor - broadcast
            short int node1 = htons(globalMyID);
            short int node2 = htons(i);

            int cost = htonl(costs[i]);
            int seqNum = htonl(sequenceNum);
            int ttl = htonl(50);
            char sendBuf[2 + sizeof(short int) + sizeof(short int) + sizeof(int) + sizeof(int) + sizeof(int)];
            strcpy(sendBuf, "ls");
            memcpy(sendBuf + 2, &node1, sizeof(short int));
            memcpy(sendBuf + 2 + sizeof(short int), &node2, sizeof(short int));
            memcpy(sendBuf + 2 + sizeof(short int) + sizeof(short int), &cost, sizeof(int));
            memcpy(sendBuf + 2 + sizeof(short int) + sizeof(short int) + sizeof(int), &seqNum, sizeof(int));
            memcpy(sendBuf + 2 + sizeof(short int) + sizeof(short int) + sizeof(int) + sizeof(int), &ttl, sizeof(int));
            sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
                   (struct sockaddr *)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
        }
}

//forward format: 'forward'<7 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
void sendForwardPacket(int nextHop, int dest, char *message)
{
    std::cout << "Forwarding Packet to " << nextHop << " with dest: " << dest << endl;
    char sendBuf[7 + sizeof(short int) + strlen(message)];
    short int no_destID = htons(dest);
    strcpy(sendBuf, "forward");
    memcpy(sendBuf + 7, &no_destID, sizeof(short int));
    memcpy(sendBuf + 7 + sizeof(short int), message, strlen(message));
    sendto(globalSocketUDP, sendBuf, sizeof(sendBuf), 0,
           (struct sockaddr *)&globalNodeAddrs[nextHop], sizeof(globalNodeAddrs[nextHop]));
}

void printVectors(std::vector<int> const &input)
{
    std::cout << "[";
    for (int i = 0; i < input.size(); i++)
    {
        std::cout << input.at(i) << ",";
    }
    std::cout << "]" << endl;
}

void printVectorEntry(std::vector<Entry> const &input)
{
    std::cout << "[";
    for (int i = 0; i < input.size(); i++)
    {
        std::cout << "{" << input.at(i).dest << "," << input.at(i).cost << "," << input.at(i).nexthop << "}, ";
    }
    std::cout << "]" << endl;
}

void printEntry(Entry e)
{
    std::cout << "{" << e.dest << "," << e.cost << "," << e.nexthop << '}';
}

void printMap(std::map<int, Entry> &myMap)
{
    std::cout << "{";
    for (auto it = myMap.cbegin(); it != myMap.cend(); ++it)
    {
        std::cout << it->first << ": ";
        printEntry(it->second);
        std::cout << ", ";
    }
    std::cout << "}" << endl;
}
/** -------------- DIJKSTRAS ---------------- **/

// https://cseweb.ucsd.edu/classes/fa10/cse123/lectures/disc.123-fa10-l8.pdf
void updateFwdTable(std::map<int, Entry> &confirmedMap, int adjMatrix[256][256])
{
    std::cout << "updatefwdtable" << endl;
    std::vector<Entry> tentativeTable;
    std::vector<int> neighborList; // list of nextHops

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
            Entry e{i, costs[i], i}; // neighbor 23, cost 1 => {23,1,23}
            tentativeTable.push_back(e);
            neighborList.push_back(i); // add to neighborlist (list of nextHops)
        }
    }
    std::cout << "Added neighbors: " << endl;
    printVectors(neighborList);
    std::cout << "tentative list" << endl;
    printVectorEntry(tentativeTable);

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
        std::cout << "loop size: " << tentativeTable.size() << endl;
        // 1) pick lowest cost in tentative
        int lowest_cost = tentativeTable[0].cost;
        int next_idx = 0; // tentative table index for next

        for (i = 1; i < tentativeTable.size(); i++)
        {
            if (tentativeTable[i].cost < lowest_cost)
            {
                lowest_cost = tentativeTable[i].cost;
                next_idx = i;
            }
        }

        // add Next to ConfirmedMap & remove from tentativeTable
        Entry nextEntry{tentativeTable[next_idx].dest, tentativeTable[next_idx].cost, tentativeTable[next_idx].nexthop};
        tentativeTable.erase(tentativeTable.begin() + next_idx);
        confirmedMap[nextEntry.dest] = nextEntry;

        std::cout << "next Entry: " << nextEntry.dest << endl;
        std::cout << "tentative table after remove next entry: ";
        printVectorEntry(tentativeTable);

        // 2) get next hop from Next
        int nextHop = nextEntry.nexthop;
        int nextID = nextEntry.dest;

        std::cout << "checking neigbhors of nextID: " << nextID << endl;
        // add it's neighbors
        for (i = 0; i < 256; i++)
        {
            if (adjMatrix[nextID][i] > 0)
            {
                // i is neighbor to Next

                //Cost(me, Neighbor - i) = Cost(me, Next) + Cost(Next, Neighbor - i)
                std::cout << "i: " << i << " is neighbor to nextID:" << nextID << endl;
                int cost = nextEntry.cost + adjMatrix[nextID][i];
                std::cout << "new cost: " << cost << endl;
                bool in_tent = false;
                // if neighbor(i) is in tentative, update cost and nextHop
                for (int x = 0; x < tentativeTable.size(); x++)
                {
                    if (tentativeTable[x].dest == i)
                    {
                        std::cout << "found neighbor in tentative, check update cost" << endl;
                        in_tent = true; // for next check, if not in tentative and confirmed
                        // if new cost is lower than current tentative cost
                        std::cout << "tentative table cost for x: " << x << " is: " << tentativeTable[x].cost << endl;
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
                    std::cout << "i isn't in tentative" << endl;
                    std::cout << "add new neighbor i: " << i << endl;
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
        std::cout << "done" << endl;
    }
    std::cout << "confirmed table   ";
    printMap(confirmedMap);
    std::cout << "exit updatefwdtable \n"
              << endl;
    return;
}
