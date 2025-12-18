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

## 🚀 Installing TeensyMaestro CE on your TeensyMaestro v2

There are two main ways to get TeensyMaestro CE onto your **Teensy 4.1**:

1. **Recommended:** Flash the latest prebuilt firmware from GitHub Releases.  
2. **Advanced:** Build the firmware yourself and flash your own binary.

For almost all users, **option 1 is strongly recommended**.

### ✅ Option 1 – Use a prebuilt firmware (recommended)

1. Go to the TeensyMaestro CE Releases page:  
   `https://github.com/tnxqso/TeensyMaestro/releases`
2. Download the **latest release** for TeensyMaestro CE (HEX file or the “compiled binary” bundle).
3. Install the official **Teensy Loader** application from PJRC (Windows/macOS/Linux).
4. Connect your TeensyMaestro v2 (with Teensy 4.1) to your computer via USB.
5. In Teensy Loader:
   - Open the downloaded `.hex` file.
   - Press the **PROGRAM** button on the Teensy (if required).
   - Click **Program** and wait until the flashing process finishes.
6. Power-cycle or reset the TeensyMaestro, and the new CE firmware should start.

This is the easiest and safest way to run TeensyMaestro CE.  
If you only want to use the firmware and not modify the code, **do not bother setting up a build environment.**

### 🧠 Option 2 – Build the firmware yourself (advanced)

Only consider this if you are already comfortable with:

- C++ development for embedded targets
- PlatformIO, or equivalent build systems
- Debugging toolchain and environment issues on your own

The environment used and tested by the CE maintainers is roughly:

- **Platform:** `teensy`
- **Framework:** `arduino`
- **Board:** `teensy41`

From a typical PlatformIO build:

- Platform: Teensy (5.0.0) → Teensy 4.1  
  - Hardware: IMXRT1062 @ 600 MHz, 512 KB RAM, 7.75 MB Flash  
  - Debug: jlink (external)

Tooling packages:

- `framework-arduinoteensy @ 1.159.0 (1.59)`
- `tool-teensy @ 1.159.0 (1.59)`
- `toolchain-gccarmnoneeabi-teensy @ 1.110301.0 (11.3.1)`

Dependency graph (libraries typically pulled in):

- AceTime @ 4.1.0  
- T4_PowerButton @ 0.0.4+sha.71de116  
- Adafruit MCP23017 Arduino Library @ 2.3.2  
- KickSort @ 1.1.0  
- FlexRigTeensy-TNXQSO @ 0.1.0  
- AceCommon @ 1.6.2  
- AceSorting @ 1.0.0  
- ST7796_t3 @ 1.0.0-local  
- SPI @ 1.0  
- NativeEthernet  
- FNET @ 0.1.3  
- SD @ 2.0.0  
- SdFat @ 2.1.2  
- MTP_Teensy @ 1.0.0  
- Encoder @ 1.4.4  
- EEPROM @ 2.0  
- Wire @ 1.0  
- XPT2046_Touchscreen  
- Adafruit BusIO @ 1.17.4  

Typical memory usage at the time of writing (example build):

- `FLASH:` code ≈ 533,912 bytes, data ≈ 182,784 bytes, headers ≈ 8,776 bytes  
  free for files ≈ 7,400,992 bytes
- `RAM1:` variables ≈ 73,956 bytes, code ≈ 325,096 bytes, padding ≈ 2,584 bytes  
  free for local variables ≈ 122,652 bytes
- `RAM2:` variables ≈ 75,168 bytes  
  free for `malloc/new` ≈ 449,120 bytes

These numbers are provided **for orientation only**; they will change as the code evolves.

#### Important build-support disclaimer

- Setting up a working build environment for TeensyMaestro CE is **non-trivial**.
- The TM CE team maintainers **do not provide support** for:
  - PlatformIO installation problems  
  - Toolchain issues  
  - Local library version conflicts  
  - General “how do I build this?” questions

If you are an experienced developer, this should be enough information to get going.  
If you are not, you are much better off **sticking to the prebuilt firmware from GitHub Releases**.

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

### Original Project & Documentation

- **Original TeensyMaestro firmware (KD0RC)**  
  Official upstream repository by the original author, Len Koppl.  
  https://github.com/KD0RC/Teensy-Maestro-for-Flex-6000-radios

- **TeensyMaestro User Manual (DOCX)**  
  Original user manual covering hardware operation, menus, and usage concepts.  
  https://github.com/KD0RC/Teensy-Maestro-for-Flex-6000-radios/raw/refs/heads/master/TeensyMaestro%20User%20Manual.docx

---

### Hardware Design & Schematics (v2)

- **TeensyMaestro Hardware Repository**  
  Schematics, PCB files, and hardware design sources for TeensyMaestro.  
  https://github.com/rimuadmin/TeensyMaestro-Hardware

- **TeensyMaestro Hardware Wiki**  
  Hardware build notes, revisions, BOM details, and assembly guidance.  
  https://github.com/rimuadmin/TeensyMaestro-Hardware/wiki

---

### Community & Discussion

- **FlexRadio Community – TeensyMaestro Thread**  
  Long-running discussion with user experiences, troubleshooting, and background.  
  https://community.flexradio.com/discussion/8023866/teensymaestro/

---

### Kits & Commercial Hardware

- **TeensyMaestro v2 Hardware Kit (G7UFO Radio Shop)**  
  Commercially available TeensyMaestro hardware kit based on the v2 design.  
  https://shop.g7ufo.radio/products/kit-teensy-maestro?variant=52210433687890

---

### Project-Specific References

- **License – Creative Commons BY-NC-SA 3.0**  
  https://creativecommons.org/licenses/by-nc-sa/3.0/

- **WinKey Emulation Overview**  
  [WK_README.md](WK_README.md)

- **WinKey Protocol Notes**  
  [WINKEYER_PROTOCOL_V2_EMULATION.md](WINKEYER_PROTOCOL_V2_EMULATION.md)

- **Changelog**  
  [CHANGELOG.md](CHANGELOG.md)

---

© 2015–2025 Len Koppl (KD0RC), Vincenzo Stefanazzi (IW7DMH), and contributors.  
Extended and maintained by **TNX QSO** — a non-profit community developing open-source utilities for ham radio enthusiasts.
