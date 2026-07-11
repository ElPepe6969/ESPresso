# ESPresso — Wake-on-LAN Dashboard via Tailscale

ESP32-S3 device that joins your [Tailscale](https://tailscale.com/) tailnet and serves a web dashboard to wake computers on your local network — from anywhere, behind CGNAT, no port forwarding.

**⚡ ESP + espresso = wakes you (and your devices) up.**

![architecture](https://img.shields.io/badge/ESP32--S3-8MB_PSRAM-blue) ![framework](https://img.shields.io/badge/ESP--IDF-v5.3-green) ![tailscale](https://img.shields.io/badge/Tailscale-ts2021-purple) ![license](https://img.shields.io/badge/license-MIT-orange)

```
 You, anywhere          ESP32 at home              Your sleeping PC
 ┌──────────┐   TLS     ┌──────────────┐   WoL     ┌──────────────┐
 │ Browser  │──────────▶│  ESPresso    │──────────▶│  PC wakes!   │
 │ on phone │  Tailscale │  dashboard   │  UDP bcast│  (Ethernet)  │
 └──────────┘           └──────────────┘           └──────────────┘
```

## Features

- **Web dashboard** — see all your devices, their online/offline state, MAC, IP
- **One-click wake** — tap a button to send a WoL magic packet
- **Add/remove hosts** — manage everything from the dashboard, persisted to flash
- **Online status** — ICMP ping monitor shows which devices are up
- **Tailscale-only access** — dashboard accessible ONLY via your tailnet, invisible to the internet
- **Ultra-low power** — <1W always-on, runs off any USB charger
- **Zero port forwarding** — works behind CGNAT, no router config needed

## Hardware

| Board | PSRAM | Price | Notes |
|-------|-------|-------|-------|
| **ESP32-S3-DevKitC-1** (official) | 8MB | $15–20 | Recommended, best support |
| **Seeed XIAO ESP32S3** | 8MB | $8–10 | Tiny form factor, low profile |
| **Waveshare ESP32-S3-Dev-Kit** | 8MB | $12–15 | N8R8 variant |
| ESP32-WROOM-32 (no PSRAM) | none | $3–5 | Works, limited to ~30 tailnet peers |

**Requires ESP32-S3 with PSRAM for tailnets >30 devices.** PSRAM holds the Tailscale network map.

Target PCs **must be connected via Ethernet** — WiFi NICs power off completely during sleep and cannot receive WoL magic packets.

## Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) (`pip install platformio`) — handles ESP-IDF toolchain automatically
- Tailscale account with auth key: https://login.tailscale.com/admin/settings/keys
- ESP32-S3 with PSRAM

### 1. Clone

```bash
git clone --recurse-submodules https://github.com/peterzahora/ESPresso.git
cd ESPresso
```

### 2. Configure credentials

Copy and edit the credentials file:

```bash
cp sdkconfig.credentials.example sdkconfig.credentials
```

Edit `sdkconfig.credentials` with your values:

```ini
CONFIG_ML_WIFI_SSID="YourWiFi"
CONFIG_ML_WIFI_PASSWORD="YourPassword"
CONFIG_ML_TAILSCALE_AUTH_KEY="tskey-auth-k..."
CONFIG_ML_DEVICE_NAME="espresso"
```

Or use menuconfig:

```bash
pio run -t menuconfig
# Navigate to: MicroLink V2 Configuration → Credentials
```

### 3. Build & Flash

```bash
pio run -t upload        # Build and flash
pio run -t monitor       # Open serial monitor
```

### 4. Open the Dashboard

Watch the serial output for the Tailscale IP:

```
I (15234) espresso: Tailscale connected! VPN IP: 100.92.45.67
I (15235) espresso: Ready! Dashboard at http://100.92.45.67
```

Open `http://100.92.45.67` from any device on your Tailscale network. Add your devices and start waking.

## Usage

### Wake a device

1. Open the dashboard at the ESP32's Tailscale IP
2. Click **⏻ Wake** on any host card
3. A WoL magic packet is broadcast to your LAN

### Add a host

1. Click **+ Add Host**
2. Enter name, MAC address (`AA:BB:CC:DD:EE:FF`), and LAN IP
3. The host appears on the dashboard immediately

### Online status

The ESP32 pings each host every 60 seconds:
- 🟢 Green dot = online (ping responded)
- 🔴 Red dot = offline
- 🟡 Yellow dot = unknown (never seen yet)

## REST API

All endpoints available at the ESP32's Tailscale IP:

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/hosts` | List all hosts with online state |
| `POST` | `/api/hosts` | Add new host `{name, mac, ip}` |
| `DELETE` | `/api/hosts?id=N` | Remove host by ID |
| `POST` | `/api/wake?id=N` | Send WoL magic packet |
| `GET` | `/api/status` | ESP32 health (uptime, heap, VPN IP) |

## Target PC Setup

### BIOS/UEFI
- Enable "Wake on LAN" / "PCIe Wake" in power management
- Disable "Deep Sleep" / "ErP Ready" to keep NIC powered

### Windows
- Device Manager → Network Adapter → Properties → Power Management
- ✅ "Allow this device to wake the computer"
- ✅ "Only allow a magic packet to wake the computer"

### Linux
```bash
sudo ethtool eth0 | grep Wake-on    # Check support
sudo ethtool -s eth0 wol g          # Enable magic packet
```

## Architecture

```
ESP32-S3
├── MicroLink v2     — Tailscale ts2021 (WireGuard, DERP, DISCO, STUN)
├── HTTP Server      — Serves dashboard SPA + REST API on port 80
├── ICMP Monitor     — Pings hosts every 60s, tracks online state
├── WoL Sender       — Builds and broadcasts magic packets
└── NVS Storage      — Persists host config (JSON in flash, survives reboot)
```

Built on [MicroLink v2](https://github.com/CamM2325/microlink) by Malone Technologies — production Tailscale client for ESP32.

## Troubleshooting

**Build fails with PSRAM errors:**
- Ensure your board has PSRAM. For ESP32 without PSRAM, add to `sdkconfig.defaults`:
  ```ini
  CONFIG_ML_H2_BUFFER_SIZE_KB=64
  CONFIG_ML_JSON_BUFFER_SIZE_KB=64
  CONFIG_ML_MAX_PEERS=8
  ```

**MicroLink init fails:**
- Verify Tailscale auth key is valid and reusable (not one-off)
- Check serial output for "Free heap" — should be >100KB

**Can't access dashboard:**
- Confirm you're on the same Tailscale network
- Check the ESP32 appears in your Tailscale admin console
- Try `tailscale ping esp32-espresso` from another tailnet device

**Wake doesn't work:**
- Target PC must be on Ethernet (not WiFi)
- WoL must be enabled in BIOS and OS
- Verify MAC address format: `AA:BB:CC:DD:EE:FF` (colons or hyphens)

## License

MIT — see [LICENSE](LICENSE).

## Credits

- [MicroLink v2](https://github.com/CamM2325/microlink) — Tailscale client for ESP32 (MIT)
- [ESP-IDF](https://github.com/espressif/esp-idf) — Espressif IoT Development Framework (Apache 2.0)
- [cJSON](https://github.com/DaveGamble/cJSON) — JSON parser (MIT)
