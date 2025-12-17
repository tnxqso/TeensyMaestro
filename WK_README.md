# TeensyMaestro WinKey Emulation (TM WK)

TeensyMaestro Community Edition includes a built-in **WinKeyer emulator** (“TM WK”) so your logging or contest software can treat TeensyMaestro as if it were a classic **K1EL WinKeyer**.

The goal is simple:  
You use your favorite logging program (DXLog, N1MM, SkookumLogger, etc.), select **WinKey** as the interface – and TeensyMaestro sends CW for you with proper timing.

This document explains what TM WK does, what it does **not** do, and what you should expect as a user.

---

## 1. What TM WK is (in plain language)

TM WK makes TeensyMaestro behave like a **WinKey2-compatible CW keyer** over:

- A **serial (USB) port**, and/or  
- A **TCP port** (WinKey over network).

Your logging program:

- Opens a connection (serial or TCP),
- Thinks it is talking to a K1EL WinKeyer,
- Sends text (“CQ TEST …”) and commands (speed changes, abort, etc.),
- TM WK does all the Morse timing and keys your radio.

You do **not** need to know protocol details; that is handled by TeensyMaestro.

---

## 2. What you can do with TM WK

From the logging program’s point of view, TM WK is a normal WinKeyer. You can:

- Send **CW messages/macros** (CQ, TU, exchange, etc.).
- Adjust **speed (WPM)** from the logging software.
- Adjust **speed from the physical knob** on TeensyMaestro, and have that reflected back to the software.
- Use **paddle input** locally (TeensyMaestro keyer) while the software is connected.
- Use the **WinKey “Abort/Clear Buffer” (ESC)** function in the logger to stop a message.
- Use **temporary speed changes inside a macro** (for example, faster call sign, slower report) when the logger supports it.

The intent is that if your software works with a real WinKey2, it should work in essentially the same way with TM WK.

---

## 3. Supported and tested environments

TM WK has been exercised with:

- **DXLog.net** (Windows, WinKey over TCP and Serial)
- **SkookumLogger** (macOS, WinKey over Serial)
- Other WinKey-aware loggers should work as long as they follow the WinKey v2 protocol correctly.

If something behaves differently than with a real WinKey:

- It is either a **missing feature** in TM WK, or
- A **corner case** in how that logger uses the protocol.

In that case, SD traces / logs are extremely valuable to track down differences.

---

## 4. How connection and speed work (user view)

### Connection

In your logger you typically select:

- **Device / Interface**: WinKey or WinKey2  
- **Port**:
  - Serial: the TeensyMaestro COM port  
  - TCP: the IP address of TeensyMaestro and the WinKey TCP port configured in TM

Once connected:

- The logger sends an “open” command.
- TM WK replies with a **firmware revision code** (as a real WinKey would).
- After that, the logger treats TeensyMaestro as a WinKey unit.

### Speed handling

- The logger can set WPM; TM WK applies it to the internal keyer.
- The TeensyMaestro WPM encoder can also change speed locally.
- TM WK tries to keep **host and front panel in sync**:
  - If the host changes speed, TM WK honors that.
  - If you turn the knob yourself, TM WK reports the new speed so the logger can update its display.
- Some loggers send **temporary speed changes inside macros**; TM WK supports this and applies those changes at the correct points in the message.

---

## 5. Abort / ESC behavior

From the logger’s perspective, pressing **ESC** (or “Clear Buffer”) should:

- Stop any CW that is currently sending,
- Clear all queued text,
- Leave the keyer idle and ready for the next message.

TM WK implements the WinKey **Clear Buffer** command with the following logic:

- If CW is **currently sending** or **data is queued**, ESC is treated as a **real abort**:
  - Transmission stops immediately.
  - Internal text queue is cleared.
  - Any partially received macro bytes are discarded (so the next macro starts clean).
  - TM WK sends an “idle” status back to the logger.

- If the keyer is **idle** (no text queued, no macro in progress), ESC is treated as **spurious/stale**:
  - Nothing happens internally.
  - This is to avoid accidentally discarding the *next* macro if the logger sends a “late” ESC after everything is already finished.

This design is tuned so that:

- Rapid “ESC → send new macro” sequences from the host do not cause messages to be lost,
- Background or “stuck” ESC bursts from certain loggers do not corrupt upcoming messages.

If you see different behavior in your logger (e.g. ESC seemingly ignored), a protocol trace usually reveals whether the logger’s ESC is sent at a time when the keyer is already fully idle.

---

## 6. What TM WK currently does **not** emulate fully

TM WK focuses on the parts of WinKey that **real loggers actually use in practice**.

Some areas are simplified or partially implemented:

- **Detailed weighting / element shaping**:  
  The “weight” setting is stored, but current builds do not yet apply a sophisticated shaping model. In normal contest use this is rarely noticed.
- **Certain admin/diagnostic commands**:  
  Some obscure or rarely used admin queries are replied to in a compatible but simplified way (for example always reporting certain defaults).
- **Exotic modes / options**:  
  Some features of WinKey3 that are not normally exposed by major logging programs may be ignored.

The goal is that **no mainstream logger** should fail to work because of these simplifications. If you hit a limitation, please report:

- Which program and version you use,
- What you tried to do,
- What you expected vs. what happened,
- Any trace/log file you can provide.

---

## 7. Troubleshooting checklist

If your logger does not behave as expected with TM WK:

1. **Check the chosen interface type**  
   - It must be configured as **WinKey** / **WinKey2**, not CAT, not “straight keyer”, etc.

2. **Check the port and speed**  
   - Serial: correct COM port, 1200 baud, 8 data bits, 2 stop bits, no parity (unless the logger explicitly says otherwise for WinKey).
   - TCP: correct IP and port (as configured in TeensyMaestro).

3. **Start simple**  
   - Send a very short macro, e.g. `TEST TEST TEST`, and verify it sends once and stops.  
   - Then test ESC in the middle of a longer message.

4. **Watch for double keyers**  
   - Make sure you are not accidentally keying the radio from both the logger and another device at the same time.

5. **If problems remain**  
   - Capture a **WinKey trace** / SD log from TeensyMaestro that covers:
     - Program startup / connect,
     - Sending a macro,
     - Pressing ESC (or the abort function) once or twice.
   - Send that log along with a short description (“pressed ESC about halfway through the word TEST”).

This level of information is usually enough to see whether the issue is in the logger’s usage of WinKey protocol or in TM WK’s emulation.

---

## 8. License and origin

TM WK is part of **TeensyMaestro Community Edition (CE)**.

- It is a **clean-room implementation** based on public WinKeyer v2 documentation.
- It is designed for **non-commercial** amateur radio use.
- Licensing follows the overall TeensyMaestro CE project: **CC BY-NC-SA 3.0**.

For a deeper, protocol-level description (intended for developers and integrators), see:

- `WINKEYER_PROTOCOL_V2_EMULATION.md`
