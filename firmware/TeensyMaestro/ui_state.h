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

// ===== [TM UI_STATE DECL] BEGIN =====
#pragma once

// Slice indices used by the UI; keep them aligned with your existing A/B defines.
#ifndef A
#define A 0
#endif
#ifndef B
#define B 1
#endif

// Thin UI-facing state. No Flex types here.
namespace UIState {
  // Set overall connection status (true when the device/app is connected).
  void setConnected(bool connected);

  // Mark whether a given slice is in use (true = occupied by radio UI).
  void setSliceInUse(int idx, bool inUse);

  // Explicit UI-mode flag for "Keyer-Only" layout (radio disconnected, keyer active).
  // When true, the UI may treat the right-hand panel (Slice B area) as free for the clock widget.
  void setKeyerOnly(bool enabled);
  bool isKeyerOnly();

  // Query methods for UI modules:
  bool isConnected();
  bool isSliceFree(int idx); // free == not in use
}
// ===== [TM UI_STATE DECL] END =====
