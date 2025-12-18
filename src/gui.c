#include "gui.h"
#include "dns_spoofer.h"
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h> // for rand
#include <string.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define NODE_RADIUS 15
#define REPULSION_FORCE 2000.0f
#define SPRING_LENGTH 150.0f
#define SPRING_FORCE 0.05f

void run_gui(scan_context_t *ctx) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Network Map Live");
    SetTargetFPS(60);

    // Initialisation des positions aléatoires pour éviter que tout soit à 0,0
    for(int i=0; i<MAX_DEVICES; i++) {
        ctx->gui_props[i].x = GetScreenWidth()/2.0f + GetRandomValue(-100, 100);
        ctx->gui_props[i].y = GetScreenHeight()/2.0f + GetRandomValue(-100, 100);
    }

    Camera2D camera = { 0 };
    camera.zoom = 1.0f;
    camera.offset = (Vector2){ (float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f };
    camera.target = (Vector2){ 0, 0 };

    int gateway_idx = -1;

    float time_accum = 0.0f; // Pour animations
    
    // Search State
    char searchBuffer[64] = {0};
    int searchLen = 0;
    bool searchActive = false;

    // Spoofing Modal State
    int selectedNode = -1;
    int spoofInputFocus = 0; // 0:None, 1:Domain, 2:RedirectIP
    char spoofDomainBuf[256] = "google.com";
    char spoofIpBuf[64] = "192.168.1.50"; // Default to something example-like

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        time_accum += dt;
        
        // --- INPUT CONTROLS ---

        // Check for Modal Inputs first (Blocking other inputs if we want, or just standard)
        bool modalActive = (selectedNode != -1);
        
        // Pan avec Clic Gauche (si pas sur modal), Droit ou Molette
        // On bloque le Pan gauche si on est sur le modal pour éviter de bouger la map en cliquant sur les boutons
        if (!modalActive && (IsMouseButtonDown(MOUSE_LEFT_BUTTON) || IsMouseButtonDown(MOUSE_RIGHT_BUTTON) || IsMouseButtonDown(MOUSE_MIDDLE_BUTTON))) {
            Vector2 delta = GetMouseDelta();
            Vector2Add(camera.offset, delta);
            camera.offset.x += delta.x;
            camera.offset.y += delta.y;
            SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        } else if (modalActive && (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) || IsMouseButtonDown(MOUSE_MIDDLE_BUTTON))){
             // Allow Pan with Right/Middle even if modal active
            Vector2 delta = GetMouseDelta();
            Vector2Add(camera.offset, delta);
            camera.offset.x += delta.x;
            camera.offset.y += delta.y;
            SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        } else {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }
        

        // Zoom Mouse Wheel
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), camera);
            camera.offset = GetMousePosition();
            camera.target = mouseWorldPos;
            const float zoomSpeed = 0.1f;
            camera.zoom += (wheel * zoomSpeed);
            if (camera.zoom < 0.1f) camera.zoom = 0.1f;
            if (camera.zoom > 5.0f) camera.zoom = 5.0f;
        }

        // --- KEYBOARD INPUT (Search vs Spoof Modal) ---
        int key = GetCharPressed();
        while (key > 0) {
            if (modalActive) {
                // Handle Spoof Modal Inputs
                if (spoofInputFocus == 1 && strlen(spoofDomainBuf) < 255) {
                    int len = strlen(spoofDomainBuf);
                    spoofDomainBuf[len] = (char)key;
                    spoofDomainBuf[len+1] = '\0';
                }
                else if (spoofInputFocus == 2 && strlen(spoofIpBuf) < 63) {
                     int len = strlen(spoofIpBuf);
                     spoofIpBuf[len] = (char)key;
                     spoofIpBuf[len+1] = '\0';
                }
            } else {
                // Hanlde Search Input
                if ((key >= 32) && (key <= 125) && (searchLen < 63)) {
                    searchBuffer[searchLen] = (char)key;
                    searchBuffer[searchLen+1] = '\0';
                    searchLen++;
                    searchActive = true;
                }
            }
            key = GetCharPressed();
        }
        
        if (IsKeyPressed(KEY_BACKSPACE)) {
            if (modalActive) {
                if (spoofInputFocus == 1) {
                    int len = strlen(spoofDomainBuf);
                    if (len > 0) spoofDomainBuf[len-1] = '\0';
                }
                else if (spoofInputFocus == 2) {
                     int len = strlen(spoofIpBuf);
                     if (len > 0) spoofIpBuf[len-1] = '\0';
                }
            } else {
                if (searchLen > 0) {
                    searchLen--;
                    searchBuffer[searchLen] = '\0';
                    if (searchLen == 0) searchActive = false;
                }
            }
        }
        
        if (IsKeyPressed(KEY_TAB) && modalActive) {
            spoofInputFocus++;
            if (spoofInputFocus > 2) spoofInputFocus = 1;
        }

        // --- PHYSICS ---
        pthread_mutex_lock(&ctx->list_lock);
        int count = ctx->device_count;

        // Init/Reset Gateway
        if (gateway_idx == -1) {
             for (int i = 0; i < count; i++) {
                if (strcmp(ctx->devices[i].ip_str, ctx->gateway_ip) == 0) {
                    gateway_idx = i;
                    ctx->gui_props[i].x = 0; 
                    ctx->gui_props[i].y = 0;
                    ctx->gui_props[i].target_x = 0;
                    ctx->gui_props[i].target_y = 0;
                    break;
                }
             }
        }

        // Physics Loop (Repulsion + Attraction)
        for (int i = 0; i < count; i++) {
            Vector2 pos1 = { ctx->gui_props[i].x, ctx->gui_props[i].y };
            Vector2 force = { 0, 0 };

            // 1. Repulsion
            for (int j = 0; j < count; j++) {
                if (i == j) continue;
                Vector2 pos2 = { ctx->gui_props[j].x, ctx->gui_props[j].y };
                Vector2 diff = Vector2Subtract(pos1, pos2);
                float dist = Vector2Length(diff);
                if (dist < 20.0f) dist = 20.0f;

                float f = REPULSION_FORCE / (dist * dist);
                force = Vector2Add(force, Vector2Scale(Vector2Normalize(diff), f));
            }
            // 2. Attraction vers Gateway
            if (gateway_idx != -1 && i != gateway_idx) {
                Vector2 gatewayPos = { ctx->gui_props[gateway_idx].x, ctx->gui_props[gateway_idx].y };
                
                // Distance idéale basée sur le ping (RTT)
                // Base 100px + 4px par ms. Cap à 1000px max pour éviter que les appareils lents soient trop loin.
                float rtt = ctx->devices[i].rtt_ms;
                float dynamicLength = 120.0f + (rtt * 4.0f); 
                if (dynamicLength > 800.0f) dynamicLength = 800.0f;
                
                Vector2 diff = Vector2Subtract(gatewayPos, pos1);
                float dist = Vector2Length(diff);
                float displacement = dist - dynamicLength;
                
                // Force de rappel (Spring)
                force = Vector2Add(force, Vector2Scale(Vector2Normalize(diff), displacement * SPRING_FORCE));
            }

            if (dt > 0.05f) dt = 0.05f; // Cap à 20 FPS min pour éviter explosion physique

            // Apply
            float moveX = force.x * dt * 40.0f;
            float moveY = force.y * dt * 40.0f;
            
            // Verif NaN
            if (isnan(moveX) || isinf(moveX)) moveX = 0;
            if (isnan(moveY) || isinf(moveY)) moveY = 0;

            ctx->gui_props[i].target_x += moveX;
            ctx->gui_props[i].target_y += moveY;
            
            // Safety Check Limit
            if (ctx->gui_props[i].target_x > 20000) ctx->gui_props[i].target_x = 20000;
            if (ctx->gui_props[i].target_x < -20000) ctx->gui_props[i].target_x = -20000;
            if (ctx->gui_props[i].target_y > 20000) ctx->gui_props[i].target_y = 20000;
            if (ctx->gui_props[i].target_y < -20000) ctx->gui_props[i].target_y = -20000;
        }

        // Integration (Lerp vers target)
        for (int i = 0; i < count; i++) {
            if (i == gateway_idx) {
                ctx->gui_props[i].x = 0; ctx->gui_props[i].y = 0;
                continue;
            }
            ctx->gui_props[i].x = Lerp(ctx->gui_props[i].x, ctx->gui_props[i].target_x, 0.1f);
            ctx->gui_props[i].y = Lerp(ctx->gui_props[i].y, ctx->gui_props[i].target_y, 0.1f);
            ctx->gui_props[i].target_x = ctx->gui_props[i].x; 
            ctx->gui_props[i].target_y = ctx->gui_props[i].y;
        }
        pthread_mutex_unlock(&ctx->list_lock);

        // --- DRAWING ---
        BeginDrawing();
        BeginDrawing();
            // Background
            ClearBackground(GetColor(0x0f172aFF)); 
            DrawCircleGradient(GetScreenWidth()/2, GetScreenHeight()/2, GetScreenWidth(), GetColor(0x1e293bFF), GetColor(0x020617FF));

            BeginMode2D(camera);
                
                // Mouse position in World
                Vector2 worldMouse = GetScreenToWorld2D(GetMousePosition(), camera);
                int hoveredNode = -1;

                // Grille
                const int gridSize = 100;
                const int gridLines = 100;
                Color gridColor = Fade(SKYBLUE, 0.05f);
                for(int i=-gridLines; i<=gridLines; i++) {
                    DrawLine(i*gridSize, -gridLines*gridSize, i*gridSize, gridLines*gridSize, gridColor);
                    DrawLine(-gridLines*gridSize, i*gridSize, gridLines*gridSize, i*gridSize, gridColor);
                }

                pthread_mutex_lock(&ctx->list_lock);
                
                // 1. Dessin des Liens
                if (gateway_idx != -1) {
                    Vector2 p1 = { ctx->gui_props[gateway_idx].x, ctx->gui_props[gateway_idx].y };
                    for (int i = 0; i < ctx->device_count; i++) {
                        if (i == gateway_idx) continue;
                        Vector2 p2 = { ctx->gui_props[i].x, ctx->gui_props[i].y };
                        
                        Color linkCol = Fade(GRAY, 0.3f);
                        if (ctx->devices[i].rtt_ms < 5) linkCol = Fade(GREEN, 0.3f);
                        else if (ctx->devices[i].rtt_ms < 100) linkCol = Fade(SKYBLUE, 0.3f);
                        else linkCol = Fade(RED, 0.3f);

                        DrawLineEx(p1, p2, 2.0f, linkCol);
                    }
                }

                // 2. Dessin des Noeuds
                for (int i = 0; i < ctx->device_count; i++) {
                    Vector2 pos = { ctx->gui_props[i].x, ctx->gui_props[i].y };
                    bool isGateway = (i == gateway_idx);
                    
                    // Check Hover and Click
                    if (CheckCollisionPointCircle(worldMouse, pos, NODE_RADIUS + 5)) {
                        hoveredNode = i;
                        // Select Node on Click (DISABLE IF GATEWAY)
                        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !modalActive && !isGateway) {
                             selectedNode = i;
                             // Load existing values if already spoofed
                             if (ctx->devices[i].is_being_spoofed) {
                                 strcpy(spoofDomainBuf, ctx->devices[i].spoof_domain);
                                 strcpy(spoofIpBuf, ctx->devices[i].spoof_redirect_ip);
                             }
                        }
                    }

                    Color coreColor = SKYBLUE;
                    Color glowColor = Fade(BLUE, 0.3f);
                    float pulse = sinf(time_accum * 3.0f + i) * 2.0f;
                    float radiusVar = 0;

                    if (isGateway) {
                        coreColor = GOLD;
                        glowColor = Fade(ORANGE, 0.4f);

                        DrawCircleV(pos, NODE_RADIUS * 2.5f + pulse, Fade(glowColor, 0.2f)); 
                    } else {
                        if (ctx->devices[i].rtt_ms < 5) { coreColor = GREEN; glowColor = Fade(LIME, 0.3f); }
                        else if (ctx->devices[i].rtt_ms > 100) { coreColor = RED; glowColor = Fade(MAROON, 0.3f); }
                        
                        // Spoof Color Override
                        if (ctx->devices[i].is_being_spoofed) {
                            coreColor = MAGENTA;
                            glowColor = Fade(PURPLE, 0.5f);
                        }
                    }

                    // Inactive (Lost) Override
                    if (!ctx->devices[i].active) {
                        coreColor = Fade(GRAY, 0.2f);
                        glowColor = Fade(DARKGRAY, 0.1f);
                    }

                    // SEARCH FILTER
                    int match = 1;
                    if (searchActive) {
                        // Case insensitive search logic could be better, but strstr is okay for now
                        // Let's make it simpler: Check if substring exists
                        // To allow better search we often convert both to lower, but let's stick to direct match or simple improvement
                        // TODO: strcasestr is strict C, might need _GNU_SOURCE. Let's iterate.
                        
                        // Simple custom case-insensitive check or just assuming exact/partial match
                        // Or we duplicate buffers to lowercase
                        
                        // For simplicity, let's assume Case Sensitive for now OR use a helper if we had one.
                        // Actually, users hate case sensitive. Let's do a poor man's contains loop or just use strcasestr if available.
                        // Linux Usually has strcasestr with _GNU_SOURCE but we are standard.
                        // Let's copy to stack temp buffers.
                        
                        // BUT: We don't want to alloc in draw loop.
                        // Let's stick to strstr for now (Case Sensitive) to be safe on compile, 
                        // unless user complains or we add a helper.
                        // ACTUALLY, let's just use strcasestr, usually available in <string.h> with _GNU_SOURCE defined.
                        // If not, we fall back. Let's try to be smart.
                        
                        // Fallback: Check IP (always numbers/dots) + Hostname
                        if (strstr(ctx->devices[i].ip_str, searchBuffer) == NULL && 
                            strstr(ctx->devices[i].hostname, searchBuffer) == NULL) {
                            match = 0;
                        }
                    }

                    if (!match && !isGateway) {
                        coreColor = Fade(coreColor, 0.05f);
                        glowColor = Fade(glowColor, 0.02f);
                    } else {
                         if (i == hoveredNode) {
                            coreColor = WHITE;
                            radiusVar = 5.0f;
                            glowColor = Fade(WHITE, 0.5f);
                        }
                    }

                    DrawCircleV(pos, NODE_RADIUS + 8.0f + pulse + radiusVar, glowColor);
                    DrawCircleV(pos, NODE_RADIUS + radiusVar, coreColor);
                    DrawCircleLines(pos.x, pos.y, NODE_RADIUS + radiusVar, match ? WHITE : Fade(WHITE, 0.1f));
                }

                // 3. Labels (seulement si pas trop zoomé out, ou si hover)
                // OU juste Label IP simplifié
                 for (int i = 0; i < ctx->device_count; i++) {
                     Vector2 pos = { ctx->gui_props[i].x, ctx->gui_props[i].y };
                     
                     // Si c'est la gateway ou si c'est le noeud survolé, on affiche toujours
                     if (i == gateway_idx) {
                         DrawText("GATEWAY", pos.x - 30, pos.y - 35, 10, GOLD);
                     } else if (camera.zoom > 0.5f || i == hoveredNode) {
                         const char *label = ctx->devices[i].ip_str;
                         // Si on a un nom valide et différent de l'IP, on l'utilise
                         if (strlen(ctx->devices[i].hostname) > 0 && 
                             strcmp(ctx->devices[i].hostname, "Inconnu") != 0 && 
                             strcmp(ctx->devices[i].hostname, ctx->devices[i].ip_str) != 0) {
                             label = ctx->devices[i].hostname;
                         }
                         
                         Color labelCol = RAYWHITE;
                         if (!ctx->devices[i].active) labelCol = Fade(GRAY, 0.5f);
                         DrawText(label, pos.x - 30, pos.y + 20, 10, labelCol);
                     }
                 }

                // Tooltip Details (On le dessine en Overlay 'World' ou plutot Screen space après EndMode2D ?) 
                // Pour que ça suive le noeud mais reste lisible, on peut le faire ici ou dehors.
                // Faisons le ici pour utiliser les coords world rapidement converties ou drawing relative.
                
            EndMode2D(); // Fin world space

            // Dessin du Tooltip en Screen Space (au dessus de tout)
            if (hoveredNode != -1) {
                Vector2 pos = { ctx->gui_props[hoveredNode].x, ctx->gui_props[hoveredNode].y };
                Vector2 screenPos = GetWorldToScreen2D(pos, camera);
                
                int tipW = 220;
                int tipH = 90;
                int tipX = screenPos.x + 20;
                int tipY = screenPos.y - 20;
                
                // Garder dans l'écran
                if (tipX + tipW > GetScreenWidth()) tipX = screenPos.x - tipW - 20;
                if (tipY + tipH > GetScreenHeight()) tipY = screenPos.y - tipH - 20;

                DrawRectangle(tipX, tipY, tipW, tipH, Fade(BLACK, 0.9f));
                DrawRectangleLines(tipX, tipY, tipW, tipH, SKYBLUE);
                
                DrawText(ctx->devices[hoveredNode].ip_str, tipX + 10, tipY + 10, 20, GREEN);
                DrawText(ctx->devices[hoveredNode].hostname, tipX + 10, tipY + 35, 10, LIGHTGRAY);
                DrawText(TextFormat("MAC: %s", ctx->devices[hoveredNode].mac_addr), tipX + 10, tipY + 50, 10, GRAY);
                DrawText(TextFormat("Ping: %.2f ms", ctx->devices[hoveredNode].rtt_ms), tipX + 10, tipY + 65, 10, ctx->devices[hoveredNode].rtt_ms < 50 ? GREEN : RED);
            }
            
            pthread_mutex_unlock(&ctx->list_lock);

            // --- HUD OVERLAY ---
            DrawRectangle(10, 10, 260, 110, Fade(BLACK, 0.8f));
            DrawRectangleLines(10, 10, 260, 110, GetColor(0x334155FF)); 
            
            DrawText("NETWORK MAP LIVE", 25, 20, 20, SKYBLUE);
            DrawLine(25, 45, 250, 45, GRAY);
            
            char statStr[64];
            snprintf(statStr, 64, "Devices: %d", count);
            DrawText(statStr, 25, 55, 20, WHITE);

            DrawText("[L/R-Click]: MOVE | [Hover]: INFO", 25, 85, 10, LIGHTGRAY);
            
            DrawFPS(GetScreenWidth() - 80, 10);

            // SEARCH BAR UI
            int searchBoxW = 300;
            int searchBoxX = GetScreenWidth()/2 - searchBoxW/2;
            int searchBoxY = 20;
            DrawRectangle(searchBoxX, searchBoxY, searchBoxW, 40, Fade(BLACK, 0.8f));
            
            Color bordColor = searchActive ? GREEN : GRAY;
            DrawRectangleLines(searchBoxX, searchBoxY, searchBoxW, 40, bordColor);
            
            if (searchLen > 0) {
                 DrawText(searchBuffer, searchBoxX + 10, searchBoxY + 10, 20, WHITE);
            } else {
                 DrawText("Search IP/Name...", searchBoxX + 10, searchBoxY + 10, 20, Fade(GRAY, 0.5f));
            }


            // SPOOFING MODAL
             if (selectedNode != -1 && selectedNode < count) {
                 int mW = 400;
                 int mH = 300;
                 int mX = GetScreenWidth()/2 - mW/2;
                 int mY = GetScreenHeight()/2 - mH/2;
                 
                 DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.5f)); // Dim background
                 DrawRectangle(mX, mY, mW, mH, GetColor(0x1e293bFF));
                 DrawRectangleLines(mX, mY, mW, mH, SKYBLUE);
                 
                 DrawText("DNS Spoofing & ARP Poison", mX + 20, mY + 20, 20, WHITE);
                 
                 DrawText(TextFormat("Target: %s", ctx->devices[selectedNode].ip_str), mX + 20, mY + 50, 10, GRAY);
                 
                 // Domain Input
                 DrawText("Domain to Spoof:", mX + 20, mY + 80, 10, LIGHTGRAY);
                 DrawRectangle(mX + 20, mY + 95, 360, 30, (spoofInputFocus == 1) ? GetColor(0x334155FF) : BLACK);
                 DrawRectangleLines(mX + 20, mY + 95, 360, 30, (spoofInputFocus == 1) ? GREEN : GRAY);
                 DrawText(spoofDomainBuf, mX + 25, mY + 102, 20, WHITE);
                 
                 if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){mX + 20, mY + 95, 360, 30})) {
                     if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) spoofInputFocus = 1;
                 }

                 // Redirect IP Input
                 DrawText("Redirect IP (Attacker/Server):", mX + 20, mY + 140, 10, LIGHTGRAY);
                 DrawRectangle(mX + 20, mY + 155, 360, 30, (spoofInputFocus == 2) ? GetColor(0x334155FF) : BLACK);
                 DrawRectangleLines(mX + 20, mY + 155, 360, 30, (spoofInputFocus == 2) ? GREEN : GRAY);
                 DrawText(spoofIpBuf, mX + 25, mY + 162, 20, WHITE);

                 if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){mX + 20, mY + 155, 360, 30})) {
                     if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) spoofInputFocus = 2;
                 }
                 
                 // Actions
                 bool isSpoofing = ctx->devices[selectedNode].is_being_spoofed;
                 
                 // Start/Update Button
                 Color btnCol = isSpoofing ? ORANGE : GREEN;
                 DrawRectangle(mX + 20, mY + 210, 170, 40, Fade(btnCol, 0.8f));
                 DrawText(isSpoofing ? "UPDATE ATTACK" : "START ATTACK", mX + 45, mY + 220, 10, WHITE);
                 
                 if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){mX + 20, mY + 210, 170, 40})) {
                     if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                         // Stop previous if active to restart with new params or just start
                         if (isSpoofing) stop_spoofing(&ctx->devices[selectedNode]);
                         
                         char gwMac[18] = {0};
                         get_device_mac(ctx->gateway_ip, gwMac, 18);
                         
                         start_spoofing(&ctx->devices[selectedNode], ctx->gateway_ip, gwMac, spoofDomainBuf, spoofIpBuf);
                     }
                 }
                 
                 // Stop Button
                 if (isSpoofing) {
                     DrawRectangle(mX + 210, mY + 210, 170, 40, Fade(RED, 0.8f));
                     DrawText("STOP ATTACK", mX + 255, mY + 220, 10, WHITE);
                     if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){mX + 210, mY + 210, 170, 40})) {
                         if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                             stop_spoofing(&ctx->devices[selectedNode]);
                         }
                     }
                 }
                 
                 // Close Button (Small X top right)
                 DrawText("X", mX + mW - 25, mY + 10, 20, RED);
                 if (CheckCollisionPointCircle(GetMousePosition(), (Vector2){mX + mW - 15, mY + 20}, 15)) {
                     if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                         selectedNode = -1;
                         spoofInputFocus = 0;
                     }
                 }
             }

        EndDrawing();
    }

    ctx->active = false;
    CloseWindow();
}
