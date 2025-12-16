#ifndef ICMP_SCANNER_H
#define ICMP_SCANNER_H

#include <netinet/in.h>

// Envoie un ping à l'adresse IP spécifiée.
// Retourne le temps de réponse en ms (>= 0) si l'hôte répond, -1.0 sinon.
// timeout_ms : temps d'attente en millisecondes
double ping_host(struct in_addr target_ip, int timeout_ms);

#endif
