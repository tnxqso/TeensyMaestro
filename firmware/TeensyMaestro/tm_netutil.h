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
#include <NativeEthernet.h>
#include <NativeDns.h>          // DNSClient

// Cache INI-props so we don't depend on foreign globals
void TM_NetUtil_SetIniDns(const IPAddress& dns);
void TM_NetUtil_SetIniGateway(const IPAddress& gw);

// Resolve hostname -> IPv4 (tries NIC DNS, INI DNS, GW-as-DNS, 8.8.8.8)
bool TM_ResolveHost(const char* host, IPAddress& out);
