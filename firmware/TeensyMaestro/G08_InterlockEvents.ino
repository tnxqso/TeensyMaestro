void configureInterlockEvents()
{
  // fRig.interlock.attach_timeout_event(onInterlock_timeout);
  // fRig.interlock.attach_acc_txreq_enable_event(onInterlock_acc_txreq_enable);
  // fRig.interlock.attach_rca_txreq_enable_event(onInterlock_rca_txreq_enable);
  // fRig.interlock.attach_acc_txreq_polarity_event(onInterlock_acc_txreq_polarity);
  // fRig.interlock.attach_rca_txreq_polarity_event(onInterlock_rca_txreq_polarity);
  // fRig.interlock.attach_tx1_enabled_event(onInterlock_tx1_enabled);
  // fRig.interlock.attach_tx1_delay_event(onInterlock_tx1_delay);
  // fRig.interlock.attach_tx2_enabled_event(onInterlock_tx2_enabled);
  // fRig.interlock.attach_tx2_delay_event(onInterlock_tx2_delay);
  // fRig.interlock.attach_tx3_enabled_event(onInterlock_tx3_enabled);
  // fRig.interlock.attach_tx3_delay_event(onInterlock_tx3_delay);
  // fRig.interlock.attach_acc_tx_enabled_event(onInterlock_acc_tx_enabled);
  // fRig.interlock.attach_acc_tx_delay_event(onInterlock_acc_tx_delay);
  // fRig.interlock.attach_tx_delay_event(onInterlock_tx_delay);
  fRig.interlock.attach_state_event(onInterlock_state);
  // fRig.interlock.attach_reason_event(onInterlock_reason);
  // fRig.interlock.attach_source_event(onInterlock_source);
}
// void onInterlock_timeout() {
// debugln("onInterlock_timeout() event!");
// }

// void onInterlock_acc_txreq_enable() {
// debugln("onInterlock_acc_txreq_enable() event!");
// }

// void onInterlock_rca_txreq_enable() {
// debugln("onInterlock_rca_txreq_enable() event!");
// }

// void onInterlock_acc_txreq_polarity() {
// debugln("onInterlock_acc_txreq_polarity() event!");
// }

// void onInterlock_rca_txreq_polarity() {
// debugln("onInterlock_rca_txreq_polarity() event!");
// }

// void onInterlock_tx1_enabled() {
// debugln("onInterlock_tx1_enabled() event!");
// }

// void onInterlock_tx1_delay() {
// debugln("onInterlock_tx1_delay() event!");
// }

// void onInterlock_tx2_enabled() {
// debugln("onInterlock_tx2_enabled() event!");
// }

// void onInterlock_tx2_delay() {
// debugln("onInterlock_tx2_delay() event!");
// }

// void onInterlock_tx3_enabled() {
// debugln("onInterlock_tx3_enabled() event!");
// }

// void onInterlock_tx3_delay() {
// debugln("onInterlock_tx3_delay() event!");
// }

// void onInterlock_acc_tx_enabled() {
// debugln("onInterlock_acc_tx_enabled() event!");
// }

// void onInterlock_acc_tx_delay() {
// debugln("onInterlock_acc_tx_delay() event!");
// }

// void onInterlock_tx_delay() {
// debugln("onInterlock_tx_delay() event!");
// }

// --- TX lamp helper: draw strictly from TX/Interlock state ---
static inline void UI_UpdateTxLamp(const bool on)
{
  // Do not draw during splash/menu
  if (Splash || MenuActive) return;

  //ResetScreenSaver("Update TX Lamp");

  // Select which slice owns TX and paint that lamp
  if (fRig.slice[A].tx == 1 && fRig.slice[A].in_use == 1) {
    tft.fillCircle(100, 37, 10, on ? COLOR_RED : COLOR_BLACK);
    // Clear the other side to be safe
    tft.fillCircle(350, 37, 10, COLOR_BLACK);
  } else if (fRig.slice[B].tx == 1 && fRig.slice[B].in_use == 1) {
    tft.fillCircle(350, 37, 10, on ? COLOR_RED : COLOR_BLACK);
    // Clear the other side to be safe
    tft.fillCircle(100, 37, 10, COLOR_BLACK);
  } else {
    // No TX slice -> clear both
    tft.fillCircle(100, 37, 10, COLOR_BLACK);
    tft.fillCircle(350, 37, 10, COLOR_BLACK);
  }
}

void onInterlock_state()
{
  const String& st = fRig.interlock.state;

  // Treat any interlock state change as user-meaningful TX-related activity.
  // Typical values: "READY", "RECEIVE", "TRANSMITTING", "PTT_INHIBIT", etc.
  if (st == "TRANSMITTING") {
    // Explicit TX on-air transition
    ResetScreenSaver("Radio event: TX (interlock state=TRANSMITTING)");
  } else {
    // Any other interlock state change (TX stop, inhibit, back to receive, etc.)
    ResetScreenSaver("Radio event: interlock state change");
  }

  // Update front-panel TX lamp strictly based on interlock state
  if (st == "TRANSMITTING") {
    UI_UpdateTxLamp(true);
  } else {
    UI_UpdateTxLamp(false);
  }
}


// void onInterlock_reason() {
// debugln("onInterlock_reason() event!");
// }

// void onInterlock_source() {
// debugln("onInterlock_source() event!");
// }
