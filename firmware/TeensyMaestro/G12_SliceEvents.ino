#include "tm_sketch_api.h"
#include "ClockWidget.h"
#include "tm_system_utils.h"

void RenderSlicePassive(const int sliceId);

void configureSliceEvents()
{
  for (int i = 0; i < fRig.nMaxSlice; i++)
  {
    fRig.slice[i].attach_in_use_event(onSlice_in_use);
    fRig.slice[i].attach_index_letter_event(onSlice_index_letter);
    fRig.slice[i].attach_RF_frequency_event(onSlice_RF_frequency);
    fRig.slice[i].attach_rit_on_event(onSlice_rit_on);
    fRig.slice[i].attach_rit_freq_event(onSlice_rit_freq);
    fRig.slice[i].attach_xit_on_event(onSlice_xit_on);
    fRig.slice[i].attach_xit_freq_event(onSlice_xit_freq);
    //   fRig.slice[i].attach_wide_event(onSlice_wide);
    fRig.slice[i].attach_filter_lo_event(onSlice_filter_lo);
    fRig.slice[i].attach_filter_hi_event(onSlice_filter_hi);
    //   fRig.slice[i].attach_step_event(onSlice_step);
    fRig.slice[i].attach_agc_threshold_event(onSlice_agc_threshold);
    //   fRig.slice[i].attach_agc_off_level_event(onSlice_agc_off_level);
    fRig.slice[i].attach_pan_event(onSlice_pan);
    //   fRig.slice[i].attach_loopa_event(onSlice_loopa);
    //   fRig.slice[i].attach_loopb_event(onSlice_loopb);
    //   fRig.slice[i].attach_qsk_event(onSlice_qsk);
    //   fRig.slice[i].attach_dax_event(onSlice_dax);
    //   fRig.slice[i].attach_dax_clients_event(onSlice_dax_clients);
    fRig.slice[i].attach_lock_event(onSlice_lock);
    fRig.slice[i].attach_tx_event(onSlice_tx);
    //   fRig.slice[i].attach_dax_tx_event(onSlice_dax_tx);
    fRig.slice[i].attach_active_event(onSlice_active);
    fRig.slice[i].attach_audio_gain_event(onSlice_audio_gain);
    fRig.slice[i].attach_audio_pan_event(onSlice_audio_pan);
    fRig.slice[i].attach_audio_mute_event(onSlice_audio_mute);
    //   fRig.slice[i].attach_record_event(onSlice_record);
    //   fRig.slice[i].attach_record_time_event(onSlice_record_time);
    //   fRig.slice[i].attach_anf_event(onSlice_anf);
    //   fRig.slice[i].attach_anf_level_event(onSlice_anf_level);
    fRig.slice[i].attach_nr_event(onSlice_nr);
    fRig.slice[i].attach_nr_level_event(onSlice_nr_level);
    fRig.slice[i].attach_wnb_event(onSlice_wnb);
    fRig.slice[i].attach_wnb_level_event(onSlice_wnb_level);
    fRig.slice[i].attach_nb_event(onSlice_nb);
    fRig.slice[i].attach_nb_level_event(onSlice_nb_level);
    fRig.slice[i].attach_apf_event(onSlice_apf);
    fRig.slice[i].attach_apf_level_event(onSlice_apf_level);
    fRig.slice[i].attach_squelch_event(onSlice_squelch);
    fRig.slice[i].attach_squelch_level_event(onSlice_squelch_level);
    //   fRig.slice[i].attach_diversity_event(onSlice_diversity);
    //   fRig.slice[i].attach_diversity_parent_event(onSlice_diversity_parent);
    //   fRig.slice[i].attach_diversity_child_event(onSlice_diversity_child);
    //   fRig.slice[i].attach_diversity_index_event(onSlice_diversity_index);
    fRig.slice[i].attach_filter_shift_event(onSlice_filter_shift);
    fRig.slice[i].attach_filter_width_event(onSlice_filter_width);
    fRig.slice[i].attach_rxant_event(onSlice_rxant);
    fRig.slice[i].attach_mode_event(onSlice_mode);
    fRig.slice[i].attach_step_list_event(onSlice_step_list);
    fRig.slice[i].attach_agc_mode_event(onSlice_agc_mode);
    fRig.slice[i].attach_txant_event(onSlice_txant);
    fRig.slice[i].attach_play_event(onSlice_play);
    fRig.slice[i].attach_ant_list_event(onSlice_ant_list);
  }
}

void onSlice_in_use(const int senderId)
{
  if (fRig.isEffectiveHeadless()) {
    if (senderId >= 0 && senderId < fRig.nMaxSlice) {
      RenderSlicePassive(senderId);
    }
    else {
      debugln("Not Headless/passive, so not rendering slice passive");
    }
  }

  if (senderId < 0 || senderId >= fRig.nMaxSlice) return;

  // Slice created or removed – treat as user-relevant activity
  ResetScreenSaver("Slice event: in_use change (created/removed)");

  // Sanity-check pan handle before anyone else uses it
  if (!TMU_IsValidPanHandle((uint32_t)fRig.slice[senderId].pan)) {
    fRig.slice[senderId].pan = -1;
    return;
  }

  //Serial.print("onSlice_in_use("); Serial.print(senderId); Serial.println(") event!");
  //Serial.print("Pan ID: "); tm_print_hex32((uint32_t)fRig.slice[senderId].pan); Serial.println();

  if (!Splash)
  {
    if (fRig.slice[senderId].in_use == 1)
    {
      // If B just became "in use", pre-clear the clock area once before repaint.
      if (senderId == B) {
        ClockWidget_OnSliceBActivated();
      }

      //CurFreq[senderId] = fRig.slice[senderId].RF_frequency;
      CurFreq[senderId] = 0.0;
      if (!InSetup)
      {
        // RefreshScreen() sets MenuActive = false as a side effect. If that
        // runs while the QSY Selector is open, every Disp* guard stops working
        // and the main screen draws over the selector. Skip the refresh; the
        // selector redraws the screen itself when it closes.
        if (!QsySel::isVisible()) {
          RefreshScreen();
        }
      }
    }
    else
    {
      CurFreq[senderId] = 0.0;

      // Tighten the clear to this slice's panel without touching the vertical divider at x=240.
      const bool isA = (senderId == A);
      const int x = isA ? 0   : 241;
      const int y = 0;
      const int w = isA ? 240 : 239;
      const int h = 239;
      // The QSY Selector owns the whole screen while it is open. Clearing
      // half the display here would black out its tile grid. The selector
      // redraws the screen itself when it closes.
      if (!QsySel::isVisible()) {
        tft.fillRect(x, y, w, h, COLOR_BLACK);
      }

      DispSlice();
    }
  }

  if (fRig.slice[senderId].active == 1)
  {
    LoadFilterMenu(fRig.slice[senderId].mode);
    // LoadFilterMenu() sets MenuIDX = FilterMenuIDX as a side effect,
    // so the MenuIDX test below cannot tell whether a filter menu is
    // actually on screen. MenuActive is also true while the QSY
    // Selector is open, and DispMenu() has no guard of its own: it
    // does a full fillScreen(). Exclude the selector explicitly.
    if (MenuActive && !QsySel::isVisible() && MenuIDX == FilterMenuIDX)
    {
      DispMenu(FilterMenuIDX);
    }

    int idx = TMU_HandleToPanIndexSafe(
        static_cast<uint32_t>(fRig.slice[senderId].pan),
        TMU_ArrayLen(fRig.panadapter));

    if (idx >= 0)
    {
        for (Band = 0; Band < static_cast<int>(sizeof(BandMenu) / sizeof(BandMenu[0])); ++Band)
        {
            if (fRig.panadapter[idx].band == BandMenu[Band])
            {
                // Serial.print("*************  Active Band: ");
                break;  // found active band
            }
        }
    }

  }
  //debug("Active Pan: "); debugln(fRig.activePanadapter);
  DispStep(senderId);
  DispProfile();
}


void onSlice_index_letter(const int senderId)
{
  debug("onSlice_index_letter(");
  debug(senderId);
  debugln(") event!");

  //ResetScreenSaver();

  if (!Splash)
  {
    DispSlice();
  }
}

void onSlice_RF_frequency(const int senderId)
{
  debug("onSlice_RF_frequency(");
  debug(senderId);
  debugln(") event!");
  debug("Slice: ");
  debug(senderId);
  debug(" - Frequency:");
  debugln(fRig.slice[senderId].RF_frequency);

  if (senderId < 0 || senderId >= fRig.nMaxSlice) return;

  // Only treat frequency changes on the active slice as user activity
  if (fRig.slice[senderId].active == 1 && !Splash) {
    ResetScreenSaver("Slice event: active slice frequency change");
  }

  int tmpStep;

  if (CurFreq[senderId] == 0.0)
  {
    CurFreq[senderId] = fRig.slice[senderId].RF_frequency;
  }

  tmpStep = fRig.slice[senderId].RF_frequency - CurFreq[senderId];

  DispFrq(senderId);
  CheckInBand(senderId);

  if (VFOTrack && fRig.slice[1 - senderId].in_use == 1)
  {
    fRig.setFreq(1 - senderId, CurFreq[1 - senderId] + tmpStep);
    DispFrq(1 - senderId);
    CheckInBand(1 - senderId);
  }

  CurFreq[senderId]     = fRig.slice[senderId].RF_frequency;
  CurFreq[1 - senderId] = fRig.slice[1 - senderId].RF_frequency;
}

void onSlice_rit_on(const int senderId)
{
  debug("onSlice_rit_on(");
  debug(senderId);
  debugln(") event!");

  //ResetScreenSaver();
  DispRIT(senderId);
}

void onSlice_rit_freq(const int senderId)
{
  debug("onSlice_rit_freq(");
  debug(senderId);
  debugln(") event!");
  debug("RIT Freq: ");
  debugln(fRig.slice[senderId].rit_freq);

  //ResetScreenSaver();
  DispRIT(senderId);
}

void onSlice_xit_on(const int senderId)
{
  debug("onSlice_xit_on(");
  debug(senderId);
  debugln(") event!");

  DispXIT(senderId);
}

void onSlice_xit_freq(const int senderId)
{
  debug("onSlice_xit_freq(");
  debug(senderId);
  debugln(") event!");

  DispXIT(senderId);
}

// void onSlice_wide(const int senderId)
// {
//   debug("onSlice_wide(");   debug(senderId);  debugln(") event!");
// }

void onSlice_filter_lo(const int senderId)
{
  debug("onSlice_filter_lo(");
  debug(senderId);
  debugln(") event!");
  debug("Filter Low: ");
  debug(fRig.slice[senderId].filter_lo);
  debug("  ");
  debugln(fRig.slice[senderId].mode);

  //ResetScreenSaver();

  if (fRig.slice[senderId].mode != "CW")
  {
    LowVal[senderId]     = fRig.slice[senderId].filter_lo / 10;
    LowValSave[senderId] = LowVal[senderId];

    if (senderId == A)
    {
      LowAEnc.write(LowVal[senderId] * LowAEncSteps);
    }
    else
    {
      LowBEnc.write(LowVal[senderId] * LowBEncSteps);
    }
  }

  DispFilter(senderId);
}

void onSlice_filter_hi(const int senderId)
{
  debug("onSlice_filter_hi(");
  debug(senderId);
  debugln(") event!");
  debug("Filter High: ");
  debugln(fRig.slice[senderId].filter_hi);

  //ResetScreenSaver();

  if (fRig.slice[senderId].mode != "CW")
  {
    HighVal[senderId]     = fRig.slice[senderId].filter_hi / 10;
    HighValSave[senderId] = HighVal[senderId];

    if (senderId == A)
    {
      HighAEnc.write(HighVal[senderId] * HighAEncSteps);
    }
    else
    {
      HighBEnc.write(HighVal[senderId] * HighBEncSteps);
    }
  }

  DispFilter(senderId);
}

// void onSlice_step(const int senderId)
// {
//   debug("onSlice_step(");   debug(senderId);  debugln(") event!");
// }

void onSlice_agc_threshold(const int senderId)
{
  debug("onSlice_agc_threshold(");
  debug(senderId);
  debugln(") event!");
  debug("Agc-T: ");
  debugln(fRig.slice[senderId].agc_threshold);

  //ResetScreenSaver();

  if (fRig.slice[senderId].mode == "FM" || fRig.slice[senderId].mode == "NFM" || fRig.slice[senderId].mode == "DFM")
  {
    return;
  }

  AGCVal[senderId] = fRig.slice[senderId].agc_threshold;
  if (AGCVal[senderId] != AGCValSave[senderId])
  {
    AGCValSave[senderId] = AGCVal[senderId];
    if (senderId == A)
    {
      AGCAEnc.write(AGCVal[A] * AGCAEncSteps);
    }
    else
    {
      AGCBEnc.write(AGCVal[B] * AGCBEncSteps);
    }
  }

  DispAGC(senderId);
}

// void onSlice_agc_off_level(const int senderId)
// {
//   debug("onSlice_agc_off_level(");   debug(senderId);  debugln(") event!");
// }

void onSlice_pan(const int senderId)
{
  //   debug("onSlice_pan(");   debug(senderId);  debugln(") event!");
  //   debug("Pan ID: "); debugln(fRig.slice[senderId].pan);
}

// void onSlice_loopa(const int senderId)
// {
//   debug("onSlice_loopa(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_loopb(const int senderId)
// {
//   debug("onSlice_loopb(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_qsk(const int senderId)
// {
//   debug("onSlice_qsk(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_dax(const int senderId)
// {
//   debug("onSlice_dax(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_dax_clients(const int senderId)
// {
//   debug("onSlice_dax_clients(");   debug(senderId);  debugln(") event!");
// }

void onSlice_lock(const int senderId)
{
  debug("onSlice_lock(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
  DispLock(senderId);
}

void onSlice_tx(const int senderId)
{
  debug("onSlice_tx(");
  debug(senderId);
  debugln(") event!");
  debug("TX: ");
  debug(senderId);
  debug("  ");
  debugln(fRig.slice[senderId].tx);

  if (senderId < 0 || senderId >= fRig.nMaxSlice) return;

  if (fRig.slice[senderId].tx == 1)
  {
    // Slice becomes TX owner – real user activity
    ResetScreenSaver("Slice event: TX slice selected");
    TXSlice = senderId;
    if (fRig.slice[senderId].mode == "CW") {
      GotSpeedParm = true;
    }
  }
  else
  {
    // TX cleared on this slice – also user activity
    ResetScreenSaver("Slice event: TX cleared on slice");
  }

  DispTX(senderId);

  CheckInBand(A);
  CheckInBand(B);
}

// void onSlice_dax_tx(const int senderId)
// {
//   debug("onSlice_dax_tx(");   debug(senderId);  debugln(") event!");
// }

void onSlice_active(const int senderId)
{
  // Bounds guard: we only support slice 0 (A) and 1 (B)
  if (senderId != 0 && senderId != 1) {
    return;
  }

  debug("onSlice_active(");
  debug(senderId);
  debugln(") event!");
  debug("Slice ");
  debug(senderId);
  debug(": ");
  debugln(fRig.slice[senderId].active);

  const int activeFlag = fRig.slice[senderId].active;

  if (activeFlag == 1)
  {
    // User explicitly selected this slice as active
    ResetScreenSaver("Slice event: active slice selected");
  }
  else
  {
    // Active flag cleared – still user-driven in SmartSDR
    ResetScreenSaver("Slice event: active flag cleared");
  }

  if (fRig.slice[senderId].active == 1)
  {
    SliceActiveVal = senderId;
    LoadFilterMenu(fRig.slice[senderId].mode);
    // LoadFilterMenu() sets MenuIDX = FilterMenuIDX as a side effect,
    // so the MenuIDX test below cannot tell whether a filter menu is
    // actually on screen. MenuActive is also true while the QSY
    // Selector is open, and DispMenu() has no guard of its own: it
    // does a full fillScreen(). Exclude the selector explicitly.
    if (MenuActive && !QsySel::isVisible() && MenuIDX == FilterMenuIDX)
    {
      DispMenu(FilterMenuIDX);
    }

    int panId = 0;
    int idx = TMU_HandleToPanIndexSafe((uint32_t)fRig.slice[senderId].pan, TMU_ArrayLen(fRig.panadapter));
    if (idx >= 0) {
      const uint32_t idxU = (uint32_t)idx;
      panId = (idxU <= 1u) ? (int)idxU : 0;
    }

    if (TMU_IsValidPanHandle(static_cast<uint32_t>(fRig.panadapter[panId].pan)))
    {
      for (Band = 0; Band < static_cast<int>(sizeof(BandMenu) / sizeof(BandMenu[0])); ++Band)
      {
        if (fRig.panadapter[panId].band == BandMenu[Band])
        {
          // Active band matched; nothing else to do here for UI
          break;
        }
      }
    }
    // Defer SPLIT finalization (no sending here)
  }

  // UI hook: if B just became active, prepare clock area once
  if (senderId == 1 /*B*/ && fRig.slice[senderId].active == 1) {
    ClockWidget_OnSliceBActivated();
  }

  // Always redraw slice UI at the end
  DispSlice();
}

void onSlice_audio_gain(const int senderId)
{
  // Guard: valid slice index
  if (senderId < 0 || senderId >= fRig.nMaxSlice) return;

  // <-- This line was missing in your build, causing "val not declared"
  const int val = fRig.slice[senderId].audio_gain;

  // Accept only sane range 0..100 to avoid uninitialized garbage (e.g. 16843009)
  if (val < 0 || val > 100) return;

  debug("onSlice["); debug(senderId); debugln("] audio_gain event!");
  debug("Audio Gain: "); debugln(val);

  // Update UI state
  VolVal[senderId] = val;
  if (VolVal[senderId] != VolValSave[senderId])
  {
    VolValSave[senderId] = VolVal[senderId];
    if (senderId == A)
    {
      VolAEnc.write(VolVal[A] * VolAEncSteps);
    }
    else if (senderId == B)
    {
      VolBEnc.write(VolVal[B] * VolBEncSteps);
    }
    DispVol(senderId);
  }

  //ResetScreenSaver();
}


void onSlice_audio_pan(const int senderId)
{
  if (senderId < 0 || senderId >= fRig.nMaxSlice) return;

  const int val = fRig.slice[senderId].audio_pan;
  if (val < 0 || val > 100) return;

  debug("onSlice_audio_pan("); debug(senderId); debugln(") event!");
  debug("Audio Pan: "); debugln(val);

  //ResetScreenSaver();
}

void onSlice_audio_mute(const int senderId)
{
  if (senderId < 0 || senderId >= fRig.nMaxSlice) return;

  const int val = fRig.slice[senderId].audio_mute;
  // guard: treat anything other than 0/1 as noise
  if (!(val == 0 || val == 1)) return;

  debug("onSlice_audio_mute("); debug(senderId); debugln(") event!");
  debug("Audio Mute Slice "); debug(senderId); debug(": "); debugln(val);

  //ResetScreenSaver();

  MuteVal[senderId] = val;
  DispMute(senderId);
}


// void onSlice_record(const int senderId)
// {
//   debug("onSlice_record(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_record_time(const int senderId)
// {
//   debug("onSlice_record_time(");   debug(senderId);  debugln(") event!");
// }


// void onSlice_anf(const int senderId)
// {
//   debug("onSlice_anf(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_anf_level(const int senderId)
// {
//  debug("onSlice_anf_level(");   debug(senderId);  debugln(") event!");
// }

void onSlice_nr(const int senderId)
{
  debug("onSlice_nr(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
  DispNR(senderId);
}

void onSlice_nr_level(const int senderId)
{
  debug("onSlice_nr_level(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
  DispNR(senderId);
}

void onSlice_wnb(const int senderId)
{
  debug("onSlice_wnb(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
}

void onSlice_nb(const int senderId)
{
  debug("onSlice_nb(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
  DispNB(senderId);
}

void onSlice_wnb_level(const int senderId)
{
  debug("onSlice_wnb_level(");
  debug(senderId);
  debugln(") event!");
  if (fRig.slice[senderId].in_use == 1)
  {
    WNBLevel     = fRig.slice[senderId].wnb_level;
    WNBLevelSave = WNBLevel;
  }

  if (Encoder_9 == Enc9_WNBLevel && !MenuActive && fRig.slice[senderId].in_use == 1)
  {
    CWMicEnc.write(WNBLevel * CWEncSteps);
    DispWNB();
  }

  //ResetScreenSaver();
}

void onSlice_nb_level(const int senderId)
{
  debug("onSlice_nb_level(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
  DispNB(senderId);
}

void onSlice_apf(const int senderId)
{
  debug("onSlice_apf(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
}

void onSlice_apf_level(const int senderId)
{
  debug("onSlice_apf_level(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
}

void onSlice_squelch(const int senderId)
{
  debug("onSlice_squelch(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
}

void onSlice_squelch_level(const int senderId)
{
  debug("onSlice_squelch_level(");
  debug(senderId);
  debugln(") event!");
  //ResetScreenSaver();
  if (fRig.slice[senderId].mode != "FM" && fRig.slice[senderId].mode != "NFM" && fRig.slice[senderId].mode != "DFM")
  {
    return;
  }

  AGCVal[senderId] = fRig.slice[senderId].squelch_level;
  if (AGCVal[senderId] != AGCValSave[senderId])
  {
    AGCValSave[senderId] = AGCVal[senderId];
    if (senderId == A)
    {
      AGCAEnc.write(AGCVal[A] * AGCAEncSteps);
    }
    else
    {
      AGCBEnc.write(AGCVal[B] * AGCBEncSteps);
    }
  }

  DispAGC(senderId);
}

// void onSlice_diversity(const int senderId)
// {
//   debug("onSlice_diversity(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_diversity_parent(const int senderId)
// {
//   debug("onSlice_diversity_parent(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_diversity_child(const int senderId)
// {
//   debug("onSlice_diversity_child(");   debug(senderId);  debugln(") event!");
// }

// void onSlice_diversity_index(const int senderId)
// {
//   debug("onSlice_diversity_index(");   debug(senderId);  debugln(") event!");
// }

void onSlice_filter_shift(const int senderId)
{
  debug("onSlice_filter_shift(");
  debug(senderId);
  debugln(") event!");
  debug("Shift: ");
  debugln(fRig.slice[senderId].filter_shift);

  //ResetScreenSaver();

  if (fRig.slice[senderId].mode == "CW")
  //if(fRig.slice[senderId].mode != "LSB" && fRig.slice[senderId].mode != "USB" && fRig.slice[senderId].mode != "DIGL" && fRig.slice[senderId].mode != "DIGU")
  {
    LowVal[senderId]     = fRig.slice[senderId].filter_shift / 10;
    LowValSave[senderId] = LowVal[senderId];

    if (senderId == A)
    {
      LowAEnc.write(LowVal[senderId] * LowAEncSteps);
    }
    else
    {
      LowBEnc.write(LowVal[senderId] * LowBEncSteps);
    }
  }
}

void onSlice_filter_width(const int senderId)
{
  debug("onSlice_filter_width(");
  debug(senderId);
  debugln(") event!");
  debug("Width: ");
  debugln(fRig.slice[senderId].filter_width);

  //ResetScreenSaver();

  if (fRig.slice[senderId].mode == "CW")
  //if(fRig.slice[senderId].mode != "LSB" && fRig.slice[senderId].mode != "USB" && fRig.slice[senderId].mode != "DIGL" && fRig.slice[senderId].mode != "DIGU")
  {
    HighVal[senderId]     = fRig.slice[senderId].filter_width / 10;
    HighValSave[senderId] = HighVal[senderId];

    if (senderId == A)
    {
      HighAEnc.write(HighVal[senderId] * HighAEncSteps);
    }
    else
    {
      HighBEnc.write(HighVal[senderId] * HighBEncSteps);
    }

    DispFilter(senderId);
  }
}

void onSlice_rxant(const int senderId)
{
  //  debug("onSlice_rxant(");   debug(senderId);  debugln(") event!");
  RXAnt = fRig.slice[senderId].rxant;
  //ResetScreenSaver();
}

void onSlice_mode(const int senderId)
{
  debug("onSlice_mode(");
  debug(senderId);
  debugln(") event!");
  debug("Mode: ");
  debugln(fRig.slice[senderId].mode);

  if (senderId < 0 || senderId >= fRig.nMaxSlice) return;

  // Only treat mode changes on the active slice as user activity
  if (fRig.slice[senderId].active == 1) {
    ResetScreenSaver("Slice event: mode change on active slice");
  }

  if (fRig.slice[senderId].mode != "CW")
  {
    VFOStep[senderId] = VFOStepSSB[senderId];
    DispStep(senderId);
    VFOTuningRate[senderId] = TuningRateSSB[senderId];
  }
  else if (fRig.slice[senderId].mode == "CW")
  {
    VFOStep[senderId] = VFOStepCW[senderId];
    DispStep(senderId);
    VFOTuningRate[senderId] = TuningRateCW[senderId];
    if (senderId == TXSlice) {
      GotSpeedParm = true;
    }
  }

  VFOTuningRateSave[senderId] = VFOTuningRate[senderId];

  if (fRig.slice[senderId].mode == "FM" || fRig.slice[senderId].mode == "NFM" || fRig.slice[senderId].mode == "DFM")
  {
    AGCVal[senderId] = fRig.slice[senderId].squelch_level;
  }
  else
  {
    AGCVal[senderId] = fRig.slice[senderId].agc_threshold;
  }

  if (senderId == A)
  {
    AGCAEnc.write(AGCVal[senderId] * AGCAEncSteps);
  }
  else
  {
    AGCBEnc.write(AGCVal[senderId] * AGCBEncSteps);
  }

  AGCValSave[senderId] = AGCVal[senderId];

  DispAGC(senderId);
  DispMode(senderId);
  DispFilter(senderId);

  CheckInBand(A);
  CheckInBand(B);

  if (fRig.slice[senderId].active == 1)
  {
    LoadFilterMenu(fRig.slice[senderId].mode);
    // LoadFilterMenu() sets MenuIDX = FilterMenuIDX as a side effect,
    // so the MenuIDX test below cannot tell whether a filter menu is
    // actually on screen. MenuActive is also true while the QSY
    // Selector is open, and DispMenu() has no guard of its own: it
    // does a full fillScreen(). Exclude the selector explicitly.
    if (MenuActive && !QsySel::isVisible() && MenuIDX == FilterMenuIDX)
    {
      DispMenu(FilterMenuIDX);
    }
  }

  // Keep the QSY Selector's mode highlight in sync. This updates
  // two small rectangles only, never a full redraw.
  QsySel::onRadioModeChanged();
}

void onSlice_step_list(const int senderId)
{
  //   debug("onSlice_step_list(");   debug(senderId);  debugln(") event!");
}

void onSlice_agc_mode(const int senderId)
{
  //  debug("onSlice_agc_mode(");   debug(senderId);  debugln(") event!");
}

void onSlice_txant(const int senderId)
{
  //   debug("onSlice_txant(");   debug(senderId);  debugln(") event!");
}

void onSlice_play(const int senderId)
{
  //   debug("onSlice_play(");   debug(senderId);  debugln(") event!");
}

void onSlice_ant_list(const int senderId)
{
  //   debug("onSlice_ant_list(");   debug(senderId);  debugln(") event!");
}
