#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include "network_scan.h"
#include "icmp_scanner.h"
#include "device.h"
#include "web_export.h"

#define MAX_THREADS 50
#define MAX_DEVICES 256

// Structure partagée pour les threads
typedef struct {
    uint32_t current_ip;
    uint32_t end_ip;
    pthread_mutex_t lock;
    
    device_t devices[MAX_DEVICES];
    int device_count;
    pthread_mutex_t list_lock;
} scan_context_t;

void *scan_thread(void *arg) {
    scan_context_t *ctx = (scan_context_t *)arg;
    
    while (1) {
        struct in_addr target_ip;
        
        // Récupérer la prochaine IP à scanner de manière thread-safe
        pthread_mutex_lock(&ctx->lock);
        if (ctx->current_ip > ctx->end_ip) {
            pthread_mutex_unlock(&ctx->lock);
            break;
        }
        target_ip.s_addr = htonl(ctx->current_ip);
        ctx->current_ip++;
        pthread_mutex_unlock(&ctx->lock);

        // Scan
        double rtt = ping_host(target_ip, 500); // 500ms timeout
        if (rtt >= 0) { 
            device_t new_device;
            new_device.ip_addr = target_ip;
            new_device.active = 1;
            new_device.rtt_ms = rtt;
            inet_ntop(AF_INET, &target_ip, new_device.ip_str, INET_ADDRSTRLEN);
            
            // Récupération des infos supplémentaires
            get_device_hostname(target_ip, new_device.hostname, sizeof(new_device.hostname));
            get_device_mac(new_device.ip_str, new_device.mac_addr, sizeof(new_device.mac_addr));

            // Tentative d'enrichissement du nom si "Inconnu"
            if (strcmp(new_device.hostname, "Inconnu") == 0) {
                char vendor[64];
                get_mac_vendor(new_device.mac_addr, vendor, sizeof(vendor));
                if (strlen(vendor) > 0) {
                    snprintf(new_device.hostname, sizeof(new_device.hostname), "%s Device", vendor);
                }
            }

            // Ajout à la liste des résultats
            pthread_mutex_lock(&ctx->list_lock);
            if (ctx->device_count < MAX_DEVICES) {
                ctx->devices[ctx->device_count++] = new_device;
                printf("[+] Trouvé: %-15s | %-20s | %.2f ms\n", new_device.ip_str, new_device.hostname, rtt);
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

    printf("=== Network Map Scanner (Multi-threaded) ===\n");

    local_iface_info_t info;
    if (get_local_network_info(&info) != 0) {
        fprintf(stderr, "Erreur: Impossible de trouver une interface réseau active.\n");
        return 1;
    }

    char gateway_ip[INET_ADDRSTRLEN] = "Inconnu";
    if (get_gateway_ip(gateway_ip) == 0) {
        printf("Passerelle (Routeur): %s\n", gateway_ip);
    } else {
        printf("Passerelle: Non détectée\n");
    }

    printf("Interface: %s\n", info.interface_name);
    printf("IP Locale: %s\n", info.ip_address);
    printf("Masque   : %s\n", info.netmask);

    struct in_addr start_ip, end_ip;
    get_network_range(info.ip_addr_obj, info.netmask_obj, &start_ip, &end_ip);

    char start_str[INET_ADDRSTRLEN];
    char end_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &start_ip, start_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &end_ip, end_str, INET_ADDRSTRLEN);

    printf("Plage de scan: %s - %s\n", start_str, end_str);
    printf("Démarrage du scan avec %d threads...\n\n", MAX_THREADS);

    // Initialisation du contexte de scan
    scan_context_t ctx;
    ctx.current_ip = ntohl(start_ip.s_addr);
    ctx.end_ip = ntohl(end_ip.s_addr);
    ctx.device_count = 0;
    pthread_mutex_init(&ctx.lock, NULL);
    pthread_mutex_init(&ctx.list_lock, NULL);

    // Création des threads
    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, scan_thread, &ctx) != 0) {
            perror("pthread_create");
        }
    }

    // Attente de la fin des threads
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&ctx.lock);
    pthread_mutex_destroy(&ctx.list_lock);

    // Affichage de la carte finale console
    printf("\n=== CARTE DU RÉSEAU ===\n");
    printf("%-16s | %-18s | %-8s | %s\n", "Adresse IP", "Adresse MAC", "Latence", "Nom d'hôte");
    printf("--------------------------------------------------------------------------------\n");
    
    for (int i = 0; i < ctx.device_count; i++) {
        printf("%-16s | %-18s | %6.2f ms | %s\n", 
            ctx.devices[i].ip_str, 
            ctx.devices[i].mac_addr, 
            ctx.devices[i].rtt_ms,
            ctx.devices[i].hostname);
    }
    printf("--------------------------------------------------------------------------------\n");
    printf("Total appareils trouvés: %d\n", ctx.device_count);

    // Export HTML
    export_to_html("network_map.html", ctx.devices, ctx.device_count, gateway_ip);
    printf("\n[OK] Carte visuelle générée : network_map.html\n");
    printf("Ouvrez ce fichier dans votre navigateur pour voir le graphe.\n");

    return 0;
}
