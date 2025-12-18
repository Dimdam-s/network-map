# Network Map Live

A real-time, visual network scanner and mapper application built in C using [Raylib](https://www.raylib.com/).
It automatically scans your local network, discovers devices/hosts, and visualizes them in a dynamic, physics-based graph.

![Network Map Screenshot](https://github.com/Dimdam-s/network-map/blob/main/screenshot.png?raw=true)

## Features

- **Live Scanning**: Continuously scans the network for new devices.
- **Dynamic Visualization**: Physics-based layout that organizes nodes around a central gateway.
- **Latency Visualization**:
  - Distance from Gateway is proportional to Ping/RTT.
  - Link colors indicate connection quality (Green < 5ms, Blue < 100ms, Red > 100ms).
- **Name Resolution**:
  - DNS (Reverse Lookup).
  - NetBIOS (`nmblookup`) for Windows/Samba names.
  - OUI / MAC Vendor Lookup for unknown devices.
- **Interactive UI**:
  - Pan (Drag with Left/Right/Middle Mouse).
  - Zoom (Mouse Wheel).
  - Hover for details (IP, MAC, Hostname, Ping).

## Requirements

The application runs on Linux. It requires root privileges (`sudo`) to use Raw Sockets for ICMP scanning.

### Dependencies (Debian/Ubuntu)

You need `build-essential` for compiling and `samba-common-bin` for improved NetBIOS name resolution.
Raylib also requires several system libraries for windowing and graphics.

```bash
sudo apt update
sudo apt install build-essential git samba-common-bin \
    libasound2-dev libx11-dev libxrandr-dev libxi-dev \
    libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev \
    libxinerama-dev libwayland-dev libxkbcommon-dev
```

## Installation

1. **Clone the repository**:
   ```bash
   git clone https://github.com/yourusername/network-map.git
   cd network-map
   ```


2.  **Compile**:
    ```bash
    make
    ```



## Usage

Run the application with `sudo` (required for network scanning):

```bash
sudo ./bin/network-map
```

### Controls
- **Left/Right/Middle Click + Drag**: Move the map (Pan).
- **Mouse Wheel**: Zoom In/Out.
- **Hover Node**: View detailed device information.
- **ESC**: Exit the application.

## Troubleshooting

- **Permissions Error**: Ensure you run with `sudo`.
- **No Hostnames**: Some devices firewall ICMP or NetBIOS. The app tries its best with DNS, NetBIOS, and MAC Vendor lookup.
