#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "icmp_scanner.h"

// Fonction de calcul du checksum (standard pour ICMP/IP)
unsigned short calculate_checksum(unsigned short *paddress, int len) {
    int nleft = len;
    int sum = 0;
    unsigned short *w = paddress;
    unsigned short answer = 0;

    while (nleft > 1) {
        sum += *w++;
        nleft -= 2;
    }

    if (nleft == 1) {
        *(unsigned char *)&answer = *(unsigned char *)w;
        sum += answer;
    }

    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return answer;
}

double ping_host(struct in_addr target_ip, int timeout_ms, unsigned short id) {
    int sockfd;
    struct icmp icmp_packet;
    struct sockaddr_in dest_addr;
    struct timeval tv;
    struct timeval start, end;
    
    // Création du socket brut (Nécessite les droits root)
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        return -1.0; 
    }

    // Configuration du timeout de réception
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        close(sockfd);
        return -1.0;
    }

    // Préparation de l'adresse de destination
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr = target_ip;

    // Construction du paquet ICMP Echo Request (Type 8)
    memset(&icmp_packet, 0, sizeof(icmp_packet));
    icmp_packet.icmp_type = ICMP_ECHO;
    icmp_packet.icmp_code = 0;
    icmp_packet.icmp_id = id; 
    icmp_packet.icmp_seq = 1;
    icmp_packet.icmp_cksum = calculate_checksum((unsigned short *)&icmp_packet, sizeof(icmp_packet));

    // Envoi du paquet
    gettimeofday(&start, NULL);
    if (sendto(sockfd, &icmp_packet, sizeof(icmp_packet), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) <= 0) {
        close(sockfd);
        return -1.0;
    }

    // Attente de la réponse
    char buffer[1024];
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    
    while (1) {
        int bytes = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&src_addr, &addr_len);
        if (bytes <= 0) {
            close(sockfd);
            return -1.0; // Timeout ou erreur
        }

        gettimeofday(&end, NULL);

        // Analyse du paquet IP pour trouver l'en-tête ICMP
        struct ip *ip_header = (struct ip *)buffer;
        int ip_header_len = ip_header->ip_hl * 4;
        struct icmp *icmp_reply = (struct icmp *)(buffer + ip_header_len);

        // Vérifier si c'est une réponse Echo Reply (Type 0) et si l'ID correspond NOTRE ID
        if (icmp_reply->icmp_type == ICMP_ECHOREPLY && icmp_reply->icmp_id == id) {
            // Vérifier que ça vient bien de la cible
            if (src_addr.sin_addr.s_addr == target_ip.s_addr) {
                close(sockfd);
                
                // Calcul du RTT en ms
                double rtt = (end.tv_sec - start.tv_sec) * 1000.0;
                rtt += (end.tv_usec - start.tv_usec) / 1000.0;
                return rtt;
            }
        }
    }

    close(sockfd);
    return -1.0;
}
