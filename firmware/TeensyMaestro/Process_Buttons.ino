#include "tm_sketch_api.h"
#include "tm_system_utils.h"
#include "TM_Keyer_Engine.h"

extern TM_Keyer_Engine g_keyerEngine;

// Returns true if the radio can be controlled (online, not standalone, not headless).
static bool RigIsControllable()
{
  if (!fRig.connected) {
    debugln(F("RigIsControllable(): false (rig not connected)"));
    return false;
  }
  if (StandAlone) {
    debugln(F("RigIsControllable(): false (standalone mode)"));
    return false;
  }
  if (FlexIsHeadless()) {
    debugln(F("RigIsControllable(): false (headless mode)"));
    return false;
  }
  return true;
}

/*********************** GetButton *********************/
bool isLongPress(long start_tm)
{
  return (millis() - start_tm >= LongPress);
}

bool isShortPress(long start_tm)
{
  return (millis() - start_tm < LongPress);
}


uint8_t isButtonPressed(ButtonName button)
{
  PinData pinData = buttonMap[button];
  if (pinData.mux == 0)
  {
    return muxA.digitalRead(pinData.pin) == LOW;
  }
  else if (pinData.mux == 1)
  {
    return muxB.digitalRead(pinData.pin) == LOW;
  }
  else if (pinData.mux == 2)
  {
    return muxC.digitalRead(pinData.pin) == LOW;
  }
  else
  {
    return false;
  }
}

/**
* Check for a button press. Return the first button found, else return BTN_NONE;
*/
ButtonName getFirstButtonPress()
{

  // Iterate through the I/O Expander A pins
  if (muxA_found)
  {
    for (int x = 0; x < 8; x++)
    {
      if ((muxA_arr[x].gpioDirection == GPIO_INPUT) && (!muxA.digitalRead(x)))  // NV0E - handle only input pins
      {
        return muxA_arr[x].name;
      }
    }
  }

  // Iterate through the I/O Expander B pins
  if (muxB_found)
  {
    for (int x = 0; x < 8; x++)
    {
      if ((muxB_arr[x].gpioDirection == GPIO_INPUT) && (!muxB.digitalRead(x)))  // NV0E - handle only input pins
      {
        return muxB_arr[x].name;
      }
    }
  }

  // Iterate through the I/O Expander C pins
  if (muxC_found)
  {
    for (int x = 0; x < 16; x++)
    {
      if ((muxC_arr[x].gpioDirection == GPIO_INPUT) && (!muxC.digitalRead(x)))  // NV0E - handle only input pins
      {
        return muxC_arr[x].name;
      }
    }
  }

  return BTN_NONE;
}

void GetIOExpanderButton()
{
  GotBtn = false;

  ButtonName pressedBtn = getFirstButtonPress();
  if (pressedBtn != BTN_NONE)
  {
    if (pressedBtn != BTN_PTT && ShortPressClick)
    {
      tone(STPin, BtnClickTone, BtnClickDur);
    }

    debug("Button=");
    debugln(buttonMap[pressedBtn].name);

    ResetScreenSaver("Buttons: bank A");
    GotBtn = true;

    if (SplashM)
    {
      SplashM = false;
      Splash  = false;
      GotBtn  = false;
      RefreshScreen();
      return;
    }

    if (InSetup)
    {
      tft.fillScreen(COLOR_NAVY);
      tft.setCursor(100, 10);
      tft.setFont(Arial_24_Bold);
      tft.println("Standalone Mode");
      tft.setCursor(100, 50);
      tft.setFont(Arial_18_Bold);
      tft.println("Release button to start");
      return;
    }

    processIOExpanderBtn(pressedBtn);
  }
}


void processIOExpanderBtn(int pressedBtn)
{
  // Convert once, keep the rest of the code type-safe.
  ButtonName btn = (ButtonName)pressedBtn;

  //ResetScreenSaver();
  delay(BtnDebounce);  // Debounce time.

  bool DispOnce  = false;
  bool ClickOnce = false;

  // Per-hold one-shot guards
  bool pttStarted   = false;
  bool tuneStarted  = false;
  bool so2rAStarted = false;
  bool so2rBStarted = false;

  ButtonTime = millis();

  // Loop until the button is released
  while (isButtonPressed(btn))
  {
    if (!g_systemReady) return;
    // Service network/events while the button is held (critical for interlock)
    TM_PumpCoop();

    if (!ClickOnce)
    {
      if (isLongPress(ButtonTime) && LongPressClick)
      {
        tone(STPin, BtnClickTone, BtnClickDur);
        ClickOnce = true;
      }
    }

    // If Menu Select btn was pressed
    if ((millis() - ButtonTime) > RestartTime && !DispOnce && btn == BTN_MENU_SEL)
    {
      tft.fillScreen(COLOR_NAVY);
      tft.setTextColor(COLOR_YELLOW);
      tft.setCursor(40, 100);
      tft.setFont(Arial_28_Bold);
      tft.print("Release to RESTART");
      DispOnce = true;
    }

    // -------- PTT (front panel) --------
    if (btn == BTN_PTT)
    {
      if (!fRig.connected) { return; }

      if (isButtonPressed(BTN_PTT) && !pttStarted)
      {
        HandleBtnPTTBefore();
        pttStarted = true;
      }

      // Wait until the PTT Button is released
      while (isButtonPressed(BTN_PTT))
      {
        TM_PumpCoop();
      }

      if (pttStarted)
      {
        HandleBtnPTTAfter();
        pttStarted = false;
      }

      GotBtn = false;
      return;
    }

    // -------- SO2R: Slice A foot pedal --------
    if (btn == BTN_ACC_8)  // 0 PTT button pushed
    {
      if (!fRig.connected) { return; }
      if (fRig.interlock.state != "READY" && fRig.interlock.state != "RECEIVE") { return; }

      if (isButtonPressed(BTN_ACC_8))
      {
        if (fRig.slice[A].in_use == 1)
        {
          fRig.setTx(A, 1);
          fRig.process();
        }
        else
        {
          return;
        }

        if (!so2rAStarted)
        {
          HandleBtnPTTBefore();
          so2rAStarted = true;
        }
      }

      while (isButtonPressed(BTN_ACC_8))
      {
        TM_PumpCoop();
      }

      // Release TX
      fRig.send("xmit 0");
      debug("Button pressed: ");
      debugln(fRig.interlock.state);

      if (so2rAStarted)
      {
        HandleBtnPTTAfter();
        so2rAStarted = false;
      }

      GotBtn = false;
      return;
    }

    // -------- SO2R: Slice B foot pedal --------
    if (btn == BTN_ACC_9)  // 1 PTT button pushed
    {
      if (!fRig.connected) { return; }
      if (fRig.interlock.state != "READY" && fRig.interlock.state != "RECEIVE") { return; }

      if (isButtonPressed(BTN_ACC_9))
      {
        if (fRig.slice[B].in_use == 1)
        {
          fRig.setTx(B, 1);
          fRig.process();
        }
        else
        {
          return;
        }

        if (!so2rBStarted)
        {
          HandleBtnPTTBefore();
          so2rBStarted = true;
        }
      }

      while (isButtonPressed(BTN_ACC_9))
      {
        TM_PumpCoop();
      }

      // Release TX
      fRig.send("xmit 0");
      debug("Button pressed: ");
      debugln(fRig.interlock.state);

      if (so2rBStarted)
      {
        HandleBtnPTTAfter();
        so2rBStarted = false;
      }

      GotBtn = false;
      return;
    }

    // -------- TUNE button --------
    if (btn == BTN_TUNE)
    {
      if (!fRig.connected) { return; }

      if (isButtonPressed(BTN_TUNE) && !tuneStarted)
      {
        HandleBtnTuneBefore();
        tuneStarted = true;
      }

      // Wait until the TUNE Button is released
      while (isButtonPressed(BTN_TUNE))
      {
        TM_PumpCoop();
      }

      if (tuneStarted)
      {
        HandleBtnTuneAfter();
        tuneStarted = false;
      }

      GotBtn = false;
      return;
    }
  } // end while(isButtonPressed(btn))

  if (DispOnce)
  {
    tft.fillScreen(COLOR_NAVY);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(40, 100);
    tft.setFont(Arial_28_Bold);
    tft.print("Restarting...");
  }

  // --- Post-processing for short/long press (unchanged) ---
  switch (btn)
  {
    case BTN_NONE: break;

    case BTN_VFO_A_MUTE_SLICE: HandleBtnMuteA(); break;
    case BTN_VFO_A_NB:         HandleBtnNoiseBlankA(); break;
    case BTN_VFO_A_NR:         HandleBtnNoiseReductionA(); break;
    case BTN_VFO_A_RIT_SEL:    HandleBtnRitSetA(); break;

    case BTN_VFO_B_MUTE_SLICE: HandleBtnMuteB(); break;
    case BTN_VFO_B_NB:         HandleBtnNoiseBlankB(); break;
    case BTN_VFO_B_NR:         HandleBtnNoiseReductionB(); break;
    case BTN_VFO_B_RIT_SEL:    HandleBtnRitSetB(); break;

    case BTN_MENU_SEL:         HandleBtnSelect(); break;

    case BTN_VFO_A_RIT:        HandleBtnRitA(); break;
    case BTN_VFO_A_XIT:        HandleBtnXitA(); break;
    case BTN_VFO_B_RIT:        HandleBtnRitB(); break;
    case BTN_VFO_B_XIT:        HandleBtnXitB(); break;

    case BTN_PTT:  break;  // handled above
    case BTN_TUNE: break;  // handled above

    case BTN_CW_MSG_1: CWButton(1, isLongPress(ButtonTime)); break;
    case BTN_CW_MSG_2: CWButton(2, isLongPress(ButtonTime)); break;
    case BTN_CW_MSG_3: CWButton(3, isLongPress(ButtonTime)); break;
    case BTN_CW_MSG_4: CWButton(4, isLongPress(ButtonTime)); break;
    case BTN_CW_MSG_5: CWButton(5, isLongPress(ButtonTime)); break;
    case BTN_CW_MSG_6: CWButton(6, isLongPress(ButtonTime)); break;

    // Future accessories (no-op for now)
    case BTN_ACC_0:
    case BTN_ACC_1:
    case BTN_ACC_2:
    case BTN_ACC_3 ... BTN_ACC_7:
    case BTN_ACC_8:
    case BTN_ACC_9:
      break;
  }
}

/*********************** GetButton *********************/
void GetButton()
{
  GotBtn = false;

  // Select MUX board
  for (MUXidx = 0; MUXidx < 2; MUXidx++)
  {
    // Set Ena address on Teensy pins
    for (int i = 0; i < 2; i++)
    {
      for (int j = 0; j < 2; j++)
      {
        digitalWrite(MUXEnaPin[j], HIGH);
      }
      digitalWrite(MUXEnaPin[MUXidx], bitRead(MUXidx, i));
    }

    delayMicroseconds(250);

    for (BTNidx = 0; BTNidx < 16; BTNidx++)
    {
      // Set Mux address on Teensy pins
      for (int k = 0; k < 4; k++)
      {
        digitalWrite(MUXPin[k], bitRead(BTNidx, k));
      }

      delayMicroseconds(250);

      if (digitalRead(MUXIOPin) == LOW)
      {
        if (BTNidx != BtnPTT && ShortPressClick)
        {
          tone(STPin, BtnClickTone, BtnClickDur);
        }

        debug("Button ");
        debug(MUXidx);
        debug(" ");
        debugln(BTNidx);

        ResetScreenSaver("Buttons: bank B");
        GotBtn = true;

        if (SplashM)
        {
          SplashM = false;
          Splash  = false;
          GotBtn  = false;
          RefreshScreen();
          return;
        }

        if (InSetup)
        {
          tft.fillScreen(COLOR_NAVY);
          tft.setCursor(100, 10);
          tft.setFont(Arial_24_Bold);
          tft.println("Standalone Mode");
          tft.setCursor(100, 50);
          tft.setFont(Arial_18_Bold);
          tft.println("Release button to start");
          return;
        }

        ProcessButtons();
        return;
      }  // end if(digitalRead(MUXIOPin) == LOW)
    }    //end for(BTNidx=0; BTNidx<16; BTNidx++)
  }      //end for(MUXidx=0; MUXidx<2; MUXidx++)
}  // end GetButton()


/*********************** ProcessButtons *********************/
void ProcessButtons()
{
  if (!GotBtn)
    return;

  //ResetScreenSaver();
  delay(BtnDebounce);  // Debounce time.

  bool DispOnce  = false;
  bool ClickOnce = false;

  // One-shot guards for hold-type buttons
  bool pttStarted   = false;
  bool tuneStarted  = false;
  bool so2rAStarted = false; // MUXidx=1, BTNidx=6
  bool so2rBStarted = false; // MUXidx=1, BTNidx=7

  ButtonTime = millis();

  // Hold until the currently addressed MUX input is released
  while (digitalRead(MUXIOPin) == LOW)
  {
    // Keep Flex/events alive during the hold
    TM_PumpCoop();

    // Long-press click tone (once)
    if (!ClickOnce && isLongPress(ButtonTime) && LongPressClick) {
      tone(STPin, BtnClickTone, BtnClickDur);
      ClickOnce = true;
    }

    // “Release to RESTART” banner
    if ((millis() - ButtonTime) > RestartTime && !DispOnce && BTNidx == 8) {
      tft.fillScreen(COLOR_NAVY);
      tft.setTextColor(COLOR_YELLOW);
      tft.setCursor(40, 100);
      tft.setFont(Arial_28_Bold);
      tft.print("Release to RESTART");
      DispOnce = true;
    }

    // ---- HOLD-time behavior for special buttons ----
    // Front-panel PTT (MUX0, BtnPTT)
    if (MUXidx == 0 && BTNidx == BtnPTT) {
      if (!fRig.connected) { return; }
      if (!pttStarted) {
        HandleBtnPTTBefore();
        pttStarted = true;
      }
      // (we stay in the outer hold loop until release)
      continue;
    }

    // TUNE (MUX0, BtnTune)
    if (MUXidx == 0 && BTNidx == BtnTune) {
      if (!fRig.connected) { return; }
      if (!tuneStarted) {
        HandleBtnTuneBefore();
        tuneStarted = true;
      }
      continue;
    }

    // SO2R pedal: Slice A (MUX1, BTNidx==6)
    if (MUXidx == 1 && BTNidx == 6) {
      if (!fRig.connected) { return; }
      if (fRig.interlock.state != "READY" && fRig.interlock.state != "RECEIVE") { return; }

      if (!so2rAStarted) {
        if (fRig.slice[A].in_use == 1) {
          fRig.setTx(A, 1);
          fRig.process();
          HandleBtnPTTBefore();
          so2rAStarted = true;
        } else {
          return;
        }
      }
      continue;
    }

    // SO2R pedal: Slice B (MUX1, BTNidx==7)
    if (MUXidx == 1 && BTNidx == 7) {
      if (!fRig.connected) { return; }
      if (fRig.interlock.state != "READY" && fRig.interlock.state != "RECEIVE") { return; }

      if (!so2rBStarted) {
        if (fRig.slice[B].in_use == 1) {
          fRig.setTx(B, 1);
          fRig.process();
          HandleBtnPTTBefore();
          so2rBStarted = true;
        } else {
          return;
        }
      }
      continue;
    }

    // Other buttons: do nothing special during hold;
    // we just keep pumping until release.
  }

  // ---- Release edge: run After-hooks for the ones we started ----
  if (pttStarted) {
    HandleBtnPTTAfter();
    pttStarted = false;
    GotBtn = false;
    return;
  }

  if (tuneStarted) {
    HandleBtnTuneAfter();
    tuneStarted = false;
    GotBtn = false;
    return;
  }

  if (so2rAStarted) {
    // Release TX and run After
    fRig.send("xmit 0");
    debug("Button pressed: ");
    debugln(fRig.interlock.state);
    HandleBtnPTTAfter();
    so2rAStarted = false;
    GotBtn = false;
    return;
  }

  if (so2rBStarted) {
    // Release TX and run After
    fRig.send("xmit 0");
    debug("Button pressed: ");
    debugln(fRig.interlock.state);
    HandleBtnPTTAfter();
    so2rBStarted = false;
    GotBtn = false;
    return;
  }

  // If we showed the restart banner
  if (DispOnce) {
    tft.fillScreen(COLOR_NAVY);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(40, 100);
    tft.setFont(Arial_28_Bold);
    tft.print("Restarting...");
  }

  // --- Short/Long press post-processing (unchanged logic) ---
  switch (MUXidx)
  {
    case 0:  // MUX 0
      switch (BTNidx)
      {
        case BtnMuteA:  HandleBtnMuteA(); break;
        case BtnNBA:    HandleBtnNoiseBlankA(); break;
        case BtnNRA:    HandleBtnNoiseReductionA(); break;
        case BtnRitSetA:HandleBtnRitSetA(); break;

        case BtnMuteB:  HandleBtnMuteB(); break;
        case BtnNBB:    HandleBtnNoiseBlankB(); break;
        case BtnNRB:    HandleBtnNoiseReductionB(); break;
        case BtnRitSetB:HandleBtnRitSetB(); break;

        case BtnMenuSel:HandleBtnSelect(); break;

        case BtnRitA:   HandleBtnRitA(); break;
        case BtnXitA:   HandleBtnXitA(); break;
        case BtnRitB:   HandleBtnRitB(); break;
        case BtnXitB:   HandleBtnXitB(); break;

        case BtnPTT:    /* handled in hold-phase above */ break;
        case BtnTune:   /* handled in hold-phase above */ break;

        case 15:
          if (isShortPress(ButtonTime)) {
            // Short press
          } else {
            // Long press
          }
          break;
      }
      break;

    case 1:  // MUX 1
      switch (BTNidx)
      {
        case BtnCW1: CWButton(1, isLongPress(ButtonTime)); break;
        case BtnCW2: CWButton(2, isLongPress(ButtonTime)); break;
        case BtnCW3: CWButton(3, isLongPress(ButtonTime)); break;
        case BtnCW4: CWButton(4, isLongPress(ButtonTime)); break;
        case BtnCW5: CWButton(5, isLongPress(ButtonTime)); break;
        case BtnCW6: CWButton(6, isLongPress(ButtonTime)); break;

        case 6:  /* spare */ break;
        case 7:  /* spare */ break;
        case 8:  /* spare */ break;
        case 9:  /* spare */ break;
        case 10: /* spare */ break;
        case 11: /* spare */ break;
        case 12: /* spare */ break;
        case 13: /* spare */ break;
        case 14: /* spare */ break;
        case 15: /* spare */ break;
      }
      break;
  }
}

// Internal helpers that actually perform PTT / TUNE actions.
// Return true if the command was sent to the rig, false if it was skipped.
bool DoRigPTT(bool on)
{
  if (!RigIsControllable()) {
    debugln(F("DoRigPTT(): skipped (rig not controllable)"));
    return false;
  }

  fRig.send(on ? "xmit 1" : "xmit 0");
  debug(F("DoRigPTT(): PTT "));
  debugln(on ? F("ON") : F("OFF"));
  return true;
}

static bool DoRigTune(bool on)
{
  if (!RigIsControllable()) {
    debugln(F("DoRigTune(): skipped (rig not controllable)"));
    return false;
  }

  // 1. Tell FlexRadio to Tune (Uses Tune Power setting)
  fRig.send(on ? "transmit tune 1" : "transmit tune 0");
  
  // 2. Tell Engine to Tune (Handles Local Sidetone, PTT Pin, Key Pin)
  // Note: The Engine will trigger Engine_Key_Callback via this call.
  // Since fRig is already handled above, the duplicate "cw key 1" 
  // sent by the callback might be redundant but usually harmless on Flex.
  // If it causes issues, we can filter it in the callback.
  g_keyerEngine.setTune(on);

  debug(F("DoRigTune(): TUNE "));
  debugln(on ? F("ON") : F("OFF"));
  return true;
}

// Public API used by Remote Command Server (RCS) to request PTT/TUNE.
// These return true if the action was actually sent to the rig.
bool TM_RCS_RequestPTT(bool on)
{
  return DoRigPTT(on);
}

bool TM_RCS_RequestTune(bool on)
{
  return DoRigTune(on);
}

void HandleBtnTuneBefore()
{
  // Request TX TUNE; UI is driven by interlock events
  (void)DoRigTune(true);
  debug(F("HandleBtnTuneBefore() Button pressed: "));
  debugln(fRig.interlock.state);
  //ResetScreenSaver();
}

void HandleBtnTuneAfter()
{
  // Stop TUNE
  (void)DoRigTune(false);
  debug(F("HandleBtnTuneAfter() Button pressed: "));
  debugln(fRig.interlock.state);
}

void HandleBtnPTTBefore()
{
  (void)DoRigPTT(true);
  //delay(100);
  debug(F("HandleBtnPTTBefore() Button pressed: "));
  debugln(fRig.interlock.state);
  //ResetScreenSaver();
}

void HandleBtnPTTAfter()
{
  (void)DoRigPTT(false);
  //delay(100);
  debug(F("HandleBtnPTTAfter() Button pressed: "));
  debugln(fRig.interlock.state);
}

void HandleBtnMuteA()
{
  if (isShortPress(ButtonTime))
  {
    // short press - Mute A
    debugln("Mute A");
    if (MuteVal[A] == 1)
    {
      MuteVal[A] = 0;
    }
    else
    {
      MuteVal[A] = 1;
    }
    if (fRig.connected)
    {
      fRig.setAudioMute(A, MuteVal[A]);
    }
  }
  else
  {
    // long press - Select Slice A
    SelectSlice(A);

    debugln("Select Slice A");
  }
}

void HandleBtnNoiseBlankA()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (fRig.slice[A].nb == 0)
    {
      fRig.setNb(A, 1);
    }
    else
    {
      fRig.setNb(A, 0);
    }
  }
  else
  {
    // Long press
    if (!SetNB[A])
    {
      SetNB[A] = true;
      fRig.setNb(A, 1);
      LowAEnc.write(fRig.slice[A].nb_level * LowAEncSteps);
      SelectedTimer = millis();
    }
    else
    {
      SetNB[A] = false;
      LowAEnc.write(LowVal[A] * LowAEncSteps);
    }
    DispNB(A);
  }
}

void HandleBtnNoiseReductionA()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (fRig.slice[A].nr == 0)
    {
      fRig.setNr(A, 1);
    }
    else
    {
      fRig.setNr(A, 0);
    }
  }
  else
  {
    // Long press
    if (!SetNR[A])
    {
      SetNR[A] = true;
      fRig.setNr(A, 1);
      HighAEnc.write(fRig.slice[A].nr_level * HighAEncSteps);
      SelectedTimer = millis();
    }
    else
    {
      SetNR[A] = false;
      HighAEnc.write(HighVal[A] * HighAEncSteps);
    }
    DispNR(A);
  }
}

void HandleBtnRitSetA()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    // RIT A set
    if (!SetRIT[A] && !SetXIT[A])
    {
      //                  tft.drawRect(0, 117, 90, 23, HX8357_WHITE);
      fRig.setRit(A, 1);
      SetRIT[A] = true;
      SetXIT[A] = false;
      DispRIT(A);
      AGCAEnc.write((fRig.slice[A].rit_freq * AGCAEncSteps) / 10);
      SelectedTimer = millis();
    }
    else
    {
      tft.drawRect(0, 117, 90, 23, COLOR_BLACK);
      tft.drawRect(100, 117, 90, 23, COLOR_BLACK);
      SetRIT[A] = false;
      SetXIT[A] = false;
      AGCAEnc.write(fRig.slice[A].agc_threshold * AGCAEncSteps);
    }
  }
  else
  {
    // Long press
    // XIT A set
    if (!SetXIT[A] && !SetRIT[A])
    {
      //                  tft.drawRect(100, 117, 90, 23, HX8357_WHITE);
      fRig.setXit(A, 1);
      SetXIT[A] = true;
      SetRIT[A] = false;
      DispXIT(A);
      AGCAEnc.write((fRig.slice[A].xit_freq * AGCAEncSteps) / 10);
      SelectedTimer = millis();
    }
    else
    {
      if (SetRIT[A])
      {
        fRig.setRitFreq(A, 0);  // Zero RIT
        AGCAEnc.write(0);
      }
      else if (SetXIT[A])
      {
        fRig.setXitFreq(A, 0);  // Zero XIT
        AGCAEnc.write(0);
      }
      else
      {
        tft.drawRect(100, 117, 90, 23, COLOR_BLACK);
        tft.drawRect(0, 117, 90, 23, COLOR_BLACK);
        SetXIT[A] = false;
        SetRIT[A] = false;
        AGCAEnc.write(fRig.slice[A].agc_threshold * AGCAEncSteps);
      }
    }
  }
}

void HandleBtnMuteB()
{
  if (isShortPress(ButtonTime))
  {
    // short press - Mute B
    debugln("Mute B");
    if (MuteVal[B] == 1)
    {
      MuteVal[B] = 0;
    }
    else
    {
      MuteVal[B] = 1;
    }

    fRig.setAudioMute(B, MuteVal[B]);
  }
  else
  {
    // long press - Select Slice B
    SelectSlice(B);

    debugln("Select Slice B");
  }
}

void HandleBtnNoiseBlankB()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (fRig.slice[B].nb == 0)
    {
      fRig.setNb(B, 1);
    }
    else
    {
      fRig.setNb(B, 0);
    }
  }
  else
  {
    // Long press
    if (!SetNB[B])
    {
      SetNB[B] = true;
      fRig.setNb(B, 1);
      LowBEnc.write(fRig.slice[B].nb_level * LowBEncSteps);
      SelectedTimer = millis();
    }
    else
    {
      SetNB[B] = false;
      LowBEnc.write(LowVal[B] * LowBEncSteps);
    }
    DispNB(B);
  }
}

void HandleBtnNoiseReductionB()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (fRig.slice[B].nr == 0)
    {
      fRig.setNr(B, 1);
    }
    else
    {
      fRig.setNr(B, 0);
    }
  }
  else
  {
    // Long press
    if (!SetNR[B])
    {
      SetNR[B] = true;
      fRig.setNr(B, 1);
      HighBEnc.write(fRig.slice[B].nr_level * HighBEncSteps);
      SelectedTimer = millis();
    }
    else
    {
      SetNR[B] = false;
      HighBEnc.write(HighVal[B] * HighBEncSteps);
    }
    DispNR(B);
  }
}

void HandleBtnRitSetB()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    // RIT B set
    if (!SetRIT[B] && !SetXIT[B])
    {
      //                  tft.drawRect(250, 117, 90, 23, HX8357_WHITE);
      fRig.setRit(B, 1);
      SetRIT[B] = true;
      SetXIT[B] = false;
      DispRIT(B);
      AGCBEnc.write((fRig.slice[B].rit_freq * AGCBEncSteps) / 10);
      SelectedTimer = millis();
    }
    else
    {
      tft.drawRect(250, 117, 90, 23, COLOR_BLACK);
      tft.drawRect(350, 117, 90, 23, COLOR_BLACK);
      SetRIT[B] = false;
      SetXIT[B] = false;
      AGCBEnc.write(fRig.slice[B].agc_threshold * AGCBEncSteps);
    }
  }
  else
  {
    // Long press
    // XIT B set
    if (!SetXIT[B] && !SetRIT[B])
    {
      //                  tft.drawRect(350, 117, 90, 23, HX8357_WHITE);
      fRig.setXit(B, 1);
      SetXIT[B] = true;
      SetRIT[B] = false;
      DispXIT(B);
      AGCBEnc.write((fRig.slice[B].xit_freq * AGCBEncSteps) / 10);
      SelectedTimer = millis();
    }
    else
    {
      if (SetRIT[B])
      {
        fRig.setRitFreq(B, 0);  // Zero RIT
        AGCBEnc.write(0);
      }
      else if (SetXIT[B])
      {
        fRig.setXitFreq(B, 0);  // Zero XIT
        AGCBEnc.write(0);
      }
      else
      {
        tft.drawRect(250, 117, 90, 23, COLOR_BLACK);
        tft.drawRect(350, 117, 90, 23, COLOR_BLACK);
        SetXIT[B] = false;
        SetRIT[B] = false;
        AGCBEnc.write(fRig.slice[B].agc_threshold * AGCBEncSteps);
      }
    }
  }
}

void HandleBtnSelect()
{
  bool CWmode = false;

  if (isShortPress(ButtonTime))
  {
    // Short press
    if (MenuActive)
    {
      if (MenuItemIDX == 0)
      {
        MenuExit();
      }

      debug("MenuIDX: ");
      debug(MenuIDX);
      debug("  MenuItemIDX: ");
      debug(MenuItemIDX);
      debug("  MenuAction: ");
      debugln(MenuAction[MenuIDX][MenuItemIDX - 1]);
      switch (MenuAction[MenuIDX][MenuItemIDX - 1])
      {
        case BtnMenuNone:
          // NOP
          break;

        case BtnMenuProfLoad:
          // Load named global profile via API
          fRig.loadGlobalProfile(MenuItem[MenuIDX][MenuItemIDX - 1]);
          MenuExit();
          break;

        case BtnMenuConfigReload:  // reload config file
          CWMsgNum = 0;
          for (int i = 0; i < 12; i++)
          {
            CWMsg[i] = "";
          }
          GetConfigFile();
          LoadCWMsgMenu();
          MenuExit();
          break;

        case BtnMenuFilter:
          if (fRig.slice[SliceActiveVal].mode == "USB")
          {
            debugln("SSB Filter");
            fRig.setRxFiltLow(SliceActiveVal, SSBFilterLow[MenuItemIDX - 1]);
            fRig.setRxFiltHigh(SliceActiveVal, SSBFilterHigh[MenuItemIDX - 1]);
          }

          if (fRig.slice[SliceActiveVal].mode == "LSB")
          {
            debugln("SSB Filter");
            fRig.setRxFiltLow(SliceActiveVal, SSBFilterHigh[MenuItemIDX - 1] * -1);
            fRig.setRxFiltHigh(SliceActiveVal, SSBFilterLow[MenuItemIDX - 1] * -1);
          }

          if (fRig.slice[SliceActiveVal].mode == "DIGU")
          {
            debugln("DIGU Filter");
            fRig.setRxFiltLow(SliceActiveVal, DigiFilterLow[MenuItemIDX - 1]);
            fRig.setRxFiltHigh(SliceActiveVal, DigiFilterHigh[MenuItemIDX - 1]);
          }

          if (fRig.slice[SliceActiveVal].mode == "DIGL")
          {
            debugln("DIGL Filter");
            fRig.setRxFiltLow(SliceActiveVal, DigiFilterHigh[MenuItemIDX - 1] * -1);
            fRig.setRxFiltHigh(SliceActiveVal, DigiFilterLow[MenuItemIDX - 1] * -1);
          }

          if (fRig.slice[SliceActiveVal].mode == "RTTY")
          {
            debugln("RTTY Filter");
            fRig.setRxFiltLow(SliceActiveVal, RTTYFilterLow[MenuItemIDX - 1]);
            fRig.setRxFiltHigh(SliceActiveVal, RTTYFilterHigh[MenuItemIDX - 1]);
          }

          if (fRig.slice[SliceActiveVal].mode == "AM" || fRig.slice[SliceActiveVal].mode == "SAM")
          {
            debugln("AM Filter");
            fRig.setRxFiltLow(SliceActiveVal, AMFilterLow[MenuItemIDX - 1]);
            fRig.setRxFiltHigh(SliceActiveVal, AMFilterHigh[MenuItemIDX - 1]);
          }

          if (fRig.slice[SliceActiveVal].mode == "DFM")
          {
            debugln("DFM Filter");
            fRig.setRxFiltLow(SliceActiveVal, DFMFilterLow[MenuItemIDX - 1]);
            fRig.setRxFiltHigh(SliceActiveVal, DFMFilterHigh[MenuItemIDX - 1]);
          }


          if (fRig.slice[SliceActiveVal].mode == "CW")
          {
            debugln("CW Filter");
            fRig.setRxFiltWidth(SliceActiveVal, CWFilterWidth[MenuItemIDX - 1]);
            fRig.setRxFiltShift(SliceActiveVal, 0);
          }

          MenuExit();
          break;

        case BtnMenuMem:
          fRig.send("memory apply " + String(fRig.Mem[MenuItemIDX - 1 + ((MenuIDX - MemMenuStart) * (MaxMenuItems - 1))]));
          MenuExit();

          break;

        case BtnMenuCWMsg:
          if (CWMsgMenuOpt)
          {
            SendMsg(CWMsg[MenuItemIDX - 1]);
          }

          break;

        case BtnMenuCWMsgSource:
          debug("<");
          debug(CWMsgSource);
          debugln(">");
          if (CWMsgSource == "TEENSY")
          {
            CWMsgSource = "FLEX";
          }
          else
          {
            CWMsgSource = "TEENSY";
          }

          MenuItem[MenuIDX][0] = "CW Message Source: " + CWMsgSource;
          //DispMenu(MenuIDX);
          MenuItemIDX = 0;
          MenuExit();

          break;

        case BtnMenuCWPaddleHand:
          //
          debug("Handed: ");
          debugln(Handed);
          if (Handed == 0)
          {
            Handed = 1;
          }
          else
          {
            Handed = 0;
          }

          if (Handed == 0)
          {
            DotPin    = 30;
            DashPin   = 31;
            HandedTxt = "Right Handed";
          }
          else
          {
            DotPin    = 31;
            DashPin   = 30;
            HandedTxt = "Left Handed";
          }

          if (MyCall == "W4WKU" && Handed == 1)
          {
            HandedTxt = "Dave Handed";
          }

          MenuItem[MenuIDX][1] = "CW Paddles: " + HandedTxt;

          //DispMenu(MenuIDX);
          MenuItemIDX = 0;
          MenuExit();

          break;

        case BtnMenuCWKeyMode:
          if (KeyMode == "A")
          {
            KeyMode = "B";
          }
          else if (KeyMode == "B")
          {
            KeyMode = "C";
          }
          else if (KeyMode == "C")
          {
            KeyMode = "S";
          }
          else if (KeyMode == "S")
          {
            KeyMode = "U";
          }
          else if (KeyMode == "U")
          {
            KeyMode = "A";
          }

          MenuItem[MenuIDX][2] = "CW Mode: " + KeyMode;

          DispMenu(MenuIDX);
          MenuItemIDX = 0;
          //RefreshScreen();

          break;

        case BtnMenuOOBInd:
          if (OOBindicator)
          {
            OOBindicator = false;
            OOBI         = "OFF";
          }
          else
          {
            OOBindicator = true;
            OOBI         = "ON";
          }

          MenuItem[MenuIDX][3] = "Out of Band Display: " + OOBI;

          CheckInBand(A);
          CheckInBand(B);

          DispMenu(MenuIDX);
          MenuItemIDX = 0;

          break;

        case BtnMenuTeensyRestart:
          tft.fillScreen(COLOR_NAVY);
          // tft.fillScreen(HX8357_NAVY);
          tft.setTextColor(COLOR_YELLOW);
          // tft.setTextColor(COLOR_YELLOW);
          tft.setFont(Arial_28_Bold);
          tft.setCursor(100, 130);
          tft.print("RESTARTING");
          debug("Restart*********************************************");
          arm_reset();

          //                      MenuItemIDX = 0;
          //                      MenuExit();

          break;

        case BtnMenuCWSetSerNum:
          if (SerNumMenuActive)
          {
            SerNumMenuActive = false;
            TM_SerNum_SaveEEPROM();
            MenuItemIDX = 0;
            MenuExit();
          }
          else
          {
            tmpSerNum        = SerNum;
            SerNumMenuActive = true;
            CWMicEnc.write(SerNum * CWEncSteps);
            tft.drawRect(0, 123, 480, 21, COLOR_BLACK);
            tft.drawRect(225, 123, 100, 21, COLOR_YELLOW);
            tft.setFont(Arial_14);
            tft.setTextColor(COLOR_YELLOW);
          }

          break;

        case BtnMenuCWResetSerNum:
          SerNum     = 1;
          SerNumSave = 1;
          tmpSerNum  = 1;
          TM_SerNum_SaveEEPROM();
          MenuItem[CWMenu][5] = "Set contest serial number: " + String(SerNum);
          MenuItemIDX         = 0;
          MenuExit();

          break;

        case BtnMenuPowerOff:
          power_down("menu: Power Off selected");
          break;

        case BtnMenuSnapToStep:
          if (SnapToStep)
          {
            SnapToStep    = false;
            SnapToSteptxt = "OFF";
          }
          else
          {
            SnapToStep    = true;
            SnapToSteptxt = "ON";
          }

          MenuItem[MenuIDX][4] = "Snap to Tune Step: " + SnapToSteptxt;

          DispMenu(MenuIDX);
          MenuItemIDX = 0;

          break;

        case BtnMenuVFOTrack:
          if (VFOTrack)
          {
            VFOTrack    = false;
            VFOTrackInd = "OFF";
          }
          else
          {
            VFOTrack    = true;
            VFOTrackInd = "ON";
          }

          MenuItem[MenuIDX][5] = "VFO Tracking: " + VFOTrackInd;

          DispMenu(MenuIDX);
          MenuItemIDX = 0;

          break;

        case BtnMenuAtoB:
          if (fRig.slice[A].in_use == 1 && fRig.slice[B].in_use == 1)
          {
            fRig.setFreq(B, fRig.slice[A].RF_frequency);
            DispFrq(B);
            CurFreq[A] = fRig.slice[A].RF_frequency;
            CurFreq[B] = fRig.slice[B].RF_frequency;
          }
          MenuItemIDX = 0;
          MenuExit();

          break;

        case BtnMenuBtoA:
          if (fRig.slice[A].in_use == 1 && fRig.slice[B].in_use == 1)
          {
            fRig.setFreq(A, fRig.slice[B].RF_frequency);
            DispFrq(A);
            CurFreq[A] = fRig.slice[A].RF_frequency;
            CurFreq[B] = fRig.slice[B].RF_frequency;
          }
          MenuItemIDX = 0;
          MenuExit();

          break;

        case BtnMenuSideToneOnOff:
          SideTone = !SideTone;
          if (SideTone)
          {
            STtxt = "ON";
          }
          else
          {
            STtxt = "OFF";
          }

          MenuItem[MenuIDX][3] = "CW Sidetone: " + STtxt;

          DispMenu(MenuIDX);
          MenuItemIDX = 0;
          break;

        case BtnMenuSideToneFreq:
          if (STFreqMenuActive)
          {
            STFreqMenuActive = false;
            MenuItemIDX      = 0;
            MenuExit();
          }
          else
          {
            tmpSTFreq        = STFreq;
            STFreqMenuActive = true;
            CWMicEnc.write((STFreq * CWEncSteps) / 10);
            tft.drawRect(0, 102, 480, 21, COLOR_BLACK);
            // tft.drawRect(0, 102, 480, 21, COLOR_BLACK);
            tft.drawRect(166, 103, 60, 19, COLOR_YELLOW);
            // tft.drawRect(166, 103, 60, 19, COLOR_YELLOW);
            //delay(5000);
            tft.setFont(Arial_14);
            tft.setTextColor(COLOR_YELLOW);
            // tft.setTextColor(COLOR_YELLOW);
          }

          break;

        case BtnMenuSetMode:
          //debug(SliceActiveVal); debugln(ModeMenu[MenuItemIDX - 1]);
          fRig.setMode(SliceActiveVal, ModeMenu[MenuItemIDX - 1]);
          MenuExit();

          break;

        case BtnMenuSetRFPwr:
          if (RFPowerMenuActive)
          {
            debug("Set RF Power: ");
            debugln(RFPower);
            fRig.setRfPower(RFPower);
            fRig.process();
            RFPowerMenuActive = false;
            MenuItemIDX       = 0;
            MenuExit();
          }
          else
          {
            RFPower           = fRig.transmit.rfpower;
            RFPowerMenuActive = true;
            CWMicEnc.write(RFPower * CWEncSteps);
            tft.drawRect(0, 18, 480, 21, COLOR_BLACK);
            // tft.drawRect(0, 18, 480, 21, COLOR_BLACK);
            tft.drawRect(92, 18, 60, 19, COLOR_YELLOW);
            // tft.drawRect(92, 18, 60, 19, COLOR_YELLOW);
            tft.setFont(Arial_14);
            tft.setTextColor(COLOR_YELLOW);
            // tft.setTextColor(COLOR_YELLOW);
          }

          break;

        case BtnMenuSetMicGain:
          if (MicGainMenuActive)
          {
            debug("Set Mic Gain: ");
            debugln(MicGain);
            fRig.setMicLevel(MicGain);
            fRig.process();
            MicGainMenuActive = false;
            MenuItemIDX       = 0;
            MenuExit();
          }
          else
          {
            MicGain           = fRig.transmit.mic_level;
            MicGainMenuActive = true;
            CWMicEnc.write(MicGain * CWEncSteps);
            tft.drawRect(0, 39, 480, 21, COLOR_BLACK);
            tft.drawRect(80, 40, 60, 19, COLOR_YELLOW);
            tft.setFont(Arial_14);
            tft.setTextColor(COLOR_YELLOW);
          }

          break;

        case BtnMenuCWLocalEth:
        {
          bool CWmode = false;
          for (int i = 0; i < 2; i++) {
            if (fRig.slice[i].mode == "CW" && fRig.slice[i].tx == 1) {
              CWmode = true;
            }
          }

          // Cycle through modes: LOCAL -> ETHERNET -> BOTH -> LOCAL...
          if (KeyerOut == "LOCAL") {
            KeyerOut = "ETHERNET";
          } else if (KeyerOut == "ETHERNET") {
            KeyerOut = "BOTH";
          } else {
            KeyerOut = "LOCAL";
          }
          
          // Logic for CW Delay handling when switching TO network modes
          if (KeyerOut == "ETHERNET" || KeyerOut == "BOTH") {
             SaveBkInDelay = fRig.transmit.break_in_delay; // Backup current radio delay
             if (fRig.transmit.break_in_delay < 30 && CWmode) {
                debug("Enforcing Min Delay (Net): "); debugln(30);
                fRig.setCwBreakinInDelay(30);
             }
          } 
          // Logic for restoring delay when switching back to pure LOCAL
          else if (KeyerOut == "LOCAL") {
             debug("Restoring Delay (Local): "); debugln(SaveBkInDelay);
             fRig.setCwBreakinInDelay(SaveBkInDelay);
          }

          MenuItem[MenuIDX][7] = "Keyer Output: " + KeyerOut;
          DispMenu(MenuIDX);
          MenuItemIDX = 0;
          break;
        }

        case BtnMenuSetBand:
          ActivePan = fRig.slice[SliceActiveVal].pan;
          debug("SliceActiveVal: ");
          debugln(SliceActiveVal);
          debug("ActivePan: ");
          debugln(ActivePan);
          debug("Band: ");
          debugln(BandMenu[MenuItemIDX - 1]);
          if (ActivePan > 0)
          {
            {
              const size_t panCount = TMU_ArrayLen(fRig.panadapter);
              const int idx = TMU_HandleToPanIndexSafe((uint32_t)ActivePan, panCount);
              if (idx >= 0) {
                fRig.setBand(idx, BandMenu[MenuItemIDX - 1]);
              } else {
                // Optional debug: invalid handle
                // Serial.println("[WARN] setBand: invalid ActivePan handle");
              }
            }
          }
          MenuExit();

          break;

        case 25:
          //debugln("Shutting Down " + fRig.Client_Handle[ClientMenuIDX]);
          // disabled for now.  Not sure how to make client disconnect work
          //client.stop();
          //                      if(fRig.Client_Station[ClientMenuItem].indexOf("TeensyMaestro") > -1)
          //                      {
          //                        for(int i=0; i<fRig.nMaxSlice; i++)
          //                        {
          //                          fRig.removeSlice(i);
          //                        }
          //
          //                        debugln("********************* disconnect ********************");
          //                        debugln(fRig.Client_Handle[ClientMenuItem]);
          //                        fRig.send("client disconnect " + fRig.Client_Handle[ClientMenuItem]);
          //                        fRig.process();
          //                      }
          MenuExit();

          break;

        case BtnMenuClientBind:
          ClientMenuItem = MenuItemIDX - 1;
          fRig.send("client bind client_id=" + String(fRig.Client_ID[ClientMenuItem]));

          MenuExit();

          break;

        case BtnMenuShortPressClick:  // ShortPressClick
          ShortPressClick = !ShortPressClick;

          if (ShortPressClick)
          {
            ShortPressInd = "ON";
          }
          else
          {
            ShortPressInd = "OFF";
          }

          MenuItem[MenuIDX][6] = "Short Press Click: " + ShortPressInd;
          DispMenu(MenuIDX);
          MenuItemIDX = 0;

          break;

        case BtnMenuLongPressClick:  // LongPressClick
          LongPressClick = !LongPressClick;

          if (LongPressClick)
          {
            LongPressInd = "ON";
          }
          else
          {
            LongPressInd = "OFF";
          }
          MenuItem[MenuIDX][7] = "Long Press Click: " + LongPressInd;
          DispMenu(MenuIDX);
          MenuItemIDX = 0;

          break;

        case BtnMenuShowSplash:  // Show info Screen
          UI_Boot::showInfo();
          MenuItemIDX = 0;
          Splash      = true;
          SplashM     = true;
          MenuActive  = false;
          MenuExit();

          break;

        case BtnMenuCenterCntlFcn:  // Center Control Function
          Encoder_9++;
          if (Encoder_9 > Enc9_Max)
          {
            Encoder_9 = 0;
          }

          MenuItem[MenuIDX][11] = "Center Control Function: " + Enc9_Text[Encoder_9];
          DispMenu(MenuIDX);
          MenuItemIDX = 0;

          break;

        case BtnMenuVFOaccel:  // VFO Acceleration on/off
          VFOaccel = !VFOaccel;
          if (VFOaccel)
          {
            VFOaccelInd = "ON";
          }
          else
          {
            VFOaccelInd = "OFF";
          }

          MenuItem[MenuIDX][12] = "VFO Acceleration: " + VFOaccelInd;
          DispMenu(MenuIDX);
          MenuItemIDX = 0;

          break;

        case BtnMenuAccelFactor:  // VFO Acceleration Factor.  Smaller is faster.
          if (AccelMenuActive)
          {
            AccelMenuActive = false;
            MenuItemIDX     = 0;
            MenuExit();
          }
          else
          {
            tmpAccelFactor  = AccelFactor;
            AccelMenuActive = true;
            CWMicEnc.write((AccelFactor * CWEncSteps) / 100);
            tft.drawRect(0, 291, 480, 21, COLOR_BLACK);
            tft.drawRect(216, 292, 80, 19, COLOR_YELLOW);
            tft.setFont(Arial_14);
            tft.setTextColor(COLOR_YELLOW);
          }

          //MenuItem[MenuIDX][13] = "VFO Acceleration Factor: " + AccelFactor;
          //DispMenu(MenuIDX);
          //MenuItemIDX = 0;
          break;
      }
    }
    else  // Menu
    {
      MenuIDX = RecentMenuIDX;
      for (int Slice = 0; Slice < 2; Slice++)
      {
        if (fRig.connected)
        {
          if (fRig.slice[Slice].tx == 1 && fRig.slice[Slice].mode == "CW")
          {
            if (CWMsgMenuOpt)
            {
              MenuIDX = CWMsgMenu;
            }
            break;
          }
        }
        else
        {
          if (CWMsgMenuOpt)
          {
            MenuIDX = CWMsgMenu;
          }
          break;
        }
      }
      MenuActive  = true;
      MenuItemIDX = 0;
      CWMicEnc.write(0);
      delay(50);
      DispMenu(MenuIDX);
    }
  }
  else  // Long Press Menu Select
  {
    if (millis() - ButtonTime > RestartTime)
    {
      //  Extra long press to reset Teensy
      arm_reset();
      return;
    }

    // Long press
    if (!MenuActive)
    {
      MenuActive  = true;
      MenuIDX     = 0;
      MenuItemIDX = 0;
      CWMicEnc.write(0);
      delay(50);
      DispMenu(MenuIDX);
    }
    else
    {
      if (SerNumMenuActive)
      {
        SerNum              = tmpSerNum;
        MenuItem[CWMenu][5] = "Set contest serial number: " + String(SerNum);
      }

      if (STFreqMenuActive)
      {
        STFreq              = tmpSTFreq;
        MenuItem[CWMenu][4] = "CW Sidetone Freq: " + String(STFreq);
      }

      if (AccelMenuActive)
      {
        AccelFactor            = tmpAccelFactor;
        MenuItem[MiscMenu][13] = "VFO Acceleration Factor: " + String(AccelFactor);
      }

      if (RFPowerMenuActive)
      {
        RFPower                   = fRig.transmit.rfpower;
        MenuItem[TransmitMenu][0] = "RF Power: " + String(RFPower);
      }
      MenuExit();
    }
  }
}

void HandleBtnRitA()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (!MenuActive)
    {
      if (fRig.slice[A].rit_on == 1)
      {
        fRig.setRit(A, 0);
      }
      else
      {
        fRig.setRit(A, 1);
      }

      DispRIT(A);
    }
    else
    {
      MenuIDX--;
      if (MenuIDX < 0)
      {
        MenuIDX = LastMenuIDX;
      }
      MenuItemIDX = 0;
      CWMicEnc.write(0);
      DispMenu(MenuIDX);
    }
  }
  else
  {
    // Long press
    if (!MenuActive)
    {
      VFOStep[A] -= 1;

      if (VFOStep[A] < 0)
      {
        VFOStep[A] = 0;
      }

      DispStep(A);
    }
    else
    {
      MenuIDX--;
      if (MenuIDX < 0)
      {
        MenuIDX = LastMenuIDX;
      }
      MenuItemIDX = 0;
      CWMicEnc.write(0);
      DispMenu(MenuIDX);
    }
  }
}

void HandleBtnXitA()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (fRig.slice[A].xit_on == 1)
    {
      fRig.setXit(A, 0);
    }
    else
    {
      fRig.setXit(A, 1);
    }

    DispXIT(A);
  }
  else
  {
    // Long press
    VFOStep[A] += 1;

    if (VFOStep[A] > 8)
    {
      VFOStep[A] = 8;
    }

    DispStep(A);
  }
}

void HandleBtnRitB()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (fRig.slice[B].rit_on == 1)
    {
      fRig.setRit(B, 0);
    }
    else
    {
      fRig.setRit(B, 1);
    }

    DispRIT(B);
  }
  else
  {
    // Long press
    VFOStep[B] -= 1;

    if (VFOStep[B] < 0)
    {
      VFOStep[B] = 0;
    }

    DispStep(B);
  }
}

void HandleBtnXitB()
{
  if (isShortPress(ButtonTime))
  {
    // Short press
    if (!MenuActive)
    {
      if (fRig.slice[B].xit_on == 1)
      {
        fRig.setXit(B, 0);
      }
      else
      {
        fRig.setXit(B, 1);
      }

      DispXIT(B);
    }
    else
    {
      MenuIDX++;
      if (MenuIDX > LastMenuIDX)
      {
        MenuIDX = 0;
      }
      MenuItemIDX = 0;
      CWMicEnc.write(0);
      DispMenu(MenuIDX);
    }
  }
  else
  {
    // Long press
    if (!MenuActive)
    {
      VFOStep[B] += 1;

      if (VFOStep[B] > 8)
      {
        VFOStep[B] = 8;
      }

      DispStep(B);
    }
    else
    {
      MenuIDX++;
      if (MenuIDX > LastMenuIDX)
      {
        MenuIDX = 0;
      }
      MenuItemIDX = 0;
      CWMicEnc.write(0);
      DispMenu(MenuIDX);
    }
  }
}

/***************************** SelectSlice ***************************
 * NOTE:
 * - No automatic slice creation. This function is now a pure selector.
 * - If the requested slice does not exist (in_use == 0), it returns with no side effects.
 * - Rationale: deterministic behavior and to avoid "ghost" slices when running headless
 *   or with DisableGUIClient=true. Explicit "Split" (if desired) should be handled elsewhere.
 *********************************************************************/
void SelectSlice(int Slice)
{
  // Normalize to A/B and compute the other slice for potential future use.
  const int thisSlice  = (Slice == A) ? A : B;
  //const int otherSlice = (thisSlice == A) ? B : A;

  // If target slice does not exist, do nothing.
  if (fRig.slice[thisSlice].in_use == 0) {
    return;
  }

  // Keep the bookkeeping deterministic: make the chosen slice active.
  SliceActiveVal = thisSlice;

  // Tell the rig which slice is active.
  fRig.setActiveSlice(thisSlice, 1);
}

/***************************** CWButton ***************************/
void CWButton(int base_btn, bool isLongPress)
{
  // Compute button index (1..6 short, 7..12 long)
  int Btn = base_btn;
  if (isLongPress) {
    Btn = Btn + 6;
  }

  if (CWMsgSource == "FLEX") {
    // Let the radio send the macro if configured that way
    SendFlexMsg(Btn);
  } else {
    // Local Teensy keyer sends the macro
    SendMsg(CWMsg[Btn - 1]);
  }
}
