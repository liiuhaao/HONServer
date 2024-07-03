#ifndef FEC84_H
#define FEC84_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
typedef unsigned char u8;
#define ENCODE 0
#define DECODE 1

typedef struct
{
    int recieve_num;                                // 有多少个包已经来了
    int data_num, parity_num, block_num, block_len; // n个数据包，m个冗余包，每个矩阵r行
} Fec;

u8 **fec_encode(int data_num, int parity_num, int packet_len, u8 **data_packets);

u8 **fec_decode(int data_num, int parity_num, int packet_len, u8 **packets, u8 *marks);

void print_packets(int packet_num, int packet_len, u8 **packets);

void get_gf_table();

extern u8 gf_mul_table[16][16];
extern u8 gf_inv_table[16];

extern int gf_matrix_old[4][12];
extern int gf_matrix[4][12];

#endif // FEC84_H