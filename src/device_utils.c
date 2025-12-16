#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "device.h"

void get_device_hostname(struct in_addr ip, char *hostname, size_t len) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr = ip;

    // Résolution DNS inverse
    if (getnameinfo((struct sockaddr*)&sa, sizeof(sa), 
                    hostname, len, 
                    NULL, 0, 
                    NI_NAMEREQD) != 0) {
        // Si échec, on met "Inconnu"
        strncpy(hostname, "Inconnu", len);
    }
}

// Structure simple pour la base de données OUI
typedef struct {
    const char *prefix;
    const char *vendor;
} mac_oui_t;

// Liste très réduite de vendeurs courants
static const mac_oui_t OUI_DB[] = {
    // Raspberry Pi
    {"B8:27:EB", "Raspberry Pi"}, {"DC:A6:32", "Raspberry Pi"}, {"E4:5F:01", "Raspberry Pi"}, {"28:CD:C1", "Raspberry Pi"},
    // Apple (Liste partielle, ils en ont des centaines)
    {"D8:3C:69", "Apple"}, {"F0:18:98", "Apple"}, {"00:1C:B3", "Apple"}, {"00:23:12", "Apple"}, {"00:23:32", "Apple"},
    {"00:23:6C", "Apple"}, {"00:23:DF", "Apple"}, {"00:24:36", "Apple"}, {"00:25:00", "Apple"}, {"00:25:4B", "Apple"},
    {"00:25:BC", "Apple"}, {"00:26:08", "Apple"}, {"00:26:4A", "Apple"}, {"00:26:B0", "Apple"}, {"00:26:BB", "Apple"},
    {"28:CF:E9", "Apple"}, {"34:36:3B", "Apple"}, {"3C:07:54", "Apple"}, {"40:6C:8F", "Apple"}, {"44:2A:60", "Apple"},
    {"48:D7:05", "Apple"}, {"58:55:CA", "Apple"}, {"60:FA:CD", "Apple"}, {"64:76:BA", "Apple"}, {"7C:6D:F8", "Apple"},
    {"88:63:DF", "Apple"}, {"8C:85:90", "Apple"}, {"90:FD:61", "Apple"}, {"AC:BC:32", "Apple"}, {"B8:E8:56", "Apple"},
    {"BC:92:6B", "Apple"}, {"C8:BC:C8", "Apple"}, {"CC:20:E8", "Apple"}, {"D0:23:DB", "Apple"}, {"D8:A2:5E", "Apple"},
    {"E0:AC:CB", "Apple"}, {"F0:99:B6", "Apple"}, {"F4:5C:89", "Apple"}, {"FC:FC:48", "Apple"},
    // Samsung
    {"00:12:47", "Samsung"}, {"00:15:99", "Samsung"}, {"00:16:32", "Samsung"}, {"00:17:C9", "Samsung"}, {"00:18:AF", "Samsung"},
    {"00:19:15", "Samsung"}, {"00:1B:98", "Samsung"}, {"00:1C:43", "Samsung"}, {"00:1D:25", "Samsung"}, {"00:1E:E5", "Samsung"},
    {"00:1F:CC", "Samsung"}, {"00:21:19", "Samsung"}, {"00:23:D7", "Samsung"}, {"00:24:54", "Samsung"}, {"00:24:92", "Samsung"},
    {"00:25:38", "Samsung"}, {"00:26:37", "Samsung"}, {"08:37:3D", "Samsung"}, {"10:77:17", "Samsung"}, {"14:32:D1", "Samsung"},
    {"18:67:B0", "Samsung"}, {"20:D5:BF", "Samsung"}, {"24:F5:AA", "Samsung"}, {"28:98:7B", "Samsung"}, {"30:CD:A7", "Samsung"},
    {"34:14:5F", "Samsung"}, {"38:01:95", "Samsung"}, {"3C:62:00", "Samsung"}, {"44:4E:1A", "Samsung"}, {"48:44:F7", "Samsung"},
    {"4C:BC:A5", "Samsung"}, {"50:01:D9", "Samsung"}, {"50:B7:C3", "Samsung"}, {"50:F5:20", "Samsung"}, {"54:40:AD", "Samsung"},
    {"5C:A3:9D", "Samsung"}, {"60:6B:BD", "Samsung"}, {"60:A1:0A", "Samsung"}, {"64:B3:10", "Samsung"}, {"68:9E:19", "Samsung"},
    {"70:F9:27", "Samsung"}, {"78:40:E4", "Samsung"}, {"78:D6:F0", "Samsung"}, {"80:18:A7", "Samsung"}, {"84:25:DB", "Samsung"},
    {"84:38:38", "Samsung"}, {"84:55:A5", "Samsung"}, {"88:32:9B", "Samsung"}, {"8C:71:F8", "Samsung"}, {"8C:77:12", "Samsung"},
    {"90:18:7C", "Samsung"}, {"94:51:03", "Samsung"}, {"94:63:D1", "Samsung"}, {"94:B1:0A", "Samsung"}, {"98:1D:FA", "Samsung"},
    {"9C:02:98", "Samsung"}, {"A0:0B:BA", "Samsung"}, {"A0:21:95", "Samsung"}, {"A0:82:1F", "Samsung"}, {"A4:EB:D3", "Samsung"},
    {"A8:06:00", "Samsung"}, {"AC:36:13", "Samsung"}, {"AC:5F:3E", "Samsung"}, {"B0:C4:E7", "Samsung"}, {"B0:EC:71", "Samsung"},
    {"B4:07:F9", "Samsung"}, {"B4:62:93", "Samsung"}, {"B8:5E:7B", "Samsung"}, {"BC:20:A4", "Samsung"}, {"BC:8C:CD", "Samsung"},
    {"C0:BD:D1", "Samsung"}, {"C4:57:6E", "Samsung"}, {"C8:19:F7", "Samsung"}, {"CC:07:AB", "Samsung"}, {"CC:3A:61", "Samsung"},
    {"D0:17:6A", "Samsung"}, {"D0:C1:B1", "Samsung"}, {"D4:87:D8", "Samsung"}, {"D4:E8:B2", "Samsung"}, {"D8:57:EF", "Samsung"},
    {"DC:71:44", "Samsung"}, {"E4:7C:F9", "Samsung"}, {"E4:B0:21", "Samsung"}, {"E8:03:9A", "Samsung"}, {"E8:11:32", "Samsung"},
    {"EC:1F:72", "Samsung"}, {"F0:25:B7", "Samsung"}, {"F0:6B:CA", "Samsung"}, {"F0:E2:30", "Samsung"}, {"F4:09:D8", "Samsung"},
    {"F4:7B:5E", "Samsung"}, {"F4:D9:FB", "Samsung"}, {"F8:04:2E", "Samsung"}, {"F8:3F:51", "Samsung"}, {"F8:D0:BD", "Samsung"},
    {"FC:A1:3E", "Samsung"}, {"FC:C7:34", "Samsung"},
    // Intel (Cartes Wifi PC portables)
    {"00:02:B3", "Intel"}, {"00:03:47", "Intel"}, {"00:04:23", "Intel"}, {"00:0C:F1", "Intel"}, {"00:0E:35", "Intel"},
    {"00:12:F0", "Intel"}, {"00:13:02", "Intel"}, {"00:13:20", "Intel"}, {"00:13:E8", "Intel"}, {"00:15:00", "Intel"},
    {"00:16:6F", "Intel"}, {"00:16:EA", "Intel"}, {"00:18:DE", "Intel"}, {"00:19:D1", "Intel"}, {"00:1B:77", "Intel"},
    {"00:1C:C0", "Intel"}, {"00:1D:E0", "Intel"}, {"00:1E:64", "Intel"}, {"00:1E:65", "Intel"}, {"00:1F:3B", "Intel"},
    {"00:1F:3C", "Intel"}, {"00:21:5C", "Intel"}, {"00:21:5D", "Intel"}, {"00:21:6A", "Intel"}, {"00:21:6B", "Intel"},
    {"00:22:FB", "Intel"}, {"00:23:14", "Intel"}, {"00:23:15", "Intel"}, {"00:24:D6", "Intel"}, {"00:24:D7", "Intel"},
    {"00:27:0E", "Intel"}, {"00:27:10", "Intel"}, {"24:77:03", "Intel"}, {"34:02:86", "Intel"}, {"40:25:C2", "Intel"},
    {"48:45:20", "Intel"}, {"48:51:B7", "Intel"}, {"58:91:CF", "Intel"}, {"58:94:6B", "Intel"}, {"60:57:18", "Intel"},
    {"60:67:20", "Intel"}, {"68:05:CA", "Intel"}, {"68:5D:43", "Intel"}, {"78:92:9C", "Intel"}, {"7C:5C:F8", "Intel"},
    {"80:86:F2", "Intel"}, {"8C:70:5A", "Intel"}, {"90:2E:16", "Intel"}, {"98:4F:EE", "Intel"}, {"A0:88:B4", "Intel"},
    {"A0:A8:CD", "Intel"}, {"AC:72:89", "Intel"}, {"AC:FD:CE", "Intel"}, {"B4:6D:83", "Intel"}, {"C0:CB:38", "Intel"},
    {"C4:85:08", "Intel"}, {"CC:3D:82", "Intel"}, {"D4:D2:52", "Intel"}, {"D8:3B:BF", "Intel"}, {"D8:F8:83", "Intel"},
    {"DC:53:60", "Intel"}, {"DF:8F:78", "Intel"}, {"E0:94:67", "Intel"}, {"F0:94:67", "Intel"}, {"F8:59:71", "Intel"},
    {"FC:F8:AE", "Intel"},
    // Microsoft
    {"00:03:FF", "Microsoft"}, {"00:12:5A", "Microsoft"}, {"00:15:5D", "Microsoft"}, {"00:17:FA", "Microsoft"}, {"00:1D:D8", "Microsoft"},
    {"00:22:48", "Microsoft"}, {"00:25:AE", "Microsoft"}, {"00:50:F2", "Microsoft"}, {"28:18:78", "Microsoft"}, {"30:59:B7", "Microsoft"},
    {"48:86:E8", "Microsoft"}, {"50:1A:C5", "Microsoft"}, {"58:81:57", "Microsoft"}, {"60:45:BD", "Microsoft"}, {"7C:1E:52", "Microsoft"},
    {"98:5F:D3", "Microsoft"}, {"A4:77:33", "Microsoft"}, {"B4:AE:2B", "Microsoft"}, {"C0:33:5E", "Microsoft"}, {"D4:CF:F9", "Microsoft"},
    {"DC:98:40", "Microsoft"}, {"E0:06:E6", "Microsoft"},
    // Xiaomi
    {"00:9E:C8", "Xiaomi"}, {"0C:1D:AF", "Xiaomi"}, {"10:2A:B3", "Xiaomi"}, {"14:F6:5A", "Xiaomi"}, {"18:59:36", "Xiaomi"},
    {"20:82:C0", "Xiaomi"}, {"28:6C:07", "Xiaomi"}, {"28:D0:EA", "Xiaomi"}, {"34:80:B3", "Xiaomi"}, {"34:CE:00", "Xiaomi"},
    {"38:76:CA", "Xiaomi"}, {"3C:BD:3E", "Xiaomi"}, {"40:31:3C", "Xiaomi"}, {"4C:49:E3", "Xiaomi"}, {"50:64:2B", "Xiaomi"},
    {"50:8F:4C", "Xiaomi"}, {"50:EC:50", "Xiaomi"}, {"54:48:E6", "Xiaomi"}, {"58:44:98", "Xiaomi"}, {"5C:51:4F", "Xiaomi"},
    {"64:09:80", "Xiaomi"}, {"64:CC:2E", "Xiaomi"}, {"68:DF:DD", "Xiaomi"}, {"74:23:44", "Xiaomi"}, {"74:51:BA", "Xiaomi"},
    {"78:11:DC", "Xiaomi"}, {"7C:1D:D9", "Xiaomi"}, {"80:AD:16", "Xiaomi"}, {"84:2A:FD", "Xiaomi"}, {"8C:BE:BE", "Xiaomi"},
    {"94:87:E0", "Xiaomi"}, {"98:FA:E3", "Xiaomi"}, {"9C:99:A0", "Xiaomi"}, {"A0:86:C6", "Xiaomi"}, {"A4:50:46", "Xiaomi"},
    {"AC:C1:EE", "Xiaomi"}, {"AC:F7:F3", "Xiaomi"}, {"B0:E2:35", "Xiaomi"}, {"C4:0B:CB", "Xiaomi"}, {"C4:6B:B4", "Xiaomi"},
    {"C4:9F:4C", "Xiaomi"}, {"D4:97:0B", "Xiaomi"}, {"D8:63:75", "Xiaomi"}, {"DC:2A:14", "Xiaomi"}, {"E4:46:DA", "Xiaomi"},
    {"EC:D0:9F", "Xiaomi"}, {"F0:B4:29", "Xiaomi"}, {"F4:8B:32", "Xiaomi"}, {"F8:A4:5F", "Xiaomi"}, {"FC:64:B9", "Xiaomi"},
    // Huawei
    {"00:18:82", "Huawei"}, {"00:1E:10", "Huawei"}, {"00:25:68", "Huawei"}, {"00:46:4B", "Huawei"}, {"00:66:4B", "Huawei"},
    {"00:E0:FC", "Huawei"}, {"04:25:C5", "Huawei"}, {"04:C0:6F", "Huawei"}, {"04:F9:38", "Huawei"}, {"08:19:A6", "Huawei"},
    {"08:E8:4F", "Huawei"}, {"0C:37:DC", "Huawei"}, {"0C:96:BF", "Huawei"}, {"10:1B:54", "Huawei"}, {"10:47:80", "Huawei"},
    {"10:51:72", "Huawei"}, {"10:C6:1F", "Huawei"}, {"14:B9:68", "Huawei"}, {"1C:1D:67", "Huawei"}, {"1C:8E:5C", "Huawei"},
    {"20:08:ED", "Huawei"}, {"20:0B:C7", "Huawei"}, {"20:2B:C1", "Huawei"}, {"20:F4:1B", "Huawei"}, {"24:09:95", "Huawei"},
    {"24:4C:07", "Huawei"}, {"24:69:A5", "Huawei"}, {"24:7F:20", "Huawei"}, {"24:9E:AB", "Huawei"}, {"24:DB:AC", "Huawei"},
    {"28:31:52", "Huawei"}, {"28:3C:E4", "Huawei"}, {"28:41:C6", "Huawei"}, {"28:5F:DB", "Huawei"}, {"28:6E:D4", "Huawei"},
    {"2C:97:B1", "Huawei"}, {"30:87:30", "Huawei"}, {"30:D1:7E", "Huawei"}, {"30:F3:35", "Huawei"}, {"34:00:A3", "Huawei"},
    {"34:2E:B4", "Huawei"}, {"34:6B:D3", "Huawei"}, {"34:A2:A2", "Huawei"}, {"34:CD:BE", "Huawei"}, {"38:F2:3E", "Huawei"},
    {"3C:47:11", "Huawei"}, {"3C:F8:08", "Huawei"}, {"40:4D:8E", "Huawei"}, {"40:CB:A8", "Huawei"}, {"44:55:B1", "Huawei"},
    {"44:6E:E5", "Huawei"}, {"48:2C:A0", "Huawei"}, {"48:46:FB", "Huawei"}, {"48:62:76", "Huawei"}, {"48:7B:6B", "Huawei"},
    {"48:AD:08", "Huawei"}, {"48:DB:50", "Huawei"}, {"4C:1F:CC", "Huawei"}, {"4C:54:99", "Huawei"}, {"4C:8B:EF", "Huawei"},
    {"4C:B1:6C", "Huawei"}, {"50:04:B8", "Huawei"}, {"50:9F:27", "Huawei"}, {"50:A7:2B", "Huawei"}, {"54:39:DF", "Huawei"},
    {"54:89:98", "Huawei"}, {"54:A5:1B", "Huawei"}, {"58:1F:28", "Huawei"}, {"58:2A:F7", "Huawei"}, {"58:7F:66", "Huawei"},
    {"5C:4C:A9", "Huawei"}, {"5C:7D:5E", "Huawei"}, {"5C:F9:38", "Huawei"}, {"60:08:10", "Huawei"}, {"60:DE:44", "Huawei"},
    {"60:E7:01", "Huawei"}, {"64:16:93", "Huawei"}, {"64:3E:8C", "Huawei"}, {"68:8F:84", "Huawei"}, {"68:A0:3E", "Huawei"},
    {"70:54:F5", "Huawei"}, {"70:72:0D", "Huawei"}, {"70:7B:E8", "Huawei"}, {"70:8A:09", "Huawei"}, {"70:A8:E3", "Huawei"},
    {"74:88:2A", "Huawei"}, {"78:1D:BA", "Huawei"}, {"78:6A:89", "Huawei"}, {"78:D7:52", "Huawei"}, {"78:F5:FD", "Huawei"},
    {"7C:60:97", "Huawei"}, {"80:38:BC", "Huawei"}, {"80:71:7A", "Huawei"}, {"80:B6:86", "Huawei"}, {"80:D0:9B", "Huawei"},
    {"80:FB:06", "Huawei"}, {"84:A8:E4", "Huawei"}, {"84:AD:58", "Huawei"}, {"84:DB:AC", "Huawei"}, {"88:53:95", "Huawei"},
    {"88:86:03", "Huawei"}, {"88:CE:FA", "Huawei"}, {"88:E3:AB", "Huawei"}, {"90:17:AC", "Huawei"}, {"90:4E:91", "Huawei"},
    {"94:04:9C", "Huawei"}, {"94:77:2B", "Huawei"}, {"98:35:B8", "Huawei"}, {"9C:28:40", "Huawei"}, {"9C:37:F4", "Huawei"},
    {"9C:C1:72", "Huawei"}, {"A0:8C:FD", "Huawei"}, {"A4:99:47", "Huawei"}, {"A4:CA:A0", "Huawei"}, {"AC:4E:91", "Huawei"},
    {"AC:85:3D", "Huawei"}, {"AC:E2:15", "Huawei"}, {"AC:E8:7B", "Huawei"}, {"B0:5B:67", "Huawei"}, {"B4:15:13", "Huawei"},
    {"B4:30:52", "Huawei"}, {"BC:25:E0", "Huawei"}, {"BC:3A:EA", "Huawei"}, {"BC:76:70", "Huawei"}, {"C0:70:09", "Huawei"},
    {"C4:05:28", "Huawei"}, {"C4:07:2F", "Huawei"}, {"C4:34:6B", "Huawei"}, {"C4:86:E9", "Huawei"}, {"C4:9F:89", "Huawei"},
    {"C4:B8:B4", "Huawei"}, {"C8:D1:5E", "Huawei"}, {"CC:53:B5", "Huawei"}, {"CC:96:A0", "Huawei"}, {"CC:CC:81", "Huawei"},
    {"D0:2D:B3", "Huawei"}, {"D0:7A:B5", "Huawei"}, {"D4:40:F0", "Huawei"}, {"D4:6A:A8", "Huawei"}, {"D4:6B:A6", "Huawei"},
    {"D4:B1:10", "Huawei"}, {"D8:49:0B", "Huawei"}, {"DC:D2:FC", "Huawei"}, {"E0:24:7F", "Huawei"}, {"E0:97:96", "Huawei"},
    {"E4:68:A3", "Huawei"}, {"E4:A7:C5", "Huawei"}, {"E8:08:8B", "Huawei"}, {"E8:CD:2D", "Huawei"}, {"EC:23:3D", "Huawei"},
    {"EC:8C:A2", "Huawei"}, {"EC:CB:30", "Huawei"}, {"F0:2F:A7", "Huawei"}, {"F4:55:9C", "Huawei"}, {"F4:63:1F", "Huawei"},
    {"F4:9F:F3", "Huawei"}, {"F4:C7:14", "Huawei"}, {"F4:DC:F9", "Huawei"}, {"F8:01:13", "Huawei"}, {"F8:3D:FF", "Huawei"},
    {"F8:4A:BF", "Huawei"}, {"F8:98:B9", "Huawei"}, {"F8:E8:12", "Huawei"}, {"FC:48:EF", "Huawei"}, {"FC:E3:3C", "Huawei"},
    // Autres
    {"00:11:32", "Synology"}, {"00:11:22", "Cisco"}, {"00:0C:29", "VMware"}, {"00:50:56", "VMware"}, {"00:1C:42", "Parallels"},
    {"08:00:27", "VirtualBox"}, {"AC:DE:48", "Private"}, {"F0:9E:63", "Espressif"}, {"24:6F:28", "Espressif"}, {"5C:CF:7F", "Espressif"},
    {"60:01:94", "Espressif"}, {"A4:CF:12", "Espressif"}, {"D0:73:D5", "Lifx"}, {"00:17:88", "Philips Hue"}, {"00:24:E4", "Withings"},
    {"70:EE:50", "Netatmo"}, {"00:04:20", "Slim Devices"}, {"00:1A:11", "Google"}, {"F4:F5:D8", "Google"}, {"A4:77:33", "Google"},
    {"00:1A:73", "Gemtek"}, {"00:1D:AA", "Sonos"}, {"00:0E:58", "Sonos"}, {"48:A9:D2", "Sonos"}, {"94:9F:3E", "Sonos"},
    {"78:28:CA", "Sonos"}, {"B8:E9:37", "Sonos"}, {"5C:AA:FD", "Sonos"},
    {NULL, NULL}
};

void get_mac_vendor(const char *mac_addr, char *vendor, size_t len) {
    if (strlen(mac_addr) < 8) {
        strncpy(vendor, "", len);
        return;
    }

    // Convertir le début de la MAC en majuscules pour la comparaison
    char prefix[9];
    strncpy(prefix, mac_addr, 8);
    prefix[8] = '\0';
    for(int i=0; i<8; i++) prefix[i] = toupper(prefix[i]);

    for (int i = 0; OUI_DB[i].prefix != NULL; i++) {
        if (strncmp(prefix, OUI_DB[i].prefix, 8) == 0) {
            strncpy(vendor, OUI_DB[i].vendor, len);
            return;
        }
    }
    
    strncpy(vendor, "", len);
}

void get_device_mac(const char *ip_str, char *mac_addr, size_t len) {
    FILE *fp;
    char line[256];
    char ip[INET_ADDRSTRLEN];
    char hw_type[16], flags[16], mac[18], mask[16], dev[16];

    // Valeur par défaut
    strncpy(mac_addr, "??:??:??:??:??:??", len);

    fp = fopen("/proc/net/arp", "r");
    if (fp == NULL) {
        return;
    }

    // Ignorer la première ligne (en-têtes)
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        // Format: IP address       HW type     Flags       HW address            Mask     Device
        //         192.168.1.1      0x1         0x2         00:11:22:33:44:55     *        eth0
        if (sscanf(line, "%s %s %s %s %s %s", ip, hw_type, flags, mac, mask, dev) == 6) {
            if (strcmp(ip, ip_str) == 0) {
                strncpy(mac_addr, mac, len);
                break;
            }
        }
    }

    fclose(fp);
}
