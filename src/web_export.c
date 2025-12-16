#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "web_export.h"

void export_to_html(const char *filename, device_t *devices, int count, const char *gateway_ip) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        perror("Erreur lors de la création du fichier HTML");
        return;
    }

    fprintf(fp, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(fp, "  <title>Network Map</title>\n");
    fprintf(fp, "  <meta charset=\"UTF-8\">\n");
    fprintf(fp, "  <script type=\"text/javascript\" src=\"https://unpkg.com/vis-network/standalone/umd/vis-network.min.js\"></script>\n");
    fprintf(fp, "  <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0/css/all.min.css\">\n");
    fprintf(fp, "  <style type=\"text/css\">\n");
    fprintf(fp, "    body { margin: 0; padding: 0; background: #111827; color: #e5e7eb; font-family: 'Inter', 'Segoe UI', sans-serif; overflow: hidden; }\n");
    fprintf(fp, "    #mynetwork { width: 100vw; height: 100vh; background: radial-gradient(circle at center, #1f2937 0%%, #111827 100%%); }\n");
    fprintf(fp, "    .overlay { position: absolute; top: 20px; left: 20px; background: rgba(31, 41, 55, 0.7); backdrop-filter: blur(12px); padding: 24px; border-radius: 16px; border: 1px solid rgba(255,255,255,0.1); box-shadow: 0 8px 32px rgba(0,0,0,0.4); max-width: 320px; z-index: 10; }\n");
    fprintf(fp, "    h1 { margin: 0 0 16px 0; font-size: 1.5rem; font-weight: 700; background: linear-gradient(45deg, #60a5fa, #a78bfa); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }\n");
    fprintf(fp, "    .stats { margin-bottom: 20px; font-size: 0.9rem; color: #9ca3af; }\n");
    fprintf(fp, "    .legend-item { display: flex; align-items: center; margin-bottom: 8px; font-size: 0.85rem; color: #d1d5db; }\n");
    fprintf(fp, "    .dot { width: 10px; height: 10px; border-radius: 50%%; margin-right: 12px; box-shadow: 0 0 8px currentColor; }\n");
    fprintf(fp, "    .footer { margin-top: 20px; font-size: 0.75rem; color: #6b7280; border-top: 1px solid rgba(255,255,255,0.1); padding-top: 12px; }\n");
    fprintf(fp, "  </style>\n");
    fprintf(fp, "</head>\n<body>\n");
    
    fprintf(fp, "<div class=\"overlay\">\n");
    fprintf(fp, "  <h1>Network Map</h1>\n");
    fprintf(fp, "  <div class=\"stats\">%d appareils détectés<br>Gateway: %s</div>\n", count, gateway_ip);
    fprintf(fp, "  <div class=\"legend\">\n");
    fprintf(fp, "    <div class=\"legend-item\"><div class=\"dot\" style=\"background: #fbbf24; color: #fbbf24;\"></div>Gateway / Routeur</div>\n");
    fprintf(fp, "    <div class=\"legend-item\"><div class=\"dot\" style=\"background: #34d399; color: #34d399;\"></div>Excellent (< 5ms)</div>\n");
    fprintf(fp, "    <div class=\"legend-item\"><div class=\"dot\" style=\"background: #60a5fa; color: #60a5fa;\"></div>Bon (< 50ms)</div>\n");
    fprintf(fp, "    <div class=\"legend-item\"><div class=\"dot\" style=\"background: #f87171; color: #f87171;\"></div>Lent (> 50ms)</div>\n");
    fprintf(fp, "  </div>\n");
    fprintf(fp, "  <div class=\"footer\">Généré par Network Map</div>\n");
    fprintf(fp, "</div>\n");
    
    fprintf(fp, "<div id=\"mynetwork\"></div>\n");
    
    fprintf(fp, "<script type=\"text/javascript\">\n");
    
    // Génération des noeuds (Nodes)
    fprintf(fp, "  var rawNodes = [\n");
    
    int gateway_id = -1;

    // Trouver l'ID de la gateway
    for (int i = 0; i < count; i++) {
        if (strcmp(devices[i].ip_str, gateway_ip) == 0) {
            gateway_id = i;
            break;
        }
    }

    // Si la gateway n'est pas dans la liste, on l'ajoute
    if (gateway_id == -1) {
        fprintf(fp, "    {id: 9999, ip: '%s', mac: 'N/A', rtt: 0, label: '<b>Gateway</b>\\n<i>%s</i>', group: 'router', value: 40, font: { multi: 'html' }},\n", gateway_ip, gateway_ip);
        gateway_id = 9999;
    }

    for (int i = 0; i < count; i++) {
        char *group = "device";
        int value = 20;
        char label[512];
        
        // Construction du label plus propre
        if (strlen(devices[i].hostname) > 0 && strcmp(devices[i].hostname, "Inconnu") != 0) {
            snprintf(label, sizeof(label), "<b>%s</b>\\n<i>%s</i>", devices[i].hostname, devices[i].ip_str);
        } else {
            snprintf(label, sizeof(label), "<b>%s</b>", devices[i].ip_str);
        }

        if (strcmp(devices[i].ip_str, gateway_ip) == 0) {
            group = "router";
            value = 40;
        } else {
            if (devices[i].rtt_ms < 5.0) {
                group = "fast";
            } else if (devices[i].rtt_ms < 50.0) {
                group = "medium";
            } else {
                group = "slow";
            }
        }

        fprintf(fp, "    {id: %d, ip: '%s', mac: '%s', rtt: %.2f, label: \"%s\", group: '%s', value: %d, font: { multi: 'html' }},\n", 
                i, 
                devices[i].ip_str, devices[i].mac_addr, devices[i].rtt_ms,
                label,
                group,
                value);
    }
    fprintf(fp, "  ];\n\n");

    fprintf(fp, "  var nodes = new vis.DataSet(rawNodes.map(function(n) {\n");
    fprintf(fp, "    var title = document.createElement('div');\n");
    fprintf(fp, "    title.style.cssText = 'padding:8px; background:#1f2937; color:#fff; border-radius:4px; border:1px solid #374151; font-family: sans-serif;';\n");
    fprintf(fp, "    title.innerHTML = '<strong>IP:</strong> ' + n.ip + '<br><strong>MAC:</strong> ' + n.mac + '<br><strong>Latence:</strong> ' + n.rtt + ' ms';\n");
    fprintf(fp, "    return { id: n.id, label: n.label, group: n.group, value: n.value, title: title, font: n.font };\n");
    fprintf(fp, "  }));\n\n");

    // Génération des liens (Edges)
    fprintf(fp, "  var edges = new vis.DataSet([\n");
    for (int i = 0; i < count; i++) {
        if (i == gateway_id) continue; 

        int length = (int)(devices[i].rtt_ms * 8); 
        if (length < 100) length = 100;
        if (length > 500) length = 500;

        // Couleur du lien
        char *color = "#4b5563";
        if (devices[i].rtt_ms < 5.0) color = "#34d399";
        else if (devices[i].rtt_ms < 50.0) color = "#60a5fa";
        else color = "#f87171";

        fprintf(fp, "    {from: %d, to: %d, length: %d, color: { color: '%s', opacity: 0.4 }, width: 2, dashes: %s },\n", 
                gateway_id, i, length, color, (devices[i].rtt_ms > 100) ? "true" : "false");
    }
    fprintf(fp, "  ]);\n\n");

    // Configuration Vis.js
    fprintf(fp, "  var container = document.getElementById('mynetwork');\n");
    fprintf(fp, "  var data = { nodes: nodes, edges: edges };\n");
    fprintf(fp, "  var options = {\n");
    fprintf(fp, "    nodes: {\n");
    fprintf(fp, "      shape: 'dot',\n");
    fprintf(fp, "      size: 20,\n");
    fprintf(fp, "      borderWidth: 2,\n");
    fprintf(fp, "      shadow: { enabled: true, color: 'rgba(0,0,0,0.5)', size: 10, x: 5, y: 5 },\n");
    fprintf(fp, "      font: {\n");
    fprintf(fp, "        color: '#e5e7eb',\n");
    fprintf(fp, "        size: 14,\n");
    fprintf(fp, "        strokeWidth: 3,\n");
    fprintf(fp, "        strokeColor: '#111827',\n");
    fprintf(fp, "        multi: 'html',\n");
    fprintf(fp, "        bold: { color: '#e5e7eb', size: 14, mod: 'bold' },\n");
    fprintf(fp, "        ital: { color: '#9ca3af', size: 12, mod: '' }\n");
    fprintf(fp, "      }\n");
    fprintf(fp, "    },\n");
    fprintf(fp, "    groups: {\n");
    fprintf(fp, "      router: {\n");
    fprintf(fp, "        shape: 'icon',\n");
    fprintf(fp, "        icon: { face: '\"Font Awesome 6 Free\"', code: '\\uf233', weight: 'bold', size: 50, color: '#fbbf24' }\n");
    fprintf(fp, "      },\n");
    fprintf(fp, "      fast: {\n");
    fprintf(fp, "        shape: 'icon',\n");
    fprintf(fp, "        icon: { face: '\"Font Awesome 6 Free\"', code: '\\uf108', weight: 'bold', size: 30, color: '#34d399' }\n");
    fprintf(fp, "      },\n");
    fprintf(fp, "      medium: {\n");
    fprintf(fp, "        shape: 'icon',\n");
    fprintf(fp, "        icon: { face: '\"Font Awesome 6 Free\"', code: '\\uf109', weight: 'bold', size: 30, color: '#60a5fa' }\n");
    fprintf(fp, "      },\n");
    fprintf(fp, "      slow: {\n");
    fprintf(fp, "        shape: 'icon',\n");
    fprintf(fp, "        icon: { face: '\"Font Awesome 6 Free\"', code: '\\uf12a', weight: 'bold', size: 30, color: '#f87171' }\n");
    fprintf(fp, "      }\n");
    fprintf(fp, "    },\n");
    fprintf(fp, "    edges: {\n");
    fprintf(fp, "      smooth: { type: 'continuous', roundness: 0.5 }\n");
    fprintf(fp, "    },\n");
    fprintf(fp, "    physics: {\n");
    fprintf(fp, "      stabilization: false,\n");
    fprintf(fp, "      barnesHut: {\n");
    fprintf(fp, "        gravitationalConstant: -4000,\n");
    fprintf(fp, "        springConstant: 0.04,\n");
    fprintf(fp, "        springLength: 150,\n");
    fprintf(fp, "        damping: 0.09\n");
    fprintf(fp, "      }\n");
    fprintf(fp, "    },\n");
    fprintf(fp, "    interaction: {\n");
    fprintf(fp, "      hover: true,\n");
    fprintf(fp, "      tooltipDelay: 200\n");
    fprintf(fp, "    }\n");
    fprintf(fp, "  };\n");
    fprintf(fp, "  var network = new vis.Network(container, data, options);\n");
    fprintf(fp, "</script>\n</body>\n</html>\n");

    fclose(fp);
    
    // Correction des permissions si lancé avec sudo
    char *sudo_uid = getenv("SUDO_UID");
    char *sudo_gid = getenv("SUDO_GID");
    if (sudo_uid && sudo_gid) {
        uid_t uid = atoi(sudo_uid);
        gid_t gid = atoi(sudo_gid);
        if (chown(filename, uid, gid) != 0) {
            perror("Attention: Impossible de changer le propriétaire du fichier HTML");
        }
    }
    
    printf("Fichier HTML généré : %s\n", filename);
}
