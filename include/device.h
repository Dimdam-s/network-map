#ifndef DEVICE_H
#define DEVICE_H

#include <netinet/in.h>
#include <stdint.h>

typedef struct {
    struct in_addr ip_addr;
    char ip_str[INET_ADDRSTRLEN];
    char mac_addr[18];
    char hostname[256];
    double rtt_ms;
    int active;
    
    int is_being_spoofed;
    char spoof_domain[256];
    char spoof_redirect_ip[INET_ADDRSTRLEN];
    void *spoof_data;

    int missed_scans;
} device_t;

void get_device_hostname(struct in_addr ip, char *hostname, size_t len);
void get_device_mac(const char *ip_str, char *mac_addr, size_t len);
void get_mac_vendor(const char *mac_addr, char *vendor, size_t len);

void init_oui_db();
void cleanup_oui_db();

#endif
