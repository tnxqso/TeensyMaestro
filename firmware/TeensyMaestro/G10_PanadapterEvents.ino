void configurePanadapterEvents()
{
  for (int i = 0; i < fRig.nMaxPanadapter; i++)
  {
    fRig.panadapter[i].attach_pan_event(onPanadapter_pan);
    // fRig.panadapter[i].attach_y_pixels_event(onPanadapter_y_pixels);
    // fRig.panadapter[i].attach_min_dbm_event(onPanadapter_min_dbm);
    // fRig.panadapter[i].attach_max_dbm_event(onPanadapter_max_dbm);
    // fRig.panadapter[i].attach_fps_event(onPanadapter_fps);
    // fRig.panadapter[i].attach_average_event(onPanadapter_average);
    // fRig.panadapter[i].attach_weighted_average_event(onPanadapter_weighted_average);
    // fRig.panadapter[i].attach_rfgain_event(onPanadapter_rfgain);
    // fRig.panadapter[i].attach_wide_event(onPanadapter_wide);
    // fRig.panadapter[i].attach_loopa_event(onPanadapter_loopa);
    // fRig.panadapter[i].attach_loopb_event(onPanadapter_loopb);
    fRig.panadapter[i].attach_band_event(onPanadapter_band);
    // fRig.panadapter[i].attach_daxiq_event(onPanadapter_daxiq);
    // fRig.panadapter[i].attach_daxiq_rate_event(onPanadapter_daxiq_rate);
    // fRig.panadapter[i].attach_capacity_event(onPanadapter_capacity);
    // fRig.panadapter[i].attach_available_event(onPanadapter_available);
    // fRig.panadapter[i].attach_waterfall_event(onPanadapter_waterfall);
    fRig.panadapter[i].attach_xvtr_event(onPanadapter_xvtr);
    // fRig.panadapter[i].attach_pre_event(onPanadapter_pre);
    // fRig.panadapter[i].attach_min_bw_event(onPanadapter_min_bw);
    // fRig.panadapter[i].attach_max_bw_event(onPanadapter_max_bw);
    // fRig.panadapter[i].attach_bandwidth_event(onPanadapter_bandwidth);
    // fRig.panadapter[i].attach_center_event(onPanadapter_center);
    // fRig.panadapter[i].attach_ant_list_event(onPanadapter_ant_list);
    // fRig.panadapter[i].attach_rxant_event(onPanadapter_rxant);
  }
}

void onPanadapter_pan(const int senderId)
{
  //   debug("onPanadapter_pan(");   debug(senderId);  debugln(") event!");
  //   debug("Pan: "); debugln(fRig.panadapter[senderId].pan);
}

// void onPanadapter_x_pixels(const int senderId) {
// debug("onPanadapter_x_pixels(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_y_pixels(const int senderId) {
// debug("onPanadapter_y_pixels(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_min_dbm(const int senderId) {
//debug("onPanadapter_min_dbm(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_max_dbm(const int senderId) {
//debug("onPanadapter_max_dbm(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_fps(const int senderId) {
// debug("onPanadapter_fps(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_average(const int senderId) {
//debug("onPanadapter_average(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_weighted_average(const int senderId) {
// debug("onPanadapter_weighted_average(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_rfgain(const int senderId) {
// debug("onPanadapter_rfgain(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_wide(const int senderId) {
// debug("onPanadapter_wide(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_loopa(const int senderId) {
// debug("onPanadapter_loopa(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_loopb(const int senderId) {
// debug("onPanadapter_loopb(");   debug(senderId);  debugln(") event!");
// }

void onPanadapter_band(const int senderId)
{
  debug("onPanadapter_band(");
  debug(senderId);
  debugln(") event!");
  debug("Band:");
  debugln(fRig.panadapter[senderId].band);
  debug("SenderId: ");
  debugln(senderId);

  //ResetScreenSaver();

  //Band = fRig.panadapter[ActivePan].band

  if (senderId == A)
  {
    Band0 = fRig.panadapter[senderId].band;
    debug("Band A: ");
    debugln(Band0);
  }
  else
  {
    Band1 = fRig.panadapter[senderId].band;
    debug("Band B: ");
    debugln(Band1);
  }

  for (Band = 0; Band < static_cast<int>(sizeof(BandMenu) / sizeof(BandMenu[0])); ++Band)
  {
    if (fRig.panadapter[senderId].band == BandMenu[Band])
    {
      break;  // found matching band index
    }
  }

  // Serial.print("*************  Band: ");
  // Serial.println(BandMenu[Band]);

  CurFreq[senderId] = 0.0;  // Reset CurFreq for band changes

  MicSelInt = true; // get the proper mic profile when changing bands

  //Do your business here
}

// void onPanadapter_daxiq(const int senderId) {
// debug("onPanadapter_daxiq(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_daxiq_rate(const int senderId) {
// debug("onPanadapter_daxiq_rate(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_capacity(const int senderId) {
// debug("onPanadapter_capacity(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_available(const int senderId) {
// debug("onPanadapter_available(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_waterfall(const int senderId) {
// debug("onPanadapter_waterfall(");   debug(senderId);  debugln(") event!");
// }

void onPanadapter_xvtr(const int senderId)
{
  debug("onPanadapter_xvtr(");
  debug(senderId);
  debugln(") event!");
  CurFreq[senderId] = 0.0;
}

// void onPanadapter_pre(const int senderId) {
// debug("onPanadapter_pre(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_min_bw(const int senderId) {
// debug("onPanadapter_min_bw(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_max_bw(const int senderId) {
// debug("onPanadapter_max_bw(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_bandwidth(const int senderId) {
//debug("onPanadapter_bandwidth(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_center(const int senderId) {
// debug("onPanadapter_center(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_ant_list(const int senderId) {
// debug("onPanadapter_ant_list(");   debug(senderId);  debugln(") event!");
// }

// void onPanadapter_rxant(const int senderId) {
// debug("onPanadapter_rxant(");   debug(senderId);  debugln(") event!");
// }
