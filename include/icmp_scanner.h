#ifndef ICMP_SCANNER_H
#define ICMP_SCANNER_H

#include <netinet/in.h>

// Envoie un ping à l'adresse IP spécifiée.
// Scan d'un hôte spécifique
// Retourne le RTT en ms, ou -1.0 si échec/timeout
double ping_host(struct in_addr target_ip, int timeout_ms, unsigned short id);

#endif
