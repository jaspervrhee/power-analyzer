# Connecting to the Power Analyzer Pi

How to SSH into the Raspberry Pi 4 that runs `power_analyzer`.

## Login

| | |
|---|---|
| **User** | `jaspervanrhee` |
| **Password** | `12345678` |
| **Repo on Pi** | `~/projects/power-analyzer` |

## IP scheme (134.188.0.0/16)

| Device | IP | Notes |
|---|---|---|
| FLOG server | `134.188.254.132:17540` | logging target |
| **Pi `eth0`** | **`134.188.254.133`** | must be above `.130` or FLOG rejects the logs |
| Laptop ethernet | `134.188.254.140` | static, for the direct cable |

## Method 1 — over ethernet (direct cable, recommended)

The Pi's ethernet cable normally goes to the FLOG/Canon-LAN port. To connect:

1. **Unplug that cable from the FLOG/LAN and plug it directly into your laptop's
   ethernet port.**
2. Make sure your **laptop ethernet has the static IP** (one-time, admin PowerShell):
   ```powershell
   netsh interface ip set address name="Ethernet" static 134.188.254.140 255.255.0.0
   ```
   (or via Settings → Network → Ethernet → IP assignment → Manual → IPv4:
   `134.188.254.131`, mask `255.255.0.0`, no gateway/DNS)
3. SSH:
   ```powershell
   ssh jaspervanrhee@134.188.254.133
   ```

