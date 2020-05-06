#include <iostream>

// end <0 or 1>, seqNum <net order int 4 bytes>, DataSize <net order int 4 bytes>, data <data_size big>
// returns size of frame
int create_send_frame(char *frame, int seq_num, char *data, int data_size, int end)
{
    frame[0] = end == 0 ? 0x0 : 0x1;
    uint32_t nSeq = htonl(seq_num);
    uint32_t nDataSize = htonl(data_size);

    memcpy(frame + 1, &nSeq, sizeof(uint32_t));
    memcpy(frame + 5, &nDataSize, sizeof(uint32_t));
    memcpy(frame + 9, data, data_size);
    // TODO: checksum?

    return 9 + data_size;
}

// create ack frame
void create_ack_frame(char *ack_frame, int seq_num)
{
    uint32_t nSeq = htonl(seq_num);
    memcpy(ack_frame, &nSeq, sizeof(uint32_t));
    // TODO: maybe send ack if error?
}

// read ack frame sent
// set values seq_num, ack
void read_ack(char *ack, int *seq_num)
{
    uint32_t nSeq;
    memcpy(&nSeq, ack, sizeof(uint32_t));
    *seq_num = ntohl(nSeq);
}

// reading frame
// set values: seq_num, data, data_size, end
void read_send_frame(char *frame, int *seq_num, char *data, int *data_size, int *end)
{
    *end = frame[0] == 0x0 ? 0 : 1; // 0x0 or 0x1

    // get sequence number
    uint32_t newSeq;
    memcpy(&newSeq, frame + 1, sizeof(uint32_t));
    *seq_num = (int)ntohl(newSeq);

    // get data size
    uint32_t newDataSize;
    memcpy(&newDataSize, frame + 5, sizeof(uint32_t));
    *data_size = (int)ntohl(newDataSize);

    // copy over data to data
    memcpy(data, frame + 9, *data_size);
}