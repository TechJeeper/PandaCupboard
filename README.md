# PandaCupboard

Custom **PandaTouch / K-Touch** firmware that shows a Bambu Lab farm dashboard, styled like Bambu Studio’s device list.

Built from the PaxxTouch board support (ESP32-S3, LVGL, GT911). The Snapmaker remote-mirror UI is replaced with LAN Bambu monitoring.

## What you get

- Compact table: **Device Name**, **Task Name**, **Device Status**
- About **8 printer rows** on screen; scroll for more
- Store up to 24 printers
- Sort by **Device Status** (default) or Device Name
- Active jobs with **higher completion** stay at the top
- Setup with **IP address + LAN access code**
- **SSDP discovery** for Bambu printers on the LAN
- Status over **MQTT/TLS port 8883**, username `bblp`, password = access code

## Printer requirements

On each Bambu:

1. Enable **LAN Mode**
2. Note the **Access Code**
3. Enable **Developer Mode** if you want control later (status reads work with LAN Mode)

The panel and printers must be on the same LAN.

## Flash

```bash
cd C:\Projects\PandaCupboard
pio run -e pandacupboard-arduino-3x -t upload --upload-port COM5
```

First boot: **Gear → WiFi Setup**, then **Add Printer** (or Discover on LAN and enter the access code).

This image replaces stock Panda Touch firmware. Keep a BTT `.bin` + `.img` if you want to restore official firmware.
