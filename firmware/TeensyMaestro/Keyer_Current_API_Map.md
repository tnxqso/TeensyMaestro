# TeensyMaestro Keyer – Current API Mapping (Baseline Study)

**Revision:** 2025-10-14  
**Scope:** Functional and dependency mapping between `Keyer.ino` and main sketch (`TeensyMaestroPlusKeyerV002.001.ino`).  
**Goal:** Identify the *current effective API surface* of the keyer module to guide modularization and refactor planning.

---

## 1. Overview

The current CW keyer logic is embedded as a large monolithic component inside `Keyer.ino`, with extensive cross-dependencies to the main sketch (`TeensyMaestroPlusKeyerV002.001.ino`).  
Its responsibilities include:

- Paddle input handling (Dot/Dash ISR)
- Timing of elements and inter-element gaps
- WPM and speed control
- Transmission gating to FlexRadio (via `fRig.send()`)
- Local sidetone generation
- Message and macro playback
- Abort and straight-key handling
- Integration with the main UI, encoder logic, and config parameters

---

## 2. Public Surface (Functions Referenced by Other Files)

| Function | Description | Likely Callers |
|-----------|--------------|----------------|
| `KeyerLoop()` | Main per-cycle service for keyer timing and deferred ISR events. | Main sketch loop |
| `Keyer()` | Core CW state machine; invoked by ISR-deferred kicks and within element routines. | KeyerLoop, deferred ISR service |
| `SendChar(char ch)` | Sends one character as CW, manages spacing and WPM conversion. | Message playback / macros |
| `CharSpace()` | Implements inter-character spacing and synchronization. | SendChar |
| `WordSpace()` | Implements inter-word spacing. | Message playback |
| `KeyTrans()` | Asserts transmitter (Key ON). | Keyer state machine |
| `UnKeyISR()` | Releases transmitter (Key OFF). | Timer callback |
| `DotKeyISR()`, `DashKeyISR()` | Paddle input interrupts. | Hardware ISR |
| `StopDotTimerISR()` | Timer callback marking element end. | Timer event |
| `Keyer_ServiceDeferred()` | Executes deferred work posted from ISR. | KeyerLoop / SendChar |
| `FlexSendCwKey(...)` | Builds and transmits "cw key" command string to FlexRadio. | KeyTrans / UnKeyISR |

---

## 3. Globals Exported or Used Across Files

| Variable | Declared in | Access | Purpose |
|-----------|--------------|---------|----------|
| `volatile bool ElementWait` | Keyer.ino | R/W from ISR and main | Marks element in progress |
| `volatile bool AbortMsg` | Main INO | Shared | Abort/stop current message |
| `volatile bool StraightKeyActive` | Main INO | Shared | Straight-key active mode |
| `volatile unsigned int CWIndex` | Keyer.ino | Shared | Running counter for Flex key packets |
| `KeyOutPin` | Main INO | Read | GPIO pin used for TX keying |
| `DotPin`, `DashPin` | Main INO | Read | Paddle inputs |
| `DotTimer` | Keyer.ino | Used | IntervalTimer controlling element timing |
| `fRig` | Main INO | Used | FlexRadio transport object providing `send()` |
| `Encoder_9`, `MenuActive`, `CWMicEnc` | Main INO | Used | CW speed adjustment & UI link |
| `ProSign` | Keyer.ino | Used | Indicates prosign merge state |
| `CFG_FlexHost` / `ClientMenuItem` | Main INO | Used | Host parameters for Flex send |

The keyer relies heavily on globals from the main sketch. Conversely, other modules depend on globals inside `Keyer.ino`.

---

## 4. External Dependencies

- **FlexRig Transport:** Provides `fRig.send()`; expects newline-terminated text commands (`cw key 0/1 ...`).
- **IntervalTimer (Teensy):** Provides microsecond timers for element and inter-space scheduling.
- **Audio (tone generation):** Sidetone controlled through `tone()` / `noTone()`.
- **Encoder and UI:** Adjust WPM, select macros, start message playback.
- **Abort / StraightKey flags:** Shared control state.

---

## 5. Event and State Model

```text
Paddle ISR (Dot/Dash)
        │
        ├──> set flags, defer Keyer()
        │
KeyerLoop() ──> Keyer_ServiceDeferred() ──> Keyer()
        │
        ├──> if element start → KeyTrans()
        ├──> if element end   → UnKeyISR() [timer callback]
        └──> triggers FlexSendCwKey(1/0) accordingly
```

### Timing constants
- Element length = computed from WPM.
- DotTimer controls element boundaries.
- Debounce ~2 ms on paddle edges.

---

## 6. Transport Contracts (FlexRadio)

Command format (string):
```
"cw key <0|1> time=0xXXXX index=<N> client_handle=<handle>\n"
```
- Must be newline-terminated.
- `index` increments monotonically.
- Sent via `fRig.send(String(buf));` from non-ISR context.
- Key-on (`1`) sent at element start, key-off (`0`) at element end.

---

## 7. Concurrency Contracts

| Rule | Enforcement |
|------|--------------|
| No transport or heap operations in ISR | ✅ done (deferred via flags) |
| Shared variables marked `volatile` | ✅ |
| `CWIndex` increment atomic between ISR and main | ✅ (`__atomic_fetch_add`) |
| Deferred execution serviced in `KeyerLoop()` | ✅ |

---

## 8. Current "Leaky" API Issues

| Category | Symptom |
|-----------|----------|
| Global coupling | Many `extern` symbols between main sketch and Keyer.ino. |
| Undefined ownership | Variables like `AbortMsg` controlled by multiple modules. |
| No header contract | Keyer.ino lacks header; no clear API boundary. |
| Implicit initialization | State depends on main sketch setup sequence. |
| Tight transport coupling | Keyer calls `fRig.send()` directly. |
| Mixed ISR/non-ISR logic | Previously fixed, but architecture still monolithic. |

---

## 9. Proposed Clean API (Target)

| Module | Key Public Functions | Responsibility |
|---------|----------------------|----------------|
| **keyer_engine** | `KeyerInit()`, `KeyerLoop()`, `SendChar()`, `Abort()` | Core timing, state machine |
| **keyer_isr** | `DotKeyISR()`, `DashKeyISR()`, `UnKeyISR()`, `StopDotTimerISR()` | ISR & flag handling only |
| **keyer_transport** | `FlexSendCwKey()` | Formatting and sending of CW packets |
| **keyer_pins** | `KEY_OUT_HIGH()`, `KEY_OUT_LOW()` | Hardware abstraction |
| **keyer_state** | `ElementWait`, `AbortMsg`, `StraightKeyActive` | Shared state encapsulation |

---

## 10. Shim Strategy for Refactor

1. **Introduce headers** for `keyer_engine.h`, `keyer_isr.h`, `keyer_transport.h` to declare public symbols.
2. Move `FlexSendCwKey()` first → `keyer_transport.cpp` (no functional change).
3. Extract ISR functions → `keyer_isr.cpp`, reference globals as `extern`.
4. Introduce `keyer_pins.h` with `digitalWriteFast()` helpers.
5. Leave all behavior identical; adjust includes progressively.
6. Add “dry-run” flag or serial logger backend for regression testing.

---

## 11. Migration Checklist

- [ ] Inventory of all globals used between files.  
- [ ] Header prototypes for each logical group.  
- [ ] Stepwise relocation of functions to separate .cpp files.  
- [ ] Build each stage and verify CW and TX remain functional.  
- [ ] Introduce lightweight serial logger for parallel testing.  
- [ ] Document final API after refactor.  

---

## 12. Summary

The keyer currently functions correctly but relies on implicit cross-file coupling.  
Defining an explicit API boundary is the next critical step before modularization.  
This mapping serves as the baseline reference for that process.

---

**Prepared for:** TeensyMaestro development refactor  
**Author:** ChatGPT (analysis from Keyer.ino and TeensyMaestroPlusKeyerV002.001.ino)  
**Date:** 2025‑10‑14
