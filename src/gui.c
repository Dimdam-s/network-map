#include "gui.h"
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
    camera.offset = (Vector2){ (float)SCREEN_WIDTH / 2.0f, (float)SCREEN_HEIGHT / 2.0f };
    camera.target = (Vector2){ 0, 0 };

    int gateway_idx = -1;
    float time_accum = 0.0f; // Pour animations

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        time_accum += dt;
        
        // --- INPUT CONTROLS ---
        
        // Pan avec Clic Gauche, Droit ou Molette
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) || IsMouseButtonDown(MOUSE_RIGHT_BUTTON) || IsMouseButtonDown(MOUSE_MIDDLE_BUTTON)) {
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
            // Background
            ClearBackground(GetColor(0x0f172aFF)); 
            DrawCircleGradient(SCREEN_WIDTH/2, SCREEN_HEIGHT/2, SCREEN_WIDTH, GetColor(0x1e293bFF), GetColor(0x020617FF));

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
                    
                    // Check Hover
                    if (CheckCollisionPointCircle(worldMouse, pos, NODE_RADIUS + 5)) {
                        hoveredNode = i;
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
                    }

                    if (i == hoveredNode) {
                        coreColor = WHITE;
                        radiusVar = 5.0f;
                        glowColor = Fade(WHITE, 0.5f);
                    }

                    DrawCircleV(pos, NODE_RADIUS + 8.0f + pulse + radiusVar, glowColor);
                    DrawCircleV(pos, NODE_RADIUS + radiusVar, coreColor);
                    DrawCircleLines(pos.x, pos.y, NODE_RADIUS + radiusVar, WHITE);
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
                         DrawText(label, pos.x - 30, pos.y + 20, 10, RAYWHITE);
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
                if (tipX + tipW > SCREEN_WIDTH) tipX = screenPos.x - tipW - 20;
                if (tipY + tipH > SCREEN_HEIGHT) tipY = screenPos.y - tipH - 20;

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
            
            DrawFPS(SCREEN_WIDTH - 80, 10);

        EndDrawing();
    }

    ctx->active = false;
    CloseWindow();
}
