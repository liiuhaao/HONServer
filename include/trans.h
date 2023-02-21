#ifndef TRANS_H
#define TRANS_H

#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include "nat.h"
struct out_param
{
    unsigned char *buf;
    unsigned int data_size;
    int tun_fd;
    in_addr_t clinet_vpn_ip;
    in_port_t clinet_vpn_port;
};

void *output(void *args);

#endif