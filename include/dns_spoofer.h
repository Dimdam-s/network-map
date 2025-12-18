#ifndef DNS_SPOOFER_H
#define DNS_SPOOFER_H

#include <pthread.h>
#include <arpa/inet.h>
#include "device.h"

typedef struct {
    char target_ip[INET_ADDRSTRLEN];
    char gateway_ip[INET_ADDRSTRLEN];
    char target_mac[18];
    char gateway_mac[18];
    
    char spoof_domain[256];
    char redirect_ip[INET_ADDRSTRLEN];
    
    int active;
    pthread_t thread_id;
    int stop_signal;
} spoof_session_t;

int start_spoofing(device_t *target, const char *gateway_ip, const char *gateway_mac, const char *domain, const char *redirect_ip);
void stop_spoofing(device_t *target);

#endif
