#ifndef CODE_H
#define CODE_H

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "../lib/rs.h"
#include "nat.h"

#define DEC_TIMEOUT ((long)1e10)
#define ENC_TIMEOUT ((long)1e6)

#define MAX_BLOCK_SIZE (1500 - 20 - 8 - 24) // 1448
#define MAX_DATA_NUM 64
#define MAX_PACKET_BUF MAX_BLOCK_SIZE *MAX_DATA_NUM // 46336

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))

struct enc_record
{
    in_addr_t client_vpn_ip;
    in_port_t client_vpn_port;

    unsigned char *packet_buf;
    unsigned int data_size;
    unsigned int packet_num;

    unsigned char *extra_packet;
    unsigned int extra_size;

    pthread_cond_t cond;
    pthread_mutex_t mutex;

    struct timespec touch;

    struct enc_record *next;
} *enc_table;
pthread_mutex_t enc_table_mutex;

struct dec_record
{
    unsigned int hash_code;

    unsigned int data_size;
    unsigned int block_size;

    unsigned int data_num;
    unsigned int block_num;
    unsigned int receive_num;

    unsigned char decoded;

    unsigned char **data_blocks;
    unsigned char *marks;

    pthread_mutex_t mutex;

    struct timespec touch;

    struct dec_record *next;
} *dec_table;
pthread_mutex_t dec_table_mutex;

struct input_param
{
    pthread_t tid;

    unsigned char *packet;
    unsigned int packet_size;

    int udp_fd;

    in_addr_t client_vpn_ip;
    in_port_t client_vpn_port;
};

struct output_param
{
    pthread_t tid;

    unsigned char *packet;
    unsigned int packet_size;

    int tun_fd;

    in_addr_t client_vpn_ip;
    in_port_t client_vpn_port;
};

struct enc_param
{
    pthread_t tid;

    struct enc_record *enc;

    int udp_fd;

    in_addr_t client_vpn_ip;
    in_port_t client_vpn_port;
};

struct dec_param
{
    pthread_t tid;

    struct dec_record *dec;

    int tun_fd;

    in_addr_t client_vpn_ip;
    in_port_t client_vpn_port;
};

void *serve_input(void *args);

struct enc_record *enc_get(struct sockaddr_in *client_addr, int udp_fd);

void *encode(void *args);

void enc_delete(in_addr_t client_vpn_ip, in_port_t client_vpn_port);

void free_enc(struct enc_record *record);

unsigned int get_hash_code();

void *serve_output(void *args);

struct dec_record *dec_get(int hash_code, int data_size, int block_size, int data_num, int block_num);

void *decode(void *args);

void free_dec(struct dec_record *record);

#endif
