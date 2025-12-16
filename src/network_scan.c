#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "network_scan.h"

int get_local_network_info(local_iface_info_t *info) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    // Parcourir la liste des interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // On s'intéresse uniquement à IPv4 (AF_INET)
        if (ifa->ifa_addr->sa_family == AF_INET) {
            // Ignorer l'interface loopback (127.0.0.1)
            if (strcmp(ifa->ifa_name, "lo") == 0)
                continue;

            // On prend la première interface valide trouvée
            strncpy(info->interface_name, ifa->ifa_name, sizeof(info->interface_name) - 1);
            
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;

            info->ip_addr_obj = addr->sin_addr;
            info->netmask_obj = mask->sin_addr;

            inet_ntop(AF_INET, &addr->sin_addr, info->ip_address, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &mask->sin_addr, info->netmask, INET_ADDRSTRLEN);

            found = 1;
            break; 
        }
    }

    freeifaddrs(ifaddr);
    return found ? 0 : -1;
}

void get_network_range(struct in_addr ip, struct in_addr mask, struct in_addr *start_ip, struct in_addr *end_ip) {
    uint32_t ip_val = ntohl(ip.s_addr);
    uint32_t mask_val = ntohl(mask.s_addr);

    uint32_t network_addr = ip_val & mask_val;
    uint32_t broadcast_addr = network_addr | (~mask_val);

    // Plage utilisable : Network + 1 à Broadcast - 1
    start_ip->s_addr = htonl(network_addr + 1);
    end_ip->s_addr = htonl(broadcast_addr - 1);
}

int get_gateway_ip(char *gateway_ip) {
    FILE *fp;
    char line[256];
    char iface[16];
    unsigned long dest, gateway;
    
    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        return -1;
    }

    // Ignorer la première ligne
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%s %lx %lx", iface, &dest, &gateway) == 3) {
            // Destination 0 (0.0.0.0) signifie default gateway
            if (dest == 0) {
                struct in_addr addr;
                addr.s_addr = gateway;
                inet_ntop(AF_INET, &addr, gateway_ip, INET_ADDRSTRLEN);
                fclose(fp);
                return 0;
            }
        }
    }

    fclose(fp);
    return -1;
}
