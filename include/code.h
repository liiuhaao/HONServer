#ifndef CODE_H
#define CODE_H

#include <pthread.h>
#include <stdio.h>
#include "../lib/rs.h"
#include "trans.h"

#define DEC_TIMEOUT 60

struct dec_param
{
    struct dec_record *dec;

    int tun_fd;

    in_addr_t clinet_vpn_ip;
    in_port_t clinet_vpn_port;
};

struct enc_record
{
    in_addr_t clinet_vpn_ip;
    in_port_t clinet_vpn_port;

    unsigned char *packet_buf;
    unsigned int packet_num;

    pthread_t tid;

    struct enc_record *next;
} *enc_table;

struct dec_record
{
    unsigned int hash_code;

    unsigned int data_size;
    unsigned int block_size;

    unsigned int data_num;
    unsigned int block_num;
    unsigned int receive_num;

    unsigned char **data_blocks;
    unsigned char *marks;

    time_t touch;

    struct dec_record *next;
} *dec_table;

void *decode(void *args);

#endif
