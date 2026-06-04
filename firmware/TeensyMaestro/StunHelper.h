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
#include <IPAddress.h>

// Public result container filled by STUN
struct StunMapping {
  bool      ok = false;
  IPAddress externalIP;
  uint16_t  externalPort = 0;
  const char* serverUsed = nullptr;
  IPAddress serverAddr;
};

// Filled by STUN on success: external mapped UDP port for VITA-49
extern int STUN_ExternalVitaPort;

// Run STUN and remember external port; call after Ethernet is up.
bool DoStunAndAdoptVitaPort(uint16_t localPort /* usually UDP_VITA49_PORT */);

// Returns true for RFC1918 / CGNAT / link-local / loopback ranges.
// STUN is only meaningful when the target is reached over the public Internet
// through our own NAT. A private target means a routed/VPN/LAN path with no NAT,
// so STUN must be skipped to avoid announcing a meaningless WAN-mapped port.
bool isPrivateIp(const IPAddress &a);
