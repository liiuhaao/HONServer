#ifndef NAT_H
#define NAT_H

#include <linux/types.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <netinet/ip_icmp.h>
#include <time.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

#define MIN_FAKE_PORT 1024
#define MAX_FAKE_PORT 65535
#define NAT_TIMEOUT 60 /* in seconds */

#define FAKE_IP "10.10.0.15"

#define IN_NAT 1
#define OUT_NAT 0

struct nat_record
{
    __be32 client_addr;
    __be16 client_port;

    __be32 fake_addr;
    __be16 fake_port;

    in_addr_t client_vpn_ip;
    in_port_t client_vpn_port;

    time_t touch;

    struct nat_record *next;
} *nat_table;
pthread_mutex_t nat_table_mutex;

struct pseudohdr
{
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t zero;
    uint8_t proto;
    uint16_t length;
} pseudo_hdr;

union protohdr
{
    struct tcphdr tcp_hdr;
    struct udphdr udp_hdr;
};

char *addr2str(__be32 addr);

int packet_nat(struct sockaddr_in *client_addr, char *buf, int in_or_out);

struct nat_record *nat_in(__be32 fake_saddr, __be16 fake_source);

struct nat_record *nat_out(struct sockaddr_in *client_addr, __be32 saddr, __be16 source);

struct dec_record *dec_get(int hash_code, int data_size, int symbol_size, int k, int n);

void free_dec(struct dec_record *record);

int dec_put(struct dec_record *record, int index, unsigned char *block);

in_port_t get_fake_port();

uint16_t get_ip_icmp_check(const void *const addr, const size_t length);

uint16_t get_tcp_udp_check(const struct iphdr *ip_hdr, union protohdr *proto_hdr);

#endif