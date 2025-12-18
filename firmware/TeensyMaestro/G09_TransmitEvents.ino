static void onTransmit_lo_wrap() { onTransmit_lo(); }
static void onTransmit_hi_wrap() { onTransmit_hi(); }
static void onTransmit_rfpower_wrap() { onTransmit_rfpower(); }
static void onTransmit_tunepower_wrap() { onTransmit_tunepower(); }
static void onTransmit_vox_level_wrap() { onTransmit_vox_level(); }
static void onTransmit_vox_delay_wrap() { onTransmit_vox_delay(); }
static void onTransmit_mic_level_wrap() { onTransmit_mic_level(); }
static void onTransmit_speed_wrap() { onTransmit_speed(); }
static void onTransmit_break_in_wrap() { onTransmit_break_in(); }
static void onTransmit_break_in_delay_wrap() { onTransmit_break_in_delay(); }
static void onTransmit_mon_gain_sb_wrap() { onTransmit_mon_gain_sb(); }
static void onTransmit_tune_wrap() { onTransmit_tune(); }

void configureTransmitEvents()
{
  // fRig.transmit.attach_freq_event(onTransmit_freq_wrap);
  fRig.transmit.attach_lo_event(onTransmit_lo_wrap);
  fRig.transmit.attach_hi_event(onTransmit_hi_wrap);
  fRig.transmit.attach_rfpower_event(onTransmit_rfpower_wrap);
  fRig.transmit.attach_tunepower_event(onTransmit_tunepower_wrap);
  // fRig.transmit.attach_am_carrier_level_event(onTransmit_am_carrier_level_wrap);
  // fRig.transmit.attach_vox_enable_event(onTransmit_vox_enable_wrap);
  fRig.transmit.attach_vox_level_event(onTransmit_vox_level_wrap);
  fRig.transmit.attach_vox_delay_event(onTransmit_vox_delay_wrap);
  fRig.transmit.attach_mic_level_event(onTransmit_mic_level_wrap);
  // fRig.transmit.attach_mic_boost_event(onTransmit_mic_boost_wrap);
  // fRig.transmit.attach_mic_bias_event(onTransmit_mic_bias_wrap);
  // fRig.transmit.attach_mic_acc_event(onTransmit_mic_acc_wrap);
  // fRig.transmit.attach_compander_event(onTransmit_compander_wrap);
  // fRig.transmit.attach_compander_level_event(onTransmit_compander_level_wrap);
  // fRig.transmit.attach_dax_event(onTransmit_dax_wrap);
  // fRig.transmit.attach_pitch_event(onTransmit_pitch_wrap);
  fRig.transmit.attach_speed_event(onTransmit_speed_wrap);
  // fRig.transmit.attach_iambic_event(onTransmit_iambic_wrap);
  // fRig.transmit.attach_iambic_mode_event(onTransmit_iambic_mode_wrap);
  // fRig.transmit.attach_cwl_enabled_event(onTransmit_cwl_enabled_wrap);
  // fRig.transmit.attach_swap_paddles_event(onTransmit_swap_paddles_wrap);
  fRig.transmit.attach_break_in_event(onTransmit_break_in_wrap);
  fRig.transmit.attach_break_in_delay_event(onTransmit_break_in_delay_wrap);
  // fRig.transmit.attach_sidetone_event(onTransmit_sidetone_wrap);
  // fRig.transmit.attach_monitor_event(onTransmit_monitor_wrap);
  // fRig.transmit.attach_sb_monitor_event(onTransmit_sb_monitor_wrap);
  // fRig.transmit.attach_mon_gain_cw_event(onTransmit_mon_gain_cw_wrap);
  fRig.transmit.attach_mon_gain_sb_event(onTransmit_mon_gain_sb_wrap);
  // fRig.transmit.attach_mon_pan_cw_event(onTransmit_mon_pan_cw_wrap);
  // fRig.transmit.attach_mon_pan_sb_event(onTransmit_mon_pan_sb_wrap);
  fRig.transmit.attach_tune_event(onTransmit_tune_wrap);
  // fRig.transmit.attach_met_in_rx_event(onTransmit_met_in_rx_wrap);
  // fRig.transmit.attach_hwalc_enabled_event(onTransmit_hwalc_enabled_wrap);
  // fRig.transmit.attach_inhibit_event(onTransmit_inhibit_wrap);
  // fRig.transmit.attach_speech_processor_enable_event(onTransmit_speech_processor_enable_wrap);
  // fRig.transmit.attach_speech_processor_level_event(onTransmit_speech_processor_level_wrap);
  // fRig.transmit.attach_show_tx_in_waterfall_event(onTransmit_show_tx_in_waterfall_wrap);
  // fRig.transmit.attach_mic_selection_event(onTransmit_mic_selection_wrap);
}

void onTransmit_lo()
{
  debugln("onTransmit_lo() event!");
  debug("Lo: ");
  debugln(fRig.transmit.lo);
}

void onTransmit_hi()
{
  debugln("onTransmit_hi() event!");
  debug("Hi: ");
  debugln(fRig.transmit.hi);
}

void onTransmit_rfpower()
{
  debugln("onTransmit_rfpower() event!");
  debug("Transmit Power: ");
  debugln(fRig.transmit.rfpower);

  if (TransmitMenuOn)
  {
    MenuItem[TransmitMenu][0] = "RF Power: " + String(fRig.transmit.rfpower);
  }

  RFPower     = fRig.transmit.rfpower;
  RFPowerSave = RFPower;
  if (Encoder_9 == Enc9_RFPower && !MenuActive)
  {
    CWMicEnc.write(RFPower * CWEncSteps);
  }
  //ResetScreenSaver();
  DispRF();
}

void onTransmit_tunepower()
{
  debugln("onTransmit_tunepower() event!");

  TunePower     = fRig.transmit.tunepower;
  TunePowerSave = TunePower;

  if (Encoder_9 == Enc9_TunePower && !MenuActive)
  {
    CWMicEnc.write(TunePower * CWEncSteps);
    DispTune();
  }

  //ResetScreenSaver();
}

// void onTransmit_am_carrier_level() {
// debugln("onTransmit_am_carrier_level() event!");
// }

// void onTransmit_vox_enable() {
//debugln("onTransmit_vox_enable() event!");
// }

void onTransmit_vox_level()
{
  debugln("onTransmit_vox_level() event!");

  VOXLevel     = fRig.transmit.vox_level;
  VOXLevelSave = VOXLevel;

  if (Encoder_9 == Enc9_VOXLevel && !MenuActive)
  {
    CWMicEnc.write(VOXLevel * CWEncSteps);
    DispVOXLevel();
  }
 //ResetScreenSaver();
}

void onTransmit_vox_delay()
{
  debugln("onTransmit_vox_delay() event!");

  VOXDelay     = fRig.transmit.vox_delay;
  VOXDelaySave = VOXDelay;

  if (Encoder_9 == Enc9_VOXDelay && !MenuActive)
  {
    CWMicEnc.write(VOXDelay * CWEncSteps);
    DispVOXDelay();
  }
  //ResetScreenSaver();
}

void onTransmit_mic_level()
{
  debugln("onTransmit_mic_level() event!");
  debug("Mic: ");
  debugln(fRig.transmit.mic_level);
  if (TransmitMenuOn)
  {
    MenuItem[TransmitMenu][1] = "Mic Gain: " + String(fRig.transmit.mic_level);
  }

  MicGain     = fRig.transmit.mic_level;
  MicGainSave = MicGain;

  if (Encoder_9 == Enc9_MicGain && !MenuActive)
  {
    CWMicEnc.write(MicGain * CWEncSteps);
    DispMicGain();
  }
  //ResetScreenSaver();
}

// void onTransmit_mic_boost() {
// debugln("onTransmit_mic_boost() event!");
// }

// void onTransmit_mic_bias() {
// debugln("onTransmit_mic_bias() event!");
// }

// void onTransmit_mic_acc() {
// debugln("onTransmit_mic_acc() event!");
// }

// void onTransmit_compander() {
//debugln("onTransmit_compander() event!");
// }

// void onTransmit_compander_level() {
// debugln("onTransmit_compander_level() event!");
// }

// void onTransmit_dax() {
// debugln("onTransmit_dax() event!");
// }

// void onTransmit_pitch() {
// debugln("onTransmit_pitch() event!");
// }

void onTransmit_speed()
{
  // Only act when not headless
  if (FlexIsHeadless()) {
    debugln("onTransmit_speed: headless -> ignore");
    return;
  }

  // Determine current TX slice and mode (Flex uses "CW", not CWL/CWU)
  const int tx = TXSlice;
  const String mode = (tx >= 0) ? fRig.slice[tx].mode : String("");
  debug("onTransmit_speed: txSlice=");
  debug(tx);
  debug(" mode=");
  debugln(mode);

  // Only react in CW
  const bool isCW = (mode == "CW");
  if (!isCW) {
    debugln("onTransmit_speed: non-CW -> ignore");
    return;
  }

  // Read reported WPM from radio
  const int reported = fRig.transmit.speed;
  debug("onTransmit_speed: reported=");
  debugln(reported);

  // Guard invalid/missing values
  if (reported <= 0) {
    debugln("onTransmit_speed: invalid (<=0) -> ignore");
    return;
  }

  // Optional clamp to a sane CW range; adjust if you prefer different bounds
  int clamped = reported;
  if (clamped < 5)  clamped = 5;
  if (clamped > 60) clamped = 60;

  // Apply only if changed
  if (clamped != CWVal) {
    CWVal     = clamped;
    CWValSave = CWVal;

    debug("onTransmit_speed: applied=");
    debugln(CWVal);

    if (Encoder_9 == Enc9_CWSpeed && !MenuActive) {
      CWMicEnc.write(CWVal * CWEncSteps);
      DispCWSpeed();
    }
  } else {
    debugln("onTransmit_speed: no change");
  }
}


// void onTransmit_iambic() {
//debugln("onTransmit_iambic() event!");
// }

// void onTransmit_iambic_mode() {
// debugln("onTransmit_iambic_mode() event!");
// }

// void onTransmit_cwl_enabled() {
// debugln("onTransmit_cwl_enabled() event!");
// }

// void onTransmit_swap_paddles() {
// debugln("onTransmit_swap_paddles() event!");
// }

void onTransmit_break_in()
{
  debugln("onTransmit_break_in() event!");
  debug("Break In: ");
  debugln(fRig.transmit.break_in);
}

void onTransmit_break_in_delay()
{
  debugln("onTransmit_break_in_delay() event!");
  debug("Break In Delay: ");
  debugln(fRig.transmit.break_in_delay);
  //SaveBkInDelay = fRig.transmit.break_in_delay;
}

// void onTransmit_sidetone() {
//debugln("onTransmit_sidetone() event!");
// }

// void onTransmit_monitor() {
//debugln("onTransmit_monitor() event!");
// }

// void onTransmit_sb_monitor() {
//debugln("onTransmit_sb_monitor() event!");
// }

// void onTransmit_mon_gain_cw() {
// debugln("onTransmit_mon_gain_cw() event!");
// }

void onTransmit_mon_gain_sb()
{
  debugln("onTransmit_mon_gain_sb() event!");

  MonLevel     = fRig.transmit.mon_gain_sb;
  MonLevelSave = MonLevel;

  if (Encoder_9 == Enc9_MonLevel && !MenuActive)
  {
    CWMicEnc.write(MonLevel * CWEncSteps);
    DispMonLevel();
  }
  //ResetScreenSaver();
}

// void onTransmit_mon_pan_cw() {
// debugln("onTransmit_mon_pan_cw() event!");
// }

// void onTransmit_mon_pan_sb() {
// debugln("onTransmit_mon_pan_sb() event!");
// }

void onTransmit_tune()
{
  debugln("onTransmit_tune() event!");

  // User-initiated TX TUNE from SmartSDR – treat as real activity
  ResetScreenSaver("Radio event: TX tune");
}

// void onTransmit_met_in_rx()
// {
//    debugln("onTransmit_met_in_rx() event!");
//
// }

// void onTransmit_hwalc_enabled() {
// debugln("onTransmit_hwalc_enabled() event!");
// }

// void onTransmit_inhibit()
// {
//    debugln("onTransmit_inhibit() event!");
// }

// void onTransmit_speech_processor_enable() {
//debugln("onTransmit_speech_processor_enable() event!");
// }

// void onTransmit_speech_processor_level() {
// debugln("onTransmit_speech_processor_level() event!");
// }

// void onTransmit_show_tx_in_waterfall() {
// debugln("onTransmit_show_tx_in_waterfall() event!");
// }

// void onTransmit_mic_selection() {
// debugln("onTransmit_mic_selection() event!");
// }
