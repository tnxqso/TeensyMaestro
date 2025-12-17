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

// ===== [TM UI_STATE IMPL] BEGIN =====
#include "ui_state.h"

namespace {
  struct {
    bool connected = false;
    bool sliceInUse[2] = {false, false};
    bool keyerOnly = false; // true => UI behaves as "Keyer-Only" mode (radio disconnected)
  } g;
}

namespace UIState {
  // --- Setters -------------------------------------------------------------
  void setConnected(bool connected) { g.connected = connected; }

  void setSliceInUse(int idx, bool inUse) {
    if (idx == A || idx == B) g.sliceInUse[idx] = inUse;
  }

  void setKeyerOnly(bool enabled) { g.keyerOnly = enabled; }

  // --- Getters -------------------------------------------------------------
  bool isConnected() { return g.connected; }

  bool isSliceFree(int idx) {
    if (idx != A && idx != B) return true; // out-of-range -> treat as free
    return g.sliceInUse[idx] ? false : true;
  }

  bool isKeyerOnly() { return g.keyerOnly; }
}
// ===== [TM UI_STATE IMPL] END =====
