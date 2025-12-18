# Network Map Live

A real-time, visual network scanner and mapper application. It visualizes your network topology, latency, and devices in a physics-based graph.

## Features

-   **Visual Mapping**: Hosts are displayed as nodes linked to a central gateway.
-   **Live Discovery**: Continuous scanning detects new devices and disconnections.
-   **Ethical Hacking Tools**:
    -   **DNS Spoofing**: Redirect traffic.
    -   **Distributed Stress Test**: Use multiple "Drone" machines to generate load on a target.

## Installation & Usage

### 🐧 Linux (Recommended)

Requires `sudo`.

1.  **Dependencies**:
    ```bash
    sudo apt install build-essential libraylib-dev
    # (See source/Makefile for full list if building from scratch)
    ```
2.  **Build & Run**:
    ```bash
    make
    sudo ./bin/network-map
    ```

### 🪟 Windows (Native - Drone Only)

You can use Windows machines as **Drones** (Stress Test Agents) without any VM or WSL.

1.  **Install MinGW/GCC**: Ensure `gcc` is available in your command prompt.
2.  **Compile**:
    -   Double-click `compile_drone.bat`.
    -   *Or run:* `gcc src/windows_drone.c -o drone.exe -lws2_32`

### 🪟 Windows (Full GUI)

To run the **Master GUI** on Windows, you **MUST** use [WSL 2](https://learn.microsoft.com/en-us/windows/wsl/install).

## Distributed Cluster Mode

### 1. External / Internet Deployment (WAN)

For drones outside your local network (e.g., Internet, different Subnet).

**On Master (Your Machine):**
1.  Forward UDP Port **7123** on your router to your machine.
2.  Run the GUI: `sudo ./bin/network-map`
3.  The Master passively listens for "Heartbeats".

**On Drones (Remote Machines):**
Run the drone executable with the `-m` (Master) argument. They will "Phone Home" every 5 seconds.

*Linux:*
```bash
sudo ./bin/network-map -d -m <YOUR_PUBLIC_IP>
```

*Windows:*
```cmd
drone.exe -m <YOUR_PUBLIC_IP>
```
*Note: Ensure "drone.exe" is allowed through Windows Firewall.*

### 2. Local LAN (Automatic Discovery)

If Drones are on the same WiFi/Ethernet, you don't need `-m`. The Master sends a Discovery Broadcast.
1.  Start Drones with just `./drone.exe` or `./network-map -d`.
2.  On Master GUI, click `[SCAN]` under "Drones: 0" to detect them.

### Launching Attack

1.  Observe "Drones: N" count in HUD (Increases automatically as Heartbeats arrive).
2.  Select a target node.
3.  Click **"LAUNCH CLUSTER ATTACK"**.

> [!WARNING]
> Use this software responsibly. Only perform tests on networks you own.
