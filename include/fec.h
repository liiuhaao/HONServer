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
    char type;
    struct list *time_head;
    struct list *time_tail;
};

struct encoder
{
    unsigned int group_id;
    int index;
    unsigned char **packet_buffers;
    unsigned int *packet_sizes;

    struct list *udp_infos;
    // struct list *vpn_addrs;

    pthread_mutex_t mutex;

    struct timespec dead_touch;
};

struct decoder
{
    unsigned int group_id;

    unsigned int receive_num;

    unsigned int block_size;

    unsigned char **data_blocks;
    unsigned char *marks;
    unsigned int *packet_sizes;

    pthread_mutex_t mutex;
    struct timespec touch;
};

struct rx_packet
{
    unsigned int group_id;
    unsigned int index;
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

    struct encoder *enc;
};

struct output_param
{
    pthread_t tid;

    unsigned char *packet;
    unsigned int hon_size;

    int tun_fd;
    int udp_fd;

    struct sockaddr_in udp_addr;

    struct encoder *enc;
};

struct enc_param
{
    pthread_t tid;

    struct group *group;
    struct encoder *enc;
    struct udp_info *udp;

    int udp_fd;
};

struct dec_param
{
    pthread_t tid;

    struct group *group;
    struct decoder *dec;

    int tun_fd;
    int udp_fd;
};

extern pthread_mutex_t decoder_list_mutex;
extern struct list *decoder_list;

extern struct encoder *enc;

extern struct list *rx_list;
extern unsigned int rx_num;
extern unsigned int rx_id;
extern pthread_mutex_t rx_mutex;

extern double rx_time;
extern double rx_min;
extern double rx_max;


extern double enc_time;
extern double enc_min;
extern double enc_max;


extern double dec_time;
extern double dec_min;
extern double dec_max;

extern unsigned int tx_id;
extern pthread_mutex_t tx_mutex;

extern unsigned int rx_group_id;
extern unsigned int rx_index;

void *serve_input(void *args);
void *serve_output(void *args);

void input_send(int udp_fd, unsigned char *packet, int len, unsigned int group_id, unsigned int index, struct udp_info *udp);

void *encode(void *args);
void *decode(void *args);

void free_encoder_buffers(int free_self);
void free_encoder_sizes();
void free_encoder_udp_infos();
void free_encoder();

struct encoder *new_encoder();

struct decoder *get_decoder(unsigned int group_id);

struct decoder *new_decoder(unsigned int groupId);

void *monitor_decoder(void *arg);

void free_decoder(struct decoder *dec);

void rx_insert(int tun_fd, unsigned char *buf, unsigned int group_id, unsigned int index);

void monitor_rx(void *arg);

void clean_all_rx();

void print_udp_infos(struct list *udp_infos);

void rx_send(int tun_fd);

void print_rx();

struct sockaddr_in *get_packet_addr(unsigned char *buf, int in_or_out);

unsigned int get_packet_len(unsigned char *buf);

unsigned int get_groupId();

struct list *update_udp_info_list(struct list *udp_info_list, struct udp_info *udp_info);

void clean_all();

#endif
