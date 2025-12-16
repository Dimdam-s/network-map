#ifndef NETWORK_SCAN_H
#define NETWORK_SCAN_H

#include <netinet/in.h>

typedef struct {
    char interface_name[64];
    char ip_address[INET_ADDRSTRLEN];
    char netmask[INET_ADDRSTRLEN];
    struct in_addr ip_addr_obj;
    struct in_addr netmask_obj;
} local_iface_info_t;

// Récupère l'interface active (non-loopback)
int get_local_network_info(local_iface_info_t *info);

// Calcule la première et la dernière IP de la plage
void get_network_range(struct in_addr ip, struct in_addr mask, struct in_addr *start_ip, struct in_addr *end_ip);

// Récupère l'IP de la passerelle par défaut (Gateway)
// Retourne 0 si trouvé, -1 sinon
int get_gateway_ip(char *gateway_ip);

#endif
