#ifndef WEB_EXPORT_H
#define WEB_EXPORT_H

#include "device.h"

// Génère le fichier HTML de visualisation
void export_to_html(const char *filename, device_t *devices, int count, const char *gateway_ip);

#endif
