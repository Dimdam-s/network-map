#ifndef DEVICE_H
#define DEVICE_H

#include <netinet/in.h>

typedef struct {
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr ip_addr;
    char mac_addr[18]; // xx:xx:xx:xx:xx:xx
    char hostname[256];
    double rtt_ms; // Temps de réponse en ms
    int active;
} device_t;

// Récupère le nom d'hôte via DNS inverse
void get_device_hostname(struct in_addr ip, char *hostname, size_t len);

// Récupère le vendeur de l'adresse MAC (Heuristique simple)
void get_mac_vendor(const char *mac_addr, char *vendor, size_t len);

// Récupère l'adresse MAC depuis /proc/net/arp (Linux spécifique)
void get_device_mac(const char *ip_str, char *mac_addr, size_t len);

#endif
