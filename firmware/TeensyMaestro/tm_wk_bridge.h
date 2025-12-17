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
#include <Arduino.h>
#include "tm_wk_config.h"
#include "tm_wk_iface.h"
#include "tm_wk_proto.h"

// Intentionally do not include transport backends here to avoid header cycles.

namespace TM_WK_Bridge {
  // Low-level entry: explicit transport enables (used internally or in tests)
  void begin(bool enableSerial, bool enableTCP);

  // High-level entry: use CFG_WK_Enable / CFG_WK_Transport from MMConfig.ini
  void beginFromConfig();

  // Poll transports + protocol once per main loop
  void poll();
}
