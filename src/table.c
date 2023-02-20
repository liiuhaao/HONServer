#include "table.h"
#include "server.h"

char *addr2str(__be32 addr)
{
    unsigned char *p = (unsigned char *)&addr;
    char *res = (char *)malloc(15);
    sprintf(res, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
    return res;
}

/**
 * Performs NAT (network address translation) for an incoming/outgoing IP packet
 *
 * @param client_addr Pointer to the client's socket address
 * @param buf Pointer to the IP packet buffer
 * @param in_or_out: Specifies whether the packet is incoming or outgoing
 * @return Returns 1 on success, 0 on failure.
 */
int packet_nat(struct sockaddr_in *client_addr, char *buf, int in_or_out)
{
    /* IP Header*/
    struct iphdr *ip_hdr = (struct iphdr *)(buf);
    int ip_hdr_len = ip_hdr->ihl * 4;

    /* Protocol Header */
    struct tcphdr *tcp_hdr;
    struct udphdr *udp_hdr;
    struct icmphdr *icmp_hdr;
    __be16 *source;
    __be16 *dest;

    if (ip_hdr->protocol == IPPROTO_TCP)
    {
        tcp_hdr = (struct tcphdr *)(buf + ip_hdr_len);
        source = &(tcp_hdr->source);
        dest = &(tcp_hdr->dest);
    }
    else if (ip_hdr->protocol == IPPROTO_UDP)
    {
        udp_hdr = (struct udphdr *)(buf + ip_hdr_len);
        source = &(udp_hdr->source);
        dest = &(udp_hdr->dest);
    }
    else if (ip_hdr->protocol == IPPROTO_ICMP)
    {
        icmp_hdr = (struct icmphdr *)(buf + ip_hdr_len);
        source = dest = &(icmp_hdr->un.echo.id);
    }
    else
    {
        return 0;
    }
    // printf("source=%s:%d, dest=%s:%d\n", addr2str(ip_hdr->saddr), *source, addr2str(ip_hdr->daddr), *dest);

    /* Look up nat_table and translate address*/
    struct nat_record *record = NULL;
    if (in_or_out == OUT_NAT)
    {
        record = nat_out(client_addr, ip_hdr->saddr, htons(*source));
        ip_hdr->saddr = record->fake_addr;
        *source = ntohs(record->fake_port);
    }
    else if (in_or_out == IN_NAT)
    {
        record = nat_in(ip_hdr->daddr, htons(*dest));
        if (record == NULL)
            return 0;
        ip_hdr->daddr = record->client_addr;
        *dest = ntohs(record->client_port);
        client_addr->sin_addr.s_addr = record->clinet_vpn_ip;
        client_addr->sin_port = record->clinet_vpn_port;
    }

    /* Update checksum */
    if (ip_hdr->protocol == IPPROTO_TCP)
    {
        tcp_hdr->check = get_tcp_udp_check(ip_hdr, (union protohdr *)tcp_hdr);
    }
    else if (ip_hdr->protocol == IPPROTO_UDP)
    {
        udp_hdr->check = get_tcp_udp_check(ip_hdr, (union protohdr *)udp_hdr);
    }
    else if (ip_hdr->protocol == IPPROTO_ICMP)
    {
        icmp_hdr->checksum = 0;
        icmp_hdr->checksum = get_ip_icmp_check(icmp_hdr, ntohs(ip_hdr->tot_len) - ip_hdr->ihl * 4);
    }
    ip_hdr->check = 0;
    ip_hdr->check = get_ip_icmp_check(ip_hdr, sizeof(struct iphdr));
    return ntohs(ip_hdr->tot_len);
}

/**
 * Searches for an record in the nat_table with matching fake destination IP and port
 *
 * @param fake_daddr Fake destination IP
 * @param fake_dest Fake destination port
 * @return A pointer to the matching record if found, otherwise returns NULL
 */
struct nat_record *nat_in(__be32 fake_daddr, __be16 fake_dest)
{
    struct nat_record *record = nat_table;
    while (record)
    {
        if (record->fake_addr == fake_daddr &&
            record->fake_port == fake_dest &&
            record->touch + RECORD_TIMEOUT > time(NULL))
        {
            record->touch = time(NULL); /* touch! */
            break;
        }
        record = record->next;
    }
    return record;
}

/**
 * Searches for an record in the nat_table with matching fake destination IP and port
 *
 * @param client_addr Client address information
 * @param saddr Source IP address
 * @param source Source port
 * @return A pointer to the matching or newly created record.
 */
struct nat_record *nat_out(struct sockaddr_in *client_addr, __be32 saddr, __be16 source)
{

    /* Look up nat_table */
    struct nat_record *record = nat_table;
    struct nat_record *before = NULL;
    while (record)
    {
        if (record->client_addr == saddr &&
            record->client_port == source &&
            record->clinet_vpn_ip == client_addr->sin_addr.s_addr &&
            record->clinet_vpn_port == client_addr->sin_port)
        {
            record->touch = time(NULL); /* touch! */
            return record;
        }

        /* Obsolete record */
        if (record->touch + RECORD_TIMEOUT < time(NULL))
        {
            struct nat_record *tmp = record;
            if (before)
                before->next = record->next;
            record = record->next;
            free(tmp);
            continue;
        }

        before = record;
        record = record->next;
    }

    /* Add new record */
    if ((record = (struct nat_record *)malloc(sizeof(struct nat_record))) == NULL)
    {
        perror("Unable to allocate a new nat record");
        return NULL;
    }
    record->client_addr = saddr;
    record->client_port = source;

    record->fake_addr = inet_addr(FAKE_IP);
    record->fake_port = get_fake_port();

    record->clinet_vpn_ip = client_addr->sin_addr.s_addr;
    record->clinet_vpn_port = client_addr->sin_port;

    record->touch = time(NULL);

    record->next = nat_table ? nat_table : NULL;
    nat_table = record;

    return record;
}

struct dec_record *dec_get(int hash_code, int data_size, int symbol_size, int k, int n)
{
    struct dec_record *record = dec_table;
    while (record)
    {
        if (record->hash_code == hash_code &&
            record->data_size == data_size &&
            record->symbol_size == symbol_size &&
            record->k == k &&
            record->n == n)
        {
            // printf("get dec record.\n");
            record->touch = time(NULL);
            return record;
        }

        /* Obsolete record */
        if (record->touch + RECORD_TIMEOUT < time(NULL))
        {
            struct dec_record *tmp = record;
            record = record->next;
            dec_remove(tmp);
            continue;
        }
        record = record->next;
    }

    // printf("new dec record.\n");
    /* Add new record */
    if ((record = (struct dec_record *)malloc(sizeof(struct dec_record))) == NULL)
    {
        perror("Unable to allocate a new dec record");
        return NULL;
    }
    record->hash_code = hash_code;
    record->data_size = data_size;
    record->symbol_size = symbol_size;

    record->k = k;
    record->n = n;
    record->receive_num = 0;

    record->indexes = (int *)calloc(n, sizeof(int));
    record->data = (char **)calloc(n, sizeof(char *));

    record->touch = time(NULL);

    record->next = dec_table ? dec_table : NULL;
    record->before = NULL;
    dec_table = record;

    return record;
}

void dec_remove(struct dec_record *record)
{
    if (record->before)
        record->before->next = record->next;
    if (record->next)
        record->next->before = record->before;

    free(record->indexes);
    for (int i = 0; i < record->n; i++)
    {
        free(record->data[i]);
    }
    free(record->data);

    free(record);

    if (record == dec_table)
    {
        dec_table = NULL;
    }
}

int dec_put(struct dec_record *record, int index, char *d)
{
    if (record->indexes[index])
        return 0;
    record->receive_num++;
    record->indexes[index] = 1;
    record->data[index] = (char *)malloc((record->symbol_size) * sizeof(char));
    memcpy(record->data[index], d, record->symbol_size);

    // printf("[");
    // for (int i = 0; i < record->n; i++)
    // {
    //     printf("%d,", record->indexes[i]);
    // }
    // printf("]\n");
    return record->receive_num >= record->k;
}

/**
 * Allocate a new fake port
 *
 * @return A new unused fake port (or 0 if not found)
 */
uint16_t get_fake_port()
{
    uint16_t fake_port = 0;
    struct nat_record *record;

    /* Iterate over the possible fake ports */
    for (fake_port = MIN_FAKE_PORT; fake_port <= MAX_FAKE_PORT; fake_port++)
    {
        /* Check if the fake port is already in use */
        record = nat_table;
        for (record = nat_table; record; record = record->next)
        {
            if (record->fake_port == fake_port)
            {
                break;
            }
        }
        if (record == NULL)
            break;
    }
    return fake_port;
}

/**
 * Compute IP/ICMP check.
 *
 * @param addr The pointer on the begginning of the packet
 * @param length Length of the packet
 * @return The 16 bits unsigned integer check
 */
uint16_t get_ip_icmp_check(const void *const addr, const size_t length)
{
    const uint16_t *word = (const uint16_t *)addr;
    uint32_t sum = 0;
    size_t left = length;

    while (left > 1)
    {
        sum += *word;
        ++word;
        left -= 2;
    }

    sum += left ? *(uint8_t *)word : 0;

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);

    return (uint16_t)(~sum);
}

/**
 * Compute TCP/UDP check.
 *
 * @param addr The pointer on the begginning of the packet
 * @param length Length of the packet
 * @return The 16 bits unsigned integer check
 */
uint16_t get_tcp_udp_check(const struct iphdr *ip_hdr, union protohdr *proto_hdr)
{

    uint16_t ip_tot_len = ntohs(ip_hdr->tot_len);
    uint16_t proto_tot_len = ip_tot_len - ip_hdr->ihl * 4;

    if (ip_hdr->protocol == IPPROTO_TCP)
    {
        proto_hdr->tcp_hdr.check = 0;
    }
    else if (ip_hdr->protocol == IPPROTO_UDP)
    {
        proto_hdr->udp_hdr.check = 0;
    }
    else
    {
        return 0;
    }

    uint16_t pseudo_tot_len = sizeof(pseudo_hdr) + proto_tot_len;

    pseudo_hdr.src_addr = ip_hdr->saddr;
    pseudo_hdr.dst_addr = ip_hdr->daddr;
    pseudo_hdr.zero = 0;
    pseudo_hdr.proto = ip_hdr->protocol;
    pseudo_hdr.length = htons(proto_tot_len);

    char *pseudo_packet = (char *)malloc(
        pseudo_tot_len * sizeof(char));
    if (pseudo_packet == NULL)
    {
        perror("Error while allocating the pseudo TCP/UDP header!!!");
        exit(1);
    }

    memcpy(pseudo_packet, &pseudo_hdr, sizeof(pseudo_hdr));
    memcpy(pseudo_packet + sizeof(pseudo_hdr),
           proto_hdr, proto_tot_len);

    uint16_t check = get_ip_icmp_check(pseudo_packet,
                                       pseudo_tot_len);

    free(pseudo_packet);
    return check;
}