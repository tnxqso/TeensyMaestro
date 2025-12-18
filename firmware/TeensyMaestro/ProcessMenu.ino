#include "tm_sketch_api.h"
/***************************** LoadProfileMenu ***************************/
FLASHMEM void LoadProfileMenu()
{
  if (!ProfileMenu)
  {
    return;
  }

  int MenuNum = 1;
  int ProfNum = 0;

  debug("Profile   LastMenuIDX: ");
  debugln(LastMenuIDX);
  for (MenuIDX = LastMenuIDX + 1; MenuIDX < MaxMenus; MenuIDX++)
  {
    if (fRig.Profile[ProfNum] == "")
    {
      LastMenuIDX = MenuIDX - 1;
      return;
    }

    MenuTitle[MenuIDX] = "Profile Menu " + String(MenuNum);

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems - 1; MenuItemIDX++)
    {
      if (fRig.Profile[ProfNum] != "")
      {
        MenuItem[MenuIDX][MenuItemIDX]   = fRig.Profile[ProfNum];
        MenuAction[MenuIDX][MenuItemIDX] = 1;  // 1 = load profile
        ProfNum++;
      }
      else
      {
        LastMenuIDX = MenuIDX;
        debug("Profile   LastMenuIDX: ");
        debugln(LastMenuIDX);
        return;
      }
    }
    MenuNum++;
  }
  LastMenuIDX = MenuIDX;
}

/***************************** LoadCWMenu ***************************/
FLASHMEM void LoadCWMenu()
{
  if (!CWMenuOpt)
  {
    return;
  }

  debug("CWMenu   LastMenuIDX: ");
  debugln(LastMenuIDX);
  MenuIDX            = LastMenuIDX + 1;
  CWMenu             = MenuIDX;
  MenuTitle[MenuIDX] = "CW Menu";

  MenuItem[MenuIDX][0]   = "CW Message Source: " + CWMsgSource;
  MenuAction[MenuIDX][0] = 6;  // 6 = CW Message Source

  if (Handed == 0)
  {
    HandedTxt = "Right Handed";
  }
  else
  {
    HandedTxt = "Left Handed";
  }

  if (MyCall == "W4WKU" && Handed == 1)
  {
    HandedTxt = "Dave Handed";
  }

  MenuItem[MenuIDX][1]   = "CW Paddles: " + HandedTxt;
  MenuAction[MenuIDX][1] = 7;  // 7 = Paddles Left/Right Handed

  MenuItem[MenuIDX][2]   = "CW Mode: " + KeyMode;
  MenuAction[MenuIDX][2] = 8;  // 8 = CW Key Mode (A, B, U)

  if (SideTone)
  {
    STtxt = "ON";
  }
  else
  {
    STtxt = "OFF";
  }

  MenuItem[MenuIDX][3]   = "CW Sidetone: " + STtxt;
  MenuAction[MenuIDX][3] = 18;  // 18 = CW Sidetone on/off

  MenuItem[MenuIDX][4]   = "CW Sidetone Freq: " + String(STFreq);
  MenuAction[MenuIDX][4] = 19;  // 19 = CW Sidetone Freq

  MenuItem[MenuIDX][5]   = "Set contest serial number: " + String(SerNum);
  MenuAction[MenuIDX][5] = 11;

  MenuItem[MenuIDX][6]   = "Clear contest serial number to 1";
  MenuAction[MenuIDX][6] = 12;

  if (fRig.connected)
  {
    MenuItem[MenuIDX][7]   = "Keyer Output: " + KeyerOut;
    MenuAction[MenuIDX][7] = 23;
  }

  LastMenuIDX = MenuIDX;
  debug("CWMenu   LastMenuIDX: ");
  debugln(LastMenuIDX);
}

/***************************** LoadCWMsgMenu ***************************/
FLASHMEM void LoadCWMsgMenu()
{
  if (!CWMsgMenuOpt)
  {
    return;
  }

  if (!GotCWMsgMenu)
  {
    MenuIDX            = LastMenuIDX + 1;
    CWMsgMenu          = MenuIDX;
    LastMenuIDX        = MenuIDX;
    MenuTitle[MenuIDX] = "CW Message Menu ";
    GotCWMsgMenu       = true;
  }
  else
  {
    MenuIDX = CWMsgMenu;
    for (MenuItemIDX = 0; MenuItemIDX < 12; MenuItemIDX++)
    {
      MenuItem[MenuIDX][MenuItemIDX]   = "";  // clear menu in prep for new values
      MenuAction[MenuIDX][MenuItemIDX] = 0;   // 0 = NOP
    }
  }

  for (MenuItemIDX = 0; MenuItemIDX < 12; MenuItemIDX++)
  {
    MenuItem[MenuIDX][MenuItemIDX]   = CWMsg[MenuItemIDX];
    MenuAction[MenuIDX][MenuItemIDX] = 5;  // 5 = play CW message
  }

  debug("LoadCWMsgMenu   LastMenuIDX: ");
  debugln(LastMenuIDX);
}

/***************************** LoadMiscMenu ***************************/
FLASHMEM void LoadMiscMenu()
{
  debug("Misc   LastMenuIDX: ");
  debugln(LastMenuIDX);
  MenuIDX  = LastMenuIDX + 1;
  MiscMenu = MenuIDX;

  MenuTitle[MenuIDX] = "Misc Menu ";

  MenuItem[MenuIDX][0]   = "A > B";
  MenuAction[MenuIDX][0] = 16;  // 16 = A > B

  MenuItem[MenuIDX][1]   = "B > A";
  MenuAction[MenuIDX][1] = 17;  // 17 = B > A

  MenuItem[MenuIDX][2]   = "Reload CW Messages";
  MenuAction[MenuIDX][2] = 2;  // 2 = Reload Config File

  if (OOBindicator)
  {
    OOBI = "ON";
  }
  else
  {
    OOBI = "OFF";
  }

  MenuItem[MenuIDX][3]   = "Out of Band Display: " + OOBI;
  MenuAction[MenuIDX][3] = 9;  // 9 = Out of band display on/off

  if (SnapToStep)
  {
    SnapToSteptxt = "ON";
  }
  else
  {
    SnapToSteptxt = "OFF";
  }

  MenuItem[MenuIDX][4]   = "Snap to Tune Step: " + SnapToSteptxt;
  MenuAction[MenuIDX][4] = 14;  // 14 = Snap to tune step

  if (VFOTrack)
  {
    VFOTrackInd = "ON";
  }
  else
  {
    VFOTrackInd = "OFF";
  }

  MenuItem[MenuIDX][5]   = "VFO Tracking: " + VFOTrackInd;
  MenuAction[MenuIDX][5] = 15;  // 15 = VFO Tracking

  if (ShortPressClick)
  {
    ShortPressInd = "ON";
  }
  else
  {
    ShortPressInd = "OFF";
  }

  MenuItem[MenuIDX][6]   = "Short Press Click: " + ShortPressInd;
  MenuAction[MenuIDX][6] = 27;  // 27 = Short Press Click

  if (LongPressClick)
  {
    LongPressInd = "ON";
  }
  else
  {
    LongPressInd = "OFF";
  }

  MenuItem[MenuIDX][7]   = "Long Press Click: " + LongPressInd;
  MenuAction[MenuIDX][7] = 28;  // 28 = Long Press Click

  MenuItem[MenuIDX][8]   = "Restart TeensyMaestro";
  MenuAction[MenuIDX][8] = 10;  // 10 = Restart

  MenuItem[MenuIDX][9]   = "Power TeensyMaestro Off";
  MenuAction[MenuIDX][9] = 13;  // 13 = Power Off

  MenuItem[MenuIDX][10]   = "Show Splash Screen";
  MenuAction[MenuIDX][10] = 29;  // 29 = Show Splash Screen

  MenuItem[MenuIDX][11]   = "Center Control Function: " + Enc9_Text[Encoder_9];
  MenuAction[MenuIDX][11] = 30;  // 30 = Center Control Function

  if (VFOaccel)
  {
    VFOaccelInd = "ON";
  }
  else
  {
    VFOaccelInd = "OFF";
  }

  MenuItem[MenuIDX][12]   = "VFO Acceleration: " + VFOaccelInd;
  MenuAction[MenuIDX][12] = 31;  // 31 = VFO Acceleration

  MenuItem[MenuIDX][13]   = "VFO Acceleration Factor: " + String(AccelFactor);
  MenuAction[MenuIDX][13] = 32;  // 32 = VFO Acceleration Factor. Smaller is faster.

  //  MenuItem[MenuIDX][8] = "Disconnect TeensyMaestro";
  //  MenuAction[MenuIDX][8] = 25;  // 25 = disconnect TeensyMaestro

  LastMenuIDX = MenuIDX;
  debug("Misc   LastMenuIDX: ");
  debugln(LastMenuIDX);
}

/***************************** LoadFilterMenu ***************************/
FLASHMEM void LoadFilterMenu(String Mode)
{
  if (!FilterMenu)
  {
    return;
  }

  debug("FilterMenuIDX: ");
  debugln(FilterMenuIDX);
  debug("LastMenuIDX: ");
  debugln(LastMenuIDX);

  if (!GotFilterMenu)
  {
    debugln("GotFilterMenu = false");
    MenuIDX       = LastMenuIDX + 1;
    FilterMenuIDX = LastMenuIDX + 1;
    LastMenuIDX   = FilterMenuIDX;
    GotFilterMenu = true;
  }

  MenuIDX = FilterMenuIDX;

  debug("FilterMenuIDX: ");
  debugln(FilterMenuIDX);
  debug("LastMenuIDX: ");
  debugln(LastMenuIDX);

  if (Mode == "USB" || Mode == "LSB")
  {
    MenuTitle[FilterMenuIDX] = "SSB Filter Menu ";

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      if (MenuItemIDX >= SSBMaxFilterItems)
      {
        MenuItem[FilterMenuIDX][MenuItemIDX]   = "";
        MenuAction[FilterMenuIDX][MenuItemIDX] = 0;
        break;
      }
      MenuItem[FilterMenuIDX][MenuItemIDX]   = SSBFilterDisp[MenuItemIDX];
      MenuAction[FilterMenuIDX][MenuItemIDX] = 3;  // 3 = Apply Filter
    }
  }

  if (Mode == "DIGU" || Mode == "DIGL")
  {
    MenuTitle[FilterMenuIDX] = "Digi Filter Menu ";

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      if (MenuItemIDX >= DigiMaxFilterItems)
      {
        MenuItem[FilterMenuIDX][MenuItemIDX]   = "";
        MenuAction[FilterMenuIDX][MenuItemIDX] = 0;
        break;
      }
      MenuItem[FilterMenuIDX][MenuItemIDX]   = DigiFilterDisp[MenuItemIDX];
      MenuAction[FilterMenuIDX][MenuItemIDX] = 3;  // 3 = Apply Filter
    }
  }

  if (Mode == "RTTY")
  {
    MenuTitle[FilterMenuIDX] = "RTTY Filter Menu ";

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      if (MenuItemIDX >= RTTYMaxFilterItems)
      {
        MenuItem[FilterMenuIDX][MenuItemIDX]   = "";
        MenuAction[FilterMenuIDX][MenuItemIDX] = 0;
        break;
      }
      MenuItem[FilterMenuIDX][MenuItemIDX]   = RTTYFilterDisp[MenuItemIDX];
      MenuAction[FilterMenuIDX][MenuItemIDX] = 3;  // 3 = Apply Filter
    }
  }

  if (Mode == "AM" || Mode == "SAM")
  {
    MenuTitle[FilterMenuIDX] = "AM Filter Menu ";

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      if (MenuItemIDX >= AMMaxFilterItems)
      {
        MenuItem[FilterMenuIDX][MenuItemIDX]   = "";
        MenuAction[FilterMenuIDX][MenuItemIDX] = 0;
        break;
      }
      MenuItem[FilterMenuIDX][MenuItemIDX]   = AMFilterDisp[MenuItemIDX];
      MenuAction[FilterMenuIDX][MenuItemIDX] = 3;  // 3 = Apply Filter
    }
  }

  if (Mode == "DFM")
  {
    MenuTitle[FilterMenuIDX] = "DFM Filter Menu ";

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      if (MenuItemIDX >= DFMMaxFilterItems)
      {
        MenuItem[FilterMenuIDX][MenuItemIDX]   = "";
        MenuAction[FilterMenuIDX][MenuItemIDX] = 0;
        break;
      }
      MenuItem[FilterMenuIDX][MenuItemIDX]   = DFMFilterDisp[MenuItemIDX];
      MenuAction[FilterMenuIDX][MenuItemIDX] = 3;  // 3 = Apply Filter
    }
  }

  if (Mode == "CW")
  {
    MenuTitle[FilterMenuIDX] = "CW Filter Menu ";

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      if (MenuItemIDX >= CWMaxFilterItems)
      {
        MenuItem[FilterMenuIDX][MenuItemIDX]   = "";
        MenuAction[FilterMenuIDX][MenuItemIDX] = 0;
        break;
      }
      MenuItem[FilterMenuIDX][MenuItemIDX]   = CWFilterDisp[MenuItemIDX];
      MenuAction[FilterMenuIDX][MenuItemIDX] = 3;  // 3 = Apply Filter
    }
  }

  if (Mode == "FM" || Mode == "NFN")
  {
    MenuTitle[FilterMenuIDX] = "No Selectable FM filters available ";

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      MenuItem[FilterMenuIDX][MenuItemIDX]   = "";
      MenuAction[FilterMenuIDX][MenuItemIDX] = 0;
    }
  }
}

/***************************** LoadModeMenu ***************************/
FLASHMEM void LoadModeMenu()
{
  if (!ModeMenuOn)
  {
    return;
  }

  MenuIDX = LastMenuIDX + 1;

  MenuTitle[MenuIDX] = "Mode Menu";

  for (MenuItemIDX = 0; MenuItemIDX < 11; MenuItemIDX++)
  {
    MenuItem[MenuIDX][MenuItemIDX]   = ModeMenu[MenuItemIDX];
    MenuAction[MenuIDX][MenuItemIDX] = 20;  // 20 = mode
  }
  LastMenuIDX = MenuIDX;
}

/***************************** LoadBandMenu ***************************/
FLASHMEM void LoadBandMenu()
{
  if (!BandMenuOn)
  {
    return;
  }

  MenuIDX = LastMenuIDX + 1;

  MenuTitle[MenuIDX] = "Band Menu";

  for (MenuItemIDX = 0; MenuItemIDX < 13; MenuItemIDX++)
  {
    MenuItem[MenuIDX][MenuItemIDX]   = String(BandMenu[MenuItemIDX]);
    MenuAction[MenuIDX][MenuItemIDX] = 24;  // 24 = band
  }
  LastMenuIDX = MenuIDX;
}

/***************************** LoadMemoryMenu ***************************/
FLASHMEM void LoadMemoryMenu()
{
  int MenuNum = 1;

  if (!MemoryMenu)
  {
    return;
  }

  for (int i = 0; i < fRig.MaxMemNum; i++)
  {
    Memories[i] = fRig.MemName[i] + "$" + fRig.MemFreq[i] + "$" + String(fRig.Mem[i]);
    debugln(Memories[i]);
  }

  KickSort<String>::bubbleSort(Memories, fRig.MaxMemNum, KickSort_Dir::ASCENDING);
  debugln("Sorted Memories:");

  for (int i = 0; i < fRig.MaxMemNum; i++)
  {
    debugln(Memories[i]);

    int S           = Memories[i].indexOf("$");
    fRig.MemName[i] = Memories[i].substring(0, S);
    //debugln(Memories[i].substring(0, S));
    int S1          = Memories[i].indexOf("$", S + 1);
    fRig.MemFreq[i] = Memories[i].substring(S + 1, S1);
    //debugln(Memories[i].substring(S + 1, S1));
    fRig.Mem[i] = Memories[i].substring(S1 + 1).toInt();
    //debugln(Memories[i].substring(S1 + 1).toInt());
  }

  debugln("LoadMemoryMenu");
  debug("fRig.MaxMemNum: ");
  debugln(fRig.MaxMemNum);

  debug("Memory   LastMenuIDX: ");
  debugln(LastMenuIDX);

  MemMenuStart = LastMenuIDX + 1;
  for (MenuIDX = LastMenuIDX + 1; MenuIDX < MaxMenus; MenuIDX++)
  {
    MenuTitle[MenuIDX] = "Memory Menu " + String(MenuNum);

    for (MenuItemIDX = 0; MenuItemIDX < MaxMenuItems; MenuItemIDX++)
    {
      // Compute flattened absolute index into the memory tables
      const int idx = MenuItemIDX + ((MenuNum - 1) * (MaxMenuItems - 1));

      // Safe debug print using the correct index
      debug("fRig.Mem[idx]: ");
      if (idx >= 0 && idx < fRig.MaxMemNum) {
        debugln(fRig.Mem[idx]);
      } else {
        debugln(-1);
      }

      if (idx < fRig.MaxMemNum)
      {
        // Use idx consistently for both frequency and name
        MenuItem[MenuIDX][MenuItemIDX]   = fRig.MemFreq[idx] + "    " + fRig.MemName[idx];
        MenuAction[MenuIDX][MenuItemIDX] = 4;  // 4 = go to memory
      }
      else
      {
        // No more memories to list on this page
        LastMenuIDX = MenuIDX;
        debug("Memory   LastMenuIDX: ");
        debugln(LastMenuIDX);
        return;
      }
    }

    MenuNum++;
  }
}

/***************************** LoadTransmitMenu ***************************/
FLASHMEM void LoadTransmitMenu()
{
  if (!TransmitMenuOn)
  {
    return;
  }

  MenuIDX      = LastMenuIDX + 1;
  TransmitMenu = MenuIDX;

  MenuTitle[MenuIDX] = "Transmit Menu";

  MenuItem[MenuIDX][0]   = "RF Power: " + String(fRig.transmit.rfpower);
  MenuAction[MenuIDX][0] = 21;  // 21 = RF Power

  MenuItem[MenuIDX][1]   = "Mic Gain: " + String(fRig.transmit.mic_level);
  MenuAction[MenuIDX][1] = 22;  // 22 = Mic Gain

  LastMenuIDX = MenuIDX;

  debug("Transmit Menu: ");
  debugln(MenuItem[MenuIDX][0]);
  debug("Transmit Menu: ");
  debugln(MenuItem[MenuIDX][1]);
}

/***************************** LoadClientMenu ***************************/
FLASHMEM void LoadClientMenu()
{
  if (!ClientMenuOn)
  {
    return;
  }

  if (!ClientMenuActive)
  {
    MenuIDX            = LastMenuIDX + 1;
    ClientMenuIDX      = MenuIDX;
    MenuTitle[MenuIDX] = "Client Menu";
    LastMenuIDX        = MenuIDX;
  }

  for (int i = 0; i < 4; i++)
  {
    MenuItem[ClientMenuIDX][i]   = "";
    MenuAction[ClientMenuIDX][i] = 0;
  }

  for (int i = 0; i < 4; i++)
  {
    if (fRig.Client_ID[i] != "")
    {
      MenuItem[ClientMenuIDX][i] = fRig.Client_Station[i];
      if (ClientMenuItem == i)
      {
        MenuItem[ClientMenuIDX][i] = MenuItem[ClientMenuIDX][i] + " (Connected)";
      }
      MenuAction[ClientMenuIDX][i] = 26;  // 26 = bind to client
      debug("Client Station: ");
      debugln(MenuItem[ClientMenuIDX][i]);
    }
  }
}

/***************************** LoadAntennaMenu ***************************/
FLASHMEM void LoadAntennaMenu()
{
  if (!AntennaMenuOn)
  {
    return;
  }
}

/***************************** MenuExit ***************************/
FLASHMEM void MenuExit()
{
  MenuActive        = false;
  SerNumMenuActive  = false;
  STFreqMenuActive  = false;
  RFPowerMenuActive = false;
  MicGainMenuActive = false;
  ClientMenuActive  = false;
  AccelMenuActive   = false;

  RecentMenuIDX = MenuIDX;
  //SetSideTone = false;

  switch (Encoder_9)
  {
    case Enc9_CWSpeed:  // CW Speed
      // Load the baseline into live values, but do not move the baseline.
      Keyer_Apply_Wpm(CWValSave, true);  // preserveBaseline = true
      break;

    case Enc9_MicGain:  // Mic Gain
      MicGain = MicGainSave;
      debug("Mic Gain: ");
      debugln(MicGain);
      CWMicEnc.write(MicGain * CWEncSteps);
      break;

    case Enc9_RFPower:  // RF Power
      RFPower = RFPowerSave;
      debug("RF Power: ");
      debugln(RFPower);
      CWMicEnc.write(RFPower * CWEncSteps);
      break;

    case Enc9_TunePower:  // Tune Power
      TunePower = TunePowerSave;
      debug("Tune Power: ");
      debugln(TunePower);
      CWMicEnc.write(TunePower * CWEncSteps);
      break;

    case Enc9_WNBLevel:  // WNB Level
      WNBLevel = WNBLevelSave;
      debug("WNB Level: ");
      debugln(WNBLevel);
      CWMicEnc.write(WNBLevel * CWEncSteps);
      break;

    case Enc9_MonLevel:  // Mon Level
      MonLevel = MonLevelSave;
      debug("Mon Level: ");
      debugln(MonLevel);
      CWMicEnc.write(MonLevel * CWEncSteps);
      break;

    case Enc9_VOXLevel:  // VOX Level
      VOXLevel = VOXLevelSave;
      debug("VOX Level: ");
      debugln(VOXLevel);
      CWMicEnc.write(VOXLevel * CWEncSteps);
      break;

    case Enc9_VOXDelay:  // VOX Delay
      VOXDelay = VOXDelaySave;
      debug("VOX Delay: ");
      debugln(VOXDelay);
      CWMicEnc.write(VOXDelay * CWEncSteps);
      break;

    case Enc9_Band:  // Band
      Band = BandSave;
      debug("Band: ");
      debugln(BandMenu[Band]);
      CWMicEnc.write(Band * CWEncSteps);
      break;
  }

  delay(100);

  if (!SplashM)
  {
    RefreshScreen();
  }
}
