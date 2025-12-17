#include <ctype.h>
#include <Arduino.h>
#include "tm_wk_proto.h"
#include "tm_system_utils.h"

#ifndef KEYER_MICRO_DEBOUNCE_DEFINED
#define KEYER_MICRO_DEBOUNCE_DEFINED
#endif

extern "C" {
// Provided by tm_wk_proto.cpp (step 3)
void WK_OnCharEcho(uint8_t ch);

// Queried by TM_KeyerAdapter::txqUsed() (step 2)
// Return how many bytes are currently waiting in the keyer's internal TX queue.
uint16_t Keyer_TxQ_Used(void);
}

// PTT helper implemented in Process_Buttons.ino
extern bool DoRigPTT(bool on);


// === CW timing guard =========================================================
// When true, we are inside timing-critical CW element/spacing and the service
// pump must avoid any artificial sleeps (e.g., delay(1)).
volatile bool g_KeyerTimingActive = false;
volatile uint32_t g_lastKeyDownUs = 0;

// --- fast I/O helpers (keep ISRs short) ---
static inline void KEY_OUT_LOW()  { digitalWriteFast(KeyOutPin, LOW);  }
static inline void KEY_OUT_HIGH() { digitalWriteFast(KeyOutPin, HIGH); }

// Format and send Flex "cw key" line (works from ISR and non-ISR)
static inline void FlexSendCwKey(uint8_t on,
                                 uint32_t t_hex,
                                 unsigned idx,
                                 const char* handle_cstr)
{
  char buf[128];
  snprintf(buf, sizeof(buf),
           "cw key %u time=0x%X index=%u client_handle=%s\n",
           (unsigned)on, (unsigned)(t_hex & 0xFFFF), idx, handle_cstr);

  fRig.send(buf);
}

// --- Flex CW edge queue (ISR-safe, small ring) -------------------------------
// NOTE: We have up to 4 Flex clients according to FlexRigTeensy.h

static constexpr uint8_t FLEX_MAX_CLIENTS = 4;

// Size of the CW edge ring buffer (must be power-of-two)
static constexpr uint8_t FLEX_CWQ_SIZE = 128;
static constexpr uint8_t FLEX_CWQ_MASK = FLEX_CWQ_SIZE - 1;

struct FlexCwEdge
{
  uint8_t  on;        // 1 = key down, 0 = key up
  uint16_t t_hex;     // low 16 bits of millis()
  uint16_t idx;       // CWIndex snapshot
  uint8_t  clientIdx; // index into fRig.Client_Handle[]
};

static volatile uint8_t g_flexCwHead = 0;
static volatile uint8_t g_flexCwTail = 0;
static FlexCwEdge       g_flexCwQ[FLEX_CWQ_SIZE];

// Enqueue from ISR or timing-critical code
static inline void FlexQueueCwKeyFromISR(uint8_t on,
                                         uint32_t t_hex,
                                         unsigned idx,
                                         uint8_t clientIdx)
{
  uint8_t next = (uint8_t)((g_flexCwHead + 1u) & FLEX_CWQ_MASK);
  if (next == g_flexCwTail)
  {
    // Queue full - drop event; better to lose one edge than lock up the ISR
    return;
  }

  if (clientIdx >= FLEX_MAX_CLIENTS)
  {
    clientIdx = 0;
  }

  g_flexCwQ[g_flexCwHead].on        = on;
  g_flexCwQ[g_flexCwHead].t_hex     = (uint16_t)(t_hex & 0xFFFFu);
  g_flexCwQ[g_flexCwHead].idx       = (uint16_t)idx;
  g_flexCwQ[g_flexCwHead].clientIdx = clientIdx;

  __asm__ volatile ("" ::: "memory");
  g_flexCwHead = next;
}

void Keyer_FlushFlexCwQueue()
{
  if (!fRig.connected)
  {
    // Consume any stale events so the queue does not grow forever
    g_flexCwTail = g_flexCwHead;
    return;
  }

  while (g_flexCwTail != g_flexCwHead)
  {
    uint8_t tail = g_flexCwTail;
    FlexCwEdge e = g_flexCwQ[tail];
    g_flexCwTail = (uint8_t)((tail + 1u) & FLEX_CWQ_MASK);

    uint8_t ci = e.clientIdx;

    if (ci >= FLEX_MAX_CLIENTS)               { continue; }
    if (ci >= (uint8_t)fRig.Max_Clients)      { continue; }

    const char* handle = fRig.Client_Handle[ci].c_str();
    FlexSendCwKey(e.on, e.t_hex, e.idx, handle);
  }
}

// Clamp and start UnKey timer safely (calls UnKeyISR)
static inline void StartUnKeyTimer_us(uint32_t us) {
  if (us < 1) us = 1;
  DotTimer.begin(UnKeyISR, us);
}

// Clamp and start DotTimer safely
static inline void StartDotTimer_us(uint32_t us) {
  if (us < 1) us = 1;
  DotTimer.begin(StopDotTimerISR, us);
}

// --- micro-debounce for paddle ISRs (cheap guard; avoids 20 ms spin-loops in ISRs) ---
static constexpr uint32_t KEY_DEBOUNCE_US = 2000; // ~2 ms
static inline bool isrDebounceElapsed(volatile uint32_t &lastUs)
{
  const uint32_t now = micros();
  const uint32_t dt  = now - lastUs;
  if (dt < KEY_DEBOUNCE_US) return false;
  lastUs = now;
  return true;
}

// --- keyer → screensaver activity tick (cheap, ISR-safe) ---
static inline void Keyer_ActivityTick()
{
  ScreenSaveTimer = millis();
  if (ScreenSaveActive) {
    KeyPressed = true;
  }
}

// --- very light "kick" flag to avoid calling Keyer() directly inside ISRs ---
static volatile bool g_keyerKick = false;
static inline void Keyer_DeferKickFromISR() { g_keyerKick = true; }

// Pending hard unkey for Flex (set from ISR, serviced in KeyerLoop)
static volatile bool g_FlexAbortPending = false;

// Call this from safe, non-ISR paths that already run often (e.g. after SendChar/CharSpace).
static inline void Keyer_ServiceDeferred() {
  static bool g_inKeyerService = false; // re-entry guard
  if (g_inKeyerService) return;
  if (!g_keyerKick) return;

  g_inKeyerService = true;
  g_keyerKick = false;
  Keyer();
  g_inKeyerService = false;
}

static bool g_parseParams = true;
static constexpr float DOT_TO_DASH = 3.0f;
static constexpr float CHAR_GAP_FACTOR = 1.5f;

// -----------------------------------------------------------------------------
// Outgoing CW message queue in RAM2 (no RAM1 allocations while enqueuing)
// -----------------------------------------------------------------------------
#ifndef KEYER_TXQ_SIZE
#define KEYER_TXQ_SIZE 32          // number of entries in the ring
#endif

#ifndef KEYER_TXQ_CHUNK
#define KEYER_TXQ_CHUNK 64        // bytes per entry; matches WK burst size
#endif

DMAMEM static uint16_t g_txq_len[KEYER_TXQ_SIZE];
DMAMEM static char     g_txq_data[KEYER_TXQ_SIZE][KEYER_TXQ_CHUNK];
static  uint8_t        g_txq_head = 0;
static  uint8_t        g_txq_tail = 0;
static volatile uint16_t g_txq_bytes = 0;  // total bytes currently queued

static inline bool Keyer_TxQ_IsFull()  { return (uint8_t)((g_txq_head + 1) % KEYER_TXQ_SIZE) == g_txq_tail; }
static inline bool Keyer_TxQ_IsEmpty() { return g_txq_head == g_txq_tail; }
uint16_t Keyer_TxQ_CapacityBytes() {
  return (uint16_t)(KEYER_TXQ_SIZE * KEYER_TXQ_CHUNK);
}

void Keyer_Beep(uint16_t freq, uint16_t ms) {
  tone(STPin, freq, ms);
}

// -----------------------------------------------------------------------------
// Timing helpers (no-UI, no-baseline changes)
// -----------------------------------------------------------------------------
static inline long Keyer_ComputeElementLen(int wpm) { return 1200000L / (wpm <= 0 ? 1 : wpm); }
static inline void Keyer_Recalc_Timing()            { ElementLen = Keyer_ComputeElementLen(WPM); }

// Enqueue raw bytes; may split into multiple entries if len > CHUNK
bool Keyer_TxQ_EnqueueRaw(const char* data, size_t len) {
  while (len > 0) {
    if (Keyer_TxQ_IsFull()) {
      debugf("WK: EnqueueRaw FULL, bytes=%u\n", (unsigned)g_txq_bytes);
      return false;
    }
    const size_t n = (len > KEYER_TXQ_CHUNK) ? KEYER_TXQ_CHUNK : len;
    memcpy(g_txq_data[g_txq_head], data, n);
    g_txq_len[g_txq_head] = (uint16_t)n;
    g_txq_bytes = (uint16_t)(g_txq_bytes + (uint16_t)n);  // O(1) byte accounting
    debugf("WK: EnqueueRaw +%u => %u\n", (unsigned)n, (unsigned)g_txq_bytes);
    g_txq_head = (uint8_t)((g_txq_head + 1) % KEYER_TXQ_SIZE);
    data += n;
    len  -= n;
  }
  return true;
}

// Dequeue one entry; returns pointer/len into RAM2 storage (valid until next dequeue)
bool Keyer_TxQ_Peek(const char*& ptr, uint16_t& n) {
  if (Keyer_TxQ_IsEmpty()) return false;
  ptr = g_txq_data[g_txq_tail];
  n   = g_txq_len[g_txq_tail];
  return true;
}

void Keyer_TxQ_Pop() {
  if (!Keyer_TxQ_IsEmpty()) {
    uint16_t n = g_txq_len[g_txq_tail];
    if (n > 0) {
      // prevent underflow in case of accidental double-pop
      g_txq_bytes = (uint16_t)((g_txq_bytes >= n) ? (g_txq_bytes - n) : 0);
      debugf("WK: Pop -%u => %u\n", (unsigned)n, (unsigned)g_txq_bytes);
    }
    g_txq_len[g_txq_tail] = 0;
    g_txq_tail = (uint8_t)((g_txq_tail + 1) % KEYER_TXQ_SIZE);
  }
}

uint16_t Keyer_TxQ_Used(void) {
  // Return how many bytes are currently queued in the keyer's internal TX queue.
  // Maintained in O(1) by Keyer_TxQ_EnqueueRaw() and Keyer_TxQ_Pop().
  return g_txq_bytes;
}

// Flush all pending host TX data (used on abort for WinKey/DXLog)
static inline void Keyer_TxQ_Flush()
{
  g_txq_head  = g_txq_tail;
  g_txq_bytes = 0;

  // Clear per-entry lengths to avoid stale values
  for (uint8_t i = 0; i < KEYER_TXQ_SIZE; ++i) {
    g_txq_len[i] = 0;
  }
}

void Keyer_ServiceQueue()
{
  if (!MsgActive)
  {
    const char* p; uint16_t n;
    if (Keyer_TxQ_Peek(p, n))
    {
      //Serial.printf("Temporary serial.printf Keyer: Start chunk len=%u at WPM=%d (ElementLen=%ld)\n",
      //  (unsigned)n, (int)WPM, (long)ElementLen);
      SendMsgRaw(p, n);   // zero-allocation path
      Keyer_TxQ_Pop();
    }
  }
}

// -----------------------------------------------------------------------------
// Apply CW speed from any source (panel encoder, DXLog via WinKey, etc.).
// Keeps CWVal, WPM, ElementLen, encoder and display in sync.
//
// If preserveBaseline==false (default):
//   - Updates CWValSave as well, creating a new baseline speed.
//   - Typical for panel/menu or FlexRadio when the user or radio selects a
//     permanent new speed.
//
// If preserveBaseline==true:
//   - Leaves CWValSave unchanged, treating the change as temporary.
//   - Typical for WinKey/DXLog or in-macro $Snn speed changes.
//
// Summary of intent:
//   • CWValSave  = baseline (used for restoring after temporary changes)
//   • CWVal + WPM = current live speed and timing basis
//   • ElementLen = 1200000 / WPM  (duration of one element in µs)
//     calculated in Keyer_Recalc_Timing()
//
// Typical use:
//   - Panel/menus → preserveBaseline=false → CWValSave follows (new default)
//   - Macros or WinKey/DXLog → preserveBaseline=true → temporary speed only
//   - After macro send, CWValSave is restored as baseline (SendMsg → restoreWpm)
// -----------------------------------------------------------------------------
void Keyer_Apply_Wpm(int newWpm, bool preserveBaseline)
{
  newWpm = TMU_ClampWpm(newWpm);  // <- use helper clamp

  CWVal = newWpm;
  if (!preserveBaseline) {
    CWValSave = CWVal;   // baseline follows panel/menu changes
  }
  WPM = CWVal;
  Keyer_Recalc_Timing();

  if (Encoder_9 == Enc9_CWSpeed) {
    CWMicEnc.write(CWVal * CWEncSteps);
  }
  if (Encoder_9 == Enc9_CWSpeed || Encoder_9 == Enc9_RFPower || Encoder_9 == Enc9_Band) {
    DispCWSpeed();
  }

  debugf("Keyer: Apply_Wpm=%d (ElementLen=%ld)%s\n",
         WPM, (long)ElementLen, preserveBaseline ? " [preserveBaseline]" : "");
}

/*********************** KeyerSetup *********************/
void KeyerSetup()
{
  //  audioamp.begin();
  //  audioamp.enableChannel(true, false);  // right, left
  //  audioamp.setGain(-28);  // -28dB to +30dB

  //  dac.begin(0x62);

  //  for (int i = 0; i < 512; i++)
  //  {
  //    dac.setVoltage(pgm_read_word(&(DACLookup_FullSine_9Bit[i])), false);
  //  }

  // Set up I/O pins and interrupts
  pinMode(DotPin, INPUT_PULLUP);
  pinMode(DashPin, INPUT_PULLUP);
  pinMode(KeyOutPin, OUTPUT);
  pinMode(StraightKeyPin, INPUT_PULLUP);

  KEY_OUT_LOW();  // Unkey transmitter

  Keyer_DeferKickFromISR();
  attachInterrupt(digitalPinToInterrupt(DotPin), DotKeyISR, CHANGE);
  Keyer_DeferKickFromISR();
  attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);

  attachInterrupt(digitalPinToInterrupt(StraightKeyPin), StraightKeyISR, CHANGE);

  Keyer_Recalc_Timing();

  XmitOn = false;

  DotPressed  = false;
  DashPressed = false;

  XmitOn = true;
}

/*********************** KeyerLoop *********************/
void KeyerLoop()
{
  // Service deferred work posted by ISRs (paddle/timers)
  Keyer_ServiceDeferred();

  if (AbortMsg)
  {
    // Central non-ISR abort handling: drop key, stop timers, notify radio
    _KeyerAbortAndCleanup();
    return;
  }

  if (KeyDown)
  {
    if (!OldKeyDown)  // only update tft screen if KeyDown state changes
    {
      OldKeyDown = true;
    }
  }
  else
  {
    if (OldKeyDown)
    {
      OldKeyDown = false;
      noTone(STPin);
    }
  }
  // Service pending host text (WinKey/TCP). Starts a new message when idle.
  Keyer_ServiceQueue();

  // Flush any queued Flex CW edges from ISR context
  if (KeyerOut == "ETHERNET")
  {
    Keyer_FlushFlexCwQueue();
  }

  // Extra safety: if an abort happened during Ethernet CW keying, force a direct cw key 0
  // from non-ISR context. Harmless if the radio is already unkeyed.
  if (g_FlexAbortPending && KeyerOut == "ETHERNET" && fRig.connected)
  {
    const unsigned __t   = (unsigned)(millis() % 0xFFFFu);
    const unsigned __idx = (unsigned)CWIndex;
    const char*    __h   = fRig.Client_Handle[ClientMenuItem].c_str();

    FlexSendCwKey(0, __t, __idx, __h);
    CWIndex++;
    g_FlexAbortPending = false;
  }
  
  // Failsafe: hard unkey if something got stuck
  if (KeyDown)
  {
    uint32_t now = micros();
    uint32_t dt  = now - g_lastKeyDownUs;
    if (dt > 500000UL) // 500 ms, tune as needed
    {
      // Single, centralized hard-unkey (LOCAL + Flex)
      Keyer_HardUnkeyNow();
    }
  }
  
}

/***************************** Keyer ***************************/
void Keyer()
{
  // Service any deferred work (timer ISRs, etc.) before we decide on next element
  Keyer_ServiceDeferred();

  // Cootie/sideswiper mode uses special ISR handling only
  if (KeyMode == "C")
  {
    return;
  }

  if (!ElementWait)
  {
    // Always take a fresh snapshot of paddle inputs
    DotPinVal  = digitalRead(DotPin);
    DashPinVal = digitalRead(DashPin);

    if (DotPinVal + DashPinVal == 2)  // neither paddle pressed
    {
      // Dot and Dash pin interrupts turned off in the ISR and turned back on after servicing the current int
      Keyer_DeferKickFromISR();
      attachInterrupt(digitalPinToInterrupt(DotPin),  DotKeyISR,  CHANGE);
      attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);
    }

    // In iambic A we do not want trailing elements when both paddles are released
    if ((KeyMode != "B") && (DotPinVal + DashPinVal > 1))  // neither paddle pressed
    {
      CurElement  = 0;
      DotPressed  = false;
      DashPressed = false;
      return;
    }

    if (!StraightKeyActive)
    {
      if (DotPressed)  // insert dot (latched in iambic mode)
      {
        KeyTrans();
        DotPressed = false;
        CurElement = Dot;
        DotTimer.begin(UnKeyISR, ElementLen);
      }
      else if (DashPressed)  // insert dash (latched in iambic mode)
      {
        KeyTrans();
        DashPressed = false;
        CurElement  = Dash;
        DotTimer.begin(UnKeyISR, ElementLen * DOT_TO_DASH );
      }
      else if (DotPinVal == 0 && DashPinVal == 1)  // Dot paddle only
      {
        KeyTrans();
        CurElement = Dot;
        DotTimer.begin(UnKeyISR, ElementLen);
      }
      else if (DotPinVal == 1 && DashPinVal == 0)  // Dash paddle only
      {
        KeyTrans();
        CurElement = Dash;
        DotTimer.begin(UnKeyISR, ElementLen * DOT_TO_DASH );
      }
      else if (DotPinVal == 0 && DashPinVal == 0)  // Both paddles
      {
        if (KeyMode == "A" || KeyMode == "B")
        {
          if (CurElement == Dot)
          {
            KeyTrans();
            CurElement = Dash;
            DotTimer.begin(UnKeyISR, ElementLen * DOT_TO_DASH );
          }
          else if (CurElement == Dash)
          {
            KeyTrans();
            CurElement = Dot;
            DotTimer.begin(UnKeyISR, ElementLen);
          }
        }
        else
        {
          // Non-iambic mode: repeat the same element type when both paddles are held
          if (CurElement == Dot)
          {
            KeyTrans();
            DotTimer.begin(UnKeyISR, ElementLen);
          }
          else if (CurElement == Dash)
          {
            KeyTrans();
            DotTimer.begin(UnKeyISR, ElementLen * DOT_TO_DASH );
          }
        }
      }
    }  // end not straight key active

    if (DotPinVal + DashPinVal == 2)  // neither paddle pressed
    {
      CurElement  = 0;
      DotPressed  = false;
      DashPressed = false;
      if (AbortMsg)
      {
        Keyer_HardUnkeyNow();
      }
    }
  }  // end if !ElementWait

  // Re-arm paddle interrupts after servicing the current interrupt
  attachInterrupt(digitalPinToInterrupt(DotPin),  DotKeyISR,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);
}


/***************************** KeyTrans ***************************/
void KeyTrans()
{
  // Do not use element timing when straight key is active
  if (StraightKeyActive) {
    return;
  }
  
  // Enter timing-critical section: from key-down through the end of the
  // inter-element spacing driven by timers.
  g_KeyerTimingActive = true;
  g_lastKeyDownUs     = micros();

  if (AbortMsg)
  {
    noTone(STPin);
  }

  if (XmitOn && !AbortMsg)
  {
    if (KeyerOut == "LOCAL")
    {
      KEY_OUT_HIGH();  // Key transmitter
    }
    else if (KeyerOut == "ETHERNET" && fRig.connected)
    {
      // Find the active TX slice once and use it consistently
      int txSlice = -1;
      for (int s = 0; s < fRig.nMaxSlice; ++s)
      {
        if (fRig.slice[s].tx == 1 && fRig.slice[s].in_use == 1)
        {
          txSlice = s;
          break;
        }
      }

      if (txSlice >= 0 &&
          fRig.slice[txSlice].mode == "CW" &&
          fRig.transmit.break_in == 1)
      {
        const unsigned __t   = (unsigned)(millis() % 0xFFFF);
        const unsigned __idx = (unsigned)CWIndex;

        // Only enqueue; actual send is done in Keyer_FlushFlexCwQueue()
        FlexQueueCwKeyFromISR(1, __t, __idx, (uint8_t)ClientMenuItem);
        CWIndex++;
      }
    }
  }

  ElementWait = true;
  KeyDown = true;

  if (SideTone && !AbortMsg)
  {
    tone(STPin, STFreq);
  }

  // do NOT clear g_KeyerTimingActive here; it should stay true until the
  // element + inter-element spacing is completed (StopDotTimerISR).
  return;
}
// end KeyTrans

// -----------------------------------------------------------------------------
// SendMsg() and SendMsgRaw()Shared helpers
// -----------------------------------------------------------------------------

// Abort/cleanup 
// Hard unkey helper used by failsafe and external aborts.
// This must immediately drop the key line and stop any CW timing.
// Hard unkey helper used by failsafe and external aborts.
// This must immediately drop the key line and stop any CW timing.
// Hard unkey helper used by failsafe and external aborts.
// This must immediately drop the key line and stop any CW timing.
static inline void Keyer_HardUnkeyNow()
{
  // Local key line
  KEY_OUT_LOW();          // Unkey transmitter output
  KeyDown             = false;
  ElementWait         = false;
  g_KeyerTimingActive = false;

  // Stop element timers / sidetone
  DotTimer.end();
  noTone(STPin);

  // Clear current message state (but do NOT touch AbortMsg here)
  MsgActive   = false;
  DotPressed  = false;
  DashPressed = false;
  CurElement  = 0;

  // Flex hard unkey: bypass queue to guarantee "cw key 0" reaches the radio
  if (KeyerOut == "ETHERNET" && fRig.connected)
  {
    const unsigned __t   = (unsigned)(millis() % 0xFFFFu);
    const unsigned __idx = (unsigned)CWIndex;
    const char*    __h   = fRig.Client_Handle[ClientMenuItem].c_str();
    FlexSendCwKey(0, __t, __idx, __h);
    CWIndex++;
  }
}

// Abort/cleanup entry used by the message sender and external aborts
static inline void _KeyerAbortAndCleanup()
{
  // Drop any in-progress element and clear message state
  Keyer_HardUnkeyNow();

  // If we are in raw/host mode (DXLog/WinKey), also drop any remaining
  // bytes in the keyer's internal TX queue so the host sees us as idle.
  if (!g_parseParams) {
    Keyer_TxQ_Flush();
  }

  // Now clear the abort flag and parser state
  AbortMsg = false;
  ProSign  = false;
  GotParm  = false;
}

// External hard abort hook used by the WinKey adapter (ESC from host etc.)
// External hard abort hook used by the WinKey adapter (ESC from host etc.)
void Keyer_AbortNow(void)
{
  // Mark the current message as aborted.
  // The main keyer/sender paths will observe AbortMsg and perform cleanup.
  AbortMsg = true;
}

// Common tail that can optionally restore speed after a message
static inline void _KeyerFinishMessage(bool restoreWpm)
{
  ProSign   = false;
  MsgActive = false;

  debugf("WK: FinishMessage restore=%d  AbortMsg=%d  bytesQueued=%u\n",
         (int)restoreWpm, (int)AbortMsg, (unsigned)Keyer_TxQ_Used());

  // When DXLog changes speed via <1C><nn>, that speed remains in effect for subsequent text
  // segments until the host changes it again — exactly like a genuine WinKeyer.
  if (restoreWpm && CWVal != CWValSave) {
    Keyer_Apply_Wpm(CWValSave, false);
  }
}

// -----------------------------------------------------------------------------
// Core sender: processes a raw byte stream. If doParse==true it will invoke
// ParseParm(String&, i) in sync with the String source (passed via srcStr).
// For raw input (DXLog/WK), doParse=false and srcStr==nullptr (zero alloc path).
// -----------------------------------------------------------------------------
static void _SendMsgCore(const char* data, size_t len, bool doParse, String* srcStr, bool restoreWpm)
{
  if (!data || len == 0) return;

  MsgActive = true;

  for (size_t i = 0; i < len; i++) {
    if (AbortMsg) { _KeyerAbortAndCleanup(); return; }

    char c = data[i];
    if (c == '\r' || c == '\n') continue;

    SendChar(c);

    // RAW path: never leave a dangling parameter state from '$'
    if (!doParse && GotParm) {
      GotParm = false;
    }

    if (doParse && GotParm) {
      unsigned int idx = (unsigned int)i;
      GoodParm = ParseParm(*srcStr, idx);
      i = idx;
    }
  }
  TM_PumpFast();
  _KeyerFinishMessage(restoreWpm);
}

// -----------------------------------------------------------------------------
// Public API: original String-based path (keeps $PARM behavior intact)
// -----------------------------------------------------------------------------
void SendMsg(String M)
{
  g_parseParams = true;
  _SendMsgCore(M.c_str(), M.length(), /*doParse=*/true, &M, /*restoreWpm=*/true);
  g_parseParams = false;
}

// -----------------------------------------------------------------------------
// Public API: zero-allocation raw path (used by WinKey queue service)
// -----------------------------------------------------------------------------
void SendMsgRaw(const char* data, size_t len)
{
  g_parseParams = false;
  _SendMsgCore(data, len, /*doParse=*/false, /*srcStr=*/nullptr, /*restoreWpm=*/false);
}

/***************************** SendChar ***************************/
void SendChar(char Ch)
{
  //  GetButton();
  //  GetMsgAbortBtn();

  if (!g_parseParams) {
    if (Ch == '$' || Ch == '+' || Ch == '-') {
      // Ignore silently in raw mode
      return;
    }
  }

  if (AbortMsg)
  {
    KeyDown     = false;
    ElementWait = false;
    ProSign     = false;
    GotParm     = false;
    GotBtn      = false;

    KEY_OUT_LOW();  // Unkey transmitter
    DotTimer.end();
    DotPressed  = false;
    DashPressed = false;
    CurElement  = 0;
    MsgActive   = false;
    noTone(STPin);
    return;
  }

  // Word space
  if (Ch == ' ')
  {
    WordSpace();
    return; // nothing else to do for a space
  }

  // Default: no prosign/cut-zero for normal characters
  if (Ch != '*') ProSign = false;

  // --- Character to MorseIndex mapping ---
  bool haveIndex = false;

  // Prosign start
  if (Ch == '*') {
    ProSign = true;
    // no elements to send for the marker itself
    return;
  }
  // Parameter marker
  else if (Ch == '$') {
    if (g_parseParams) {
      GotParm = true;
    }
    ProSign = false;
    return;
  }

  // 'Cut zero' (long dash)
  else if (Ch == '_') {
    CutZero    = true;
    MorseIndex = 41;   // table index for '_' (already present)
    haveIndex  = true;
  }
  // Punctuation mapped in the tables
  else if (Ch == '/') { MorseIndex = 36; haveIndex = true; }
  else if (Ch == '=') { MorseIndex = 37; haveIndex = true; }
  else if (Ch == '?') { MorseIndex = 38; haveIndex = true; }
  else if (Ch == ',') { MorseIndex = 39; haveIndex = true; }
  else if (Ch == '.') { MorseIndex = 40; haveIndex = true; }
  else if (Ch == '-') { MorseIndex = 42; haveIndex = true; } // hyphen "-....-"
  // Additional prosign-style symbols
  else if (Ch == '+') { MorseIndex = 43; haveIndex = true; }
  else if (Ch == '@') { MorseIndex = 48; haveIndex = true; }

  // Swedish and German extended letters:
  // Placeholders from TCP-filter:  '^' (Å), '{' (Ä), '}' (Ö), '~' (Ü)
  // Latin-1 direct (if any slipped through): Å/å=C5/E5, Ä/ä=C4/E4, Ö/ö=D6/F6, Ü/ü=DC/FC
  else if (Ch == '^' || (unsigned char)Ch == 0xC5 || (unsigned char)Ch == 0xE5) { // Å/å
    MorseIndex = 44; haveIndex = true;
#if WK_INFO_TRACE
    Serial.println(F("SC: map Å -> idx=44"));
#endif
  }
  else if (Ch == '{' || (unsigned char)Ch == 0xC4 || (unsigned char)Ch == 0xE4) { // Ä/ä
    MorseIndex = 45; haveIndex = true;
#if WK_INFO_TRACE
    Serial.println(F("SC: map Ä -> idx=45"));
#endif
  }
  else if (Ch == '}' || (unsigned char)Ch == 0xD6 || (unsigned char)Ch == 0xF6) { // Ö/ö
    MorseIndex = 46; haveIndex = true;
#if WK_INFO_TRACE
    Serial.println(F("SC: map Ö -> idx=46"));
#endif
  }
  else if (Ch == '~' || (unsigned char)Ch == 0xDC || (unsigned char)Ch == 0xFC) { // Ü/ü
    MorseIndex = 47; haveIndex = true;
#if WK_INFO_TRACE
    Serial.println(F("SC: map Ü -> idx=47"));
#endif
  }

  // Digits
  else if (isDigit(Ch)) {
    MorseIndex = (Ch - '0') + 26;  // 26..35
    haveIndex  = true;
  }
  // Letters A..Z (case-insensitive)
  else if (isAlpha(Ch)) {
    char U = (char)toupper((unsigned char)Ch);
    MorseIndex = U - 'A';          // 0..25
    haveIndex  = true;
  }

  // Unknown → treat like a word gap (skip silently)
  if (!haveIndex) {
    WordSpace();
    return;
  }

#if WK_INFO_TRACE
  {
    uint8_t c8 = (uint8_t)Ch;
    Serial.printf("SC: ch='%c' 0x%02X -> idx=%d len=%d val=",
                  (c8>=32 && c8<127)?Ch:'.', c8, MorseIndex, MorseLen[MorseIndex]);
    for (int bi = MorseLen[MorseIndex]-1; bi >= 0; --bi) {
      Serial.print(bitRead(MorseVal[MorseIndex], bi) ? '1' : '0');
    }
    Serial.println();
  }
#endif

  // --- Send selected character using table-driven elements ---
  // NOTE: MorseLen/MorseVal must include indices up to at least 47 (Å,Ä,Ö,Ü)
  for (long i = 0; i < MorseLen[MorseIndex]; i++)
  {
    // Safety checks for control markers (shouldn't happen here, but keep legacy guards)
    if (Ch == ' ' || Ch == '*' || Ch == '$')
    {
      if (AbortMsg)
      {
        i           = MorseLen[MorseIndex];
        GotParm     = false;
        ElementWait = false;
        return;
      }
      break;
    }

    // 0 = dot, 1 = dash (LSB-first)
    if (bitRead(MorseVal[MorseIndex], i) == 0)
    {
      if (AbortMsg) { ElementWait = false; return; }

      // send dot
      DotTimer.begin(UnKeyISR, ElementLen);
      KeyTrans();
      // debug("."); // optional
    }
    else
    {
      if (AbortMsg) { ElementWait = false; return; }

      // send dash (or cut-zero long dash once)
      if (CutZero)
      {
        CharSpace();
        DotTimer.begin(UnKeyISR, (ElementLen * DOT_TO_DASH) + (ElementLen * CHAR_GAP_FACTOR));
        CutZero = false;
      }
      else
      {
        DotTimer.begin(UnKeyISR, ElementLen * DOT_TO_DASH);
      }
      KeyTrans();
      // debug("_"); // optional
    }

    while (ElementWait)
    {
      if (AbortMsg) { ElementWait = false; break; }

      TM_PumpFast();
      Keyer_ServiceDeferred();  // pick up deferred work while waiting

      if (KeyerOut == "ETHERNET")
      {
        Keyer_FlushFlexCwQueue();
      }
    }

  } // end for

  // ON-AIR-COMPLETE
  // Notify host (e.g., DXLog) that a character actually has been sent
  WK_OnCharEcho((uint8_t)Ch);

  CharSpace();
  Keyer_ServiceDeferred();
  //ReadCWMicEnc();  // allow speed changes during message playback
  return;
}


/***************************** ParseParm ***************************/
int ParseParm(String &M, unsigned int &ChIDX)  // pass in pointer to current char in msg string
{
  unsigned long D = 0;

  bool GotPermSpd = false;

  int j = 0;

  //int MNum;
  //char MNumChar[2] = {'\0','\0'};

  char PauseParm[6] = { '\0', '\0', '\0', '\0', '\0', '\0' };

  //unsigned int R = 0;
  //bool RptActive = false;
  char RptParm[3]   = { '\0', '\0', '\0' };
  char SpeedParm[3] = { '\0', '\0', '\0' };

  char tmpChr[5] = { 0, 0, 0, 0, 0 };
  String tmpStr;

  //int  Spd = CWVal;  //  Save speed so plain $S will return to it

  for (ChIDX = ChIDX + 1; ChIDX < M.length(); ChIDX++)
  {
    //GetMsgAbortBtn();
    if (AbortMsg)
    {
      MsgActive = false;
      KEY_OUT_LOW();  // Unkey transmitter
      DotTimer.end();
      DotPressed  = false;
      DashPressed = false;
      CurElement  = 0;
      ChIDX       = M.length();
      return 1;
    }

    debug("M[ChIDX]: ");
    debugln(M[ChIDX]);
    debug("char(M[ChIDX]): ");
    debugln(char(M[ChIDX]));
    switch (char(M[ChIDX]))
    {
      case ' ':  // end of parm
        GotParm = false;

        return true;
        break;

      case 0x0A:  // end of parm
        GotParm = false;

        return true;
        break;

      case 0x0D:  // end of parm
        GotParm = false;

        return true;
        break;

      case 'C':  // Insert MyCall (letters + digits) without rewriting M in-place
        // Do not rewrite M here; emit characters directly to avoid data/len desync.
        GotParm = false;

        // Emit callsign exactly as stored in MyCall (including digits).
        {
          char callBuf[16]; // adjust if your callsign buffer can be longer
          MyCall.toCharArray(callBuf, sizeof(callBuf));
          for (unsigned int i = 0; i < strlen(callBuf); ++i) {
            SendChar(callBuf[i]);
          }
        }

        // Advance parser to the next character AFTER 'C'
        // When _SendMsgCore() sets i = ChIDX, the for-loop's i++ will move to the next char.
        // So we keep ChIDX as-is (pointing at 'C'), do NOT try to rewind like "-= 2".
        // (The old "ChIDX -= 2" was the root cause of truncated output.)
        return true;
        break;

      case 'N':  // Serial Number
        debugln(M);
        debug("ChIDX: ");
        debugln(ChIDX);
        if (M[ChIDX + 1] == ' ')
        {
          // send SerNum
          debugln("blank");
          tmpStr = String(SerNum);
          tmpStr.toCharArray(tmpChr, 5);
          for (unsigned int i = 0; i < tmpStr.length(); i++)
          {
            SendChar(tmpChr[i]);
          }
          SerNum += 1;
          TM_SerNum_SaveEEPROM_Throttled();
        }
        else if (M[ChIDX + 1] == '+')
        {
          debugln("+");
          debugln(M);
          SerNum += 1;
          TM_SerNum_SaveEEPROM_Throttled();
          ChIDX += 1;
        }
        else if (M[ChIDX + 1] == '-')
        {
          debugln("-");
          SerNum -= 1;

          if (SerNum < 1)
          {
            SerNum = 1;
          }
          TM_SerNum_SaveEEPROM_Throttled();
          ChIDX += 1;
        }
        else if (M[ChIDX + 1] == 'R')
        {
          debugln("R");
          debugln(M);

          if (SerNum > 1)
          {
            tmpStr = String(SerNum - 1);
          }
          else
          {
            tmpStr = "1";
          }

          tmpStr.toCharArray(tmpChr, 5);
          for (unsigned int i = 0; i < tmpStr.length(); i++)
          {
            SendChar(tmpChr[i]);
          }
          ChIDX += 1;
        }

        MenuItem[CWMenu][5] = "Set contest serial number: " + String(SerNum);
        DispSerNum();
        GotParm = false;
        return true;

        break;

      case 'P':  // pause milliseconds
        if (AbortMsg)
        {
          GotParm = false;
          break;
        }
        for (int j = 0; j < 6; j++)
        {
          if (isDigit(M[ChIDX + 1 + j]))
          {
            PauseParm[j] = M[ChIDX + 1 + j];
          }
          else
          {
            D = atol(PauseParm);
            DelayMillis(D);
            ChIDX   = ChIDX + j + 1;  // point to next char to send in msg string
            GotParm = false;
            return true;
          }
        }  // end for
        break;

      case 'R':  // repeat message
        if (!RptActive && !AbortMsg)
        {
          for (j = 0; j < 4; j++)
          {
            if (isDigit(M[ChIDX + 1 + j]))
            {
              RptParm[j] = M[ChIDX + 1 + j];
              R          = atoi(RptParm);
              RptActive  = true;
              break;
            }
            else
            {
              ChIDX   = -1;  // beginning of msg
              GotParm = false;
              return true;
            }
          }  // end for

          //            if(R == 0)
          //            {
          //              ChIDX = ChIDX + 1 + j;
          //              GotParm = false;
          //              RptActive = false;
          //              return true;
          //            }
        }  // end if(!RptActive)

        if (R > 0)
        {
          R -= 1;
          ChIDX   = -1;  // beginning of msg
          GotParm = false;
          //            debug("R = ");
          //            debugln(R);
          return true;
        }
        else
        {
          ChIDX     = ChIDX + 1 + j;
          GotParm   = false;
          RptActive = false;
          return true;
        }

        break;

      case 'S':
        debugln("Case S");
        for (int i = 0; i < 3; i++)
        {
          SpeedParm[i] = '\0';  // ensure clean parse (no leftover digits)
        }

        for (j = 0; j < 3; j++)
        {
          if (AbortMsg)
          {
            GotParm = false;
            break;
          }

          if (M[ChIDX + 1 + j] == 'P')
          {
            debugln("Got P");
            GotPermSpd = true;
            ChIDX += 1;
          }

          if (isDigit(M[ChIDX + 1 + j]))
          {
            SpeedParm[j] = M[ChIDX + 1 + j];
          }
          else
          {
            break;
          }
        }

        {
          int spd = atoi(SpeedParm);
          if (spd < 1)
          {
            // Plain $S with no digits: restore baseline CWValSave
            spd = CWValSave;
          }

          // Use central helper to keep CWVal/WPM/ElementLen/encoders in sync
          Keyer_Apply_Wpm(spd, /*preserveBaseline=*/!GotPermSpd);

          if (GotPermSpd)
          {
            GotPermSpd = false;

            if (fRig.connected)
            {
              fRig.setCwSpeed(CWVal);
            }

            debug("CWVal (perm): ");
            debugln(CWVal);
          }
        }

        ChIDX   = ChIDX + j + 1;  // point to next char to send in msg string
        GotParm = false;
        return true;

      default:  // no valid parm found
        debugln("Default");
        ChIDX   = ChIDX + 1;
        GotParm = false;
        return false;
        break;

    }  // end switch
  }    //end for
  return true;
}  // end ParseParm

/***************************** CharSpace ***************************/
void CharSpace()
{
  if (ProSign || (AbortMsg))
  {
    StopDotTimerISR();
    return;
  }

  ElementWait = true;

  StartUnKeyTimer_us(ElementLen);

  while (ElementWait)
  {
    TM_PumpFast();

    if (KeyerOut == "ETHERNET")
    {
      Keyer_FlushFlexCwQueue();
    }
  }
  Keyer_ServiceDeferred();
}  // end CharSpace

/***************************** WordSpace ***************************/
void WordSpace()
{
  if (AbortMsg)
  {
    ProSign = false;
    //    StopDotTimerISR();
    return;
  }

  ProSign     = false;
  ElementWait = true;
  // Serial.print("ElementLen: ");
  // Serial.println(ElementLen);
  StartUnKeyTimer_us((ElementLen * WordSp));

  while (ElementWait)
  {
    TM_PumpFast();

    if (KeyerOut == "ETHERNET")
    {
      Keyer_FlushFlexCwQueue();
    }
  }
  Keyer_ServiceDeferred();
}  // end WordSpace

/***************************** DelayMillis ***************************/
void DelayMillis(unsigned long D)
{
  unsigned long TempTimer;

  TempTimer = millis();

  while (millis() < TempTimer + D)  // Allow paddle to stop the message during a delay
  {
    //GetMsgAbortBtn();

    //if (AbortMsg)
    //{
      // delay(250); Lets try without this
    //}

    if ((digitalRead(DotPin) + digitalRead(DashPin) < 2) || AbortMsg)
    {
      AbortMsg    = true;
      DotPressed  = false;
      DashPressed = false;
      DotPinVal   = 1;
      DashPinVal  = 1;

      break;
    }
  }
  if (KeyerOut == "ETHERNET")
  {
    Keyer_FlushFlexCwQueue();
  }
}  // end DelayMillis

/*********************** SendFlexMsg *********************/
void SendFlexMsg(int M)
{
  if (!fRig.connected) {
    return;
  }

  // Find active TX slice in CW mode
  for (int slice = 0; slice < fRig.nMaxSlice; ++slice)
  {
    if (fRig.slice[slice].in_use == 1 &&
        fRig.slice[slice].tx == 1 &&
        fRig.slice[slice].mode == "CW")
    {
      // Build the macro command and send it via FlexRig
      char buf[32];
      snprintf(buf, sizeof(buf), "cwx macro send %d", M);
      fRig.send(buf);   // uses the const char* overload

      // If you really need throttling, you can reintroduce a small delay here.
      // delay(5);

      break;
    }
  }
}

/*********************** Interrupt Service Routines *********************/
/*********************** UnKeyISR *********************/
// NOTE: Do not clear g_KeyerTimingActive here; we remain timing-critical
// until StopDotTimerISR() ends the inter-element spacing.
void UnKeyISR()
{
  //    debugln("UnKeyISR");

  if (StraightKeyActive) {
    ElementWait = false;
    return;
  }
  if (!AbortMsg) {
    StartDotTimer_us(ElementLen);
    ElementWait = true;
  }

  if (KeyerOut == "LOCAL")
  {
    KEY_OUT_LOW();  // Unkey transmitter
  }
  else if (KeyerOut == "ETHERNET" && fRig.connected)
  {
    int txSlice = -1;
    for (int s = 0; s < fRig.nMaxSlice; ++s)
    {
      if (fRig.slice[s].tx == 1 && fRig.slice[s].in_use == 1)
      {
        txSlice = s;
        break;
      }
    }

    if (txSlice >= 0 &&
        fRig.slice[txSlice].mode == "CW" &&
        fRig.transmit.break_in == 1)
    {
      const unsigned __t   = (unsigned)(millis() % 0xFFFF);
      const unsigned __idx = (unsigned)CWIndex;

      FlexQueueCwKeyFromISR(0, __t, __idx, (uint8_t)ClientMenuItem);
      CWIndex++;
    }
  }

  KeyDown = false;
  noTone(STPin);

  if (AbortMsg)
  {
    StopDotTimerISR();
  }
  else
  {
    Keyer_DeferKickFromISR();
  }

  //  debugln("Exit UnKeyISR");

  return;
}  //end UnKeyISR

void StopDotTimerISR()
{
  // End of element space: resume keyer immediately
  DotTimer.end();
  ElementWait = false;

  // We are now past the timing-critical part of this element cycle.
  g_KeyerTimingActive = false;

  Keyer_DeferKickFromISR();
  return;
}

void DotKeyISR()
{
  static volatile uint32_t s_lastUsDot = 0;
  if (!isrDebounceElapsed(s_lastUsDot)) {
    Keyer_DeferKickFromISR();
    attachInterrupt(digitalPinToInterrupt(DotPin), DotKeyISR, CHANGE);
    return;
  }

  detachInterrupt(DotPin);

  // debugln("DotKeyISR");

  if (MsgActive)
  {
    // Abort active CW message just like a WinKey/host ESC abort
    Keyer_AbortNow();
    WPM = CWValSave;
    Keyer_Recalc_Timing();

    attachInterrupt(digitalPinToInterrupt(DotPin), DotKeyISR, CHANGE);
  }
  else
  {
    DotDown = true;

    if (KeyMode == "C")  // Cootie/Sideswiper
    {
      TimeIt = millis();
      while (millis() - TimeIt < 20)  // up to 20 ms debounce time
      {
        DotPinVal = digitalRead(DotPin);
        delayMicroseconds(5);
        DashPinVal = digitalRead(DashPin);
        delayMicroseconds(5);

        if (DotPinVal == 0 || DashPinVal == 0)
        {
          break;  // No need to keep reading.
        }
      }

      if (DotPinVal == 0 || DashPinVal == 0)
      {
        StraightKeyActive = true;
        KeyTrans();
      }
      else
      {
        StraightKeyActive = false;
        UnKeyISR();
      }

      attachInterrupt(digitalPinToInterrupt(DotPin), DotKeyISR, CHANGE);
      attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);

      Keyer_ActivityTick();
      return;
    }

    if (CurElement == Dash)
    {
      DotPressed = true;
    }

    // Original behavior: call Keyer() directly from ISR
    Keyer_DeferKickFromISR();
  }

  Keyer_ActivityTick();

  // debugln("Exit DotKeyISR");
  return;
}


/*********************** DashKeyISR *********************/
void DashKeyISR()
{
  static volatile uint32_t s_lastUsDash = 0;
  if (!isrDebounceElapsed(s_lastUsDash)) {
    Keyer_DeferKickFromISR();
    attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);
    return;
  }

  detachInterrupt(DashPin);

  // debugln("DashKeyISR");

  if (MsgActive)
  {
    // Abort active CW message just like a WinKey/host ESC abort
    Keyer_AbortNow();
    WPM = CWValSave;
    Keyer_Recalc_Timing();

    attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);
  }
  else
  {
    DashDown = true;

    if (KeyMode == "C")  // Cootie/Sideswiper
    {
      TimeIt = millis();
      while (millis() - TimeIt < 20)  // up to 20 ms debounce time
      {
        DashPinVal = digitalRead(DashPin);
        delayMicroseconds(5);
        DotPinVal = digitalRead(DotPin);
        delayMicroseconds(5);

        if (DotPinVal == 0 || DashPinVal == 0)
        {
          break;  // No need to keep reading.
        }
      }

      if (DotPinVal == 0 || DashPinVal == 0)
      {
        StraightKeyActive = true;
        DashDown          = false;
        KeyTrans();
      }
      else
      {
        StraightKeyActive = false;
        UnKeyISR();
      }

      Keyer_DeferKickFromISR();
      attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);
      attachInterrupt(digitalPinToInterrupt(DotPin),  DotKeyISR,  CHANGE);

      Keyer_ActivityTick();
      return;

    }

    if (KeyMode == "S")  // Semiautomatic/Bug
    {
      TimeIt = micros();
      while (micros() - TimeIt < 100 && DashDown)
      {
        DashPinVal = digitalRead(DashPin);
        delayMicroseconds(5);
        if (DashPinVal == LOW)
        {
          DashDown = false;
          break;
        }
      }

      if (DashPinVal == LOW)
      {
        StraightKeyActive = true;
        KeyTrans();
      }
      else
      {
        StraightKeyActive = false;
        UnKeyISR();
      }

      attachInterrupt(digitalPinToInterrupt(DashPin), DashKeyISR, CHANGE);
      attachInterrupt(digitalPinToInterrupt(DotPin),  DotKeyISR,  CHANGE);

      Keyer_ActivityTick();
      return;

    }

    if (CurElement == Dot)
    {
      DashPressed = true;
    }

    // Original behavior: call Keyer() directly from ISR
    Keyer_DeferKickFromISR();
  }

  Keyer_ActivityTick();

  // debugln("Exit DashKeyISR");
  return;
}


/*********************** StraightKeyISR *********************/
void StraightKeyISR()
{
  // Abort active message first
  if (MsgActive)
  {
    // Abort active CW message just like a WinKey/host ESC abort
    Keyer_AbortNow();
    WPM = CWValSave;
    Keyer_Recalc_Timing();
    // Re-arm straight key interrupt and leave
    attachInterrupt(digitalPinToInterrupt(StraightKeyPin), StraightKeyISR, CHANGE);
    return;
  }
  
  // Prevent re-entrancy while we settle the contact
  detachInterrupt(digitalPinToInterrupt(StraightKeyPin));

  // Quick settle: two reads with a short delay; if they differ, read once more
  int v1 = digitalRead(StraightKeyPin);
  delayMicroseconds(150);
  int v2 = digitalRead(StraightKeyPin);
  if (v1 != v2) {
    delayMicroseconds(150);
    v2 = digitalRead(StraightKeyPin);
  }
  StraightKeyPinVal = v2; // keep your original variable updated

  if (StraightKeyPinVal == LOW)
  {
    // Key down
    if (!StraightKeyActive)
    {
      StraightKeyActive = true;

      if (XmitOn && !AbortMsg)
      {
        if (KeyerOut == "LOCAL")
        {
          // Direct keying of external rig
          KEY_OUT_HIGH();
        }
        else if (KeyerOut == "ETHERNET" && fRig.connected)
        {
          // Key Flex via TCP (straight key down)
          int txSlice = -1;
          for (int s = 0; s < fRig.nMaxSlice; ++s)
          {
            if (fRig.slice[s].tx == 1 && fRig.slice[s].in_use == 1)
            {
              txSlice = s;
              break;
            }
          }

          if (txSlice >= 0 &&
              fRig.slice[txSlice].mode == "CW" &&
              fRig.transmit.break_in == 1)
          {
            const unsigned __t   = (unsigned)(millis() % 0xFFFFu);
            const unsigned __idx = (unsigned)CWIndex;
            FlexQueueCwKeyFromISR(1, __t, __idx, (uint8_t)ClientMenuItem);
            CWIndex++;
          }
        }
      }

      if (SideTone && !AbortMsg)
      {
        tone(STPin, STFreq);
      }
    }
  }
  else
  {
    // Key up
    if (StraightKeyActive)
    {
      StraightKeyActive = false;

      if (KeyerOut == "LOCAL")
      {
        KEY_OUT_LOW();
      }
      else if (KeyerOut == "ETHERNET" && fRig.connected)
      {
        // Key up Flex via TCP
        int txSlice = -1;
        for (int s = 0; s < fRig.nMaxSlice; ++s)
        {
          if (fRig.slice[s].tx == 1 && fRig.slice[s].in_use == 1)
          {
            txSlice = s;
            break;
          }
        }

        if (txSlice >= 0 &&
            fRig.slice[txSlice].mode == "CW" &&
            fRig.transmit.break_in == 1)
        {
          const unsigned __t   = (unsigned)(millis() % 0xFFFFu);
          const unsigned __idx = (unsigned)CWIndex;
          FlexQueueCwKeyFromISR(0, __t, __idx, (uint8_t)ClientMenuItem);
          CWIndex++;
        }
      }

      noTone(STPin);
    }
  }

  // Mark keyer activity for screensaver
  Keyer_ActivityTick();

  // Re-arm interrupt on both edges
  attachInterrupt(digitalPinToInterrupt(StraightKeyPin), StraightKeyISR, CHANGE);
}
