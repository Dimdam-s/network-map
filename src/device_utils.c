#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <unistd.h>
#include "device.h"

typedef struct {
    char prefix[9];
    char *vendor;
} mac_entry_t;

static mac_entry_t *oui_db = NULL;
static int oui_count = 0;
static int oui_capacity = 0;

void trim_vendor(char *str) {
    size_t len = strlen(str);
    while(len > 0 && (isspace((unsigned char)str[len-1]))) {
        str[len-1] = '\0';
        len--;
    }
}

void download_oui_db() {
    printf("INFO: Checking OUI database...\n");
    system("mkdir -p bin");
    
    int ret = system("wget -nc -O bin/oui.txt http://standards-oui.ieee.org/oui/oui.txt >/dev/null 2>&1");
    if (ret != 0) {
        FILE *f = fopen("bin/oui.txt", "r");
        if (!f) {
            printf("WARN: Failed to download OUI database. Check internet connection.\n");
        } else {
            fclose(f);
        }
    }
}

void init_oui_db() {
    download_oui_db();

    FILE *f = fopen("bin/oui.txt", "r");
    if (!f) {
        printf("WARN: bin/oui.txt not found. Vendor lookup will be limited.\n");
        return;
    }
    
    oui_capacity = 35000;
    oui_db = malloc(sizeof(mac_entry_t) * oui_capacity);
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "(hex)")) {
            char prefix_raw[16];
            char vendor_raw[256];
            
            if (sscanf(line, "%s (hex) %[^\n]", prefix_raw, vendor_raw) == 2) {
                char clean_prefix[7];
                int p = 0;
                for(int i=0; prefix_raw[i] && p<6; i++) {
                    if (isxdigit(prefix_raw[i])) {
                        clean_prefix[p++] = prefix_raw[i];
                    }
                }
                clean_prefix[6] = '\0';
                
                if (oui_count >= oui_capacity) {
                    oui_capacity *= 2;
                    oui_db = realloc(oui_db, sizeof(mac_entry_t) * oui_capacity);
                }
                
                strcpy(oui_db[oui_count].prefix, clean_prefix);
                trim_vendor(vendor_raw);
                oui_db[oui_count].vendor = strdup(vendor_raw);
                oui_count++;
            }
        }
    }
    fclose(f);
    printf("INFO: Loaded %d vendor entries from OUI database.\n", oui_count);
}

void cleanup_oui_db() {
    if (oui_db) {
        for(int i=0; i<oui_count; i++) {
            free(oui_db[i].vendor);
        }
        free(oui_db);
    }
}

const char* lookup_oui(const char *mac_addr) {
    if (!oui_db) return NULL;
    
    char search_prefix[7];
    int p = 0;
    for(int i=0; mac_addr[i] && p<6; i++) {
        if (isxdigit(mac_addr[i])) {
            search_prefix[p++] = toupper(mac_addr[i]);
        }
    }
    search_prefix[6] = '\0';
    
    if (strlen(search_prefix) < 6) return NULL;

    for(int i=0; i<oui_count; i++) {
        if (strcmp(oui_db[i].prefix, search_prefix) == 0) {
            return oui_db[i].vendor;
        }
    }
    return NULL;
}

void get_mac_vendor(const char *mac_addr, char *vendor, size_t len) {
    const char *v = lookup_oui(mac_addr);
    if (v) {
        strncpy(vendor, v, len);
        vendor[len-1] = '\0';
    } else {
        strncpy(vendor, "", len);
    }
}

int run_cmd_capture(const char *cmd, char *output, size_t len) {
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) return 0;
    
    if (fgets(output, len, fp) != NULL) {
        output[strcspn(output, "\n")] = 0;
        pclose(fp);
        return 1;
    }
    pclose(fp);
    return 0;
}

void get_device_hostname(struct in_addr ip, char *hostname, size_t len) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr = ip;
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip, ip_str, INET_ADDRSTRLEN);

    int found = 0;

    if (getnameinfo((struct sockaddr*)&sa, sizeof(sa), 
                    hostname, len, 
                    NULL, 0, 
                    NI_NAMEREQD) == 0) {
        found = 1;
    }

    if (!found) {
        char cmd[256];
        char output[1024];
        snprintf(cmd, sizeof(cmd), "nmblookup -A %s 2>/dev/null", ip_str);
        
        FILE *fp = popen(cmd, "r");
        if (fp) {
            while (fgets(output, sizeof(output), fp)) {
                if (strstr(output, "<00>") && !strstr(output, "<GROUP>")) {
                    char name_buf[64];
                    if (sscanf(output, "%63s", name_buf) == 1) {
                        strncpy(hostname, name_buf, len);
                        found = 1;
                        break;
                    }
                }
            }
            pclose(fp);
        }
    }

    if (!found) {
        strncpy(hostname, "Inconnu", len);
    }
}

void get_device_mac(const char *ip_str, char *mac_addr, size_t len) {
    FILE *fp;
    char line[256];
    char ip[INET_ADDRSTRLEN];
    char hw_type[16], flags[16], mac[18], mask[16], dev[16];

    strncpy(mac_addr, "??:??:??:??:??:??", len);

    fp = fopen("/proc/net/arp", "r");
    if (fp == NULL) {
        return;
    }

    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%s %s %s %s %s %s", ip, hw_type, flags, mac, mask, dev) == 6) {
            if (strcmp(ip, ip_str) == 0) {
                strncpy(mac_addr, mac, len);
                break;
            }
        }
    }
    fclose(fp);
}
