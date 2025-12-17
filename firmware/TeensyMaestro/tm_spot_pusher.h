/*
  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO

  A community-maintained edition with open-source utilities
  for ham radio enthusiasts, focusing on FlexRadio® and Wavelog integrations.

  Based on the original TeensyMaestro by Len Koppl (KD0RC),
  which integrates the FlexRadio 6000 library by IW7DMH.
  Portions of this work remain © Len Koppl and © IW7DMH as noted.

  See LICENSE for full license text and NOTICE for attributions.
  Creative Commons BY-NC-SA 3.0: https://creativecommons.org/licenses/by-nc-sa/3.0/
*/

#pragma once
// tm_spot_pusher.h - Non-blocking background spot sender for FlexRadio.
//
// Design:
// - No delay(); paced by millis().
// - Sends at most one spot per INTERVAL_MS.
// - Safe to call tick() every loop(); it self-throttles.
// - Can be started after setup() from main .ino when fRig.connected.
//
// API:
//   SpotPusher::start(SpotFreq, SpotText, SpotIDX);
//   SpotPusher::tick();               // call in loop()
//   SpotPusher::isRunning();          // true while pushing
//   SpotPusher::isDone();             // true when finished
//   SpotPusher::cancel();             // abort
//   SpotPusher::progress(sent, total);

#include <Arduino.h>

// Forward-declare your rig object (defined elsewhere)
extern FlexRig fRig;

namespace SpotPusher {

  // Tunables
  static constexpr uint32_t INTERVAL_MS = 1000; // one spot per second

  struct State {
    const String* freq = nullptr;   // user-provided arrays (do not free)
    const String* text = nullptr;
    int total = 0;
    int index = 0;
    bool running = false;
    uint32_t lastMs = 0;
  };

  inline State& S() { static State s; return s; }

  inline void start(const String* spotFreq, const String* spotText, int count)
  {
    State& s = S();
    s.freq    = spotFreq;
    s.text    = spotText;
    s.total   = count < 0 ? 0 : count;
    s.index   = 0;
    s.running = (s.total > 0);
    s.lastMs  = 0;
  }

  inline void cancel()
  {
    State& s = S();
    s.running = false;
    s.index   = 0;
    s.total   = 0;
    s.freq    = nullptr;
    s.text    = nullptr;
  }

  inline bool isRunning() { return S().running; }
  inline bool isDone()    { const State& s = S(); return (!s.running && s.total > 0); }

  inline void progress(int& sent, int& total)
  {
    const State& s = S();
    sent  = s.index;
    total = s.total;
  }

  inline void tick()
  {
    State& s = S();
    if (!s.running) return;

    // Abort if rig disconnected
    if (!fRig.connected) { cancel(); return; }

    const uint32_t now = millis();
    if (s.lastMs != 0 && (now - s.lastMs) < INTERVAL_MS) return;

    if (s.index >= s.total) { cancel(); return; }

    // Build one line and send
    const String& f = s.freq[s.index];
    const String& t = s.text[s.index];
    fRig.send("spot add rx_freq=" + f + " callsign=" + t);
    //Serial.printf("SpotPusher: sent spot %d/%d: freq=%s text=%s\n",
    //              s.index + 1, s.total, f.c_str(), t.c_str());

    s.index++;
    s.lastMs = now;

    if (s.index >= s.total) { cancel(); } // finished
  }

} // namespace SpotPusher
