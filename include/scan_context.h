#ifndef SCAN_CONTEXT_H
#define SCAN_CONTEXT_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "device.h"

#define MAX_DEVICES 256

// Extension de la structure device pour la GUI
typedef struct {
    float x, y;         // Position actuelle
    float target_x, target_y; // Position cible (force-directed)
    float mass;
    bool dragging;
} device_gui_props_t;

typedef struct {
    uint32_t current_ip;
    uint32_t end_ip;
    pthread_mutex_t lock;
    
    device_t devices[MAX_DEVICES];
    device_gui_props_t gui_props[MAX_DEVICES]; // Propriétés GUI parallèles
    int device_count;
    pthread_mutex_t list_lock;

    bool active; // Flag pour arrêter les threads
    char gateway_ip[16];
} scan_context_t;

#endif
