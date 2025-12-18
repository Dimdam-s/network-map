#ifndef SCAN_CONTEXT_H
#define SCAN_CONTEXT_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "device.h"

#define MAX_DEVICES 256

typedef struct {
    float x, y;
    float target_x, target_y;
    float mass;
    bool dragging;
} device_gui_props_t;

typedef struct {
    uint32_t start_ip_val;
    uint32_t current_ip;
    uint32_t end_ip;
    pthread_mutex_t lock;
    
    device_t devices[MAX_DEVICES];
    device_gui_props_t gui_props[MAX_DEVICES];
    int device_count;
    pthread_mutex_t list_lock;

    bool active;
    char gateway_ip[16];
    
    int thread_count;
    pthread_t *threads;

    pthread_t manager_thread;
    bool manager_active;
    bool restart_requested;
    int next_thread_count;
    bool is_updating;
} scan_context_t;

void init_scan_manager(scan_context_t *ctx);
void shutdown_scan_manager(scan_context_t *ctx);
void request_thread_update(scan_context_t *ctx, int new_count);

#endif
