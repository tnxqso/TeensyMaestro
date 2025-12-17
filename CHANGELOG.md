# TeensyMaestro — Community Edition (CE)
## Change Log (Baseline vs. Official KD0RC Version)

This change log provides a **high-level baseline** describing
what functionality in **TeensyMaestro Community Edition (CE)** differs from the
original firmware by **Len Koppl (KD0RC)**.

Detailed, incremental change history will be added going forward as new
community changes are introduced.

---

## Baseline — Community Edition Differences

### Firmware Scope
- Community-maintained fork of the original TeensyMaestro firmware
- Focus on robustness, automation, and modern contesting workflows
- Hardware **V2 only** (ST7796 display + MCP23017 I/O)

---

### User Interface & Input
- Unified **touch zone model**
  - Top bar: 4 discrete zones
  - Bottom bar: 4 discrete zones
  - Center: continuous touch area
  - Reset corner with short/long press actions
- Long‑press handling with configurable timing
- Deterministic short vs. long press semantics
- Acoustic feedback on long‑press threshold
- Profile selector accessible from center touch area
- Cleaner UI layout with integrated UTC clock display

---

### CW & Keying
- WinKeyer v2 emulation
  - TCP transport
  - USB Virtual COM (Serial) transport
- Configuration‑driven WinKeyer transport selection
- Reworked CW keyer internals for:
  - Stable timing
  - Reduced ISR load
  - Improved paddle handling
- Improved reliability under high keying and TX load

---

### Networking & Automation
- STUN / NAT traversal support
- Fixed‑IP operation support
- NTP time synchronization
- Lightweight **Remote Command Server (RCS)** over TCP
  - Deterministic request/response protocol
  - Guarded PTT and TUNE control
  - Designed for scripting, stream decks, and automation tools

---

### Radio Control Behavior
- Deterministic slice handling
  - No implicit slice creation
  - Safe operation when running headless or with GUI disabled
- Improved startup ordering and configuration loading

---

### Stability & Architecture
- Simplified internal architecture
- Removal of legacy compile‑time feature flags
- Hardened startup and initialization paths
- Improved fault tolerance and watchdog handling

---

## Credits & License
Based on the original **TeensyMaestro** firmware by  
**Len Koppl (KD0RC)**

Includes contributions inspired by community use and feedback.

Licensed under **CC BY‑NC‑SA 3.0**
