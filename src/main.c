#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "network_scan.h"
#include "icmp_scanner.h"
#include "device.h"
#include "scan_context.h"
#include "gui.h"
#include "cluster.h"

#define MAX_THREADS 255

int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        printf("INFO: Elevation required. Restarting with sudo...\n");
        
        char **new_argv = malloc(sizeof(char *) * (argc + 2));
        new_argv[0] = "sudo";
        for (int i = 0; i < argc; i++) {
            new_argv[i + 1] = argv[i];
        }
        new_argv[argc + 1] = NULL;
        
        execvp("sudo", new_argv);
        
        perror("sudo execvp");
        free(new_argv);
        return 1;
    }

    int thread_count = 50; // default
    int drone_mode = 0;
    char master_ip[64] = {0};
    
    int opt;
    while ((opt = getopt(argc, argv, "t:dm:")) != -1) {
        switch (opt) {
        case 't':
            thread_count = atoi(optarg);
            if (thread_count < 1) thread_count = 1;
            if (thread_count > 255) thread_count = 255;
            break;
        case 'd': 
            drone_mode = 1;
            break;
        case 'm':
            strncpy(master_ip, optarg, 63);
            drone_mode = 1; // Implies drone mode
            break;
        default:
            fprintf(stderr, "Usage: %s [-t threads] [-d] [-m master_ip]\n", argv[0]);
            return 1;
        }
    }
    
    if (drone_mode) {
         #include "cluster.h"
         run_drone_mode(master_ip); // Now accepts IP
         return 0;
    }

    printf("=== Network Map Live (Raylib) ===\n");

    local_iface_info_t info;
    if (get_local_network_info(&info) != 0) {
        fprintf(stderr, "Erreur: Impossible de trouver une interface réseau active.\n");
        return 1;
    }

    struct in_addr start_ip, end_ip;
    get_network_range(info.ip_addr_obj, info.netmask_obj, &start_ip, &end_ip);

    init_oui_db();

    scan_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.start_ip_val = ntohl(start_ip.s_addr);
    ctx.current_ip = ntohl(start_ip.s_addr);
    ctx.end_ip = ntohl(end_ip.s_addr);
    ctx.device_count = 0;
    ctx.active = true;
    ctx.thread_count = thread_count;
    
    if (get_gateway_ip(ctx.gateway_ip) != 0) {
        strcpy(ctx.gateway_ip, "192.168.1.1");
    }

    // Init Cluster Master
    init_cluster_manager();
    // Auto discovery at start
    discover_drones();

    pthread_mutex_init(&ctx.lock, NULL);
    pthread_mutex_init(&ctx.list_lock, NULL);

    init_scan_manager(&ctx);

    run_gui(&ctx);

    shutdown_scan_manager(&ctx);

    pthread_mutex_destroy(&ctx.lock);
    pthread_mutex_destroy(&ctx.list_lock);
    
    cleanup_oui_db();

    return 0;
}
