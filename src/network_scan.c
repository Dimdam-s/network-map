#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "network_scan.h"
#include "icmp_scanner.h"
#include "device.h"
#include "scan_context.h"

// --- existing helpers ---
int get_local_network_info(local_iface_info_t *info) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    // Parcourir la liste des interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // On s'intéresse uniquement à IPv4 (AF_INET)
        if (ifa->ifa_addr->sa_family == AF_INET) {
            // Ignorer l'interface loopback (127.0.0.1)
            if (strcmp(ifa->ifa_name, "lo") == 0)
                continue;

            // On prend la première interface valide trouvée
            strncpy(info->interface_name, ifa->ifa_name, sizeof(info->interface_name) - 1);
            
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            struct sockaddr_in *mask = (struct sockaddr_in *)ifa->ifa_netmask;

            info->ip_addr_obj = addr->sin_addr;
            info->netmask_obj = mask->sin_addr;

            inet_ntop(AF_INET, &addr->sin_addr, info->ip_address, INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &mask->sin_addr, info->netmask, INET_ADDRSTRLEN);

            found = 1;
            break; 
        }
    }

    freeifaddrs(ifaddr);
    return found ? 0 : -1;
}

void get_network_range(struct in_addr ip, struct in_addr mask, struct in_addr *start_ip, struct in_addr *end_ip) {
    uint32_t ip_val = ntohl(ip.s_addr);
    uint32_t mask_val = ntohl(mask.s_addr);

    uint32_t network_addr = ip_val & mask_val;
    uint32_t broadcast_addr = network_addr | (~mask_val);

    // Plage utilisable : Network + 1 à Broadcast - 1
    start_ip->s_addr = htonl(network_addr + 1);
    end_ip->s_addr = htonl(broadcast_addr - 1);
}

int get_gateway_ip(char *gateway_ip) {
    FILE *fp;
    char line[256];
    char iface[16];
    unsigned long dest, gateway;
    
    fp = fopen("/proc/net/route", "r");
    if (fp == NULL) {
        return -1;
    }

    // Ignorer la première ligne
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%s %lx %lx", iface, &dest, &gateway) == 3) {
            // Destination 0 (0.0.0.0) signifie default gateway
            if (dest == 0) {
                struct in_addr addr;
                addr.s_addr = gateway;
                inet_ntop(AF_INET, &addr, gateway_ip, INET_ADDRSTRLEN);
                fclose(fp);
                return 0;
            }
        }
    }

    fclose(fp);
    return -1;
}

// --- THREADING LOGIC ---

// Thread de scan
void *scan_thread(void *arg) {
    scan_context_t *ctx = (scan_context_t *)arg;
    
    while (ctx->active) {
        struct in_addr target_ip;
        
        // Récupérer la prochaine IP à scanner
        pthread_mutex_lock(&ctx->lock);
        if (ctx->current_ip > ctx->end_ip) {
            // Fin du pass. On reset pour scanner en continu (Live Update)
            pthread_mutex_unlock(&ctx->lock);
            
            // On attend
            // On attend (Loop to allow quick exit)
            for(int k=0; k<20; k++) {
                if (!ctx->active) break;
                usleep(100000); // 100ms * 20 = 2 sec
            }
            
            pthread_mutex_lock(&ctx->lock);
            if (ctx->current_ip > ctx->end_ip) { 
                // Reset du scan
                ctx->current_ip = ctx->start_ip_val;
            }
            pthread_mutex_unlock(&ctx->lock);
            continue;
        }
        target_ip.s_addr = htonl(ctx->current_ip);
        ctx->current_ip++;
        pthread_mutex_unlock(&ctx->lock);

        // Check active again
        if (!ctx->active) break;

        // Scan ICMP
        // Use unique ID per thread to avoid packet stealing
        unsigned short thread_id = (unsigned short)(pthread_self() & 0xFFFF);
        double rtt = ping_host(target_ip, 200, thread_id); // 200ms timeout
        int discovered = 0;
        char mac_check[18];
        char ip_str_temp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &target_ip, ip_str_temp, INET_ADDRSTRLEN);

        if (rtt >= 0) { 
            discovered = 1;
        } else {
            // Tentative de fallback ARP (si firewall bloque ICMP)
            // L'envoi du ping a forcé une requête ARP par le noyau.
            get_device_mac(ip_str_temp, mac_check, sizeof(mac_check));
            if (strcmp(mac_check, "??:??:??:??:??:??") != 0 && 
                strcmp(mac_check, "00:00:00:00:00:00") != 0 &&
                strcmp(mac_check, "00:00:00:00:00:00") != 0) { // Check double zero just in case
                
                discovered = 1;
                rtt = 50.0; // Latence artificielle pour les devices bloqués
            }
        }

        // Check if device already exists in list to update status
        int existing_idx = -1;
        pthread_mutex_lock(&ctx->list_lock);
        for(int i=0; i<ctx->device_count; i++) {
             if(strcmp(ctx->devices[i].ip_str, ip_str_temp) == 0) {
                 existing_idx = i;
                 break;
             }
        }
        pthread_mutex_unlock(&ctx->list_lock);

        if (discovered) { 
            // If exists, update
            if (existing_idx != -1) {
                pthread_mutex_lock(&ctx->list_lock);
                ctx->devices[existing_idx].active = 1;
                ctx->devices[existing_idx].rtt_ms = rtt;
                ctx->devices[existing_idx].missed_scans = 0;
                pthread_mutex_unlock(&ctx->list_lock);
            } else {
                // New Device
                device_t new_device;
                memset(&new_device, 0, sizeof(device_t)); // Safe init
                new_device.ip_addr = target_ip;
                new_device.active = 1;
                new_device.rtt_ms = rtt;
                new_device.missed_scans = 0;
                strcpy(new_device.ip_str, ip_str_temp);
                
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
                     ctx->devices[ctx->device_count] = new_device;
                     // Init GUI props
                     ctx->gui_props[ctx->device_count].x = 0;
                     ctx->gui_props[ctx->device_count].y = 0;
                     ctx->device_count++;
                     printf("[+] Trouvé: %-15s | %s\n", new_device.ip_str, new_device.hostname);
                }
                pthread_mutex_unlock(&ctx->list_lock);
            }
        } else {
            // Not discovered (Ping/Arp failed)
            // If it existed, we mark missed scan
             if (existing_idx != -1) {
                pthread_mutex_lock(&ctx->list_lock);
                ctx->devices[existing_idx].missed_scans++;
                if (ctx->devices[existing_idx].missed_scans > 2) {
                    if (ctx->devices[existing_idx].active) {
                        printf("[-] Perdu:  %-15s\n", ctx->devices[existing_idx].ip_str);
                    }
                    ctx->devices[existing_idx].active = 0;
                    ctx->devices[existing_idx].rtt_ms = 999.0; // High latency visual
                }
                pthread_mutex_unlock(&ctx->list_lock);
            }
        }
    }
    return NULL;
}

// --- WORKER MANAGEMENT (Private) ---

void _start_workers(scan_context_t *ctx) {
    if (ctx->thread_count <= 0) ctx->thread_count = 10;
    
    ctx->threads = malloc(sizeof(pthread_t) * ctx->thread_count);
    ctx->active = true;
    
    printf("INFO: Starting %d scan threads...\n", ctx->thread_count);
    for (int i = 0; i < ctx->thread_count; i++) {
        if (pthread_create(&ctx->threads[i], NULL, scan_thread, ctx) != 0) {
            perror("pthread_create worker");
        }
    }
}

void _stop_workers(scan_context_t *ctx) {
    printf("INFO: Stopping scan threads...\n");
    ctx->active = false; // Signal threads to stop
    
    if (ctx->threads) {
        for (int i = 0; i < ctx->thread_count; i++) {
            pthread_join(ctx->threads[i], NULL);
        }
        free(ctx->threads);
        ctx->threads = NULL;
    }
}

// --- MANAGER THREAD ---

void *manager_thread_func(void *arg) {
    scan_context_t *ctx = (scan_context_t *)arg;
    
    // Start initial workers
    _start_workers(ctx);
    
    while (ctx->manager_active) {
        if (ctx->restart_requested) {
            ctx->is_updating = true;
            printf("MANAGER: Restart requested. New count: %d\n", ctx->next_thread_count);
            
            // 1. Stop workers (Blocking, but in this thread, not GUI)
            _stop_workers(ctx);
            
            // 2. Update config
            ctx->thread_count = ctx->next_thread_count;
            ctx->restart_requested = false;
            
            // 3. Start workers
            _start_workers(ctx);
            
            ctx->is_updating = false;
        }
        
        usleep(100000); // Check every 100ms
    }
    
    // Cleanup on exit
    _stop_workers(ctx);
    return NULL;
}

// --- PUBLIC API ---

void init_scan_manager(scan_context_t *ctx) {
    ctx->manager_active = true;
    ctx->restart_requested = false;
    ctx->is_updating = false;
    
    if (pthread_create(&ctx->manager_thread, NULL, manager_thread_func, ctx) != 0) {
        perror("pthread_create manager");
        // Fallback to sync start if manager fails
        _start_workers(ctx);
    }
}

void shutdown_scan_manager(scan_context_t *ctx) {
    ctx->manager_active = false;
    pthread_join(ctx->manager_thread, NULL);
}

void request_thread_update(scan_context_t *ctx, int new_count) {
    if (new_count < 1) new_count = 1;
    if (new_count > 255) new_count = 255;
    
    if (new_count == ctx->thread_count && !ctx->restart_requested) return;
    
    ctx->next_thread_count = new_count;
    ctx->restart_requested = true;
    ctx->is_updating = true; // Set immediately for UI feedback
}

