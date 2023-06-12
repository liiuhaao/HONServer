#ifndef CODE_H
#define CODE_H

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <linux/types.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/time.h>
#include "../lib/rs.h"

#define DEC_TIMEOUT ((long)1e7)
#define ENC_TIMEOUT ((long)1e3)
#define GROUP_TIMEOUT ((long)1e9)

#define MAX_BLOCK_SIZE (1200 - 20 - 8 - 24) // 1448
#define MAX_DATA_NUM 64
#define MAX_PACKET_BUF MAX_BLOCK_SIZE *MAX_DATA_NUM // 46336
#define MAX_PACKET_NUM 10
#define PARITY_RATE 0

#define INPUT 1
#define OUTPUT 0

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))

struct list
{
    void *data;
    struct list *next;
};

struct address
{
    in_addr_t ip;
    in_port_t port;
};

struct group
{
    unsigned int groupID;

    pthread_mutex_t mutex;

    struct list *udp_addrs;
    struct list *vpn_addrs;

    struct encoder *enc;
    struct decoder *dec;

    struct timespec touch;
};

struct encoder
{
    unsigned char *packet_buf;
    unsigned int data_size;
    unsigned int packet_num;

    pthread_mutex_t mutex;

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

    pthread_mutex_t mutex;

    struct timespec touch;
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

    in_addr_t udp_ip;
    in_port_t udp_port;
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

    struct decoder *dec;

    int tun_fd;

    in_addr_t client_vpn_ip;
    in_port_t client_vpn_port;
};

void *serve_input(void *args);
void *serve_output(void *args);

void *encode(void *args);
void *decode(void *args);

struct group *get_group(unsigned int groupID, struct address *addr, int udp_fd);
struct group *new_group(unsigned int groupID, unsigned int data_size, unsigned int block_size, unsigned int data_num, unsigned int block_num, in_addr_t udp_ip, in_port_t udp_port, struct address *addr);
void free_group(struct group *group);

struct address *get_packet_addr(unsigned char *buf, int in_or_out);
unsigned int get_packet_len(unsigned char *buf);

unsigned int get_random_groupID();

extern struct list *group_list;
extern pthread_mutex_t group_list_mutex;
extern struct list *group_iter;

#endif
