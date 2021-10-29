// SPDX-License-Identifier: GPL-3.0-only

#ifndef MASSRESOLVER_NET_H
#define MASSRESOLVER_NET_H

#include <stdbool.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef PCAP_SUPPORT
    #include <sys/ioctl.h>
#endif
#include <inttypes.h>

#define loop_sockets(sockets) \
        for (socket_info_t *socket = (sockets)->data; socket < ((socket_info_t*)(sockets)->data) + (sockets)->len; socket++)

typedef enum
{
    SOCKET_TYPE_INTERFACE,
    SOCKET_TYPE_QUERY,
    SOCKET_TYPE_CONTROL
} socket_type_t;

typedef enum
{
    NETMODE_EPOLL,
    NETMODE_BUSYPOLL
} netmode_t;

typedef struct
{
    sa_family_t protocol;
    int descriptor;
    socket_type_t type;
    void *data;
} socket_info_t;

void socket_noblock(socket_info_t* socket)
{
    int sd = socket->descriptor;
    int flags = fcntl(sd, F_GETFL, 0);
    fcntl(sd, F_SETFL, flags | O_NONBLOCK);
}

socklen_t sockaddr_storage_size(struct sockaddr_storage *storage)
{
    if(storage->ss_family == AF_INET)
    {
        return sizeof(struct sockaddr_in);
    }
    else if(storage->ss_family == AF_INET6)
    {
        return sizeof(struct sockaddr_in6);
    }
    return 0;
}

#ifdef HAVE_EPOLL
void add_sockets(int epollfd, uint32_t events, int op, buffer_t *sockets)
{
    socket_info_t *interface_sockets = sockets->data;
    for (size_t i = 0; i < sockets->len; i++)
    {
        struct epoll_event ev;
        bzero(&ev, sizeof(ev));
        ev.data.ptr = &interface_sockets[i];
        ev.events = events;
        if (epoll_ctl(epollfd, op, interface_sockets[i].descriptor, &ev) != 0)
        {
            perror("Failed to add epoll event");
            exit(EXIT_FAILURE);
        }
    }
}
#endif

bool str_to_addr(char *str, uint16_t default_port, struct sockaddr_storage *addr)
{
    if(str == NULL || str[0] == 0)
    {
        return false;
    }
    while(*str == ' ' || *str == '\t') // Skip whitespaces ("trim left")
    {
        str++;
    }
    unsigned long int port = default_port;

    if(str[0] == '[')
    {
        str++;
        char *closing_bracket = strstr(str, "]");
        if(!closing_bracket)
        {
            return false;
        }
        if(closing_bracket[1] == ':') // Is there a port separator?
        {
            *closing_bracket = 0;
            char *invalid_char;
            port = strtoul(closing_bracket + 2, &invalid_char, 10);
            if (*invalid_char != 0 || port >= UINT16_MAX)
            {
                return false;
            }
        }
    }
    else // We either have an IPv6 address without square brackets or an IPv4 address
    {
        bool v4 = false;
        char *colon = NULL;
        for(char *c = str; *c != 0; c++)
        {
            if(*c == '.' && colon == NULL) // dot before colon
            {
                v4 = true;
            }
            if(*c == ':')
            {
                colon = c;
            }
        }
        if(v4 && colon) // We found the port separator
        {
            *colon = 0;
            char *invalid_char;
            port = strtoul(colon + 1, &invalid_char, 10);
            if (*invalid_char != 0 || port >= UINT16_MAX)
            {
                return false;
            }
        }

    }
    if (inet_pton(AF_INET, str, &((struct sockaddr_in*)addr)->sin_addr) == 1)
    {
        ((struct sockaddr_in*)addr)->sin_port = htons((uint16_t )port);
        ((struct sockaddr_in*)addr)->sin_family = AF_INET;
        return true;
    }
    else if (inet_pton(AF_INET6, str, &((struct sockaddr_in6*)addr)->sin6_addr) == 1)
    {
        ((struct sockaddr_in6*)addr)->sin6_port = htons((uint16_t )port);
        ((struct sockaddr_in6*)addr)->sin6_family = AF_INET6;
        return true;
    }
    return false;
}

const char* ip2ptr(const char *qname)
{
    struct sockaddr_storage ptr_addr_storage;
    static char ptr_query[1024];

    if (inet_pton(AF_INET, qname, &((struct sockaddr_in*)&ptr_addr_storage)->sin_addr) == 1)
    {
        uint8_t *addr = (uint8_t*)&((struct sockaddr_in*)&ptr_addr_storage)->sin_addr.s_addr;
        snprintf(ptr_query, sizeof(ptr_query) - 1, "%"PRIu8".%"PRIu8".%"PRIu8".%"PRIu8".in-addr.arpa.",
                 addr[3], addr[2], addr[1], addr[0]);
        return ptr_query;
    }
    else if (inet_pton(AF_INET6, qname, &((struct sockaddr_in6*)&ptr_addr_storage)->sin6_addr) == 1)
    {
        uint8_t *addr = (uint8_t*)&((struct sockaddr_in6*)&ptr_addr_storage)->sin6_addr.s6_addr;
        snprintf(ptr_query, sizeof(ptr_query) - 1,
                 "%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x."
                 "%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x."
                 "ip6.arpa.",
                 addr[15] & 0xF, (addr[15] >> 4) & 0xF,
                 addr[14] & 0xF, (addr[14] >> 4) & 0xF,
                 addr[13] & 0xF, (addr[13] >> 4) & 0xF,
                 addr[12] & 0xF, (addr[12] >> 4) & 0xF,
                 addr[11] & 0xF, (addr[11] >> 4) & 0xF,
                 addr[10] & 0xF, (addr[10] >> 4) & 0xF,
                 addr[9] & 0xF, (addr[9] >> 4) & 0xF,
                 addr[8] & 0xF, (addr[8] >> 4) & 0xF,
                 addr[7] & 0xF, (addr[7] >> 4) & 0xF,
                 addr[6] & 0xF, (addr[6] >> 4) & 0xF,
                 addr[5] & 0xF, (addr[5] >> 4) & 0xF,
                 addr[4] & 0xF, (addr[4] >> 4) & 0xF,
                 addr[3] & 0xF, (addr[3] >> 4) & 0xF,
                 addr[2] & 0xF, (addr[2] >> 4) & 0xF,
                 addr[1] & 0xF, (addr[1] >> 4) & 0xF,
                 addr[0] & 0xF, (addr[0] >> 4) & 0xF
        );
        return ptr_query;
    }
    return NULL;
}

#ifdef PCAP_SUPPORT
int get_iface_hw_addr(char *iface, uint8_t *hw_mac)
{
    int s;
    struct ifreq buffer;

    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s < 0)
    {
        return EXIT_FAILURE;
    }
    bzero(&buffer, sizeof(buffer));
    strncpy(buffer.ifr_name, iface, IFNAMSIZ);
    buffer.ifr_name[sizeof(buffer.ifr_name) -1] = '\0';
    ioctl(s, SIOCGIFHWADDR, &buffer);
    close(s);
    memcpy(hw_mac, buffer.ifr_hwaddr.sa_data, 6);
    return EXIT_SUCCESS;
}

#define MAC_READABLE_BUFLEN 18

int get_iface_hw_addr_readable(char *iface, char *hw_mac)
{
    uint8_t buffer[6];
    int result = get_iface_hw_addr(iface, buffer);
    for(uint8_t *b = buffer; b < buffer + 6; b++)
    {
        sprintf(hw_mac, "%02x:", *b);
        hw_mac += 3;
        if(b == buffer + 5)
        {
            *(hw_mac - 1) = 0;
        }
    }
    return result;
}
#endif

char *sockaddr2str(struct sockaddr_storage *addr)
{
    static char str[INET6_ADDRSTRLEN + sizeof(":65535") + 2]; // + 2 for [ and ]
    static uint16_t port;
    size_t len;

    if(addr->ss_family == AF_INET)
    {
        port = ntohs(((struct sockaddr_in*)addr)->sin_port);
        inet_ntop(addr->ss_family, &((struct sockaddr_in*)addr)->sin_addr, str, sizeof(str));
        len = strlen(str);
        // inet_ntop does not allow us to determine, how long the printed string was.
        // Thus, we have to use strlen.
    }
    else
    {
        str[0] = '[';
        port = ntohs(((struct sockaddr_in6*)addr)->sin6_port);
        inet_ntop(addr->ss_family, &((struct sockaddr_in6*)addr)->sin6_addr, str + 1, sizeof(str) - 1);
        len = strlen(str);
        str[len++] = ']';
        str[len] = 0;
    }

    snprintf(str + len, sizeof(str) - len, ":%" PRIu16, port);

    return str;
}

#endif //MASSRESOLVER_NET_H
