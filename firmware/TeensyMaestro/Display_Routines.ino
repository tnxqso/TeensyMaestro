#include "tm_sketch_api.h"
#include "ui_boot.h"
#include "ClockWidget.h"
#include "Display_Driver.h"
#include "ui_clockpanel.h"

extern bool gFixedEndpointUsed;
extern char gFixedEndpointLabel[64];   // existing global in main sketch

// Tracks if the clock widget was visible last frame (connected && Slice B free).
static bool sClockWasVisible = false;

// Call this BEFORE painting Slice B when it transitions Free -> Active.
// It clears any pixels the clock may have left in that region, exactly once.
void ClockWidget_OnSliceBActivated() {
  if (sClockWasVisible) {
    UI_Clock_Clear();   // one-time clear so Slice B can paint over a clean area
    sClockWasVisible = false;
  }
}

// Global UI re-entrancy flag (single definition)
volatile bool sInRefreshScreen = false;
volatile bool SMeterDirty[2] = { true, true }; // force an initial draw per slice


inline void SMeter_InvalidateAll()
{
  SMeterDirty[0] = true;
  SMeterDirty[1] = true;
}

// Mark a single slice dirty (0 = A, 1 = B)
inline void SMeter_InvalidateSlice(int sliceIdx)
{
  if (sliceIdx == 0 || sliceIdx == 1) SMeterDirty[sliceIdx] = true;
}


// Draw/clear the clock widget at the end of a paint pass.
// We ONLY draw when the widget should be visible (connected && Slice B free).
// We DO NOT clear here anymore — the one-time clear is done on the Active transition.
static void ClockWidget_FrameTail() {
  if (!UIState::isConnected()) {
    sClockWasVisible = false;   // disconnected => widget not visible
    return;
  }

  const bool visibleNow = UI_SliceB_Free() && !Splash && !MenuActive;

  if (visibleNow) {
    // First frame after becoming visible: clear the panel area defensively,
    // then force a full (layout + paint) pass.
    if (!sClockWasVisible) {
      UI_Clock_Clear();       // defensive cleanup of the Slice-B panel region
      ClockWidget_Invalidate();
    }
    ClockWidget_UiReady();     // idempotent draw
  }

  sClockWasVisible = visibleNow;
}

void RenderSlicePassive(const int sliceId)
{
  // Guard against bad ids
  if (sliceId < 0 || sliceId >= fRig.nMaxSlice) return;

  debug("RenderSlicePassive for slice ");
  debugln(sliceId);

  // Make the slice non-interactive (headless in UI terms).
  // We deliberately leave cosmetic fields (audio/mute/pan) unchanged.
  fRig.slice[sliceId].in_use = 0;   // <-- assignment, not comparison
  fRig.slice[sliceId].active = 0;
  fRig.slice[sliceId].tx     = 0;

  // Reflect the change to the UI-facing state immediately.
  UI_SyncSliceFromRig(sliceId);

  // Paint the new state. DispSlice() will handle slice headers, etc.,
  // and at the frame tail the ClockWidget will show itself if B just freed.
  DispSlice();
}

void SplashScreen() {
  UI_Boot::showInfo();
}


/***************************** ResetScreenSaver ***************************/
void ResetScreenSaver(const char* reason /*= nullptr*/)
{
  #if 0
  Serial.print("ResetScreenSaver: reason=");
  Serial.print(reason ? reason : "(null)");
  Serial.print(" screenSaveActive=");
  Serial.println(ScreenSaveActive ? "1" : "0");
  #endif

  ScreenSaveTimer = millis();
  if (ScreenSaveActive)
  {
    // FIX: Restart the Keyer Engine.
    // After long periods of inactivity, the engine's internal timing 
    // or interrupts may stop/freeze. This forces a logic restart.
    g_keyerEngine.begin();

    // Safety: Restore the current WPM speed immediately, 
    // as begin() typically resets the engine to a default speed.
    g_keyerEngine.setWpm((uint8_t)CWVal);

    muxA.digitalWrite(IOX_TFT_LCD, HIGH);  // Turn panel back on immediately
    ScreenSaveActive = false;
    Splash           = false;

    RefreshScreen();
  }
}

// Apply Slice-B layout to the clock panel.
// Keeps ui_clockpanel independent from MidScreen math.
static inline void ApplyClockPanelLayoutSliceB() {
  UIClockLayout lay;
  lay.freqX     = Freq_X + (MidScreen * B);  // Slice B anchor
  lay.freqY     = Freq_Y;
  lay.freqWidth = Freq_Width;
  lay.midScreen = MidScreen;                 // kept for compatibility (ignored by panel)
  ui_clockpanel_set_layout(lay);
}

// Sync UI-facing state from the underlying rig model.
// Safe to call as often as you like (idempotent).
static inline void UI_SyncFromRig() {
  UIState::setConnected(fRig.connected);
  UIState::setSliceInUse(A, fRig.slice[A].in_use == 1);
  UIState::setSliceInUse(B, fRig.slice[B].in_use == 1);
}

// Narrow helper when only one slice changes.
inline void UI_SyncSliceFromRig(int sliceId) {
  if (sliceId == A || sliceId == B) {
    UIState::setSliceInUse(sliceId, fRig.slice[sliceId].in_use == 1);
  }
}

// ===== [TM DISPLAY RefreshScreen] BEGIN =====
void RefreshScreen()
{
  if (sInRefreshScreen) return;

  sInRefreshScreen = true;
  MenuActive = false;
  Splash     = false;

  tft.setRotation(3);  // just in case...
  tft.fillScreen(COLOR_BLACK);

  // Always prepare clock panel layout for the right (Slice B) half.
  ApplyClockPanelLayoutSliceB();

  // Sync UI-facing state with the rig model
  UI_SyncFromRig();

  // In Keyer-Only mode (radio disconnected), force UI state to reflect that,
  // so the clock panel can claim the right-hand area via UI_SliceB_Free().
  UIState::setKeyerOnly(!fRig.connected);

  if (fRig.connected)
  {
    // Draw split lines as usual for connected state
    tft.drawLine(240, 0, 240, 240, COLOR_DARKGREY);  // vertical line down center
    tft.drawLine(0, 240, 480, 240, COLOR_DARKGREY);  // horizontal line near bottom

    SMeterLast[A] = 999;  // force redraw of S meter graphic
    SMeterLast[B] = 999;

    // --- Ensure Slice B clock remnants are cleared before drawing meters ---
    if (fRig.slice[B].in_use == 1) {
      // If the clock was visible last frame and B is about to paint UI,
      // perform the one-time clear NOW (before meters/scale/UI).
      ClockWidget_OnSliceBActivated();
      // Also guarantee the B S-meter will fully rebuild this frame:
      SMeter_InvalidateSlice(B);
    }

    // --- Allow the first S-meter pass to draw immediately after a full refresh ---
    SMTimeIt = 0;
    DispSMeter();
    DispSMeterScale();

    DispSlice();

    for (int Slice = 0; Slice < 2; Slice++)
    {
      if (fRig.slice[Slice].in_use == 1 && !MenuActive)
      {
        DispTX(Slice);
        DispNB(Slice);
        DispNR(Slice);
        DispMute(Slice);
        DispMode(Slice);
        DispFrq(Slice);
        DispLock(Slice);
        DispAGC(Slice);
        DispVol(Slice);
        DispFilter(Slice);
        DispRIT(Slice);
        DispXIT(Slice);
      }

      DispStep(Slice);
    }

    DispLicense();
    DispRF();
    DispProfile();
  }
  else
  {
    // Keyer-Only layout (radio disconnected): left panel shows keyer info;
    // right panel is reserved for the clock widget (via UI_SliceB_Free()).
    DispKeyerOnlyScreen();
  }

  switch (Encoder_9)
  {
    case Enc9_CWSpeed:  // CW Speed
      DispCWSpeed();
      break;

    case Enc9_MicGain:  // Mic Gain
      DispMicGain();
      break;

    case Enc9_RFPower:  // RF Power
      DispRF();
      DispCWSpeed();
      break;

    case Enc9_TunePower:  // Tune Power
      DispTune();
      break;

    case Enc9_WNBLevel:  // WNB Level
      DispWNB();
      break;

    case Enc9_MonLevel:  // Mon Level
      DispMonLevel();
      break;

    case Enc9_VOXLevel:  // VOX Level
      DispVOXLevel();
      break;

    case Enc9_VOXDelay:  // VOX Delay
      DispVOXDelay();
      break;

    case Enc9_Band:  // Band
      DispCWSpeed();
      break;
  }

  sInRefreshScreen = false;

  // Post-frame overlays
  DispSerNum();
  ClockWidget_FrameTail();
}

// Common slice header drawing function (active/passive)
static inline void DrawSliceHeader(const int sliceId, bool isActive)
{
  const bool isA = (sliceId == A);
  const int  x   = isA ? SliceA_X : SliceB_X;
  const int  y   = isA ? SliceA_Y : SliceB_Y;

  // Slice label: use index_letter if non-empty, otherwise default to "A"/"B"
  String label = fRig.slice[sliceId].index_letter;
  if (label.length() == 0) label = isA ? "A" : "B";

  // Clear header area
  tft.fillRect(x, y, SliceNameWidth, FontHeight_A24, COLOR_BLACK);

  // Choose color depending on active/passive state
  if (isActive) {
    // Active style (replace with your current active color/font settings if needed)
    tft.setTextColor(COLOR_CYAN); // This was yellow before but cyan is more readable next to the yellow mode text
  } else {
    // Passive/grey style
    tft.setTextColor(COLOR_DARKGREY);
  }

  tft.setFont(Arial_24_Bold);
  tft.setCursor(x, y);
  tft.println(label);
}

/*********************** DispSlice *********************/
void DispSlice()
{
  // Do not redraw if UI should be suppressed
  if (!fRig.connected) return;
  if (Splash || MenuActive) return;

  const bool aActive   = (fRig.slice[A].active == 1);
  const bool bActive   = (fRig.slice[B].active == 1);
  const bool anyActive = (aActive || bActive);

  // Keep UI-facing occupancy in sync on every slice paint.
  UI_SyncFromRig();

  // --- Headers --------------------------------------------------------------
  if (anyActive) {
    // Track which slice is active (preserves your behavior)
    SliceActiveVal = aActive ? A : B;

    // Draw both headers via the shared helper
    DrawSliceHeader(A, aActive);
    DrawSliceHeader(B, bActive);

    // Preserve your "same-letter highlight" behavior:
    // If both slices are in use and share the same index_letter,
    // overlay the *inactive* header text in yellow (no area clear).
    if (fRig.slice[A].in_use == 1 && fRig.slice[B].in_use == 1 &&
        fRig.slice[A].index_letter == fRig.slice[B].index_letter)
    {
      const int  inactive = aActive ? B : A;
      const bool isA      = (inactive == A);
      const int  x        = isA ? SliceA_X : SliceB_X;
      const int  y        = isA ? SliceA_Y : SliceB_Y;

      String label = fRig.slice[inactive].index_letter;
      if (label.length() == 0) label = isA ? "A" : "B";

      tft.setTextColor(COLOR_YELLOW);   // overlay highlight
      tft.setFont(Arial_24_Bold);
      tft.setCursor(x, y);
      tft.println(label);
    }
  } else {
    // Fallback: no active slice → draw both headers in passive style
    // Keeps "A/B" visible even when slices are inactive/headless.
    DrawSliceHeader(A, false);
    DrawSliceHeader(B, false);
  }

  DispProfile();

  // Render clock at the end of event-driven slice paints, but NEVER during RefreshScreen(),
  // because RefreshScreen() will render lots more after calling DispSlice().
  if (!sInRefreshScreen) {
    ClockWidget_FrameTail();
  }  
}


/*********************** DispTX *********************/
void DispTX(int Slice)
{
  if (!Splash && !MenuActive)
  {
    if (fRig.slice[Slice].tx == 1)
    {
      tft.setFont(Arial_24_Bold);
      tft.fillRoundRect(TXind_X - 4 + (MidScreen * Slice), TXind_Y, 51, FontHeight_A24 - 5, 5, COLOR_RED);
      tft.setCursor(TXind_X + (MidScreen * Slice), TXind_Y);
      tft.setTextColor(COLOR_WHITE);
      tft.print("TX");
    }
    else
    {
      tft.setFont(Arial_24_Bold);
      tft.fillRoundRect(TXind_X - 4 + (MidScreen * Slice), 0, 51, FontHeight_A24 - 5, 5, COLOR_BLACK);
      tft.setCursor(TXind_X + (MidScreen * Slice), TXind_Y);
      tft.setTextColor(COLOR_DARKGREY);
      tft.print("TX");
    }
  }
}

/*********************** DispNB *********************/
void DispNB(int Slice)
{
  if (!Splash && !MenuActive && fRig.slice[Slice].in_use == 1)
  {
    //tft.drawRect(NB_X + (MidScreen * Slice), NB_Y, NB_Width, FontHeight_A12 - 2, COLOR_WHITE);
    tft.fillRect(NB_X + (MidScreen * Slice), NB_Y, NB_Width, FontHeight_A12 - 2, COLOR_BLACK);
    tft.setFont(Arial_12_Bold);
    tft.setCursor(NB_X + (MidScreen * Slice), NB_Y);

    if (fRig.slice[Slice].nb == 1)
    {
      tft.setTextColor(COLOR_YELLOW);
    }
    else
    {
      tft.setTextColor(COLOR_DARKGREY);
    }

    if (SetNB[Slice])
    {
      tft.drawRect(NB_X - 1 + (MidScreen * Slice), NB_Y - 2, NB_Width + 3, FontHeight_A12, COLOR_WHITE);
    }
    else
    {
      tft.drawRect(NB_X - 1 + (MidScreen * Slice), NB_Y - 2, NB_Width + 3, FontHeight_A12, COLOR_BLACK);
    }

    tft.print("NB ");
    tft.print(fRig.slice[Slice].nb_level);
  }
}

/*********************** DispNR *********************/
void DispNR(int Slice)
{
  if (!Splash && !MenuActive && fRig.slice[Slice].in_use == 1)
  {
    tft.fillRect(NR_X + (MidScreen * Slice), NR_Y, NR_Width, FontHeight_A12 - 1, COLOR_BLACK);
    tft.setFont(Arial_12_Bold);
    tft.setCursor(NR_X + (MidScreen * Slice), NR_Y);

    if (fRig.slice[Slice].nr == 1)
    {
      tft.setTextColor(COLOR_YELLOW);
    }
    else
    {
      tft.setTextColor(COLOR_DARKGREY);
    }

    if (SetNR[Slice])
    {
      tft.drawRect(NR_X - 1 + (MidScreen * Slice), NR_Y - 2, NR_Width + 3, FontHeight_A12, COLOR_WHITE);
    }
    else
    {
      tft.drawRect(NR_X - 1 + (MidScreen * Slice), NR_Y - 2, NR_Width + 3, FontHeight_A12, COLOR_BLACK);
    }

    tft.print("NR ");
    tft.print(fRig.slice[Slice].nr_level);
  }
}

/*********************** DispFrq *********************/
void DispFrq(int Slice)
{
  String Freq[2];

  if (fRig.slice[Slice].in_use == 1 && !Splash && !MenuActive)
  {
    Freq[Slice] = String(fRig.slice[Slice].RF_frequency);

    if (Freq[Slice] > 0)
    {
      if (fRig.slice[Slice].RF_frequency > 9999999999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 5) + '.' += Freq[Slice].substring(5, 8) + '.' + Freq[Slice].substring(8, 11);
      }
      else if (fRig.slice[Slice].RF_frequency > 999999999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 4) + '.' += Freq[Slice].substring(4, 7) + '.' + Freq[Slice].substring(7, 10);
      }
      else if (fRig.slice[Slice].RF_frequency > 99999999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 3) + '.' += Freq[Slice].substring(3, 6) + '.' + Freq[Slice].substring(6, 9);
      }
      else if (fRig.slice[Slice].RF_frequency > 9999999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 2) + '.' += Freq[Slice].substring(2, 5) + '.' + Freq[Slice].substring(5, 8);
      }
      else if (fRig.slice[Slice].RF_frequency > 999999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 1) + '.' + Freq[Slice].substring(1, 4) + '.' + Freq[Slice].substring(4, 7);
      }
      else if (fRig.slice[Slice].RF_frequency > 99999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 3) + '.' + Freq[Slice].substring(3, 6);
      }
      else if (fRig.slice[Slice].RF_frequency > 9999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 2) + '.' + Freq[Slice].substring(2, 5);
      }
      else if (fRig.slice[Slice].RF_frequency > 999)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 1) + '.' + Freq[Slice].substring(1, 4);
      }
      else if (fRig.slice[Slice].RF_frequency > 99)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 0) + '.' + Freq[Slice].substring(0, 3);
      }
      else if (fRig.slice[Slice].RF_frequency > 9)
      {
        DispFreq[Slice] = Freq[Slice].substring(0, 3);
      }

      if (fRig.slice[Slice].RF_frequency > 999999999)
      {
        //debugln("Freq Font: Arial_24_Bold");
        tft.setFont(Arial_24_Bold);
      }
      else if (fRig.slice[Slice].RF_frequency > 99999999)
      {
        //debugln("Freq Font: Arial_28_Bold");
        tft.setFont(Arial_28_Bold);
      }
      else
      {
        //debugln("Freq Font: Arial_32_Bold");
        tft.setFont(Arial_32_Bold);
      }
      //tft.drawRect(Freq_X + (Slice * MidScreen), Freq_Y - 2, Freq_Width, FontHeight_A32, COLOR_WHITE);
      if (VFOTrack)
      {
        tft.fillRect(Freq_X + (Slice * MidScreen), Freq_Y - 2, Freq_Width, FontHeight_A32, COLOR_BLUE);  // Blank slate for new value
      }
      else
      {
        tft.fillRect(Freq_X + (Slice * MidScreen), Freq_Y - 2, Freq_Width, FontHeight_A32, COLOR_BLACK);  // Blank slate for new value
      }

      if (fRig.slice[Slice].tx == 1)
      {
        if (InBand || !OOBindicator)
        {
          tft.setTextColor(COLOR_CYAN);
        }
        else
        {
          tft.setTextColor(COLOR_RED);
        }
      }
      else
      {
        tft.setTextColor(COLOR_CYAN);
      }

      tft.setCursor(Freq_X + (Slice * MidScreen), Freq_Y);
      tft.print(DispFreq[Slice]);
    }
  }
}

/*********************** DispLock *********************/
void DispLock(int Slice)
{
  if (Splash || MenuActive)
  {
    return;
  }

  if (fRig.slice[Slice].lock == 1)
  {
    tft.setTextColor(COLOR_WHITE);
    tft.setFont(Arial_14);
    tft.setCursor(Lock_X + (Slice * MidScreen), Lock_y);
    tft.print("Lock");
  }
  else
  {
    //tft.drawRect(Lock_X + (Slice * MidScreen), Lock_y, Lock_Width, FontHeight_A14, COLOR_WHITE);
    tft.fillRect(Lock_X + (Slice * MidScreen), Lock_y, Lock_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
  }
}

/*********************** DispMute *********************/
void DispMute(int Slice)
{
  if (fRig.slice[Slice].in_use == 1 && !Splash && !MenuActive)
  {
    //    tft.drawRect(Mute_X + (Slice * MidScreen), Mute_Y, Mute_Width, FontHeight_A14, COLOR_WHITE);
    tft.fillRect(Mute_X + (Slice * MidScreen), Mute_Y, Mute_Width, FontHeight_A14, COLOR_BLACK);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(Mute_X + (Slice * MidScreen), Mute_Y);
    tft.setFont(Arial_14);
    if (MuteVal[Slice] == 1)
    {
      tft.print("Mute");
    }
    tft.setTextColor(COLOR_YELLOW);
  }
}

/*********************** DispMode *********************/
void DispMode(int Slice)
{
  if (fRig.slice[Slice].in_use == 1 && !Splash && !MenuActive)
  {
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(Mode_X + (Slice * MidScreen), Mode_Y);
    //    tft.drawRect(Mode_X + (Slice * MidScreen), Mode_Y, Mode_Width, FontHeight_A24, COLOR_WHITE);
    tft.fillRect(Mode_X + (Slice * MidScreen), Mode_Y, Mode_Width, FontHeight_A24, COLOR_BLACK);
    if (fRig.slice[Slice].mode.length() < 4)
    {
      tft.setFont(Arial_24_Bold);
    }
    else
    {
      tft.setFont(Arial_20_Bold);
    }
    tft.println(fRig.slice[Slice].mode);
  }
}

/*********************** DispVol *********************/
void DispVol(int Slice)
{
  if (!Splash && !MenuActive && fRig.slice[Slice].in_use == 1)
  {
    tft.setTextColor(COLOR_YELLOW);
    //tft.drawRect(Vol_X  + (Slice * MidScreen), Vol_Y, Vol_Width, FontHeight_A14, COLOR_WHITE);  // Blank slate for new value
    tft.fillRect(Vol_X + (Slice * MidScreen), Vol_Y, Vol_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setCursor(0 + (Slice * MidScreen), Vol_Y);
    tft.print("Vol: ");
    tft.print(VolVal[Slice]);
  }
}

/*********************** DispAGC *********************/
void DispAGC(int Slice)
{
  if (!Splash && !MenuActive && fRig.slice[Slice].in_use == 1)
  {
    //tft.drawRect(AGCT_X  + (Slice * MidScreen), AGCT_Y, AGCT_Width, FontHeight_A14, COLOR_WHITE);  // Blank slate for new value
    tft.fillRect(AGCT_X + (Slice * MidScreen), AGCT_Y, AGCT_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setTextColor(COLOR_YELLOW);
    tft.setFont(Arial_14);
    tft.setCursor(AGCT_X + (Slice * MidScreen), AGCT_Y);

    if (fRig.slice[Slice].mode == "FM" || fRig.slice[Slice].mode == "NFM" || fRig.slice[Slice].mode == "DFM")
    {
      tft.print("Squelch: ");
      tft.print(fRig.slice[Slice].squelch_level);
    }
    else
    {
      tft.print("AGC-T: ");
      tft.print(AGCVal[Slice]);
    }
  }
}

/*********************** DispRIT *********************/
void DispRIT(int Slice)
{
  if (fRig.slice[Slice].in_use == 1 && !Splash && !MenuActive)
  {
    if (fRig.slice[Slice].rit_on == 1)
    {
      tft.setTextColor(COLOR_YELLOW);
    }
    else
    {
      tft.setTextColor(COLOR_DARKGREY);
    }
    //tft.drawRect(RIT_X  + (Slice * MidScreen), RIT_Y, RIT_Width, FontHeight_A14, COLOR_WHITE);  // Blank slate for new value
    tft.fillRect(RIT_X + (Slice * MidScreen), RIT_Y, RIT_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setCursor(0 + (Slice * MidScreen), RIT_Y + 2);
    tft.print("RIT: ");
    tft.print(fRig.slice[Slice].rit_freq);

    if (SetRIT[A])
    {
      tft.drawRect(0, RIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_WHITE);
    }
    else
    {
      tft.drawRect(0, RIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_BLACK);
    }

    if (SetRIT[B])
    {
      tft.drawRect(MidScreen, RIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_WHITE);
    }
    else
    {
      tft.drawRect(MidScreen, RIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_BLACK);
    }
  }
}

/*********************** DispXIT *********************/
void DispXIT(int Slice)
{
  if (fRig.slice[Slice].in_use == 1 && !Splash && !MenuActive)
  {
    if (fRig.slice[Slice].xit_on == 1)
    {
      tft.setTextColor(COLOR_YELLOW);
    }
    else
    {
      tft.setTextColor(COLOR_DARKGREY);
    }
    //      tft.drawRect(XIT_X  + (Slice * MidScreen), XIT_Y, XIT_Width, FontHeight_A14, COLOR_WHITE);  // Blank slate for new value
    tft.fillRect(XIT_X + (Slice * MidScreen), XIT_Y, XIT_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setCursor(100 + (Slice * MidScreen), XIT_Y + 2);
    tft.print("XIT: ");
    tft.print(fRig.slice[Slice].xit_freq);

    if (SetXIT[A])
    {
      tft.drawRect(100, XIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_WHITE);
    }
    else
    {
      tft.drawRect(100, XIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_BLACK);
    }

    if (SetXIT[B])
    {
      tft.drawRect(100 + MidScreen, XIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_WHITE);
    }
    else
    {
      tft.drawRect(100 + MidScreen, XIT_Y - 3, 90, FontHeight_A14 + 4, COLOR_BLACK);
    }
  }
}

/*********************** DispFilter *********************/
void DispFilter(int Slice)
{
  if (Splash || MenuActive || fRig.slice[Slice].in_use == 0)
  {
    return;
  }
  // Draw the filter icon
  tft.drawLine(FiltLo_X + 40 + (Slice * MidScreen), FiltLo_Y - 25, 150 + (Slice * MidScreen), 185, COLOR_WHITE);
  tft.drawLine(FiltLo_X + 40 + (Slice * MidScreen), FiltLo_Y - 25, 20 + (Slice * MidScreen), 205, COLOR_WHITE);
  tft.drawLine(150 + (Slice * MidScreen), FiltLo_Y - 25, 170 + (Slice * MidScreen), 205, COLOR_WHITE);

  if (fRig.slice[Slice].mode != "CW")
  {
    tft.fillRect(FiltShift_X + (Slice * MidScreen), FiltShift_Y, FiltShift_Width, FontHeight_A14, COLOR_BLACK);  // kill shift val if prev mode was not SSB
    tft.fillRect(FiltLo_X + (Slice * MidScreen), FiltLo_Y, 60, FontHeight_A14, COLOR_BLACK);                     // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(FiltLo_X + (Slice * MidScreen), FiltLo_Y);
    tft.print(fRig.slice[Slice].filter_lo);

    tft.fillRect(FiltHi_X + (Slice * MidScreen), FiltHi_Y, FiltHi_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(FiltHi_X + (Slice * MidScreen), FiltHi_Y);
    tft.print(fRig.slice[Slice].filter_hi);

    tft.fillRect(FiltW_X + (Slice * MidScreen), FiltW_Y, FiltW_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(FiltW_X + (Slice * MidScreen), FiltW_Y);
    tft.print(abs(fRig.slice[Slice].filter_width));
  }

  if (fRig.slice[Slice].mode == "CW")
  {
    tft.fillRect(FiltLo_X + (Slice * MidScreen), FiltLo_Y, FiltLo_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(FiltLo_X + (Slice * MidScreen), FiltLo_Y);
    tft.print(fRig.slice[Slice].filter_lo);

    tft.fillRect(60 + (Slice * MidScreen), FiltW_Y, FiltW_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    //tft.drawRect(FiltW_X + (Slice * MidScreen), FiltW_Y, FiltW_Width, FontHeight_A14, COLOR_WHITE);
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(FiltW_X + (Slice * MidScreen), FiltW_Y);
    tft.print(abs(fRig.slice[Slice].filter_width));

    tft.fillRect(FiltShift_X + (Slice * MidScreen), FiltShift_Y, FiltShift_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(FiltShift_X + (Slice * MidScreen), FiltShift_Y);
    tft.print(fRig.slice[Slice].filter_shift);

    tft.fillRect(FiltHi_X + (Slice * MidScreen), FiltHi_Y, FiltHi_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(FiltHi_X + (Slice * MidScreen), FiltHi_Y);
    tft.print(fRig.slice[Slice].filter_hi);
  }
}

/*********************** DispProfile *********************/
void DispProfile()
{
  if (!Splash && !ScreenSaveActive && !MenuActive)
  {
    Profile = fRig.Current_Profile;
    tft.drawRect(Prof_X, Prof_Y, Prof_Width, FontHeight_A14, COLOR_WHITE);  // Blank slate for new value
    tft.fillRect(Prof_X, Prof_Y, Prof_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setTextColor(COLOR_CYAN);
    tft.setFont(Arial_14);
    tft.setCursor(Prof_X, Prof_Y);
    tft.print(Profile);
  }
}

#ifndef DEBUG_S_METER
#define DEBUG_S_METER 0
#endif

void DispSMeter()
{
  if (Splash || MenuActive) return;

  const bool isTx = (fRig.interlock.state == "TRANSMITTING");
  const unsigned long now = millis();

  if (isTx) { SMTimeIt = now; return; }
  if (ScreenSaveActive || (now - SMTimeIt <= SMDelay)) return;

  for (int Slice = 0; Slice < 2; ++Slice)
  {
    if (fRig.slice[Slice].in_use != 1) continue;

    const int meterIdx = (Slice == 0) ? MET_S_A : MET_S_B;

    float smDbm = fRig.metersValue[meterIdx];
    if (!isfinite(smDbm) || smDbm > 0.0f || smDbm < -200.0f) {
#if DEBUG_S_METER
      Serial.print(F("[S-METER] bad dBm ")); Serial.print(smDbm);
      Serial.print(F(" slice=")); Serial.println(Slice);
#endif
      continue; // skip this cycle
    }

    // Convert to S units (+over)
    int sUnits = (int)abs((-127.0f - smDbm) / 6.0f);
    if (sUnits < 0) sUnits = 0;
    if (sUnits > 9) sUnits = 9;

    int over = 0;
    if (smDbm > -73.0f) {
      sUnits = 9;
      over   = (int)(73.0f - abs(smDbm));
      if (over < 0) over = 0;
      if (over > 40) over = 40;
    }

    const int currSum = sUnits + over;
    const int lastSum = SMeterLast[Slice] + OvrLast[Slice];

    const bool changed = (currSum != lastSum);
    const bool forceDraw = SMeterDirty[Slice];

    if (changed || forceDraw)
    {
      // Update tracking state first
      SMeter[Slice] = sUnits;
      Ovr[Slice]    = over;

      // Peak/hold
      if (SMeter[Slice] + Ovr[Slice] > SMeterPeak[Slice] + OvrPeak[Slice]) {
        SMeterPeak[Slice] = SMeter[Slice];
        OvrPeak[Slice]    = Ovr[Slice];
        SMeterHold[Slice] = now;
      } else if (now - SMeterHold[Slice] > SMeterHoldTime) {
        SMeterPeak[Slice] = SMeter[Slice];
        OvrPeak[Slice]    = Ovr[Slice];
        SMeterLast[Slice] = 999; // force peak text refresh
        SMeterHold[Slice] = now;
      }

      tft.setTextColor(COLOR_CYAN);

      // If decreasing OR first draw after band change, clear and redraw frame
      if (forceDraw || (currSum < lastSum)) {
        tft.fillRect(Slice * MidScreen, 150, 160, 10, COLOR_BLACK);
        tft.drawRect(Slice * MidScreen, 150, 150, 10, COLOR_WHITE);

        // Draw scale whenever we rebuild the frame
        tft.setFont(Arial_12);
        tft.setTextColor(COLOR_CYAN);
        for (int j = 0; j < 5; ++j) {
          const int x = 11 + (Slice * MidScreen) + j * 20;
          tft.drawFastVLine(x, 160, 4, COLOR_CYAN);
          tft.setCursor(6 + (Slice * MidScreen) + j * 20, 166);
          tft.print((j * 2) + 1);  // no String()
        }
      }

      // Draw the bar (RX only)
      if (Ovr[Slice] < 60) {
        tft.fillRect(1 + (Slice * MidScreen), 151, (SMeter[Slice] * 10) + (Ovr[Slice]), 8, COLOR_RED);

        // Peak text/marker only when something moved or frame was rebuilt
        if ((changed || forceDraw) && SMeterPeak[Slice] < 60) {
          tft.setFont(Arial_14);
          tft.fillRect(160 + (Slice * MidScreen), 148, 70, FontHeight_A14, COLOR_BLACK);
          tft.setCursor(160 + (Slice * MidScreen), 148);
          tft.print("S");
          tft.print(SMeterPeak[Slice]);
          tft.drawLine((SMeterPeak[Slice] * 10) + (OvrPeak[Slice]) + (Slice * MidScreen) + 1,
                       150,
                       (SMeterPeak[Slice] * 10) + (OvrPeak[Slice]) + (Slice * MidScreen) + 1,
                       158,
                       COLOR_WHITE);
          if (OvrPeak[Slice] > 0) {
            tft.print(" +");
            tft.print(OvrPeak[Slice]);
          }
        }
      }

      // Clear dirty flag after we’ve drawn
      SMeterDirty[Slice] = false;

#if DEBUG_S_METER
      Serial.print(F("[S-METER] slice=")); Serial.print(Slice);
      Serial.print(F(" dBm="));           Serial.print(smDbm, 1);
      Serial.print(F(" S="));             Serial.print(SMeter[Slice]);
      Serial.print(F(" +"));              Serial.print(Ovr[Slice]);
      Serial.print(F(" force="));         Serial.print(forceDraw);
      Serial.print(F(" state="));         Serial.println(fRig.interlock.state);
#endif
    }

    // Update last (only in RX path)
    SMeterLast[Slice] = SMeter[Slice];
    OvrLast[Slice]    = Ovr[Slice];
  }

  SMTimeIt = now;
}

/*********************** DispSMeterScale *********************/
void DispSMeterScale()
{
  if (Splash || MenuActive)
  {
    return;
  }
  for (int i = 0; i < 2; ++i)
  {
    if (fRig.slice[i].in_use != 1) continue;

    tft.setFont(Arial_12);
    tft.setTextColor(COLOR_CYAN);
    const int x0 = i * MidScreen;
    for (int j = 0; j < 5; ++j)
    {
      const int x = 11 + x0 + j * 20;
      tft.drawFastVLine(x, 160, 4, COLOR_CYAN);
      tft.setCursor(6 + x0 + j * 20, 166);
      tft.print((j * 2) + 1);  // no String()
    }
  }
}

/*********************** DispStep *********************/
void DispStep(int Slice)
{
  if ((fRig.slice[Slice].in_use == 1 || VFOTrack) && !Splash && !MenuActive)
  {
    //    tft.drawRect(0 + (Slice * MidScreen), 30, 100, FontHeight_A14, COLOR_WHITE);
    tft.fillRect(0 + (Slice * MidScreen), 30, 100, FontHeight_A14, COLOR_BLACK);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(0 + (Slice * MidScreen), 30);
    tft.setFont(Arial_14);
    tft.print("Step ");
    tft.print(StepSize[VFOStep[Slice]]);
  }
}

// Dual-mode renderer for CW speed.
// - Connected: draw at the legacy Enc9Field_* rectangle (fixed X/Y).
// - Keyer-Only: draw LEFT-aligned in the left panel using a big number
//   (same size as the clock hour/minute: Arial_40_Bold) plus a small "WPM" label.
//
// This function is safe to call repeatedly (encoder changes, etc).
// ===== [TM DISPLAY DispCWSpeed] BEGIN =====
void DispCWSpeed()
{
  if (Splash || MenuActive) return;

  const char* label = " WPM";
  String num = String(CWVal);

  if (UIState::isKeyerOnly())
  {
    // --- Centered in left panel (0–239 px wide) ---------------------------
    const int16_t panelW    = 240;
    const int16_t baselineY = KeyerOnly_CWSpeed_Y;
    const int16_t gap       = 6;

    // Measure number (big) + label (small)
    tft.setFont(Arial_40_Bold);
    int16_t nx1, ny1; uint16_t nw, nh;
    tft.getTextBounds((char*)num.c_str(), 0, 0, &nx1, &ny1, &nw, &nh);

    tft.setFont(Arial_14);
    int16_t lx1, ly1; uint16_t lw, lh;
    tft.getTextBounds((char*)label, 0, 0, &lx1, &ly1, &lw, &lh);

    const int16_t totalW = (int16_t)((int16_t)nw + gap + (int16_t)lw);
    const int16_t cx     = (int16_t)((panelW - totalW) / 2);

    // Rect helper & clipped clear
    struct Rect { int16_t x, y; uint16_t w, h; };
    auto clipFillBlack = [&](int16_t x, int16_t y, uint16_t w, uint16_t h) {
      // clip to screen
      if (w == 0 || h == 0) return;
      if (x < 0) { int32_t delta = -x; if (delta >= w) return; x = 0; w = (uint16_t)(w - delta); }
      if (y < 0) { int32_t delta = -y; if (delta >= h) return; y = 0; h = (uint16_t)(h - delta); }
      int16_t maxW = (int16_t)tft.width()  - x;
      int16_t maxH = (int16_t)tft.height() - y;
      if (maxW <= 0 || maxH <= 0) return;
      uint16_t cw = (uint16_t)((w > (uint16_t)maxW) ? (uint16_t)maxW : w);
      uint16_t ch = (uint16_t)((h > (uint16_t)maxH) ? (uint16_t)maxH : h);
      tft.fillRect(x, y, cw, ch, COLOR_BLACK);
    };

    // Current union area (baselineY is the text baseline)
    Rect curr = {
      (int16_t)cx,
      (int16_t)(baselineY - (int16_t)nh),          // explicit final cast
      (uint16_t)((totalW > 0) ? totalW : 0),       // cast to unsigned width
      (uint16_t)((int16_t)nh + 4)                  // a tiny margin below glyphs
    };

    // Clear previous and current areas
    static Rect prev = { -1, -1, 0, 0 };
    if (prev.w && prev.h) clipFillBlack(prev.x, prev.y, prev.w, prev.h);
    clipFillBlack(curr.x, curr.y, curr.w, curr.h);

    // Draw number (big, green)
    tft.setFont(Arial_40_Bold);
    tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft.setCursor(cx, baselineY);
    tft.print(num);

    // Draw label (small, grey) right after the number
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_LIGHTGREY, COLOR_BLACK);
    tft.setCursor((int16_t)(cx + (int16_t)nw + gap), baselineY);
    tft.print(label);

    prev = curr;
    return;
  }

  // --- Connected (legacy) -------------------------------------------------
  tft.setFont(Arial_14);
  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  tft.fillRect(Enc9Field_X, Enc9Field_Y, Enc9Field_Width, FontHeight_A14, COLOR_BLACK);
  tft.setCursor(Enc9Field_X, Enc9Field_Y);
  tft.print(num);
  tft.print(" WPM");
}
// ===== [TM DISPLAY DispCWSpeed] END =====

/*********************** DispLicense *********************/
void DispLicense()
{
  if (!Splash && !MenuActive)
  {

    tft.fillRect(License_X, License_Y, License_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);

    if (InBand || !OOBindicator || FlexIsHeadless())
    {
      tft.setTextColor(COLOR_YELLOW);
    }
    else
    {
      tft.setTextColor(COLOR_RED);
    }

    tft.setCursor(License_X, License_Y);
    tft.print(MyLicense);
  }
}

/*********************** DispRF *********************/
void DispRF()
{
  if (!Splash && !MenuActive && fRig.connected)
  {
    tft.fillRect(RFPower_X, RFPower_Y, RFPower_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(RFPower_X, RFPower_Y);
    tft.print("RF Pwr: ");
    tft.print(fRig.transmit.rfpower);
  }

  if (MenuActive && RFPowerMenuActive && TransmitMenuOn)
  {
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(95, 21);
    //tft.drawRect(92, 18, 60, 19, HX8357_YELLOW);
    //tft.drawRect(93, 19, 58, FontHeight_A14, COLOR_WHITE);
    tft.fillRect(93, 19, 58, FontHeight_A14, COLOR_BLACK);
    tft.print(RFPower);
  }
}

/*********************** DispTune *********************/
void DispTune()
{
  if (!Splash && !MenuActive && fRig.connected)
  {
    tft.fillRect(Enc9Field_X, Enc9Field_Y, Enc9Field_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(Enc9Field_X, Enc9Field_Y);
    tft.print("Tune: ");
    tft.print(TunePower);
  }
}

/*********************** DispWNB *********************/
void DispWNB()
{
  if (!Splash && !MenuActive && fRig.connected)
  {
    tft.fillRect(Enc9Field_X, Enc9Field_Y, Enc9Field_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(Enc9Field_X, Enc9Field_Y);
    tft.print("WNB Lvl: ");
    tft.print(WNBLevel);
  }
}

/*********************** DispMonLevel *********************/
void DispMonLevel()
{
  if (!Splash && !MenuActive && fRig.connected)
  {
    tft.fillRect(Enc9Field_X, Enc9Field_Y, Enc9Field_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(Enc9Field_X, Enc9Field_Y);
    tft.print("Mon Lvl: ");
    tft.print(MonLevel);
  }
}

/*********************** DispVOXLevel *********************/
void DispVOXLevel()
{
  if (!Splash && !MenuActive && fRig.connected)
  {
    tft.fillRect(Enc9Field_X, Enc9Field_Y, Enc9Field_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(Enc9Field_X, Enc9Field_Y);
    tft.print("VOX Lvl: ");
    tft.print(VOXLevel);
  }
}

/*********************** DispVOXDelay *********************/
void DispVOXDelay()
{
  if (!Splash && !MenuActive && fRig.connected)
  {
    tft.fillRect(Enc9Field_X, Enc9Field_Y, Enc9Field_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(Enc9Field_X, Enc9Field_Y);
    tft.print("VOX Del: ");
    tft.print(VOXDelay);
  }
}

/*********************** DispMicGain *********************/
void DispMicGain()
{
  if (MenuActive && MicGainMenuActive)
  {
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(85, 42);
    //tft.drawRect(80, 40, 60, FontHeight_A14, HX8357_YELLOW);
    tft.fillRect(81, 41, 58, FontHeight_A14, COLOR_BLACK);
    tft.print(MicGain);
  }
  else if (!Splash && !MenuActive)
  {
    tft.fillRect(Enc9Field_X, Enc9Field_Y, Enc9Field_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(Enc9Field_X, Enc9Field_Y);
    tft.print("Mic: ");
    tft.print(MicGain);
  }
}

/*********************** DispCWMsgSource ********************
void DispCWMsgSource()
{
  tft.fillRect(CWMsg_X, CWMsg_Y, CWMsg_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
  tft.setFont(Arial_14);
  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(CWMsg_X, CWMsg_Y);
  tft.print("CW Msg Source: ");
  tft.setTextColor(COLOR_YELLOW);
  tft.print(CWMsgSource);
}
*/

/*********************** DispSerNum *********************/
// Draws contest serial number either on the connected home screen
// or inside the active "Serial Number" menu. Avoids EEPROM reads here.
// Assumes 'SerNum' is kept in RAM and persisted elsewhere when changed.
void DispSerNum()
{
  if (!Splash && !MenuActive && ShowContestSerialNumber)
  {
    // Define a safe draw area (reuse your existing geometry constants)
    // NOTE: We always clear before drawing to avoid artifacts.
    tft.fillRect(CWSerNum_X, CWSerNum_Y, CWSerNum_Width, FontHeight_A14, COLOR_BLACK);

    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(CWSerNum_X, CWSerNum_Y);
    tft.print("  Ser: ");
    tft.print(SerNum);
    return;
  }

  if (MenuActive && SerNumMenuActive)
  {
    // Clear menu slot area and draw the value
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW, COLOR_BLACK); // paint with bg to be extra safe
    tft.fillRect(226, 124, 98, FontHeight_A14, COLOR_BLACK);
    tft.setCursor(225, 126);
    tft.print(SerNum);
  }
}

/*********************** DispSTFreq *********************/
void DispSTFreq()
{
  if (MenuActive && STFreqMenuActive)
  {
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(169, 105);
    //tft.drawRect(166, 102, 480, FontHeight_A14, COLOR_WHITE);
    tft.fillRect(166, 102, 480, FontHeight_A14, COLOR_BLACK);
    tft.drawRect(166, 103, 60, FontHeight_A14, COLOR_YELLOW);
    tft.print(STFreq);
  }
}

/*********************** DispAccelFactor *********************/
void DispAccelFactor()
{
  if (MenuActive && AccelMenuActive)
  {
    tft.setFont(Arial_14);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(219, 295);
    //tft.drawRect(216, 292, 480, FontHeight_A14, COLOR_WHITE);
    tft.fillRect(216, 292, 480, FontHeight_A14, COLOR_BLACK);
    tft.drawRect(216, 292, 80, 19, COLOR_YELLOW);
    tft.print(AccelFactor);
  }
}

/*********************** DispHanded *********************
void DispHanded()
{
  tft.fillRect(CWHanded_X, CWHanded_Y, CWHanded_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
  tft.setFont(Arial_14);
  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(CWHanded_X, CWHanded_Y);
  tft.print("CW Paddles: ");
  tft.setTextColor(COLOR_YELLOW);
  tft.print(HandedTxt);
}

*********************** DispCWMode *********************
void DispCWMode()
{
  tft.fillRect(CWMode_X, CWMode_Y, CWMode_Width, FontHeight_A14, COLOR_BLACK);  // Blank slate for new value
  tft.setFont(Arial_14);
  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(CWMode_X, CWMode_Y);
  tft.print("  Mode: ");
  tft.setTextColor(COLOR_YELLOW);
  tft.print(KeyMode);
}
*/

// ===== [TM DISPLAY KeyerOnlyScreen] BEGIN =====
inline void DispKeyerOnlyScreen()
{
  // Draw split lines as in connected mode
  tft.drawLine(240, 0, 240, 240, COLOR_DARKGREY);
  tft.drawLine(0, 240, 480, 240, COLOR_DARKGREY);

  // --- Left panel (Area 1: 0–239 px) --------------------------------------
  const int16_t areaW   = 240;
  const int16_t titleY  = 58;   // lowered to align with clock time top
  const int16_t paddY   = 240 + 35;

  // Title: smaller font (Arial_18_Bold)
  tft.setFont(Arial_18_Bold);
  tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds("Keyer-Only Mode", 0, 0, &x1, &y1, &w, &h);
  int16_t titleX = (areaW - (int16_t)w) / 2;  // centered within left panel
  tft.setCursor(titleX, titleY);
  tft.print("Keyer-Only Mode");

  // CW speed: centered within Area 1 (0–239 px)
  DispCWSpeed();  // renders in same area, centered

  // Paddles info near bottom-left
  tft.setFont(Arial_14);
  tft.setTextColor(COLOR_LIGHTGREY, COLOR_BLACK);
  tft.setCursor(8, paddY);
  tft.print("CW Paddles: ");
  tft.print(HandedTxt);

  // --- Right panel: Clock widget ------------------------------------------
  // Do not draw the clock while a menu is active
  if (!MenuActive) {
    ClockWidget_UiReady();   // draws clock if UI_SliceB_Free()==true
  }
}
// ===== [TM DISPLAY KeyerOnlyScreen] END =====

/*********************** DispMenu *********************/
void DispMenu(int M)
{
  int i;

  tft.fillScreen(COLOR_BLACK);

  tft.setCursor(0, 0);
  tft.setFont(Arial_14);
  tft.setTextColor(COLOR_WHITE);

  tft.println(MenuTitle[M]);
  tft.setTextColor(COLOR_YELLOW);

  for (i = 0; i < (MaxMenuItems - 1); i++)
  {
    if (M == CWMsgMenu && i < 12 && CWMsgMenuOpt)
    {
      tft.print(i + 1);
      tft.print(" ");
      tft.println(MenuItem[M][i].substring(0, 34));  // don't span lines
    }
    else
    {
      if (M == CWMsgMenu && CWMsgMenuOpt)
      {
        break;
      }

      if (M == ClientMenuIDX)
      {
        ClientMenuActive = true;
        LoadClientMenu();  // Need to refresh in case clients logged in or out
      }

      tft.println(MenuItem[M][i]);
      debugln(MenuItem[M][i]);
      if (MenuItem[M][i] == "")
      {
        debug("LastMenuItemIDX ");
        debugln(LastMenuItemIDX);
        break;
      }
    }
  }
  LastMenuItemIDX = i;
}

/*********************** ResetTFTScreen *********************/
void ResetTFTScreen()
{
  debugln("TFT Reset");
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(15);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
  Display::init();

  if (!InSetup)
  {
    RefreshScreen();
  }
}

// Simple one or two line connection status, safe to call early
void TM_ShowConnectStatus(const char* line1, const char* line2)
{
  tft.fillRect(0, 0, tft.width(), 28, COLOR_NAVY);
  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(6, 6);
  if (line1 && *line1) tft.print(line1);
  if (line2 && *line2) {
    tft.setCursor(6, 16);
    tft.print(line2);
  }
}
