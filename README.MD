# TeensyMaestro — Community Edition (CE)

**Community-maintained open-source edition of TeensyMaestro for FlexRadio 6000, 8000, and Aurora radios.**  
Based on the original [TeensyMaestro by Len Koppl (KD0RC)](https://github.com/KD0RC/Teensy-Maestro-for-Flex-6000-radios).

---

## 🧩 Overview

TeensyMaestro — Community Edition (CE) is a non-commercial, open-source firmware for the **Teensy 4.1** microcontroller.  
It provides a rich front-panel control interface for FlexRadio transceivers, focusing on usability, network resilience, and expandability.

This firmware is intended for technically inclined users who are comfortable flashing microcontrollers and working with community-supported software.

---

## ⚙️ Hardware Support

- **Supported:** TeensyMaestro Hardware Version 2 (only)
  - Display: ST7796 (480×320) with XPT2046 touchscreen  
  - I/O expanders: MCP23017 (main) + optional MCP23008 muxes  

- **Not supported:** TeensyMaestro Hardware Version 1

If you are not sure which hardware revision you have, assume it is **not** supported until you have verified your PCB and parts against the Hardware v2 design.

---

## 🌐 Key Features

- **WinKeyer v2 Emulation (currently TCP-only)**  
  - Acts as a WinKey-compatible CW keyer over TCP.  
  - Targeted for use with logging/contest software such as **DXLog**, **N1MM**, and others that speak the WinKey v2 protocol.  
  - A clean-room subset implementation with documented behavior and known limitations; see  
    `WK_README.md` and `WINKEYER_PROTOCOL_V2_EMULATION.md` for details.

- **STUN/NAT Traversal and Fixed-IP Operation**  
  - Supports remote operation even when your TeensyMaestro is behind NAT or firewalls.  
  - Suitable for fixed-station and “remote shack” setups.

- **NTP Support**  
  - Periodically synchronizes the internal clock against NTP servers.  
  - Ensures accurate UTC display for logging and contest operation.

- **Enhanced User Interface**  
  - Clock widget and cleaner slice displays.  
  - Tweaked layouts and visual consistency for Hardware v2.

- **Hardware v2 Optimisations**  
  - Tuned specifically for the ST7796 display and MCP23017-based front panel found on Hardware v2.

---

## 🧪 WinKey Emulation – What to Expect

TeensyMaestro CE includes a **WinKey v2-style emulator** designed to behave like a real K1EL WinKeyer in typical contest-logging scenarios.

High-level behavior:

- Handshake and revision reporting compatible with standard WinKey v2 hosts.
- ASCII text from the logger is buffered and sent as CW, with character echo back to the host.
- Status bytes (BUSY/IDLE, flow control) are sent so the logger can track when messages start and finish.
- ESC / Clear Buffer is implemented to abort in-progress messages under realistic operating conditions.

For precise protocol details, supported commands, and which hosts we have actually tested, refer to:

- `WK_README.md` – user-focused overview of the WinKey emulation.
- `WINKEYER_PROTOCOL_V2_EMULATION.md` – technical notes about the protocol subset and behavior.

If your logger depends on obscure or rarely used WinKey features, **you should assume they may not be implemented** unless explicitly documented.

---

## 🆘 Getting Help and Reporting Problems

This is a **community edition** with **no formal support contract**. All work is done by volunteers, in spare time.

Before you open an issue or ask for help, please read and follow these points:

### 1. Do not contact the original author about CE-specific issues

- **Len Koppl (KD0RC)** is the original author of TeensyMaestro.
- TeensyMaestro **Community Edition (CE)** adds and changes features that Len did not design, ship, or support.
- If something is broken in **CE only**, do **not** ask Len about it. He is not responsible for CE bugs or behavior.

### 2. First compare with the official TeensyMaestro firmware

If you suspect a bug in CE:

1. Check whether the same behavior occurs with the **original, official TeensyMaestro firmware** from Len’s repository.
2. If the problem **also** exists there, it is not a CE-specific bug. In that case:
   - Search Len’s GitHub and documentation to see if it is a known limitation.
   - Only report it upstream if it clearly looks like a bug in the original firmware.
3. Only if the issue is unique to CE should you open an issue in the **CE repository**.

In short:  
**If it’s broken in both original and CE, it’s not our bug alone.  
If it’s only broken in CE, then it’s ours.**

### 3. How to open a useful GitHub issue

When you are sure the problem is CE-specific, use GitHub Issues in the usual way:

- Clearly state:
  - **Radio model and firmware**,  
  - **TeensyMaestro CE version / commit**,  
  - **PC OS, logger software, and version** (e.g. “Windows 11, DXLog 2.x, N1MM 1.x”, “macOS + SkookumLogger”).
- Describe:
  - What you **expected** to happen.
  - What **actually** happened.
  - Whether the issue is reproducible and **exact steps** to reproduce it.
- Attach:
  - Relevant logs (e.g. **SD trace logs**, serial logs, WinKey traces).  
  - Screenshots if helpful.

The clearer and more complete your report is, the more likely someone in the community will be able to reproduce and fix it.

### 4. No guaranteed response time

- There is **no dedicated support team** and no guaranteed response time.
- Issues will be handled by the CE contributors **as time permits**.
- Some issues may be closed if they are not reproducible, lack enough detail, or fall outside the design scope of TeensyMaestro CE.

If you need guaranteed support or commercial-level reliability, you should not rely solely on this firmware.

---

## 📜 License & Credits

This project is licensed under **Creative Commons Attribution–NonCommercial–ShareAlike 3.0 (CC BY-NC-SA 3.0)**.  
See [`LICENSE.md`](LICENSE.md) for full details.

### Credits

- **Original work:** Len Koppl (KD0RC)  
- **FlexRadio 6000 library:** Vincenzo Stefanazzi (IW7DMH)  
- **Community maintenance and extensions:** TNX QSO and contributors

---

## 📘 Links

- Original project: [Len Koppl — Teensy-Maestro-for-Flex-6000-radios](https://github.com/KD0RC/Teensy-Maestro-for-Flex-6000-radios)
- License: [CC BY-NC-SA 3.0](https://creativecommons.org/licenses/by-nc-sa/3.0/)
- WinKey emulation overview: `WK_README.md`
- WinKey protocol notes: `WINKEYER_PROTOCOL_V2_EMULATION.md`
- Changelog: [`CHANGELOG.md`](CHANGELOG.md)

---

© 2015–2025 Len Koppl (KD0RC), Vincenzo Stefanazzi (IW7DMH), and contributors.  
Extended and maintained by **TNX QSO** — a non-profit community developing open-source utilities for ham radio enthusiasts.
