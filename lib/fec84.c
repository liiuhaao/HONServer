
#include "fec.h"

u8 gf_mul_table[16][16];
u8 gf_inv_table[16];

int gf_matrix_old[4][12] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    3, 5, 9, 14, 1, 2, 4, 8, 0, 1, 0, 0,
    5, 14, 3, 9, 1, 4, 15, 2, 0, 0, 1, 0,
    15, 8, 4, 2, 1, 8, 2, 15, 0, 0, 0, 1};

int gf_matrix[4][12] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    2, 3, 4, 5, 8, 12, 13, 15, 0, 1, 0, 0,
    15, 4, 13, 2, 14, 5, 8, 9, 0, 0, 1, 0,
    13, 10, 6, 4, 11, 3, 7, 8, 0, 0, 0, 1};
// TODO: 接受数据包多余data_num个我没实现，理论上可以减少求逆的复杂度，但是项目里暂时不会遇到这个情况，所以没实现。

void print_packets(int packet_num, int packet_len, u8 **packets)
{
    for (int i = 0; i < packet_num; i++)
    {
        for (int j = 0; j < packet_len; j++)
        {
            printf("%d ", packets[i][j]);
        }
        printf("\n");
    }
}

void print_packets_T(int packet_num, int packet_len, u8 **packets)
{
    for (int j = 0; j < packet_len; j++)
    {
        for (int i = 0; i < packet_num; i++)
        {
            printf("%d ", packets[i][j]);
        }
        printf("\n");
    }
}

// 4位的位运算乘法
int _gf_mul(int a, int b)
{
    int res = 0;
    for (int i = 0; i < 4; i++)
    {
        if (!(a & (1 << i)))
            continue;
        for (int j = 0; j < 4; j++)
        {
            if (!(b & (1 << j)))
                continue;
            if (i + j == 4)
            {
                res ^= (1 << 4) - 1;
            }
            else if (i + j == 5)
            {
                res ^= 1;
            }
            else if (i + j == 6)
            {
                res ^= 2;
            }
            else
            {
                res ^= (1 << (i + j));
            }
        }
    }
    return res;
}

// 获取乘法表和求逆表
void get_gf_table()
{
    for (int i = 0; i < 16; i++)
    {
        for (int j = 0; j < 16; j++)
        {
            gf_mul_table[i][j] = _gf_mul(i, j);
            if (gf_mul_table[i][j] == 1)
            {
                gf_inv_table[i] = j;
                gf_inv_table[j] = i;
            }
        }
    }
}

// 查表计算乘法、求逆
u8 gf_mul(u8 a, u8 b)
{
    return gf_mul_table[a][b];
}
u8 gf_inv(u8 a)
{
    return gf_inv_table[a];
}

// 普通矩阵求逆
u8 **inverse_matrix(u8 **matrix, int n)
{
    // return matrix
    u8 **inv = (u8 **)malloc(n * sizeof(u8 *));

    // 初始化逆矩阵为单位矩阵
    for (int i = 0; i < n; i++)
    {
        inv[i] = (u8 *)malloc(n * sizeof(u8));
        for (int j = 0; j < n; j++)
        {
            inv[i][j] = (i == j) ? 1 : 0;
        }
    }
    // return inv;

    // 高斯-约当消元法
    for (int i = 0; i < n; i++)
    {
        // printf("逆矩阵求解过程，第%d步\n", i + 1);
        // for (int j = 0; j < n; j++)
        // {
        //     for (int k = 0; k < n; k++)
        //     {
        //         printf("%2d ", matrix[j][k]);
        //     }
        //     for (int k = 0; k < n; k++)
        //     {
        //         printf("%2d ", inv[j][k]);
        //     }
        //     printf("\n");
        // }

        if (matrix[i][i] == 0)
        {
            // 寻找非零元素交换行
            int j = i + 1;
            for (; j < n; j++)
            {
                if (matrix[j][i] != 0)
                {
                    // 交换矩阵的行
                    u8 *temp = matrix[i];
                    matrix[i] = matrix[j];
                    matrix[j] = temp;
                    // 交换逆矩阵的行
                    temp = inv[i];
                    inv[i] = inv[j];
                    inv[j] = temp;
                    break;
                }
            }
            if (j == n)
            { // 找不到非零元素，矩阵不可逆
                for (int k = 0; k < n; k++)
                    free(inv[k]);
                free(inv);
                return NULL;
            }
        }

        // 归一化行使主元素为1
        u8 inv_i = gf_inv(matrix[i][i]);
        for (int j = 0; j < n; j++)
        {
            matrix[i][j] = gf_mul(matrix[i][j], inv_i);
            inv[i][j] = gf_mul(inv[i][j], inv_i);
        }

        // printf("逆矩阵求解过程，第%d步（该行变成1）\n", i + 1);
        // for (int j = 0; j < n; j++)
        // {
        //     for (int k = 0; k < n; k++)
        //     {
        //         printf("%2d ", matrix[j][k]);
        //     }
        //     for (int k = 0; k < n; k++)
        //     {
        //         printf("%2d ", inv[j][k]);
        //     }
        //     printf("\n");
        // }

        // 消除当前列的其他行元素
        for (int j = 0; j < n; j++)
        {
            if (j != i)
            {
                u8 factor = matrix[j][i];
                for (int k = 0; k < n; k++)
                {
                    // if (i == 2)
                    // {
                    //     printf("j=%d k=%d factor=%d matrix[j][k]=%d matrix[i][k]=%d  gf_mul(factor, matrix[i][k])=%d\n", j, k, factor, matrix[j][k], matrix[i][k], gf_mul(factor, matrix[i][k]));
                    // }
                    matrix[j][k] ^= gf_mul(factor, matrix[i][k]);
                    inv[j][k] ^= gf_mul(factor, inv[i][k]);
                }
            }
        }
    }
    return inv;
}

// TODO：柯西矩阵快速求逆。柯西矩阵进行LDU分解，即M=LDU，M^{-1}=U^{-1}D^{-1}L^{-1}，其中L和U是单位下三角和上三角矩阵，D是对角矩阵。
// TODO；还有个问题，是不是可以对一些矩阵大小进行特判，比如大小为1，也能降低一些常数的复杂度。

// data_num个数据包，parity_num个冗余包，每个包分成block_num块，每一块长度是block_len
Fec *new_fec(int data_num, int parity_num, int block_num, int block_len)
{
    Fec *fec = (Fec *)(malloc(sizeof(Fec)));
    fec->recieve_num = 0;
    fec->data_num = data_num;
    fec->parity_num = parity_num;
    fec->block_num = block_num;
    fec->block_len = block_len;
    return fec;
}

// dst和src做长度为len的异或，结果保存在dst中
void region_xor(u8 *dst, u8 *src, int len)
{
    for (int i = 0; i < len; ++i)
    {
        dst[i] ^= src[i];
    }
}

// 如果初始就是0，则直接复制，但是好像没啥差别
void region_xor_initial_check(u8 *dst, u8 *src, int len, int *initial_check)
{
    if (initial_check == 0)
    {
        memcpy(dst, src, len);
    }
    else
    {
        region_xor(dst, src, len);
    }
}

// int region_xor(u8 *dst, u8 *src, int len)
// {
//     int i;
//     for (i = 0; i < len - 15; i += 16)
//     {
//         dst[i] ^= src[i];
//         dst[i + 1] ^= src[i + 1];
//         dst[i + 2] ^= src[i + 2];
//         dst[i + 3] ^= src[i + 3];
//         dst[i + 4] ^= src[i + 4];
//         dst[i + 5] ^= src[i + 5];
//         dst[i + 6] ^= src[i + 6];
//         dst[i + 7] ^= src[i + 7];
//         dst[i + 8] ^= src[i + 8];
//         dst[i + 9] ^= src[i + 9];
//         dst[i + 10] ^= src[i + 10];
//         dst[i + 11] ^= src[i + 11];
//         dst[i + 12] ^= src[i + 12];
//         dst[i + 13] ^= src[i + 13];
//         dst[i + 14] ^= src[i + 14];
//         dst[i + 15] ^= src[i + 15];
//     }
//     for (; i < len; ++i)
//     {
//         dst[i] ^= src[i];
//     }
//     return 0;
// }

// 计算两个block的异或
u8 *gf_intermediate(Fec *fec, u8 **packets, int packet_idx_x, int block_idx_x, int packet_idx_y, int block_idx_y)
{
    u8 *tmp = (u8 *)malloc(fec->block_len * sizeof(sizeof(u8)));
    memcpy(tmp, packets[packet_idx_x] + block_idx_x * fec->block_len, fec->block_len);
    region_xor(tmp, packets[packet_idx_y] + block_idx_y * fec->block_len, fec->block_len);
    return tmp;
}

// 这个版本是等所有包都到了再做异或运算。理论上可以改成来一个包做部分的运算（增加计算的并发度），但好像写起来非常非常麻烦，考虑的东西很多。
u8 **gf_fun(Fec *fec, u8 **packets, u8 *marks)
{
    // 计算出各个可复用的中间异或结果
    u8 *D00D10 = NULL, *D01D11 = NULL, *D02D12 = NULL;
    if (marks[0] == 0 && marks[1] == 0)
    {
        D00D10 = gf_intermediate(fec, packets, 0, 0, 1, 0);
        D01D11 = gf_intermediate(fec, packets, 0, 1, 1, 1);
        D02D12 = gf_intermediate(fec, packets, 0, 2, 1, 2);
    }
    u8 *D20D30 = NULL, *D21D31 = NULL, *D22D32 = NULL, *D23D33 = NULL;
    if (marks[2] == 0 && marks[3] == 0)
    {
        D20D30 = gf_intermediate(fec, packets, 2, 0, 3, 0);
        D21D31 = gf_intermediate(fec, packets, 2, 1, 3, 1);
        D22D32 = gf_intermediate(fec, packets, 2, 2, 3, 2);
        D23D33 = gf_intermediate(fec, packets, 2, 3, 3, 3);
    }
    u8 *D40D50 = NULL, *D41D51 = NULL, *D42D52 = NULL, *D43D53 = NULL;
    if (marks[4] == 0 && marks[5] == 0)
    {
        D40D50 = gf_intermediate(fec, packets, 4, 0, 5, 0);
        D41D51 = gf_intermediate(fec, packets, 4, 1, 5, 1);
        D42D52 = gf_intermediate(fec, packets, 4, 2, 5, 2);
        D43D53 = gf_intermediate(fec, packets, 4, 3, 5, 3);
    }
    u8 *D60D70 = NULL, *D61D71 = NULL, *D62D72 = NULL, *D63D73 = NULL;
    if (marks[6] == 0 && marks[7] == 0)
    {
        D60D70 = gf_intermediate(fec, packets, 6, 0, 7, 0);
        D61D71 = gf_intermediate(fec, packets, 6, 1, 7, 1);
        D62D72 = gf_intermediate(fec, packets, 6, 2, 7, 2);
        D63D73 = gf_intermediate(fec, packets, 6, 3, 7, 3);
    }

    // 这里是C相关，id为0～4的包的异或结果复用，
    u8 *C_0123 = (u8 *)malloc(fec->block_len * sizeof(sizeof(u8)));
    memset(C_0123, 0, fec->block_len * sizeof(sizeof(u8)));
    int block_idx[4] = {3, 3, 2, 2};
    int first = 1;
    for (int i = 0; i < 4; i++)
    {
        if (marks[i] == 0)
        {
            if (first)
            {
                memcpy(C_0123, packets[i] + block_idx[i] * fec->block_len, fec->block_len);
                first = 0;
            }
            else
            {
                region_xor(C_0123, packets[i] + block_idx[i] * fec->block_len, fec->block_len);
            }
        }
    }

    int **initial_R = (int **)malloc(fec->parity_num * sizeof(int *));
    for (int i = 0; i < fec->parity_num; i++)
    {
        initial_R[i] = (int *)malloc(fec->block_num * fec->block_len * sizeof(int));
        memset(initial_R[i], 0, fec->block_num * fec->block_len * sizeof(int));
    }

    // 计算R
    u8 **R_ptrs = (u8 **)malloc(fec->parity_num * sizeof(u8 *));
    for (int i = 0; i < fec->parity_num; i++)
    {
        R_ptrs[i] = (u8 *)malloc(fec->block_num * fec->block_len * sizeof(u8));
        memset(R_ptrs[i], 0, (fec->block_num * fec->block_len * sizeof(u8)));
    }
    if (marks[0] == 0 && marks[1] == 0)
    {
        /**R(0, 0)  R(0, 1)
         * 1 0 0 0, 1 0 0 0
         * 0 1 0 0, 0 1 0 0
         * 0 0 1 0, 0 0 1 0
         * 0 0 0 1, 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, D00D10, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, D01D11, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, D02D12, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[0] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 0)  R(1, 1)
         * 0 0 0 0, 1 0 0 0
         * 1 0 0 0, 1 1 0 0
         * 0 1 0 0, 0 1 1 0
         * 0 0 1 0, 0 0 1 1
         */
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, D00D10, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, D01D11, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, D02D12, fec->block_len, &initial_R[1][3]);
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[1] + 1 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[1] + 2 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 0)  R(2, 1)
         * 0 1 0 0, 0 0 0 1
         * 0 0 1 0, 0 0 0 0
         * 0 0 0 1, 1 0 0 0
         * 0 0 0 0, 0 1 0 0
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[0] + 1 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[0] + 2 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[0] + 3 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[1] + 1 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 0)  R(3, 1)
         * 0 1 0 0, 0 0 1 0
         * 1 0 1 0, 1 0 0 1
         * 0 1 0 1, 0 1 0 0
         * 0 0 1 0, 1 0 1 0
         */
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, D00D10, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, D01D11, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, D02D12, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[0] + 1 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[0] + 2 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[0] + 3 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[1] + 2 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }
    else if (marks[0] == 0)
    {
        /**R(0, 0)
         * 1 0 0 0,
         * 0 1 0 0,
         * 0 0 1 0,
         * 0 0 0 1,
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[0] + 0 * fec->block_len, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[0] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[0] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[0] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 0)
         * 0 0 0 0,
         * 1 0 0 0,
         * 0 1 0 0,
         * 0 0 1 0,
         */
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[0] + 0 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[0] + 1 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[0] + 2 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 0)
         * 0 1 0 0,
         * 0 0 1 0,
         * 0 0 0 1,
         * 0 0 0 0,
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[0] + 1 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[0] + 2 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[0] + 3 * fec->block_len, fec->block_len, &initial_R[2][2]);

        /**R(3, 0)
         * 0 1 0 0,
         * 1 0 1 0,
         * 0 1 0 1,
         * 0 0 1 0,
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[0] + 1 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[0] + 2 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[0] + 3 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[0] + 0 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[0] + 1 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[0] + 2 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }
    else if (marks[1] == 0)
    {
        /**R(0, 1)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[1] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[1] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 1)
         * 1 0 0 0
         * 1 1 0 0
         * 0 1 1 0
         * 0 0 1 1
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[1] + 1 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[1] + 2 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[1][3]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[1] + 1 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[1] + 2 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 1)
         * 0 0 0 1
         * 0 0 0 0
         * 1 0 0 0
         * 0 1 0 0
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[1] + 1 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 1)
         * 0 0 1 0
         * 1 0 0 1
         * 0 1 0 0
         * 1 0 1 0
         */
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[1] + 1 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[1] + 2 * fec->block_len, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[1] + 2 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[1] + 3 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[1] + 0 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }

    if (marks[2] == 0 && marks[3] == 0 && D22D32 != NULL)
    {
        /**R(0, 2)  R(0, 3)
         * 1 0 0 0, 1 0 0 0
         * 0 1 0 0, 0 1 0 0
         * 0 0 1 0, 0 0 1 0
         * 0 0 0 1, 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, D20D30, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, D21D31, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, D22D32, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, D23D33, fec->block_len, &initial_R[0][3]);

        /**R(1, 2)  R(1, 3)
         * 0 0 0 1, 1 0 0 1
         * 0 0 0 0, 0 1 0 0
         * 1 0 0 0, 1 0 1 0
         * 0 1 0 0, 0 1 0 1
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, D23D33, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, D20D30, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, D21D31, fec->block_len, &initial_R[1][3]);
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[3] + 0 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[3] + 1 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[3] + 2 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[3] + 3 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 2)  R(2, 3)
         * 0 1 0 0, 0 0 0 0
         * 1 0 1 0, 1 0 0 0
         * 0 1 0 1, 0 1 0 0
         * 0 0 1 0, 0 0 1 0
         */
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, D20D30, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, D21D31, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, D22D32, fec->block_len, &initial_R[2][3]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[2] + 2 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[2] + 3 * fec->block_len, fec->block_len, &initial_R[2][2]);

        /**R(3, 2)  R(3, 3)
         * 0 0 0 1, 0 0 0 1
         * 1 0 0 0, 0 0 0 0
         * 1 1 0 0, 1 0 0 0
         * 0 1 1 0, 0 1 0 0
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, D23D33, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, D20D30, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, D21D31, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[2] + 0 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[2] + 2 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }
    else if (marks[2] == 0)
    {
        /**R(0, 2)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[2] + 0 * fec->block_len, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[2] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[2] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 2)
         * 0 0 0 1
         * 0 0 0 0
         * 1 0 0 0
         * 0 1 0 0
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[2] + 3 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[2] + 0 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 2)
         * 0 1 0 0
         * 1 0 1 0
         * 0 1 0 1
         * 0 0 1 0
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[2] + 2 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[2] + 3 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[2] + 0 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[2] + 2 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 2)
         * 0 0 0 1
         * 1 0 0 0
         * 1 1 0 0
         * 0 1 1 0
         */
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[2] + 0 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[2] + 2 * fec->block_len, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[2] + 3 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[2] + 0 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[2] + 1 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }
    else if (marks[3] == 0)
    {
        /**R(0, 3)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[3] + 0 * fec->block_len, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[3] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[3] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[3] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 3)
         * 1 0 0 1
         * 0 1 0 0
         * 1 0 1 0
         * 0 1 0 1
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[3] + 0 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[3] + 1 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[3] + 2 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[3] + 3 * fec->block_len, fec->block_len, &initial_R[1][3]);
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[3] + 3 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[3] + 0 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[3] + 1 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 3)
         * 0 0 0 0
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         */
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[3] + 0 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[3] + 1 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[3] + 2 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 3)
         * 0 0 0 1
         * 0 0 0 0
         * 1 0 0 0
         * 0 1 0 0
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[3] + 3 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[3] + 0 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[3] + 1 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }

    if (marks[4] == 0 && marks[5] == 0)
    {
        /**R(0, 4)  R(0, 5)
         * 1 0 0 0, 1 0 0 0
         * 0 1 0 0, 0 1 0 0
         * 0 0 1 0, 0 0 1 0
         * 0 0 0 1, 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, D40D50, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, D41D51, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, D42D52, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, D43D53, fec->block_len, &initial_R[0][3]);

        /**R(1, 4)  R(1, 5)
         * 0 0 1 0, 0 0 1 1
         * 0 0 0 1, 0 0 0 1
         * 0 0 0 0, 1 0 0 0
         * 1 0 0 0, 1 1 0 0
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, D42D52, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, D43D53, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, D40D50, fec->block_len, &initial_R[1][3]);
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 4)  R(2, 5)
         * 1 1 0 0, 1 0 0 1
         * 0 1 1 0, 0 1 0 0
         * 0 0 1 1, 1 0 1 0
         * 0 0 0 1, 0 1 0 1
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, D40D50, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, D41D51, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, D42D52, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, D43D53, fec->block_len, &initial_R[2][3]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[4] + 2 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 4)  R(3, 5)
         * 0 1 0 1, 1 0 0 0
         * 0 0 1 0, 1 1 0 0
         * 1 0 0 1, 0 1 1 0
         * 0 1 0 0, 0 0 1 1
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[4] + 2 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[4] + 0 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[5] + 2 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[5] + 2 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }
    else if (marks[4] == 0)
    {

        /**R(0, 4)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[4] + 0 * fec->block_len, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[4] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 4)
         * 0 0 1 0
         * 0 0 0 1
         * 0 0 0 0
         * 1 0 0 0
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[4] + 2 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[4] + 0 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 4)
         * 1 1 0 0
         * 0 1 1 0
         * 0 0 1 1
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[4] + 0 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[4] + 2 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[2][3]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[4] + 2 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[2][2]);

        /**R(3, 4)
         * 0 1 0 1
         * 0 0 1 0
         * 1 0 0 1
         * 0 1 0 0
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[4] + 2 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[4] + 3 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[4] + 0 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[4] + 1 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }
    else if (marks[5] == 0)
    {
        /**R(0, 5)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[5] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 5)
         * 0 0 1 1
         * 0 0 0 1
         * 1 0 0 0
         * 1 1 0 0
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[5] + 2 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[1][3]);
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 5)
         * 1 0 0 1
         * 0 1 0 0
         * 1 0 1 0
         * 0 1 0 1
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[5] + 2 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[2][3]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 5)
         * 1 0 0 0
         * 1 1 0 0
         * 0 1 1 0
         * 0 0 1 1
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[5] + 2 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[5] + 3 * fec->block_len, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[5] + 0 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[5] + 1 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[5] + 2 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }

    if (marks[6] == 0 && marks[7] == 0)
    {
        /**R(0, 6)  R(0, 7)
         * 1 0 0 0, 1 0 0 0
         * 0 1 0 0, 0 1 0 0
         * 0 0 1 0, 0 0 1 0
         * 0 0 0 1, 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, D60D70, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, D61D71, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, D62D72, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, D63D73, fec->block_len, &initial_R[0][3]);

        /**R(1, 6)  R(1, 7)
         * 0 1 0 0, 0 1 0 0
         * 1 0 1 0, 0 0 1 0
         * 0 1 0 1, 0 0 0 1
         * 0 0 1 0, 0 0 0 0
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, D61D71, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, D62D72, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, D63D73, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[6] + 0 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[6] + 1 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 6)  R(2, 7)
         * 0 0 1 0, 1 0 1 0
         * 0 0 0 1, 0 1 0 1
         * 0 0 0 0, 0 0 1 0
         * 1 0 0 0, 1 0 0 1
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, D62D72, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, D63D73, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, D60D70, fec->block_len, &initial_R[2][3]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[7] + 0 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[7] + 1 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[7] + 2 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[7] + 3 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 6)  R(3, 7)
         * 0 1 1 0, 0 0 1 0
         * 0 0 1 1, 0 0 0 1
         * 0 0 0 1, 0 0 0 0
         * 1 0 0 0, 1 0 0 0
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, D62D72, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, D63D73, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, D60D70, fec->block_len, &initial_R[3][3]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[6] + 1 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[6] + 3 * fec->block_len, fec->block_len, &initial_R[3][2]);
    }
    else if (marks[6] == 0)
    {
        /**R(0, 6)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[6] + 0 * fec->block_len, fec->block_len, &initial_R[0][0]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[6] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[6] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 6)
         * 0 1 0 0
         * 1 0 1 0
         * 0 1 0 1
         * 0 0 1 0
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[6] + 1 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[6] + 3 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[6] + 0 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[6] + 1 * fec->block_len, fec->block_len, &initial_R[1][2]);
        region_xor_initial_check(R_ptrs[1] + 3 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[1][3]);

        /**R(2, 6)
         * 0 0 1 0
         * 0 0 0 1
         * 0 0 0 0
         * 1 0 0 0
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[6] + 3 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[6] + 0 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 6)
         * 0 1 1 0
         * 0 0 1 1
         * 0 0 0 1
         * 1 0 0 0
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[6] + 1 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 2 * fec->block_len, packets[6] + 3 * fec->block_len, fec->block_len, &initial_R[3][2]);
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[6] + 2 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[6] + 3 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[6] + 0 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }
    else if (marks[7] == 0)
    {
        /**R(0, 7)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor_initial_check(R_ptrs[0] + 0 * fec->block_len, packets[7] + 0 * fec->block_len, fec->block_len, &initial_R[0][3]);
        region_xor_initial_check(R_ptrs[0] + 1 * fec->block_len, packets[7] + 1 * fec->block_len, fec->block_len, &initial_R[0][1]);
        region_xor_initial_check(R_ptrs[0] + 2 * fec->block_len, packets[7] + 2 * fec->block_len, fec->block_len, &initial_R[0][2]);
        region_xor_initial_check(R_ptrs[0] + 3 * fec->block_len, packets[7] + 3 * fec->block_len, fec->block_len, &initial_R[0][3]);

        /**R(1, 7)
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         * 0 0 0 0
         */
        region_xor_initial_check(R_ptrs[1] + 0 * fec->block_len, packets[7] + 1 * fec->block_len, fec->block_len, &initial_R[1][0]);
        region_xor_initial_check(R_ptrs[1] + 1 * fec->block_len, packets[7] + 2 * fec->block_len, fec->block_len, &initial_R[1][1]);
        region_xor_initial_check(R_ptrs[1] + 2 * fec->block_len, packets[7] + 3 * fec->block_len, fec->block_len, &initial_R[1][2]);

        /**R(2, 7)
         * 1 0 1 0
         * 0 1 0 1
         * 0 0 1 0
         * 1 0 0 1
         */
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[7] + 0 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[7] + 1 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 2 * fec->block_len, packets[7] + 2 * fec->block_len, fec->block_len, &initial_R[2][2]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[7] + 3 * fec->block_len, fec->block_len, &initial_R[2][3]);
        region_xor_initial_check(R_ptrs[2] + 0 * fec->block_len, packets[7] + 2 * fec->block_len, fec->block_len, &initial_R[2][0]);
        region_xor_initial_check(R_ptrs[2] + 1 * fec->block_len, packets[7] + 3 * fec->block_len, fec->block_len, &initial_R[2][1]);
        region_xor_initial_check(R_ptrs[2] + 3 * fec->block_len, packets[7] + 0 * fec->block_len, fec->block_len, &initial_R[2][3]);

        /**R(3, 7)
         * 0 0 1 0
         * 0 0 0 1
         * 0 0 0 0
         * 1 0 0 0
         */
        region_xor_initial_check(R_ptrs[3] + 0 * fec->block_len, packets[7] + 2 * fec->block_len, fec->block_len, &initial_R[3][0]);
        region_xor_initial_check(R_ptrs[3] + 1 * fec->block_len, packets[7] + 3 * fec->block_len, fec->block_len, &initial_R[3][1]);
        region_xor_initial_check(R_ptrs[3] + 3 * fec->block_len, packets[7] + 0 * fec->block_len, fec->block_len, &initial_R[3][3]);
    }

    if (marks[8] == 0)
    {
        /**R(0, 8)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor(R_ptrs[0] + 0 * fec->block_len, packets[8] + 0 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[0] + 1 * fec->block_len, packets[8] + 1 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[0] + 2 * fec->block_len, packets[8] + 2 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[0] + 3 * fec->block_len, packets[8] + 3 * fec->block_len, fec->block_len);
    }
    if (marks[9] == 0)
    {
        /**R(1, 9)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor(R_ptrs[1] + 0 * fec->block_len, packets[9] + 0 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[1] + 1 * fec->block_len, packets[9] + 1 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[1] + 2 * fec->block_len, packets[9] + 2 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[1] + 3 * fec->block_len, packets[9] + 3 * fec->block_len, fec->block_len);
    }
    if (marks[10] == 0)
    {
        /**R(2, 10)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor(R_ptrs[2] + 0 * fec->block_len, packets[10] + 0 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[2] + 1 * fec->block_len, packets[10] + 1 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[2] + 2 * fec->block_len, packets[10] + 2 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[2] + 3 * fec->block_len, packets[10] + 3 * fec->block_len, fec->block_len);
    }
    if (marks[11] == 0)
    {
        /**R(3, 11)
         * 1 0 0 0
         * 0 1 0 0
         * 0 0 1 0
         * 0 0 0 1
         */
        region_xor(R_ptrs[3] + 0 * fec->block_len, packets[11] + 0 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[3] + 1 * fec->block_len, packets[11] + 1 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[3] + 2 * fec->block_len, packets[11] + 2 * fec->block_len, fec->block_len);
        region_xor(R_ptrs[3] + 3 * fec->block_len, packets[11] + 3 * fec->block_len, fec->block_len);
    }

    // return NULL;
    // 计算C
    u8 **C_ptrs = (u8 **)malloc(fec->parity_num * sizeof(u8 *));
    for (int i = 0; i < fec->parity_num; i++)
    {
        C_ptrs[i] = (u8 *)malloc(fec->block_len * sizeof(u8));
        memset(C_ptrs[i], 0, (fec->block_len * sizeof(u8)));
    }
    memcpy(C_ptrs[1], C_0123, fec->block_len);
    memcpy(C_ptrs[3], C_0123, fec->block_len);

    if (marks[0] == 0)
    {
        region_xor(C_ptrs[2], packets[0] + 0 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[0] + 0 * fec->block_len, fec->block_len);
    }
    if (marks[1] == 0)
    {
        region_xor(C_ptrs[2], packets[1] + 2 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[1] + 1 * fec->block_len, fec->block_len);
    }
    if (marks[2] == 0)
    {
        region_xor(C_ptrs[2], packets[2] + 0 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[2], packets[2] + 3 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[2] + 3 * fec->block_len, fec->block_len);
    }
    if (marks[3] == 0)
    {
        region_xor(C_ptrs[2], packets[3] + 3 * fec->block_len, fec->block_len);
    }
    if (marks[4] == 0)
    {
        region_xor(C_ptrs[1], packets[4] + 1 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[2], packets[4] + 0 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[4] + 0 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[4] + 2 * fec->block_len, fec->block_len);
    }
    if (marks[5] == 0)
    {
        region_xor(C_ptrs[1], packets[5] + 1 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[1], packets[5] + 2 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[2], packets[5] + 2 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[5] + 3 * fec->block_len, fec->block_len);
    }
    if (marks[6] == 0)
    {
        region_xor(C_ptrs[1], packets[6] + 0 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[1], packets[6] + 3 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[2], packets[6] + 1 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[6] + 0 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[6] + 1 * fec->block_len, fec->block_len);
    }
    if (marks[7] == 0)
    {
        region_xor(C_ptrs[1], packets[7] + 0 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[2], packets[7] + 1 * fec->block_len, fec->block_len);
        region_xor(C_ptrs[3], packets[7] + 1 * fec->block_len, fec->block_len);
    }

    // R^C
    for (int i = 1; i < 4; i++)
    {
        for (int j = 0; j < fec->block_num; j++)
        {
            region_xor(R_ptrs[i] + j * fec->block_len, C_ptrs[i], fec->block_len);
        }
    }

    for (int i = 0; i < fec->parity_num; i++)
    {
        free(C_ptrs[i]);
        free(initial_R[i]);
    }
    free(initial_R);
    free(C_ptrs);

    if (D00D10 != NULL)
        free(D00D10);
    if (D01D11 != NULL)
        free(D01D11);
    if (D02D12 != NULL)
        free(D02D12);

    if (D20D30 != NULL)
        free(D20D30);
    if (D21D31 != NULL)
        free(D21D31);
    if (D22D32 != NULL)
        free(D22D32);
    if (D23D33 != NULL)
        free(D23D33);

    if (D40D50 != NULL)
        free(D40D50);
    if (D41D51 != NULL)
        free(D41D51);
    if (D42D52 != NULL)
        free(D42D52);
    if (D43D53 != NULL)
        free(D43D53);

    if (D60D70 != NULL)
        free(D60D70);
    if (D61D71 != NULL)
        free(D61D71);
    if (D62D72 != NULL)
        free(D62D72);
    if (D63D73 != NULL)
        free(D63D73);

    if (C_0123 != NULL)
        free(C_0123);
    return R_ptrs;
}

// 根据一行的恢复行，恢复数据包，乘法的方法跟gf_fun类似。
// TODO: 这个能不能优化？
u8 *recover_packet(int block_num, int block_len, u8 *recover_vector, u8 **singular_packets)
{
    u8 *recovered_packet = (u8 *)malloc(block_num * block_len * sizeof(u8));
    memset(recovered_packet, 0, block_num * block_len * sizeof(u8));
    for (int i = 0; i < block_num; i++)
    {
        for (int j = 0; j < block_num; j++)
        {
            // printf("recover_packet i=%d, j=%d, recover_vector[j]=%d\n", i, j, recover_vector[j]);
            switch (recover_vector[j])
            {
            case 1:
                /**
                 * 1 0 0 0
                 * 0 1 0 0
                 * 0 0 1 0
                 * 0 0 0 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;

            case 2:
                /**
                 * 0 0 0 1
                 * 1 0 0 1
                 * 0 1 0 1
                 * 0 0 1 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;
            case 3:
                /**
                 * 1 0 0 1
                 * 1 1 0 1
                 * 0 1 1 1
                 * 0 0 1 0
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                break;
            case 4:
                /**
                 * 0 0 1 1
                 * 0 0 1 0
                 * 1 0 1 0
                 * 0 1 1 0
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                break;
            case 5:
                /**
                 * 1 0 1 1
                 * 0 1 1 0
                 * 1 0 0 0
                 * 0 1 1 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;
            case 6:
                /**
                 * 0 0 1 0
                 * 1 0 1 1
                 * 1 1 1 1
                 * 0 1 0 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;
            case 7:
                /**
                 * 1 0 1 0
                 * 1 1 1 1
                 * 1 1 0 1
                 * 0 1 0 0
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                break;
            case 8:
                /**
                 * 0 1 1 0
                 * 0 1 0 1
                 * 0 1 0 0
                 * 1 1 0 0
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                break;
            case 9:
                /**
                 * 1 1 1 0
                 * 0 0 0 1
                 * 0 1 1 0
                 * 1 1 0 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;
            case 10:
                /**
                 * 0 1 1 1
                 * 1 1 0 0
                 * 0 0 0 1
                 * 1 1 1 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;
            case 11:
                /**
                 * 1 1 1 1
                 * 1 0 0 0
                 * 0 0 1 1
                 * 1 1 1 0
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                break;
            case 12:
                /**
                 * 0 1 0 1
                 * 0 1 1 1
                 * 1 1 1 0
                 * 1 0 1 0
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                break;
            case 13:
                /**
                 * 1 1 0 1
                 * 0 0 1 1
                 * 1 1 0 0
                 * 1 0 1 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;
            case 14:
                /**
                 * 0 1 0 0
                 * 1 1 1 0
                 * 1 0 1 1
                 * 1 0 0 1
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 1 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 2 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                break;
            case 15:
                /**
                 * 1 1 0 0
                 * 1 0 1 0
                 * 1 0 0 1
                 * 1 0 0 0
                 */
                if (i == 0)
                {
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 0 * block_len, singular_packets[j] + 1 * block_len, block_len);
                }
                else if (i == 1)
                {
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 1 * block_len, singular_packets[j] + 2 * block_len, block_len);
                }
                else if (i == 2)
                {
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 0 * block_len, block_len);
                    region_xor(recovered_packet + 2 * block_len, singular_packets[j] + 3 * block_len, block_len);
                }
                else if (i == 3)
                {
                    region_xor(recovered_packet + 3 * block_len, singular_packets[j] + 0 * block_len, block_len);
                }
                break;
            default:
                break;
            }
        }
    }
    return recovered_packet;
}

// 编码。
u8 **fec_encode(int data_num, int parity_num, int packet_len, u8 **data_packets)
{
    int block_num = 4;
    int block_len = (packet_len + 4 - 1) / 4;
    Fec *fec = new_fec(data_num, parity_num, block_num, block_len);
    u8 marks[12] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
    u8 **parity_packets = gf_fun(fec, data_packets, marks);
    free(fec);
    return parity_packets;
}

u8 **inverse_sub_matrix(u8 **lost_matrix, u8 **sub_matrix, int lost_packet_num, int parity_num, int num, int id)
{
    if (num >= lost_packet_num)
    {
        // printf("sub_matrix:\n");
        // print_packets(lost_packet_num, lost_packet_num, sub_matrix);
        return inverse_matrix(sub_matrix, lost_packet_num);
    }
    for (int i = id + 1; i < parity_num; i++)
    {
        memcpy(sub_matrix[num], lost_matrix[i], lost_packet_num);
        u8 **sub_matrix_copy = (u8 **)malloc(lost_packet_num * sizeof(u8 *));
        for (int j = 0; j < lost_packet_num; j++)
        {
            sub_matrix_copy[j] = (u8 *)malloc(lost_packet_num * sizeof(u8));
            memcpy(sub_matrix_copy[j], sub_matrix[j], lost_packet_num);
        }
        u8 **res = inverse_sub_matrix(lost_matrix, sub_matrix_copy, lost_packet_num, parity_num, num + 1, i);
        for (int j = 0; j < lost_packet_num; j++)
        {
            free(sub_matrix_copy[j]);
        }
        free(sub_matrix_copy);
        if (res != NULL)
        {
            return res;
        }
    }
    return NULL;
}

// 解码。必须保证接受到的包至少data_num个，我这里就不加判断了。
u8 **fec_decode(int data_num, int parity_num, int packet_len, u8 **packets, u8 *marks)
{
    // return packets;
    int block_num = 4;
    int block_len = (packet_len + block_num - 1) / block_num;
    Fec *fec = new_fec(data_num, parity_num, block_num, block_len);

    // 获得校验包
    u8 **singular_packets = gf_fun(fec, packets, marks);
    // printf("singular_packets:\n");
    // print_packets(4, packet_len, singular_packets);

    // 根据丢失的包，得到丢失矩阵
    u8 **lost_matrix = (u8 **)malloc(block_num * sizeof(u8 *));
    for (int i = 0; i < block_num; i++)
    {
        lost_matrix[i] = (u8 *)malloc(block_num * sizeof(u8));
        // memset(lost_matrix[i], 0, block_num * sizeof(int));
    }
    int lost_packet_num = 0;
    int lost_data_num = 0;
    for (int i = 0; i < data_num + parity_num; i++)
    {
        if (marks[i] == 1)
        {
            for (int j = 0; j < block_num; j++)
            {
                lost_matrix[j][lost_packet_num] = gf_matrix[j][i];
            }
            if (i < data_num)
                lost_data_num++;
            lost_packet_num++;
        }
    }
    // printf("marks:\n");
    // for(int i=0;i<data_num + parity_num;i++){
    //     printf("%d ",marks[i]);
    // }
    // printf("\n");
    // printf("lost_matrix:\n");
    // print_packets(parity_num, parity_num, lost_matrix);
    // return NULL;
    // 求丢失矩阵的逆
    // u8 **inv_lost_matrix = lost_matrix;
    u8 **inv_lost_matrix = NULL;
    if (lost_packet_num < parity_num)
    {
        u8 **sub_matrix = (u8 **)malloc(lost_packet_num * sizeof(u8 *));
        for (int i = 0; i < lost_packet_num; i++)
        {
            sub_matrix[i] = (u8 *)malloc(lost_packet_num * sizeof(u8));
        }
        inv_lost_matrix = inverse_sub_matrix(lost_matrix, sub_matrix, lost_packet_num, parity_num, 0, -1);
    }
    else
    {
        inv_lost_matrix = inverse_matrix(lost_matrix, lost_packet_num);
    }
    // printf("inv_lost_matrix:\n");
    // print_packets(lost_packet_num, lost_packet_num, inv_lost_matrix);

    // 恢复数据包
    int lost_data_cnt = 0;
    for (int i = 0; i < data_num && lost_data_cnt < lost_data_num; i++)
    {

        if (marks[i] == 1)
        {
            // 如果P1收到了，则恢复的最后一个数据包计算方法变为P1异或其他数据包（因为P1就是所有数据包的异或结果）
            if (lost_data_cnt + 1 == lost_data_num && marks[data_num] == 0)
            {
                // printf(">>>>>>>>>>>>>>>>>>>>>>>>P1 recover %d\n", i);
                // memset(packets[i], 0, packet_len); // 默认就要是0，我这就不清空了
                memcpy(packets[i], packets[data_num], packet_len);
                // region_xor(packets[i], packets[data_num], packet_len);
                for (int j = 0; j < data_num; j++)
                {
                    if (j != i)
                    {
                        region_xor(packets[i], packets[j], packet_len);
                    }
                }
            }
            else
            {
                u8 *recovered_packet = recover_packet(block_num, block_len, inv_lost_matrix[lost_data_cnt], singular_packets);
                memcpy(packets[i], recovered_packet, packet_len);
                free(recovered_packet);
            }
            lost_data_cnt++;
        }
    }

    for (int i = 0; i < block_num; i++)
    {
        free(lost_matrix[i]);
    }
    free(lost_matrix);

    for (int i = 0; i < lost_packet_num; i++)
    {
        free(inv_lost_matrix[i]);
    }
    free(inv_lost_matrix);
    free(fec);
    return packets;
}
