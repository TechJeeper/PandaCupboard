# PandaFarm Web Flasher

Browser-based firmware installer for **BTT K-Touch** and **PandaTouch**, powered by [esptool-js](https://github.com/espressif/esptool-js) and the [Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API).

Flashes **PandaFarm** — Bambu Lab farm dashboard.

## Live URL

```
https://pandacupboard.techjeeper.com/flasher/
```

## GitHub Pages setup

1. Push this repository to GitHub
2. **Settings → Pages → Build and deployment**
   - Source: **Deploy from a branch**
   - Branch: `main`
   - Folder: **`/docs`**
3. Wait ~1 minute for the site to publish

## Creating a flashable release

```powershell
.\scripts\package-firmware.ps1 -Version "0.2.0"
```

Upload the files from `dist/firmware/` as assets on a GitHub Release tagged `v0.2.0`:

| Asset | Flash offset |
|-------|----------------|
| `pandafarm-bootloader.bin` | `0x0` |
| `pandafarm-partitions.bin` | `0x8000` |
| `pandafarm-boot_app0.bin` | `0xE000` (optional but recommended) |
| `pandafarm-firmware.bin` | `0x10000` |

The web flasher loads these from `docs/flasher/firmware/` on GitHub Pages.

## Manual flashing (no bundled bins)

1. Open the flasher page
2. Select **Manual files**
3. Build: `pio run -e pandacupboard-arduino-3x`
4. Pick bins from `.pio/build/pandacupboard-arduino-3x/`

## After flashing

1. Power on the panel and open **gear → WiFi Setup**
2. **Gear → Printers** — IP + LAN access code
3. Tap a printer on the farm list for pause / resume / stop

## Browser support

| Browser | Supported |
|---------|-----------|
| Chrome (desktop) | Yes |
| Edge (desktop) | Yes |
| Firefox | No (no Web Serial) |
| Safari | No |
| Mobile | No |

## Troubleshooting

**Port not listed / connect fails**

- Use a USB-C cable that supports data (not charge-only)
- Install [CH340 driver](https://www.wch.cn/downloads/CH341SER_EXE.html) on Windows if needed
- Reboot the device with the slider switch on the back, then connect again

**Flash fails mid-way (`status 201` or seq failed)**

- Choose **Fresh Install**
- Use a short **data-capable USB-C cable**; avoid hubs if possible
- K-Touch / PandaTouch use a **CH340 USB chip** — the flasher uses 115200 baud and uncompressed writes (~2–3 min)

## Security note

The flasher only writes data you explicitly select (from this repo’s bundled bins or local files). Review release assets before publishing.
