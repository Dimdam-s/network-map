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

int get_local_network_info(local_iface_info_t *info) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            if (strcmp(ifa->ifa_name, "lo") == 0)
                continue;

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

    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%s %lx %lx", iface, &dest, &gateway) == 3) {
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

void *scan_thread(void *arg) {
    scan_context_t *ctx = (scan_context_t *)arg;
    
    while (ctx->active) {
        struct in_addr target_ip;
        
        pthread_mutex_lock(&ctx->lock);
        if (ctx->current_ip > ctx->end_ip) {
            pthread_mutex_unlock(&ctx->lock);
            
            for(int k=0; k<20; k++) {
                if (!ctx->active) break;
                usleep(100000);
            }
            
            pthread_mutex_lock(&ctx->lock);
            if (ctx->current_ip > ctx->end_ip) { 
                ctx->current_ip = ctx->start_ip_val;
            }
            pthread_mutex_unlock(&ctx->lock);
            continue;
        }
        target_ip.s_addr = htonl(ctx->current_ip);
        ctx->current_ip++;
        pthread_mutex_unlock(&ctx->lock);

        if (!ctx->active) break;

        unsigned short thread_id = (unsigned short)(pthread_self() & 0xFFFF);
        double rtt = ping_host(target_ip, 200, thread_id);
        int discovered = 0;
        char mac_check[18];
        char ip_str_temp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &target_ip, ip_str_temp, INET_ADDRSTRLEN);

        if (rtt >= 0) { 
            discovered = 1;
        } else {
            get_device_mac(ip_str_temp, mac_check, sizeof(mac_check));
            if (strcmp(mac_check, "??:??:??:??:??:??") != 0 && 
                strcmp(mac_check, "00:00:00:00:00:00") != 0 &&
                strcmp(mac_check, "00:00:00:00:00:00") != 0) {
                
                discovered = 1;
                rtt = 50.0;
            }
        }

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
            if (existing_idx != -1) {
                pthread_mutex_lock(&ctx->list_lock);
                ctx->devices[existing_idx].active = 1;
                ctx->devices[existing_idx].rtt_ms = rtt;
                ctx->devices[existing_idx].missed_scans = 0;
                pthread_mutex_unlock(&ctx->list_lock);
            } else {
                device_t new_device;
                memset(&new_device, 0, sizeof(device_t));
                new_device.ip_addr = target_ip;
                new_device.active = 1;
                new_device.rtt_ms = rtt;
                new_device.missed_scans = 0;
                strcpy(new_device.ip_str, ip_str_temp);
                
                if (!ctx->active) break;

                get_device_hostname(target_ip, new_device.hostname, sizeof(new_device.hostname));
                get_device_mac(new_device.ip_str, new_device.mac_addr, sizeof(new_device.mac_addr));

                if (strcmp(new_device.hostname, "Inconnu") == 0) {
                    char vendor[64];
                    get_mac_vendor(new_device.mac_addr, vendor, sizeof(vendor));
                    if (strlen(vendor) > 0) {
                        snprintf(new_device.hostname, sizeof(new_device.hostname), "%s Device", vendor);
                    }
                }

                pthread_mutex_lock(&ctx->list_lock);
                if (ctx->device_count < MAX_DEVICES) {
                     ctx->devices[ctx->device_count] = new_device;
                     ctx->gui_props[ctx->device_count].x = 0;
                     ctx->gui_props[ctx->device_count].y = 0;
                     ctx->device_count++;
                     printf("[+] Trouvé: %-15s | %s\n", new_device.ip_str, new_device.hostname);
                }
                pthread_mutex_unlock(&ctx->list_lock);
            }
        } else {
             if (existing_idx != -1) {
                pthread_mutex_lock(&ctx->list_lock);
                ctx->devices[existing_idx].missed_scans++;
                if (ctx->devices[existing_idx].missed_scans > 2) {
                    if (ctx->devices[existing_idx].active) {
                        printf("[-] Perdu:  %-15s\n", ctx->devices[existing_idx].ip_str);
                    }
                    ctx->devices[existing_idx].active = 0;
                    ctx->devices[existing_idx].rtt_ms = 999.0;
                }
                pthread_mutex_unlock(&ctx->list_lock);
            }
        }
    }
    return NULL;
}

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
    ctx->active = false;
    
    if (ctx->threads) {
        for (int i = 0; i < ctx->thread_count; i++) {
            pthread_join(ctx->threads[i], NULL);
        }
        free(ctx->threads);
        ctx->threads = NULL;
    }
}

void *manager_thread_func(void *arg) {
    scan_context_t *ctx = (scan_context_t *)arg;
    
    _start_workers(ctx);
    
    while (ctx->manager_active) {
        if (ctx->restart_requested) {
            ctx->is_updating = true;
            printf("MANAGER: Restart requested. New count: %d\n", ctx->next_thread_count);
            
            _stop_workers(ctx);
            
            ctx->thread_count = ctx->next_thread_count;
            ctx->restart_requested = false;
            
            _start_workers(ctx);
            
            ctx->is_updating = false;
        }
        
        usleep(100000);
    }
    
    _stop_workers(ctx);
    return NULL;
}

void init_scan_manager(scan_context_t *ctx) {
    ctx->manager_active = true;
    ctx->restart_requested = false;
    ctx->is_updating = false;
    
    if (pthread_create(&ctx->manager_thread, NULL, manager_thread_func, ctx) != 0) {
        perror("pthread_create manager");
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
    ctx->is_updating = true;
}
