#include <iostream>

// end <0 or 1>, seqNum <net order int 4 bytes>, DataSize <net order int 4 bytes>, data <data_size big>
// returns size of frame
int create_send_frame(char *frame, int seq_num, char *data, int data_size, int end)
{
    frame[0] = end ? 0x0 : 0x1;
    uint32_t nSeq = htonl(seq_num);
    uint32_t nDataSize = htonl(data_size);
    memcpy(frame + 1, &nSeq, sizeof(uint32_t));
    memcpy(frame + 5, &nDataSize, sizeof(uint32_t));
    memcpy(frame + 9, data, data_size);
    // TODO: checksum?

    return data_size + (int)9;
}

// create ack frame
int create_ack_frame(char *frame, int seq_num)
{
    uint32_t nSeq = htonl(seq_num);
    memcpy(frame + 1, &nSeq, 4);
    // TODO: maybe send ack if error?
}