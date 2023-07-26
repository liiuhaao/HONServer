#ifndef FEC_H
#define FEC_H

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <linux/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../lib/rs.h"

#define DEC_TIMEOUT ((long)1e6)
#define ENC_TIMEOUT ((long)1e5)
#define GROUP_TIMEOUT ((long)1e10)

#define MAX_BLOCK_SIZE (1200 - 20 - 8 - 24) // 1448
#define MAX_DATA_NUM 64
#define MAX_PACKET_BUF MAX_BLOCK_SIZE *MAX_DATA_NUM // 46336
#define MAX_PACKET_NUM 10
#define PARITY_RATE 20
#define RX_MAX_NUM 50

#define INPUT 1
#define OUTPUT 0

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))

struct list
{
    void *data;
    struct list *next;
};

struct group
{
    unsigned int groupID;

    struct list *udp_addrs;
    struct list *vpn_addrs;

    struct encoder *enc;
    struct decoder *dec;

    pthread_mutex_t mutex;

    struct timespec touch;
};

struct encoder
{
    unsigned char *packet_buf;
    unsigned int data_size;
    unsigned int packet_num;

    struct timespec touch;
};

struct decoder
{
    unsigned int data_size;
    unsigned int block_size;

    unsigned int data_num;
    unsigned int block_num;
    unsigned int receive_num;

    unsigned char decoded;

    unsigned char **data_blocks;
    unsigned char *marks;

    struct timespec touch;
};

struct rx_packet
{
    unsigned int id;
    unsigned char *packet;
    unsigned int packet_len;
};

struct input_param
{
    pthread_t tid;

    unsigned char *packet;
    unsigned int packet_size;

    int udp_fd;
};

struct output_param
{
    pthread_t tid;

    unsigned char *packet;
    unsigned int packet_size;

    int tun_fd;
    int udp_fd;

    struct sockaddr_in udp_addr;
};

struct enc_param
{
    pthread_t tid;

    struct encoder *enc;
    struct list *udp_addrs;

    int udp_fd;
};

struct dec_param
{
    pthread_t tid;

    struct group *group;

    int tun_fd;
};

extern pthread_mutex_t group_list_mutex;
extern struct list *group_iter;
extern struct list *group_before;

extern struct list *rx_list;
extern unsigned int rx_num;
extern unsigned int rx_id;
extern pthread_mutex_t rx_mutex;

extern unsigned int tx_id;
extern pthread_mutex_t tx_mutex;

void *serve_input(void *args);
void *serve_output(void *args);

void *encode(void *args);
void *decode(void *args);

void rx_insert(int tun_fd, unsigned char *buf, unsigned int len, unsigned int groupId);

struct group *get_group(unsigned int groupID, struct sockaddr_in *addr, int udp_fd);

struct group *new_group(unsigned int groupID, unsigned int data_size, unsigned int block_size, unsigned int data_num, unsigned int block_num);

void free_group(struct group *group);

struct sockaddr_in *get_packet_addr(unsigned char *buf, int in_or_out);

unsigned int get_packet_len(unsigned char *buf);

unsigned int get_groupId();

struct list *update_address_list(struct list *addr_list, struct sockaddr_in *addr);

#endif
