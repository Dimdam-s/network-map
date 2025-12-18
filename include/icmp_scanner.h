#ifndef ICMP_SCANNER_H
#define ICMP_SCANNER_H

#include <netinet/in.h>

double ping_host(struct in_addr target_ip, int timeout_ms, unsigned short id);

#endif
