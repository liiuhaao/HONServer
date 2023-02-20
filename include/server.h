#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <../lib/rs.h>

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))

#define SERVER_IP "0.0.0.0"
#define SERVER_PORT 54345

#define TUN_NAME "tun0"
#define TUN_IP "10.10.0.1"
#define FAKE_IP "10.10.0.15"
#define MTU 1500

int allocate_tun(char *tun_name, char *tun_ip, int mtu);

int bind_udp(char *server_ip, int server_port);

void signal_handler(int sig);

void run(char *cmd);
