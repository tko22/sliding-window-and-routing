#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

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