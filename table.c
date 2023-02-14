#include "table.h"
#include "server.h"
#include <stdio.h>

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

    /* Look up table and translate address*/
    struct table_record *record = NULL;
    if (in_or_out == OUT_TABLE)
    {
        record = table_outbound(client_addr, ip_hdr->saddr, htons(*source));
        ip_hdr->saddr = record->fake_addr;
        *source = ntohs(record->fake_port);
    }
    else if (in_or_out == IN_TABLE)
    {
        record = table_inbound(ip_hdr->daddr, htons(*dest));
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
    return 1;
}

/**
 * Searches for an record in the table with matching fake destination IP and port
 *
 * @param fake_daddr Fake destination IP
 * @param fake_dest Fake destination port
 * @return A pointer to the matching record if found, otherwise returns NULL
 */
struct table_record *table_inbound(__be32 fake_daddr, __be16 fake_dest)
{
    struct table_record *record = table;
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
 * Searches for an record in the table with matching fake destination IP and port
 *
 * @param client_addr Client address information
 * @param saddr Source IP address
 * @param source Source port
 * @return A pointer to the matching or newly created record.
 */
struct table_record *table_outbound(struct sockaddr_in *client_addr, __be32 saddr, __be16 source)
{

    /* Look up table */
    struct table_record *record = table;
    struct table_record *before = NULL;
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
        if (before && record->touch + RECORD_TIMEOUT < time(NULL))
        {
            before->next = record->next;
            free(record);
            record = before->next;
            continue;
        }

        before = record;
        record = record->next;
    }

    /* Add new record */
    if ((record = (struct table_record *)malloc(sizeof(struct table_record))) == NULL)
    {
        perror("Unable to allocate a new record");
        return NULL;
    }
    record->client_addr = saddr;
    record->client_port = source;

    record->fake_addr = inet_addr(FAKE_IP);
    record->fake_port = get_fake_port();

    record->clinet_vpn_ip = client_addr->sin_addr.s_addr;
    record->clinet_vpn_port = client_addr->sin_port;

    record->touch = time(NULL);
    record->next = NULL;

    if (table)
    {
        record->next = table;
        table = record;
    }
    else
    {
        table = record;
    }

    return record;
}

/**
 * Allocate a new fake port
 *
 * @return A new unused fake port (or 0 if not found)
 */
in_port_t get_fake_port()
{
    uint16_t fake_port = 0;
    struct table_record *record;

    /* Iterate over the possible fake ports */
    for (fake_port = MIN_FAKE_PORT; fake_port <= MAX_FAKE_PORT; fake_port++)
    {
        /* Check if the fake port is already in use */
        record = table;
        for (record = table; record; record = record->next)
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