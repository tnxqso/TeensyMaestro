#include "ui_boot.h"
#include "StunHelper.h"
#include "tm_time.h"
#include "tm_flex_guard.h"
#include "Display_Driver.h"
#include "tm_touch_helpers.h"
#include "tm_system_utils.h"

extern bool gFixedEndpointUsed;
extern char gFixedEndpointLabel[64];
extern const uint8_t TS_CS;

// ---- Slice/pan safety helpers ----
static inline bool TM_HaveAnySlice() {
  return (fRig.slice[0].in_use == 1) || (fRig.slice[1].in_use == 1);
}

static inline bool TM_HaveCwSlice() {
  for (int s = 0; s < 2; ++s) {
    if (fRig.slice[s].in_use == 1 && fRig.slice[s].mode == "CW") return true;
  }
  return false;
}

// Returns active slice index [0..1] or -1 if none
static inline int TM_GetActiveSlice() {
  for (int s = 0; s < 2; ++s) if (fRig.slice[s].active == 1) return s;
  return -1;
}

// Convert radio pan ID (e.g. 40000000..) to array index, or -1 if invalid
static inline int TM_PanIndexFromId(int pan_id) {
  long idx = TMU_HandleToPanIndexSafe((uint32_t)pan_id, TMU_ArrayLen(fRig.panadapter));
  if (idx < 0 || idx >= fRig.nMaxPanadapter) return -1;
  return (int)idx;
}

// Show connection mode on the single bottom progress line.
FLASHMEM static void ShowConnModeProgress() {
  const char* suffix = "Auto discovery";
  switch (CFG_ConnMode) {
    case TM_CONN_FIXED:            suffix = "Fixed"; break;
    case TM_CONN_FIXED_FAILOVER:   suffix = "Fixed+Failover"; break;
    case TM_CONN_AUTO:
    default:                       suffix = "Auto discovery"; break;
  }

  char line[96];
  snprintf(line, sizeof(line), "Connection mode: %s", suffix);
  UI_Boot::Prog(BootStage::InitNetwork, line);
  UI_Boot::showProgress(BootStage::InitNetwork, line);
}

// [TM_DISPLAY_BRINGUP] --- begin
static inline void TM_DisplayBringUp()
{
  // Park CS pins HIGH before touching any SPI device
  pinMode(TS_CS, OUTPUT);
  digitalWriteFast(TS_CS, HIGH);

  // Hand the driver to our UI layer
  UI_Boot::attachTFT(&tft);

  // Panel reset & init
  Display::preInit(TS_CS);
  Display::init();

  // Detect if this is the first boot after flashing (not a normal restart)
  static uint32_t s_bootCount = 0;
  s_bootCount++;
  if (s_bootCount == 1) {
    delay(250);   // slightly longer after flash to avoid bootloop
  } else {
    delay(30);    // normal settle time
  }
}
// [TM_DISPLAY_BRINGUP] --- end

/***************************** TeensyMaestroSetup ***************************/
bool TM_ComputeFlexTargetIP();

FLASHMEM void TeensyMaestroSetup()
{
  debugln(F(TM_FULL_NAME_WITH_VERSION));

  if (PowerBtn == "POWER")
  {
    set_arm_power_button_press_on_time(arm_power_button_press_on_time_50ms);                // 50 ms to hold power button for startup
    set_arm_power_button_press_time_emergency(arm_power_button_press_time_emergency_5sec);  // 5 sec hold time to shut off
    set_arm_power_button_callback(&ShutDownCB);
  }
  else if (PowerBtn == "POWER FAST")
  {
    set_arm_power_button_press_on_time(arm_power_button_press_on_time_50ms);  // 50 ms to hold power button for startup
    set_arm_power_button_callback(&ShutDownCB);
  }
  else if (PowerBtn == "RESET")
  {
    set_arm_power_button_press_on_time(arm_power_button_press_on_time_0ms);  // 0 ms to hold power button for startup
    set_arm_power_button_callback(&ShutDownCB);                              // Immediate shut off from on/off button press
  }

 
  TM_DisplayBringUp();
  UI_Boot::showInitialBanner();
  //UI_Boot::showProgress(BootStage::InitNetwork);

    // Start I2C

  // MUX, Button, Name
  buttonMap[BTN_NONE]             = { -1, -1, "BTN_NONE" };
  buttonMap[BTN_VFO_A_MUTE_SLICE] = { 0, 0, "BTN_VFO_A_MUTE_SLICE" };
  buttonMap[BTN_VFO_A_RIT_SEL]    = { 0, 1, "BTN_VFO_A_RIT_SEL" };
  buttonMap[BTN_VFO_A_NB]         = { 0, 2, "BTN_VFO_A_NB" };
  buttonMap[BTN_VFO_A_NR]         = { 0, 3, "BTN_VFO_A_NR" };
  buttonMap[BTN_VFO_A_RIT]        = { 0, 4, "BTN_VFO_A_RIT" };
  buttonMap[BTN_VFO_A_XIT]        = { 0, 5, "BTN_VFO_A_XIT" };
  buttonMap[BTN_MENU_SEL]         = { 0, 6, "BTN_MENU_SEL" };
  // No entry for Mux A, pin 7 since it's used for output

  buttonMap[BTN_VFO_B_MUTE_SLICE] = { 1, 0, "BTN_VFO_B_MUTE_SLICE" };
  buttonMap[BTN_VFO_B_RIT_SEL]    = { 1, 1, "BTN_VFO_B_RIT_SEL" };
  buttonMap[BTN_VFO_B_NB]         = { 1, 2, "BTN_VFO_B_NB" };
  buttonMap[BTN_VFO_B_NR]         = { 1, 3, "BTN_VFO_B_NR" };
  buttonMap[BTN_VFO_B_RIT]        = { 1, 4, "BTN_VFO_B_RIT" };
  buttonMap[BTN_VFO_B_XIT]        = { 1, 5, "BTN_VFO_B_XIT" };
  buttonMap[BTN_PTT]              = { 1, 6, "BTN_PTT" };
  buttonMap[BTN_TUNE]             = { 1, 7, "BTN_TUNE" };

  buttonMap[BTN_ACC_0] = { 2, 0, "BTN_ACC_0" };
  buttonMap[BTN_ACC_1] = { 2, 1, "BTN_ACC_1" };
  buttonMap[BTN_ACC_2] = { 2, 2, "BTN_ACC_2" };  // NV0E - Future accessory button
  buttonMap[BTN_ACC_3] = { 2, 3, "BTN_ACC_3" };  // NV0E - Future accessory button
  buttonMap[BTN_ACC_4] = { 2, 4, "BTN_ACC_4" };  // NV0E - Future accessory button
  buttonMap[BTN_ACC_5] = { 2, 5, "BTN_ACC_5" };  // NV0E - Future accessory button
  buttonMap[BTN_ACC_6] = { 2, 6, "BTN_ACC_6" };  // NV0E - Future accessory button
  buttonMap[BTN_ACC_7] = { 2, 7, "BTN_ACC_7" };  // NV0E - Future accessory button

  buttonMap[BTN_CW_MSG_1] = { 2, 8, "BTN_CW_MSG_1" };
  buttonMap[BTN_CW_MSG_2] = { 2, 9, "BTN_CW_MSG_2" };
  buttonMap[BTN_CW_MSG_3] = { 2, 10, "BTN_CW_MSG_3" };
  buttonMap[BTN_CW_MSG_4] = { 2, 11, "BTN_CW_MSG_4" };
  buttonMap[BTN_CW_MSG_5] = { 2, 12, "BTN_CW_MSG_5" };
  buttonMap[BTN_CW_MSG_6] = { 2, 13, "BTN_CW_MSG_6" };
  buttonMap[BTN_ACC_8]    = { 2, 14, "BTN_ACC_8" };  // NV0E - Future accessory button
  buttonMap[BTN_ACC_9]    = { 2, 15, "BTN_ACC_9" };  // NV0E - Future accessory button

  debugln("Beginning I2C connection for muxA");
  muxA_found = muxA.begin_I2C(0x20);
  if (muxA_found)
  {
    debugln("  Success connecting to muxA");

    for (int i = 0; i < 8; i++)
    {
      if (muxA_arr[i].gpioDirection == GPIO_INPUT)
      {
        muxA.pinMode(i, INPUT_PULLUP);
        debug(i);
        debug(" ");
        debugln(muxA.digitalRead(i));
      }
      else if (muxA_arr[i].gpioDirection == GPIO_OUTPUT)
      {
        muxA.pinMode(i, OUTPUT);
        muxA.digitalWrite(i, HIGH);  // Turn GPIO to default state
      }
    }

    debugln("  GPIOs configured on muxA");
  }
  else
  {
    debugln("  FAILURE connecting to muxA");
  }

  debugln("Beginning I2C connection for muxB");
  muxB_found = muxB.begin_I2C(0x21);
  if (muxB_found)
  {
    debugln("  Success connecting to muxB");
    for (int i = 0; i < 8; i++)
    {
      if (muxB_arr[i].gpioDirection == GPIO_INPUT)
      {
        muxB.pinMode(i, INPUT_PULLUP);
        debug(i);
        debug(" ");
        debugln(muxB.digitalRead(i));
      }
      else if (muxB_arr[i].gpioDirection == GPIO_OUTPUT)
      {
        muxB.pinMode(i, OUTPUT);
        muxB.digitalWrite(i, HIGH);  // Turn GPIO to default state
      }
    }
    debugln("  GPIOs configured on muxB");
  }
  else
  {
    debugln("  FAILURE connecting to muxB");
  }

  debugln("Beginning I2C connection for muxC");
  muxC_found = muxC.begin_I2C(0x22);
  if (muxC_found)
  {
    debugln("  Success connecting to muxC");
    for (int i = 0; i < 16; i++)  // NV0E - Change to 16 for MCP23017 expander
    {
      if (muxC_arr[i].gpioDirection == GPIO_INPUT)
      {
        muxC.pinMode(i, INPUT_PULLUP);
        debug(i);
        debug(" ");
        debugln(muxC.digitalRead(i));
      }
      else if (muxC_arr[i].gpioDirection == GPIO_OUTPUT)
      {
        muxC.pinMode(i, OUTPUT);
        muxC.digitalWrite(i, HIGH);  // Turn GPIO to default state
      }
    }
    debugln("  GPIOs configured on muxC");
  }
  else
  {
    debugln("  FAILURE connecting to muxC");
  }


  getIpAddress();  // Will get DHCP address or fixed ip if no DHCP address is served

  // --- Run STUN only for Fixed / Fixed+Failover, and only if target is off-LAN ---
  auto sameSubnet = [](const IPAddress &a, const IPAddress &b, const IPAddress &mask) {
    for (int i = 0; i < 4; ++i) if ( (a[i] & mask[i]) != (b[i] & mask[i]) ) return false;
    return true;
  };
  
  if (CFG_ConnMode == TM_CONN_FIXED || CFG_ConnMode == TM_CONN_FIXED_FAILOVER) {
    // Resolve CFG_FlexHost -> CFG_FlexIp[] (literal or DNS)
    if (TM_ComputeFlexTargetIP()) {
      IPAddress myIP   = Ethernet.localIP();
      IPAddress myMask = Ethernet.subnetMask();
      IPAddress tgt(CFG_FlexIp[0], CFG_FlexIp[1], CFG_FlexIp[2], CFG_FlexIp[3]);

      // Only punch NAT if target is not on our local L2 / subnet
      if (!sameSubnet(myIP, tgt, myMask)) {
        DoStunAndAdoptVitaPort(UDP_VITA49_PORT);
      } else {
        debugln("Target is on local subnet -> STUN skipped.");
      }
    } else {
      debugln("No valid Fixed target IP -> STUN skipped.");
    }
  } else {
    debugln("Connection mode uses discovery -> STUN skipped.");
  }

    TM_AttemptFlexConnect();

  if (fRig.connected)
  {
    debugln("Connected");

    TimeIt = millis();
    while ((millis() - TimeIt) < 2000)  //simulating main loop
    {
      //fRig.fireEvents();
      fRig.process();  //and processing intial rig data
      delay(100);
    }

    //Configuring Events
    configureRadioEvents();
    configureEqEvents();
    configureInterlockEvents();
    configureTransmitEvents();
    configurePanadapterEvents();
    configureWaterfallEvents();
    configureSliceEvents();

  }  // end if (fRig.connected)
  else
  {
    StandAlone = true;
    debugln("Not connected to Flex");
    UI_Boot::Prog(BootStage::InitNetwork, "Not connected to Flex");
    delay(1000);
  }

  SplashScreen();
  delay(SplashTimer);
  pinMode(MicSelPin, INPUT_PULLUP);

  debug("Mic Sel: ");
  debugln(digitalRead(MicSelPin));

  for (int Slice = 0; Slice < 2; Slice++)
  {
    VFOVal[Slice]            = 0;
    VFOTuningRate[Slice]     = TuningRateSSB[Slice];
    VFOTuningRateSave[Slice] = VFOTuningRate[Slice];
  }
  VFOAEnc.write(0);
  VFOAEnc.write(0);

  MuteVal[A] = 0;

  if (fRig.connected)
  {
    if (!DisableGUIClient)
    {
      if (getFirstButtonPress() != BTN_NONE)
      {
        GotBtn = true;
      }
      else
      {
        GotBtn = false;
      }
    }

    if (!DisableGUIClient && (GotBtn || fRig.Client_ID[0] == ""))  // held button during t or no GUI client found forces TeensyMaestro to be a GUI Client
    {
      //fRig.send("client gui KD0RC_TeensyMaestro");
      fRig.send("client gui TeensyMaestro-" + TMID);
      //fRig.send("client station TeensyMaestro-" + TMID);
      fRig.send("client station TeensyMaestro-" + MyCall);

      TimeIt = millis();
      while (fRig.Client_ID[0] == "")
      {
        fRig.process();
        delay(100);

        if (millis() - TimeIt > 5000)  // Avoid infinite loop if it does not find a proper client id
        {
          break;
        }
      }

      fRig.send("client start_persistence 1");

      debugln("Standalone Mode************************************************************");
      debug("fRig.Client_ID: ");
      debugln(fRig.Client_ID[0]);
      debug("fRig.Client_Station: ");
      debugln(fRig.Client_Station[0]);
      debug("fRig.Client_Program: ");
      debugln(fRig.Client_Program[0]);

      fRig.setAgcThreshold(A, 33);
      fRig.setAgcThreshold(B, 33);
      fRig.setAudioGain(A, 20);
      fRig.setAudioGain(B, 20);
    }
    else
    {
      debugln("NOT Standalone Mode************************************************************");
      debug("fRig.Client_ID: ");
      debugln(fRig.Client_ID[0]);
      fRig.send("client bind client_id=" + String(fRig.Client_ID[0]));
    }

    TimeIt = millis();
    do
    {
      //fRig.fireEvents();
      fRig.process();

      if (millis() - TimeIt > 5000)
      {
        break;
      }
      delay(250);

    } while (fRig.slice[A].mode == "");

    debug("Slice A in use: ");
    debugln(fRig.slice[A].in_use);
    debug("Slice B in use: ");
    debugln(fRig.slice[B].in_use);

    if (Profile != "")
    {
      // Use the dedicated API; same async behavior as before
      fRig.loadGlobalProfile(Profile);
    }

    TimeIt = millis();
    while ((millis() - TimeIt) < 1000)
    {
      fRig.process();  // processing intial rig data
      delay(100);
    }

    for (int Slice = 0; Slice < 2; Slice++)
    {
      if (fRig.slice[Slice].active == 1)
      {
        LoadFilterMenu(fRig.slice[Slice].mode);
      }
    }
    LoadProfileMenu();
  }


  GetEEPROM();

  if (fRig.connected)
  {
    LoadMiscMenu();
    LoadMemoryMenu();
    LoadModeMenu();
    LoadBandMenu();
    LoadTransmitMenu();
    LoadAntennaMenu();
    LoadClientMenu();
  }

  //LoadCWMenu();
  LoadCWMsgMenu();
  LoadCWMenu();

  // ---------------------------------------------------------------------------------------------
  if (fRig.connected)
  {
    debugln("fRig.connected is True");

    // Let the radio stream some initial state
    TimeIt = millis();
    while ((millis() - TimeIt) < 2000) {
      fRig.process();
      delay(100);
    }

    // Configure all event subscriptions
    configureRadioEvents();
    configureEqEvents();
    configureInterlockEvents();
    configureTransmitEvents();
    configurePanadapterEvents();
    configureWaterfallEvents();
    configureSliceEvents();

    // Decide GUI vs bound client
    if (!DisableGUIClient)
    {
      GotBtn = (getFirstButtonPress() != BTN_NONE);
    }
    if (!DisableGUIClient && (GotBtn || fRig.Client_ID[0] == ""))  // be a GUI client
    {
      fRig.send("client gui TeensyMaestro-" + TMID);
      fRig.send("client station TeensyMaestro-" + MyCall);

      TimeIt = millis();
      while (fRig.Client_ID[0] == "")
      {
        fRig.process();
        delay(100);
        if (millis() - TimeIt > 5000) break; // avoid infinite wait
      }
      fRig.send("client start_persistence 1");

      debugln("Standalone Mode************************************************************");
      debug("fRig.Client_ID: ");      debugln(fRig.Client_ID[0]);
      debug("fRig.Client_Station: "); debugln(fRig.Client_Station[0]);
      debug("fRig.Client_Program: "); debugln(fRig.Client_Program[0]);

      fRig.setAgcThreshold(A, 33);
      fRig.setAgcThreshold(B, 33);
      fRig.setAudioGain(A, 20);
      fRig.setAudioGain(B, 20);
    }
    else
    {
      debugln("NOT Standalone Mode************************************************************");
      debug("fRig.Client_ID: "); debugln(fRig.Client_ID[0]);
      fRig.send("client bind client_id=" + String(fRig.Client_ID[0]));
    }

    // Give slice list a moment to populate
    TimeIt = millis();
    do
    {
      fRig.process();
      if (millis() - TimeIt > 5000) break;
      delay(250);
    } while (fRig.slice[A].mode == "");

    debug("Slice A in use: "); debugln(fRig.slice[A].in_use);
    debug("Slice B in use: "); debugln(fRig.slice[B].in_use);

    // Load requested global profile if any
    if (Profile != "")
    {
      String TmpStr = "profile global load \"" + Profile + "\"";
      fRig.send(TmpStr);
    }

    // Small extra settle after profile load
    TimeIt = millis();
    while ((millis() - TimeIt) < 1000) {
      fRig.process();
      delay(100);
    }

    // Load filter menu for any active slices
    for (int s = 0; s < 2; ++s)
    {
      if (fRig.slice[s].active == 1)
      {
        LoadFilterMenu(fRig.slice[s].mode);
      }
    }
    LoadProfileMenu();

    // ----------------------- NO ORPHAN/WEIRD SLICE -----------------------
    // Never fabricate slices in headless mode; and even when not headless, do not auto-create.
    if (FlexIsHeadless()) {
      debugln(F("Headless: skip creating any slices (no orphan/weird slice)."));
    } else {
      if (fRig.slice[A].in_use == 0 && fRig.slice[B].in_use == 0) {
        debugln(F("No slices present; not auto-creating any (wait for GUI/user)."));
      }
    }

    // ----------------------- PER-SLICE RATE/STEP (safe) ------------------
    for (int s = 0; s < 2; ++s)
    {
      if (fRig.slice[s].in_use != 1 || fRig.slice[s].mode == "") continue; // skip non-existing

      if (fRig.slice[s].mode == "USB" || fRig.slice[s].mode == "LSB") {
        VFOTuningRate[s]     = TuningRateSSB[s];
        VFOTuningRateSave[s] = VFOTuningRate[s];
        VFOStep[s]           = VFOStepSSB[s];
      } else if (fRig.slice[s].mode == "CW") {
        VFOTuningRate[s]     = TuningRateCW[s];
        VFOTuningRateSave[s] = VFOTuningRate[s];
        VFOStep[s]           = VFOStepCW[s];
      } else {
        VFOTuningRate[s]     = TuningRateSSB[s];
        VFOTuningRateSave[s] = VFOTuningRate[s];
        VFOStep[s]           = VFOStepSSB[s];
      }
    }

    // ----------------------- OTHER RADIO INIT ----------------------------
    SaveBkInDelay = fRig.transmit.break_in_delay;
    if (KeyerOut == "ETHERNET") {
      // Apply CW Delay only if configured (non-zero)
      if (CWDelay > 0) {
        // Enforce minimum value of 30 ms to avoid too-short hang times
        uint16_t appliedDelay = (CWDelay < 30) ? 30 : CWDelay;
        fRig.setCwBreakinInDelay(appliedDelay);
      }
    }

    TimeIt = millis();
    while (fRig.Client_ID[0] == "")
    {
      fRig.fireEvents();
      fRig.process();
      delay(100);
      if (millis() - TimeIt > 10000) break; // avoid infinite wait
    }

    CheckInBand(A);
    CheckInBand(B);

    // Fix RF power display glitch only if real slices exist
    bool haveAnySlice = (fRig.slice[A].in_use == 1) || (fRig.slice[B].in_use == 1);
    if (haveAnySlice)
    {
      if (fRig.slice[A].tx == 1) {
        debugln("TX*******************************************************************");
        fRig.setTx(A, 0); fRig.process(); delay(100); fRig.setTx(A, 1);
      } else if (fRig.slice[B].tx == 1) {
        fRig.setTx(B, 0); fRig.process(); delay(100); fRig.setTx(B, 1);
      } else if (fRig.slice[A].in_use == 1) {
        fRig.setTx(A, 1); fRig.process(); delay(100); fRig.setTx(A, 0);
      } else if (fRig.slice[B].in_use == 1) {
        fRig.setTx(B, 1); fRig.process(); delay(100); fRig.setTx(B, 0);
      }
    } else {
      debugln(F("Skip TX toggle: no real slices present"));
    }

    // Pan IDs may be zero/invalid early; print safely
    auto _safePrintPan = [&](const char* label, int panId){
      long idx = TMU_HandleToPanIndexSafe((uint32_t)panId, TMU_ArrayLen(fRig.panadapter));
      debug(label);
      if (idx >= 0 && idx < fRig.nMaxPanadapter) {
        debugln(panId);
      } else {
        debugln(F("invalid"));
      }
    };
    _safePrintPan("Pan A: ", fRig.slice[A].pan);
    _safePrintPan("Pan B: ", fRig.slice[B].pan);

    // Client diagnostics
    for (int i = 0; i < 4; i++)
    {
      if (fRig.Client_ID[i] != "")
      {
        debug("Max Clients: ");     debugln(fRig.Max_Clients);
        debug("Client_ID: ");       debugln(fRig.Client_ID[i]);
        debug("Client_Handle: ");   debugln(fRig.Client_Handle[i]);
        debug("Client_Program: ");  debugln(fRig.Client_Program[i]);
        debug("Client_Station: ");  debugln(fRig.Client_Station[i]);
        debug("Client_Status: ");   debugln(fRig.Client_Status[i]);
      }
    }

    attachInterrupt(digitalPinToInterrupt(MicSelPin), MicSelISR, CHANGE);

      switch (Encoder_9)
      {
      case Enc9_CWSpeed: {
        /*
        const bool headless = FlexIsHeadless();
        const int  tx       = TXSlice;
        const String mode   = (tx >= 0) ? fRig.slice[tx].mode : String("");
        const bool isCW     = (mode == "CW");
        int reported        = fRig.transmit.speed;

        //Serial.println("[Setup] Enc9_CWSpeed init");
        //Serial.print  ("[Setup] headless="); Serial.println(headless ? "YES" : "NO");
        //Serial.print  ("[Setup] txSlice=");  Serial.println(tx);
        //Serial.print  ("[Setup] mode=");     Serial.println(mode);
        //Serial.print  ("[Setup] reported="); Serial.println(reported);

        // Adopt radio WPM only when not headless, TX slice is CW, and value is sane
        if (!headless && isCW && reported > 0) {
          if (reported < 5)  reported = 5;   // clamp
          if (reported > 60) reported = 60;
          CWVal = reported;
          //Serial.print("[Setup] applied CWVal="); Serial.println(CWVal);
        } else {
          //Serial.println("[Setup] keep local CWVal (no adopt)");
        }

        CWValSave = CWVal;
        CWMicEnc.write(CWVal * CWEncSteps);
        */
        CWMicEnc.write(CWVal * CWEncSteps);
        break;
      }

      case Enc9_MicGain:  // Mic Gain
        MicGain     = fRig.transmit.mic_level;
        MicGainSave = MicGain;
        CWMicEnc.write(MicGain * CWEncSteps);
        break;

      case Enc9_RFPower:  // RF Power
        RFPower     = fRig.transmit.rfpower;
        RFPowerSave = RFPower;
        CWMicEnc.write(RFPower * CWEncSteps);
        break;

      case Enc9_TunePower:  // Tune Power
        TunePower     = fRig.transmit.tunepower;
        TunePowerSave = TunePower;
        CWMicEnc.write(TunePower * CWEncSteps);
        break;

      case Enc9_WNBLevel:  // WNB Level
        if (fRig.slice[A].in_use) {
          WNBLevel = fRig.slice[A].wnb_level;
        } else if (fRig.slice[B].in_use) {
          WNBLevel = fRig.slice[B].wnb_level;
        }
        WNBLevelSave = WNBLevel;
        CWMicEnc.write(WNBLevel * CWEncSteps);
        break;

      case Enc9_MonLevel:  // Mon Level
        MonLevel     = fRig.transmit.mon_gain_sb;
        MonLevelSave = MonLevel;
        CWMicEnc.write(MonLevel * CWEncSteps);
        break;

      case Enc9_VOXLevel:  // VOX Level
        VOXLevel     = fRig.transmit.vox_level;
        VOXLevelSave = VOXLevel;
        CWMicEnc.write(VOXLevel * CWEncSteps);
        break;

      case Enc9_VOXDelay:  // VOX Delay
        VOXDelay     = fRig.transmit.vox_delay;
        VOXDelaySave = VOXDelay;
        CWMicEnc.write(VOXDelay * CWEncSteps);
        break;

      case Enc9_Band:  // Band (safe pan index)
      {
        int activeSlice = -1;
        for (int s = 0; s < 2; ++s) {
          if (fRig.slice[s].active == 1) { activeSlice = s; break; }
        }

        if (activeSlice >= 0 && fRig.slice[activeSlice].in_use == 1) {
          const int panIdx = TMU_HandleToPanIndexSafe(
              static_cast<uint32_t>(fRig.slice[activeSlice].pan),
              TMU_ArrayLen(fRig.panadapter));

          if (panIdx >= 0 && panIdx < fRig.nMaxPanadapter) {
            const int bandCount = static_cast<int>(sizeof(BandMenu) / sizeof(BandMenu[0]));
            for (Band = 0; Band < bandCount; ++Band) {
              if (fRig.panadapter[panIdx].band == BandMenu[Band]) {
                break; // found current band index
              }
            }
          } else {
            debugln(F("Band case: invalid pan index; keeping previous Band"));
          }
        } else {
          debugln(F("Band case: no active slice; keeping previous Band"));
        }

        CWMicEnc.write(Band * CWEncSteps);
        BandSave = Band;
        break;
      }
    }
  }
  else  // Not connected -> stand-alone keyer
  {
    StandAlone = true;
    debugln("Not connected to Flex");
    Keyer_Apply_Wpm(CWVal /* already set from INI */, false);
    UI_Boot::Prog(BootStage::InitNetwork, "Not connected to Flex");
  } // End if ftRig.Connected
  // ---------------------------------------------------------------------------------------------

  touch.begin();

  touch.setRotation(3);  // Match TFT rotation for landscape mode (same axes as screen)
                         // rotation=3: Origin bottom-right in raw terms, but library maps so X→right, Y→down to match TFT

  touch.readData(&TPX, &TPY, &TPZ);

  if (TPX == 4095 && TPY == 0 && TPZ == 255)  // Trick value to show that no controller is connected
  {
    debugln("No XPT2046 Touch Controller Detected");
    GotTouch = false;
  }
  else
  {
    debugln("XPT2046 Touch Controller Detected");
    GotTouch = true;
  }

  SplashTimeIt    = millis();
  ScreenSaveTimer = millis();

  TempTimer = millis();

  QueryTimer = millis();

  Accel.begin(VFOAccelISR, 2000);  // 2 ms Timer

  InSetup = false;
}  // end TeensyMaestroSetup()


FLASHMEM void ShutDownCB()
{
  for (int i = 0; i < fRig.nMaxSlice; i++)
  {
    if (fRig.Client_Station[i].indexOf("TeensyMaestro") > -1)
    {
      fRig.removeSlice(i);
      fRig.process();
      delay(250);
    }
  }

  muxA.digitalWrite(IOX_TFT_LCD, LOW);

  fRig.disconnect();
  delay(1000);
}
FLASHMEM void TM_AttemptFlexConnect()
{
  if (Ethernet.linkStatus() != 1) {
    debugln("Ethernet link is down; skipping Flex connect.");
    return;
  }

  // Show local IP on serial (and optionally on TFT)
  debug(TM_FULL_NAME);
  debug(" IP: ");
  debugln(addr);

  // Seed tm_netutil with valid DNS/GW so all later DNS (STUN, NTP, Flex host) uses the same sources.
  IPAddress nicDns = Ethernet.dnsServerIP();
  IPAddress nicGw  = Ethernet.gatewayIP();
  TM_NetUtil_SetIniDns(nicDns);
  TM_NetUtil_SetIniGateway(nicGw);

  // --- Start NTP (UTC clock source) ---
  TMTime_begin(
    CFG_NTPServer.c_str(),   // NTP server
    8888,                    // local UDP port (will auto-retry +1 if busy)
    15UL * 60UL * 1000UL,    // resync interval: 15 minutes
    3                        // max retries per sync cycle
  );

  debugln("NTP: begin() requested");

  ShowConnModeProgress();

  // Diagnostics
  debug("ConnectSerialNum = "); debugln(ConnectSerialNum);
  debug("CFG_ConnMode = "); debugln(
    CFG_ConnMode==TM_CONN_FIXED ? "Fixed" :
    CFG_ConnMode==TM_CONN_FIXED_FAILOVER ? "Fixed+Failover" : "Auto");
  debug("CFG_FlexHost = '"); debug(CFG_FlexHost); debugln("'");

  // -----------------------------
  // A: ANY (try Fixed/Failover direct connect first, then discovery if needed)
  // -----------------------------
  if (ConnectSerialNum == "ANY") {
    ShowConnModeProgress();

    bool tried_direct = false;
    bool direct_ok    = false;

    if (CFG_ConnMode == TM_CONN_FIXED || CFG_ConnMode == TM_CONN_FIXED_FAILOVER) {
      // Resolve target IP from CFG_FlexHost (literal IPv4 or DNS)
      if (TM_ComputeFlexTargetIP()) {
        UI_Boot::Progf(BootStage::InitNetwork,
              "Connecting to: %u.%u.%u.%u:%d",
              CFG_FlexIp[0], CFG_FlexIp[1], CFG_FlexIp[2], CFG_FlexIp[3], CFG_FlexControlPort);

        // Keep existing debug output as-is
        debug("Trying direct connect to Flex ");
        debug(CFG_FlexIp[0]); debug(".");
        debug(CFG_FlexIp[1]); debug(".");
        debug(CFG_FlexIp[2]); debug(".");
        debug(CFG_FlexIp[3]); debug(":");
        debugln(CFG_FlexControlPort);

        // --- Direct connect using your new overload ---
        IPAddress ip(CFG_FlexIp[0], CFG_FlexIp[1], CFG_FlexIp[2], CFG_FlexIp[3]);
        fRig.connect(ip, (uint16_t)CFG_FlexControlPort);
        tried_direct = true;

        // Short wait for the TCP session to become established
        unsigned long t0 = millis();
        while (!fRig.connected && !StandAlone) {
          // Some implementations may need one extra nudge; harmless to call once
          fRig.connect();
          delay(250);
          if (millis() - t0 > 8000) break;  // up to ~8 s for direct attempt
        }
        direct_ok = fRig.connected;

        // Fill identity via TCP 'info' when direct connect succeeded
        if (direct_ok) {
          String hostDisp = CFG_FlexHost;
          hostDisp.toLowerCase();
          snprintf(gFixedEndpointLabel, sizeof(gFixedEndpointLabel),
          "%s:%d", hostDisp.c_str(), CFG_FlexControlPort);

          gFixedEndpointUsed = true; 
          if (!fRig.ensureIdentity(1500)) {
            debugln("Identity via TCP 'info' not complete within 1.5s (model/serial/version may appear later).");
          }
        } else {
          debugln("Direct connect failed.");
        }

      } else {
        // No usable IP → skip direct attempt
        debugln("No valid Flex target IP (Fixed/Failover); will use discovery if allowed.");
      }
    }

    // Discovery if AUTO, or Fixed+Failover and direct attempt was skipped/failed
    if (CFG_ConnMode == TM_CONN_AUTO ||
        (CFG_ConnMode == TM_CONN_FIXED_FAILOVER && (!tried_direct || (tried_direct && !direct_ok)))) {
      UI_Boot::Prog(BootStage::DiscoverFlex, F("Discovering Flex radios..."));
      debugln("===Looking for Flex Rig (UDP)===");
      fRig = FlexRig::findAFlex("");  // discover any radio
    }

    // Final connect loop (covers both discovery and the case where direct already succeeded)
    unsigned long t1 = millis();
    while (!fRig.connected && !StandAlone) {
      fRig.connect();
      delay(250);
      if (millis() - t1 > 10000) {  // up to ~10 s total
        break;
      }
    }

    if (!fRig.connected) {
      debugln("Not connected to Flex");
    }
    return;
  }

  // -----------------------------
  // B: Specific serial number → always discovery (original behavior)
  // -----------------------------
  {
    UI_Boot::Progf(BootStage::InitNetwork, "Discover SN: %s", ConnectSerialNum.c_str());

    // Keep discovering until the requested serial shows up
    fRig = FlexRig::findAFlex(ConnectSerialNum.c_str());

    while (!fRig.connected) {
      fRig = FlexRig::findAFlex(ConnectSerialNum.c_str());
      fRig.connect();
      delay(250);
    }

    if (fRig.serial[0] == 0x00) {
      // No match found through UDP discovery on the local L2 segment
      StandAlone = true;

      debugf(
        "No Flex found via L2 discovery (UDP/4992 broadcast) on local LAN.\n"
        "Our NIC: ip=%u.%u.%u.%u gw=%u.%u.%u.%u mask=%u.%u.%u.%u linkStatus=%d\n"
        "Tips: ensure the radio is powered, same VLAN, and that broadcasts aren’t filtered.\n"
        "If the radio is remote, use Connection mode = Fixed/Failover with Flex Host/Port.\n",
        Ethernet.localIP()[0], Ethernet.localIP()[1],
        Ethernet.localIP()[2], Ethernet.localIP()[3],
        Ethernet.gatewayIP()[0], Ethernet.gatewayIP()[1],
        Ethernet.gatewayIP()[2], Ethernet.gatewayIP()[3],
        Ethernet.subnetMask()[0], Ethernet.subnetMask()[1],
        Ethernet.subnetMask()[2], Ethernet.subnetMask()[3],
        Ethernet.linkStatus()
      );
    } else {
      debugln(fRig.serial);
    }

    // Short final wait to ensure we’re fully connected
    unsigned long t0 = millis();
    while (!fRig.connected && !StandAlone) {
      fRig.connect();
      delay(250);
      if (millis() - t0 > 10000) {
        break;
      }
    }
  }
}
