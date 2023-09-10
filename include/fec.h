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
#include <sys/time.h>
#include "../lib/rs.h"
#include "config.h"
#include "../lib/threadpool.h"

// #define DEC_TIMEOUT ((long)1e8)
// #define ENC_TIMEOUT ((long)1e8)
#define UDP_TIMEOUT ((long)1e9)

#define MAX_BLOCK_SIZE (1200 - 20 - 8 - 24) // 1448
#define MAX_DATA_NUM 64
#define MAX_PACKET_BUF MAX_BLOCK_SIZE *MAX_DATA_NUM // 46336
// #define MAX_PACKET_NUM 10
// #define PARITY_RATE 100
// #define RX_MAX_NUM 10

#define INPUT 1
#define OUTPUT 0

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))

struct list
{
    void *data;
    struct list *next;
};

struct time_pair
{
    long packet_send;
    struct timespec packet_receive;
};

struct udp_info
{
    struct sockaddr_in *addr;
    struct list *time_head;
    struct list *time_tail;
};

struct group
{
    unsigned int groupID;

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

    struct list *udp_infos;
    struct list *vpn_addrs;

    pthread_cond_t cond;
    pthread_mutex_t mutex;

    struct timespec touch;
};

struct decoder
{
    unsigned int groupID;

    unsigned int data_size;
    unsigned int block_size;

    unsigned int data_num;
    unsigned int block_num;
    unsigned int receive_num;

    unsigned char decoded;

    unsigned char **data_blocks;
    unsigned char *marks;

    struct list *udp_infos;

    int signaled;

    pthread_cond_t cond;
    pthread_mutex_t mutex;
};

struct rx_packet
{
    unsigned int id;
    unsigned char *packet;
    unsigned int packet_len;

    struct timespec touch;
};

struct input_param
{
    pthread_t tid;

    unsigned char *packet;
    unsigned int packet_size;

    int udp_fd;

    struct sockaddr_in udp_addr;
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

    struct group *group;
    struct encoder *enc;

    int udp_fd;
};

struct dec_param
{
    pthread_t tid;

    struct group *group;
    struct decoder *dec;

    int tun_fd;
};

extern pthread_mutex_t decoder_list_mutex;
extern struct list *decoder_iter;
extern struct list *decoder_before;

extern pthread_mutex_t encoder_list_mutex;
extern struct list *encoder_iter;
extern struct list *encoder_before;

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


void print_udp_infos(struct list *udp_infos);

void print_rx();

void rx_insert(int tun_fd, unsigned char *buf, unsigned int len, unsigned int groupId);

void clean_all_rx();

struct encoder *get_encoder(struct list *udp_infos, struct sockaddr_in *vpn_addr);

struct decoder *get_decoder(unsigned int groupID);

struct encoder *new_encoder(struct list *udp_infos);

struct decoder *new_decoder(unsigned int groupId, unsigned int data_size, unsigned int block_size, unsigned int data_num, unsigned int block_num);

void free_encoder(struct encoder *enc);

void free_decoder(struct decoder *dec);

struct sockaddr_in *get_packet_addr(unsigned char *buf, int in_or_out);

unsigned int get_packet_len(unsigned char *buf);

unsigned int get_groupId();

struct list *update_udp_info_list(struct list *udp_info_list, struct udp_info *udp_info);

struct list *update_vpn_address_list(struct list *addr_list, struct sockaddr_in *addr);

#endif
