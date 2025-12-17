# TeensyMaestro Community Edition – WinKeyer v2 Emulation

This document describes how the TeensyMaestro Community Edition (TM‑CE) firmware emulates a **K1EL WinKeyer v2** device.  
It is intended for:

- Log/contest software authors integrating with TM‑CE.
- Developers maintaining the TM‑CE WinKeyer emulation (`tm_wk_proto.*`).
- Users debugging interoperability with logging programs.

> This is **not** a copy of the K1EL manual. It documents **what TM‑CE actually does**: supported commands, behavior details, deviations, and tested environments.

---

## Table of Contents

- [1. Compatibility goals and scope](#1-compatibility-goals-and-scope)
- [2. Serial transport and framing](#2-serial-transport-and-framing)
- [3. Session lifecycle and handshake](#3-session-lifecycle-and-handshake)
  - [3.1 Admin Open / revision](#31-admin-open--revision)
  - [3.2 SkookumLogger-specific probes](#32-skookumlogger-specific-probes)
  - [3.3 EchoTest and ReadKeyerType](#33-echotest-and-readkeyertype)
  - [3.4 ASCII squelch and ready indication](#34-ascii-squelch-and-ready-indication)
- [4. Host → keyer protocol](#4-host--keyer-protocol)
  - [4.1 Byte classes](#41-byte-classes)
  - [4.2 Immediate (admin) commands](#42-immediate-admin-commands)
  - [4.3 Buffered (inline) commands](#43-buffered-inline-commands)
  - [4.4 ASCII text](#44-ascii-text)
- [5. Keyer → host protocol](#5-keyer--host-protocol)
  - [5.1 Revision and admin replies](#51-revision-and-admin-replies)
  - [5.2 Status byte (BUSY / XOFF)](#52-status-byte-busy--xoff)
  - [5.3 Character echo](#53-character-echo)
  - [5.4 Pot/speed reports](#54-potspeed-reports)
- [6. Buffering, timing, and speed handling](#6-buffering-timing-and-speed-handling)
  - [6.1 Unified RX FIFO](#61-unified-rx-fifo)
  - [6.2 Buffered speed `<1C><wpm>`](#62-buffered-speed-1cwpm)
  - [6.3 WAIT `<1A><nn>`](#63-wait-1ann)
- [7. CLEAR_BUFFER `<0A>` (ESC) behavior](#7-clear_buffer-0a-esc-behavior)
  - [7.1 Credible vs spurious ESC](#71-credible-vs-spurious-esc)
  - [7.2 Interaction with buffered text](#72-interaction-with-buffered-text)
- [8. Pot limits and WPM ranges](#8-pot-limits-and-wpm-ranges)
- [9. Deviations from K1EL reference behavior](#9-deviations-from-k1el-reference-behavior)
- [10. Command support matrix](#10-command-support-matrix)
- [11. Tested environments](#11-tested-environments)
- [12. Sources](#12-sources)

---

## 1. Compatibility goals and scope

TM‑CE aims to present itself on USB as a **WinKeyer v2–compatible keyer** so that existing logging/contest software can talk to it without any special support.

Goals:

- Look like a **WinKeyer v2 IC** with revision code `0x24` (2.4).
- Behave correctly with:
  - “Classic” WinKeyer clients (DXLog, N1MM, etc.).
  - “Picky” clients like **SkookumLogger** that probe the admin interface more aggressively.
- Preserve local panel usability:
  - Local WPM changes are reflected to the host.
  - Host‑driven settings don’t permanently clobber the user’s preferred speed.

Scope:

- This document covers the protocol core in `tm_wk_proto.cpp/.h`.
- It does **not** describe the underlying keyer DSP/timing implementation, only how it is driven by the WinKeyer protocol.

---

## 2. Serial transport and framing

Physical link (USB CDC / serial):

- **Baud:**  
  - Default: **1200 baud** (WinKeyer v2 standard).  
  - Optional: **9600 baud** if the host requests it via admin commands.
- **Framing:** `8 data bits, no parity, 2 stop bits` (**8N2**), per WinKey spec.
- Flow control:  
  - No hardware RTS/CTS.  
  - Flow control is handled in‑band via the WinKeyer status/XOFF bits.

The firmware **does not** call `Serial.begin()` from the protocol layer.  
Instead, admin baud changes are recorded and handed off to the transport/bridge layer, which is free to ignore or honor them.

---

## 3. Session lifecycle and handshake

### 3.1 Admin Open / revision

On power‑up, TM‑CE is in a **“host closed”** state. Most bytes are ignored until the host performs the standard WinKeyer **Admin Open** handshake:

```text
Host → Keyer:  <00><02>   ; Admin: Open / CONNECT
Keyer → Host:  <24>       ; Revision code = 0x24 (v2.4-compatible)
```

Behavior:

- Host mode is marked **open**.
- All internal protocol state is reset:
  - RX FIFO cleared.
  - Any armed buffered speed / WAIT cleared.
  - Baseline WPM captured from the current keyer speed.
- An **ASCII squelch window** is started to drop stray bytes during the handshake (see 3.4).

Admin Close `<00><03>`:

- TM‑CE **does not** treat `<00><03>` as a real transport close.
- It is accepted but **ignored** for state; the physical transport (USB/serial) owns open/close.
- When the actual transport closes (USB disconnect etc.), a dedicated hook `onTransportClosed()` runs:
  - Host‑open flag cleared.
  - Idle status sent.
  - Baseline WPM/weight restored.

### 3.2 SkookumLogger-specific probes

SkookumLogger issues a sequence of admin commands at startup that go beyond the original v2 manual. TM‑CE handles those explicitly:

- `<00><04>` – **EchoTest**  
  - Arms “echo next byte” mode.  
  - The very next byte is simply echoed back and then normal parsing resumes.

- `<00><09>` – **Version/Revision probe**  
  - TM‑CE replies with the same revision byte `0x24`.  
  - Used by SkookumLogger to verify a WinKeyer‑class device and that the revision is “new enough”.

- `<00><17>` – Interpreted by some hosts as **Read Minor Version**.  
  - TM‑CE replies with the low nibble of the revision (`0x04`) so the “minor” field is non‑zero.  
  - Outside the initial handshake, TM‑CE additionally treats this as a request to drop to **1200 baud** (admin‑level baud request recorded for the transport).

- `<00><18>` – **Set High Baud / 9600** (WK3 admin in some docs).  
  - TM‑CE replies with the same minor version byte (low nibble of revision).  
  - Outside the handshake window, TM‑CE records a request to move to **9600 baud**.

These admin replies are intentionally conservative: they give the host the bytes it expects, without changing behavior in surprising ways during the handshake.

### 3.3 EchoTest and ReadKeyerType

Additional admin helpers:

- `<00><04>` EchoTest – described above; used by SkookumLogger.

- `<00><0B><pp>` ReadKeyerType – TM‑CE implementation:
  - `<00><0B>` arms a one‑byte parameter.
  - The next byte `<pp>` is **consumed but ignored**.
  - TM‑CE replies with a **single type byte**:
    - Currently `0x00` = “basic keyer” for maximum compatibility.
  - This is sufficient for SkookumLogger to proceed.

After each of these probes, TM‑CE briefly extends the “unsolicited mute” timer so that status bytes do not get interleaved with the admin replies.

### 3.4 ASCII squelch and ready indication

Immediately after CONNECT (`<00><02>`), TM‑CE:

- Starts an **ASCII squelch window** (≈1.5 s):
  - Printable ASCII bytes are dropped silently.
  - Control bytes (admin, status request) are still processed.
  - This prevents garbage from the host’s port‑open sequence from being interpreted as CW text.

- Arms an optional **“ready” beep**:  
  When the squelch timer expires, a short tone is played and the squelch is lifted.

TM‑CE deliberately **does not** send the WinKey “ready pulse” byte (`0x17`) during handshake, because some picky hosts misinterpret extra bytes at this stage.

---

## 4. Host → keyer protocol

### 4.1 Byte classes

Once host mode is open, inbound bytes fall into three classes:

1. **Immediate (admin/control) commands**  
   - Command byte interpreted immediately.
   - Optional parameter bytes collected in a small side buffer.
   - Do not enter the main RX FIFO.

2. **Buffered (inline) commands**  
   - Command byte and its parameter(s) **enter the unified RX FIFO**.
   - Peeled and executed by the polling loop before ASCII text.

3. **ASCII text bytes** (`0x20..0x7E`)  
   - Buffered as text to be sent as CW.  
   - Pulled in chunks and passed to the keyer module (`enqueueText`).

Non‑ASCII/non‑control junk is dropped with a debug trace.

### 4.2 Immediate (admin) commands

Supported immediate commands (command byte followed by one or more params):

| Command             | Form               | Status                     | Notes |
|---------------------|--------------------|----------------------------|-------|
| Admin Open          | `<00><02>`         | Implemented                | Sets host‑open, sends revision 0x24 |
| Admin Close         | `<00><03>`         | Ignored for transport      | Physical transport owns close |
| EchoTest            | `<00><04>`         | Implemented                | Echoes next byte |
| Minor Version / Lo Baud | `<00><17>`     | Implemented (custom)       | Replies minor nibble; outside handshake records request for 1200 baud |
| High Baud 9600      | `<00><18>`         | Implemented (custom)       | Replies minor nibble; outside handshake records request for 9600 baud |
| Skookum Version probe | `<00><09>`       | Implemented                | Replies 0x24 revision |
| ReadKeyerType       | `<00><0B><pp>`     | Implemented                | Consumes param, replies `0x00` type |
| Sidetone enable     | `<01><onOff>`      | Accepted, ignored          | Logged; no behavior change today |
| Set WPM (immediate) | `<02><wpm>`        | Implemented                | Clamped, applied immediately |
| Set Weight          | `<03><weight>`     | Implemented (stored only)  | Stored in `_weight`, no timing effect yet |
| Set PTT lead/tail   | `<04><lead><tail>` | Placeholder                | Values stored, not yet applied |
| Set Pot Limits      | `<05><min><range><unused>` | Implemented      | See section 8 |
| WK2 Mode Register   | `<0E><mode>`       | Implemented (stored only)  | `_wkModeReg` only, no behavior change |

Notably **not implemented as immediates** (even if case stubs exist):

- `<09>` Set Outputs
- `<11>` Keying Compensation

These are not latched in `onByte()` and are thus effectively ignored on the wire in the current firmware.

### 4.3 Buffered (inline) commands

Buffered commands are enqueued into the same FIFO as text and processed by the polling loop before any ASCII in the queue head:

- **Buffered Speed**: `<1C><wpm>`  
  - Arms a temporary WPM for the following text.  
  - See [6.2](#62-buffered-speed-1cwpm).

- **Buffered PTT**: `<18><flags>`  
  - If `flags & 0x01` → PTT ON.  
  - Else → PTT OFF.

- **Buffered WAIT**: `<1A><nn>`  
  - Inserts a delay of `nn × 10 ms` **before** the following text.  
  - Implemented as a “hold‑until” timestamp (`_txHoldUntilMs`) that blocks further processing until expired.

The polling loop always peels as many buffered controls as possible from the FIFO head before touching ASCII data, both in `poll()` and in `decodeAndExecute()`.

### 4.4 ASCII text

Printable ASCII (`0x20..0x7E`) is treated as CW text:

- Pulled from the FIFO in **chunks up to 64 bytes**, stopping at:
  - End of FIFO, or
  - First non‑ASCII/control byte.
- Immediately before a chunk is handed to the keyer (`enqueueText`), any **armed buffered speed** is applied.

Characters are echoed back to the host by the keyer layer after actual RF transmission completes (see 5.3).

---

## 5. Keyer → host protocol

### 5.1 Revision and admin replies

Keyer‑initiated replies:

- **On `<00><02>` Admin Open:**  
  - Single byte: `0x24` (revision code).

- **On `<00><09>` Skookum version probe:**  
  - Single byte: `0x24`.

- **On `<00><17>` and `<00><18>`:**  
  - Single byte: `0x04` (minor nibble of revision).

- **On `<00><0B><pp>` ReadKeyerType:**  
  - Single byte: `0x00`.

- **On `<07>` Get Speed Pot**:  
  - Single byte: raw pot value in `0..31`.

Other immediates are silent (no ACK/NACK), matching typical WinKeyer behavior.

### 5.2 Status byte (BUSY / XOFF)

TM‑CE uses the standard WinKeyer status tag `0xC0` plus flags:

- Base status tag: `0xC0`
- `BUSY` bit: `0x04`
- `XOFF` bit: `0x01`
- Key‑down bit is currently not used.

Rules:

- **BUSY=1** (`0xC4`):  
  - Sent when RX FIFO becomes non‑empty or the keyer reports “busy” and status tag is not already BUSY.
  - This is the “start of activity” edge.

- **BUSY=0** (`0xC0`):  
  - Sent when:
    - RX FIFO is empty,  
    - keyer TXQ is empty, and  
    - a short debounce window (~30 ms) has passed since the last TX activity.
  - This is the “back to idle” edge.

- **XOFF=1**:  
  - Set when the keyer’s downstream TXQ exceeds ~70% of capacity.
  - Cleared when usage falls below that threshold.

Additionally, there is a **forced “idle now”** helper used in aborts and transport‑close:

- Sends `0xC0` immediately (subject to handshake‑mute guard).
- Does not depend on the debounce logic.

### 5.3 Character echo

After the keyer finishes sending each character, it calls `WK_OnCharEcho(ch)`:

- TM‑CE immediately writes the ASCII character `ch` back to the host.
- This is the **same character the host originally enqueued**, not the Morse pattern.
- Host software uses this to:
  - Highlight what has actually gone on the air.
  - Maintain a CW “monitor” window in sync with RF.

Spaces and non‑printable bytes are typically not echoed by the keyer layer.

### 5.4 Pot/speed reports

TM‑CE periodically observes `_k.getWpm()` in `poll()`:

- If the WPM changed **without** being obviously driven by the host (immediate or buffered speed), it is classified as a **local change** (front‑panel dial etc.).
- On local changes:
  - The internal **baseline WPM** is updated.
  - A pot/status report is sent to the host, allowing its GUI to track the new speed.

`Get Speed Pot` (`<07>`) returns a raw value in `0..31`, derived from:

- `_potMinWpm`, `_potRangeWpm` (set via `<05>`).
- The current `_k.getWpm()`.

---

## 6. Buffering, timing, and speed handling

### 6.1 Unified RX FIFO

Unlike the original K1EL hardware which internally had separate paths, TM‑CE uses a **single unified RX FIFO** stored in RAM (or PSRAM on Teensy 4.1):

- Capacity: `TM_WK_RXBUF_SIZE` bytes (power of two, ring buffer).
- Holds:
  - Buffered control bytes (`<18>`, `<1A>`, `<1C>`, etc.).
  - ASCII CW text.
- Immediate commands never enter this FIFO; they are handled in a separate path.

The poll loop works in this order:

1. Peel buffered controls from FIFO head as long as possible.
2. If a WAIT is armed and not yet expired: stop.
3. If FIFO head is ASCII:
   - Collect ASCII chunk, apply any armed speed, and queue to the keyer.
4. Try to send an idle status edge if everything is drained.

### 6.2 Buffered speed `<1C><wpm>`

Buffered speed is treated as a **temporary WPM override for the following text**:

- `<1C><wpm>` is enqueued in the FIFO and later peeled by the poll loop.
- When peeled:
  - `wpm` is clamped to `[TM_WK_WPM_MIN, TM_WK_WPM_MAX]`.
  - `_armedSpeedWpm` is set.
  - `_armedSpeedValid` is set to `true`.

Application of the armed speed:

- If ASCII text arrives while `_armedSpeedValid` is true:
  - Just before the first ASCII chunk is sent, TM‑CE calls `setWpmProvenance(...BufferedApplyAscii...)`.
  - `_armedSpeedValid` is cleared.
- If the FIFO drains completely and the system reaches a **true idle** state while `_armedSpeedValid` is still true:
  - The speed is applied at idle (`...BufferedApplyIdle...`) in `sendStatusIdleIfPossible()`.
  - This ensures the host GUI and the actual keyer speed stay in sync even if no text followed `<1C>`.

Important:

- Buffered speed **does not** call `recordHostSetWpm()`.  
  That means it does not overwrite the “host baseline” used to classify later local changes.

### 6.3 WAIT `<1A><nn>`

WAIT is implemented as a protocol‑level hold:

- When `<1A><nn>` is peeled:
  - A timestamp `_txHoldUntilMs = now + nn × 10 ms` is set.
- While the current time is less than `_txHoldUntilMs`, `poll()` returns early without touching the FIFO.
- Once the hold expires, processing resumes from the FIFO head.

Buffered speed and other inline commands before the WAIT run normally; ASCII text after the WAIT is delayed.

---

## 7. CLEAR_BUFFER `<0A>` (ESC) behavior

This is the **critical abort/ESC behavior** and has been refined to behave sanely with both DXLog and SkookumLogger.

### 7.1 Credible vs spurious ESC

When the host sends `<0A>` (CLEAR_BUFFER), TM‑CE inspects current state:

- `txBusy` – is the keyer actively sending Morse?
- `txqUsed` – are there bytes in the downstream keyer TX queue?
- `rxUsedNow` – are there bytes in the unified RX FIFO?
- `parserBusy` – is the protocol in the middle of parsing something?
  - Waiting opcode for a buffered command.
  - Immediate command expecting more params.
  - WAIT hold active.
  - Armed buffered speed pending.

If **any** of these is true, `<0A>` is classified as **credible** (abortable activity present).  
Otherwise it is classified as **spurious** (idle / stale).

#### Spurious ESC (idle)

Conditions:

- No TX in progress.  
- No pending TXQ.  
- RX FIFO empty.  
- No parser or WAIT state active.

Behavior:

- `<0A>` is **ignored**.
- No buffers are cleared.
- No status change is sent (other than what normal idle handling might do).

This avoids the classic “old ESC in the pipe wipes the next macro” problem seen with some hosts that send extra `<0A>` at idle.

#### Credible ESC (real abort)

When `<0A>` is credible, TM‑CE performs a **hard abort**:

1. Clear the unified RX FIFO:
   - `_rtail = _rhead;`
2. Reset parser/wait state:
   - `_rxBufferedParamArmed = false;`
   - `_waitingOpcode = 0;`
   - `_waitingSinceMs = 0;`
   - `_armedSpeedValid = false;`
   - `_txHoldUntilMs = 0;`
3. Immediately call:
   - `_k.abortNow();`
   - `_k.clearTextQueue();`
4. Emit an **idle‑now** status:
   - Sends `0xC0` (`WK_STATUS_TAG`) once, if not in the handshake mute period.

Result:

- On‑air keying stops as fast as the keyer can release PTT/key.
- Any queued text waiting to be sent is discarded.
- Subsequent macros from the host start from a clean state.

### 7.2 Interaction with buffered text

Because `<0A>` is handled in the **immediate path in `onByte()`**, it does not queue in the RX FIFO:

- A credible ESC aborts even if:
  - The FIFO still has many ASCII characters.
  - The keyer is in the middle of sending a word.
- The FIFO is explicitly cleared as part of the abort.

If the host sends **multiple `<0A>` bytes in a row**:

- The **first** one during activity is credible and triggers abort.
- Later ones, after everything is idle, are seen as spurious and ignored.

This matches what we see in practical traces: some hosts hammer ESC/`<0A>` repeatedly, and the firmware must not let a “late” `<0A>` eat the next macro.

---

## 8. Pot limits and WPM ranges

The `Set Pot Limits` command:

```text
<05><min><range><unused>
```

TM‑CE behavior:

1. Raw parameters are read:

   - `hostMin = <min>`
   - `hostRange = <range>`

2. They are clamped to **sane numeric bounds**:

   - `hostMin   < 5`  → `hostMin = 5`
   - `hostMin   > 99` → `hostMin = 99`
   - `hostRange < 1`  → `hostRange = 1`
   - `hostRange > 99` → `hostRange = 99`

3. The clamped values are stored as:

   - `_potMinWpm   = hostMin`
   - `_potRangeWpm = hostRange`

4. The `<unused>` byte is ignored but logged for debugging.

Notes:

- This allows hosts to request very wide ranges (e.g. `min=31`, `range=138`) without blowing up the mapping logic.
- The actual effective range is naturally constrained by the internal pot resolution (`0..31` steps) and `TM_WK_WPM_MIN`/`MAX`, but the protocol interface remains tolerant.

---

## 9. Deviations from K1EL reference behavior

Known and intentional differences from the reference WinKeyer v2 implementation:

- **Transport close**  
  - TM‑CE ignores `<00><03>` for real open/close. The physical transport layer controls the lifetime of the session.

- **Admin baud control**  
  - `ReadMinorVersion` and 1200/9600 baud admin commands are implemented in a way that keeps handshake robust with modern hosts, but do not blindly flip the hardware UART from inside the protocol layer.

- **Early ready pulse**  
  - TM‑CE does **not** send the “ready pulse” byte `0x17` during handshake, to avoid tripping fragile host parsers.

- **CLEAR_BUFFER semantics**  
  - Spurious ESC at true idle is ignored, explicitly to avoid wiping the next macro due to stale `<0A>` in the pipe.

- **Set Weight / WK2 mode / outputs / compensation**  
  - Some command bytes are accepted and stored for future use, but currently have no effect on actual keying timing or GPIOs.

These deviations are deliberate trade‑offs to keep TM‑CE robust in real‑world logging environments.

---

## 10. Command support matrix

Summary of command support in the current TM‑CE WinKeyer emulation:

| Command                | Form                        | Status                          | Notes |
|------------------------|-----------------------------|---------------------------------|-------|
| Admin Open             | `<00><02>`                  | ✔ Implemented                   | Sends revision 0x24 |
| Admin Close            | `<00><03>`                  | ◑ Ignored for state             | Logged only |
| EchoTest               | `<00><04>`                  | ✔ Implemented                   | Echoes next byte |
| Skookum version probe  | `<00><09>`                  | ✔ Implemented                   | Replies 0x24 |
| Minor version / lo baud| `<00><17>`                  | ✔ Implemented (custom)          | Replies minor, records 1200 baud outside handshake |
| High baud (9600)       | `<00><18>`                  | ✔ Implemented (custom)          | Replies minor, records 9600 baud outside handshake |
| ReadKeyerType          | `<00><0B><pp>`              | ✔ Implemented                   | Replies 0x00 |
| Sidetone enable        | `<01><onOff>`               | ◑ Accepted/ignored              | No audio change |
| Set speed (immediate)  | `<02><wpm>`                 | ✔ Implemented                   | Clamped, origin tracked |
| Set weight             | `<03><weight>`              | ✔ Stored                        | No timing effect yet |
| Set PTT lead/tail      | `<04><lead><tail>`          | ◑ Stored                        | Not yet used |
| Set pot limits         | `<05><min><range><unused>`  | ✔ Implemented                   | Clamped to 5..99 / 1..99 |
| Get speed pot          | `<07>`                      | ✔ Implemented                   | Returns raw 0..31 |
| Set outputs            | `<09>`                      | ✖ Not implemented on wire       | Reserved for future |
| Clear buffer (ESC)     | `<0A>`                      | ✔ Implemented                   | Credible/spurious model |
| Key immediate (tune)   | `<0B><01/00>`               | ✖ Not implemented               | Planned |
| WK2 mode register      | `<0E><mode>`                | ✔ Stored                        | No behavior yet |
| Keying compensation    | `<11><comp>`                | ✖ Not implemented on wire       | Reserved for future |
| Request status         | `<15>`                      | ✔ Implemented                   | Returns status byte |
| Buffered PTT           | `<18><flags>`               | ✔ Implemented                   | Inline PTT on/off |
| Buffered WAIT          | `<1A><nn>`                  | ✔ Implemented                   | Inline delay |
| Merge / prosign        | `<1B>`                      | ✖ Not implemented               | – |
| Buffered speed         | `<1C><wpm>`                 | ✔ Implemented                   | Armed & applied at chunk/idle |
| Cancel temp speed      | `<1E>`                      | ✖ Not implemented               | – |

ASCII CW text (`0x20..0x7E`) is, of course, fully supported and buffered.

---

## 11. Tested environments

The following combinations have been explicitly tested against TM‑CE’s WinKeyer emulation:

- **DXLog.net (Windows)**  
  - OS: Windows 10/11.  
  - Connection: USB CDC, 1200 baud, 8N2.  
  - Verified:
    - Handshake and revision detection.
    - Macros with inline speed `<1C>` and WAIT `<1A>`.
    - ESC/abort behavior using `<0A>` (CLEAR_BUFFER).
    - Local WPM changes reflected as pot/speed updates.

- **SkookumLogger (macOS)**  
  - OS: Recent macOS releases (e.g. Ventura / Sonoma).  
  - Connection: USB CDC, 1200/9600 baud as requested by the host.  
  - Verified:
    - Extended admin handshake:
      - `<00><04>` EchoTest,
      - `<00><09>` version,
      - `<00><17>/<00><18>` minor/baud probes,
      - `<00><0B>` ReadKeyerType.
    - Macro sending with CW text.  
    - Host‑side ESC translated into repeated `<0A>` sequences and correctly handled as:
      - **Credible** abort while transmission is active.
      - **Spurious** and ignored when everything is idle.

Other WinKeyer‑aware software is expected to work as long as it conforms to the standard v2 protocol and does not depend on unimplemented WK3‑specific features.

---

## 12. Sources

Primary references used while designing and validating the TM‑CE WinKeyer emulation:

- K1EL Systems – **WinKeyer2 IC – Interface & Operation Manual**, rev. 23.
- K1EL Systems – General WinKeyer documentation and FAQs.
- N1MM Logger+ and DXLog.net WinKeyer integration notes.
- Empirical traces from DXLog.net (Windows) and SkookumLogger (macOS) against TM‑CE firmware.
