#include "trans.h"

void *output(void *args)
{
    struct out_param *out_p = (struct out_param *)args;
    struct sockaddr_in client_addr;
    client_addr.sin_addr.s_addr = out_p->clinet_vpn_ip;
    client_addr.sin_port = out_p->clinet_vpn_port;

    int pos = 0;
    while (pos < out_p->data_size)
    {
        int len = packet_nat(&client_addr, out_p->buf + pos, OUT_NAT);
        if (len <= 0)
        {
            perror("Error: packet total_len=0!!!");
            break;
        }
        printf("pos=%d data_size=%d Send Packet len=%d\n", pos, out_p->data_size, len);
        int write_bytes = write(out_p->tun_fd, out_p->buf + pos, len);
        if (write_bytes < 0)
        {
            perror("Error while sending to tun_fd!!!");
        }
        pos += len;
    }
    free(out_p->buf);
    free(out_p);
}