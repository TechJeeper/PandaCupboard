# PandaFarm

Custom firmware for **BIGTREETECH K-Touch** and **PandaTouch** that shows a Bambu Lab farm dashboard, styled like Bambu Studio’s device list.

Created by [TechJeeper Designs](https://techjeeper.com/).

Built from the PaxxTouch board support (ESP32-S3, LVGL, GT911).

## Flash firmware

### Option A: Web flasher (recommended)

1. Open **[PandaFarm Web Flasher](https://pandafarm.techjeeper.com/flasher/)** in Chrome or Edge
2. Connect USB, click **Connect USB**, then **Flash PandaFarm**

See [docs/flasher/README.md](docs/flasher/README.md) for self-hosting on GitHub Pages.

### Option B: PlatformIO (developers)

```bash
pio run -e pandacupboard-arduino-3x -t upload --upload-port COM8
```

Package bins for a GitHub Release:

```powershell
.\scripts\package-firmware.ps1 -Version "0.2.2"
```

## What you get

- Compact table: **Device Name**, **Task Name**, **Device Status**
- About **8 printer rows** on screen; scroll for more
- Store up to 24 printers
- Pause / resume / stop / reprint from the printer detail screen
- Setup with **IP address + LAN access code** (from the printer’s WLAN settings)
- **SSDP discovery** for Bambu printers on the LAN
- Status over **MQTT/TLS port 8883**, username `bblp`, password = access code

## First run

1. Flash PandaFarm
2. **Gear → WiFi Setup** — connect to your LAN
3. **Gear → Printers** — add a printer (name, IP, access code)
4. Tap a printer on the farm list for job details and controls

The panel and printers must be on the same LAN. This image replaces stock Panda Touch firmware — keep a BTT `.bin` / `.img` if you want to restore official firmware.
