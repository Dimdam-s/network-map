#ifndef DNS_SPOOFER_H
#define DNS_SPOOFER_H

#include <pthread.h>
#include <arpa/inet.h>
#include "device.h"

// Configuration for a specific spoofing attack
typedef struct {
    char target_ip[INET_ADDRSTRLEN];
    char gateway_ip[INET_ADDRSTRLEN];
    char target_mac[18];
    char gateway_mac[18]; // We need gateway MAC for ARP poisoning
    
    char spoof_domain[256];
    char redirect_ip[INET_ADDRSTRLEN];
    
    int active;
    pthread_t thread_id;
    int stop_signal;
} spoof_session_t;

// Global or per-device spoofing context could be managed here
// For this project, we might attach this to the device_t or manage a single simple session.
// Let's attach a simplified version to device_t, or just have functions that take args.

// Starts the spoofing attack in a new thread
// Returns 0 on success, -1 on failure
int start_spoofing(device_t *target, const char *gateway_ip, const char *gateway_mac, const char *domain, const char *redirect_ip);

// Stops the spoofing attack for the given target
void stop_spoofing(device_t *target);

#endif
