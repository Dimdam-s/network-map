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

#define MAX_THREADS 50

// Thread de scan révisé
void *scan_thread(void *arg) {
    scan_context_t *ctx = (scan_context_t *)arg;
    
    while (ctx->active) {
        struct in_addr target_ip;
        
        // Récupérer la prochaine IP à scanner
        pthread_mutex_lock(&ctx->lock);
        if (ctx->current_ip > ctx->end_ip) {
            // Si on a fini le scan, on arrête pour l'instant
            pthread_mutex_unlock(&ctx->lock);
            
            // Attendre un peu avant de tuer le thread
            usleep(100000); 
            break; 
        }
        target_ip.s_addr = htonl(ctx->current_ip);
        ctx->current_ip++;
        pthread_mutex_unlock(&ctx->lock);

        // Check active again
        if (!ctx->active) break;

        // Scan
        double rtt = ping_host(target_ip, 500); // 500ms timeout
        if (rtt >= 0) { 
            device_t new_device;
            new_device.ip_addr = target_ip;
            new_device.active = 1;
            new_device.rtt_ms = rtt;
            inet_ntop(AF_INET, &target_ip, new_device.ip_str, INET_ADDRSTRLEN);
            
            // Si l'app est fermée entre temps
            if (!ctx->active) break;

            // Récupération des infos supplémentaires
            get_device_hostname(target_ip, new_device.hostname, sizeof(new_device.hostname));
            get_device_mac(new_device.ip_str, new_device.mac_addr, sizeof(new_device.mac_addr));

            if (strcmp(new_device.hostname, "Inconnu") == 0) {
                char vendor[64];
                get_mac_vendor(new_device.mac_addr, vendor, sizeof(vendor));
                if (strlen(vendor) > 0) {
                    snprintf(new_device.hostname, sizeof(new_device.hostname), "%s Device", vendor);
                }
            }

            // Ajout à la liste
            pthread_mutex_lock(&ctx->list_lock);
            if (ctx->device_count < MAX_DEVICES) {
                // Vérifier doublons
                int exists = 0;
                for(int i=0; i<ctx->device_count; i++) {
                     if(strcmp(ctx->devices[i].ip_str, new_device.ip_str) == 0) {
                         // Mise à jour RTT
                         ctx->devices[i].rtt_ms = rtt;
                         exists = 1; 
                         break;
                     }
                }

                if (!exists) {
                    ctx->devices[ctx->device_count] = new_device;
                    // Init GUI props
                    ctx->gui_props[ctx->device_count].x = 0;
                    ctx->gui_props[ctx->device_count].y = 0;
                    ctx->device_count++;
                    printf("[+] Trouvé: %-15s | %s\n", new_device.ip_str, new_device.hostname);
                }
            }
            pthread_mutex_unlock(&ctx->list_lock);
        }
    }
    return NULL;
}

int main() {
    if (geteuid() != 0) {
        fprintf(stderr, "Erreur: Ce programme nécessite les droits root (sudo) pour utiliser les Raw Sockets.\n");
        return 1;
    }

    printf("=== Network Map Live (Raylib) ===\n");

    local_iface_info_t info;
    if (get_local_network_info(&info) != 0) {
        fprintf(stderr, "Erreur: Impossible de trouver une interface réseau active.\n");
        return 1;
    }

    struct in_addr start_ip, end_ip;
    get_network_range(info.ip_addr_obj, info.netmask_obj, &start_ip, &end_ip);

    // Initialisation du contexte
    scan_context_t ctx;
    ctx.current_ip = ntohl(start_ip.s_addr);
    ctx.end_ip = ntohl(end_ip.s_addr);
    ctx.device_count = 0;
    ctx.active = true;
    
    // Gateway
    if (get_gateway_ip(ctx.gateway_ip) != 0) {
        strcpy(ctx.gateway_ip, "192.168.1.1"); // Fallback
    }

    pthread_mutex_init(&ctx.lock, NULL);
    pthread_mutex_init(&ctx.list_lock, NULL);

    // Lancement des threads de scan
    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, scan_thread, &ctx) != 0) {
            perror("pthread_create");
        }
    }

    // Lancement de la GUI (Bloquant jusqu'à la fermeture de la fenêtre)
    run_gui(&ctx);

    // Fermeture
    printf("Arrêt des scans...\n");
    ctx.active = false; 
    
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&ctx.lock);
    pthread_mutex_destroy(&ctx.list_lock);
    
    return 0;
}
