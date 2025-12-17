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


#include "tm_netutil.h"

// These are cached copies of INI values we already have elsewhere.
// We keep them locally to avoid tight coupling to globals in other modules.
static IPAddress g_ini_dns(0,0,0,0);
static IPAddress g_ini_gw (0,0,0,0);

void TM_NetUtil_SetIniDns(const IPAddress& dns)     { g_ini_dns = dns; }
void TM_NetUtil_SetIniGateway(const IPAddress& gw)  { g_ini_gw  = gw;  }

// Unique push helper
static void push_unique(IPAddress* arr, int& n, const IPAddress& ip) {
  if (ip == IPAddress(0,0,0,0)) return;
  for (int i=0;i<n;i++) if (arr[i] == ip) return;
  arr[n++] = ip;
}

FLASHMEM bool TM_ResolveHost(const char* host, IPAddress& out)
{
  // Fast path for IPv4 literal
  IPAddress tmp;
  if (tmp.fromString(host)) { out = tmp; return true; }

  // Build an ordered candidate list for DNS
  IPAddress cand[4]; int n = 0;
  push_unique(cand, n, Ethernet.dnsServerIP());  // NIC DNS
  push_unique(cand, n, g_ini_dns);               // INI DNS
  push_unique(cand, n, g_ini_gw);                // try gateway as DNS
  push_unique(cand, n, IPAddress(8,8,8,8));      // public fallback

  DNSClient dns;
  for (int i=0; i<n; ++i) {
    dns.begin(cand[i]);
    if (dns.getHostByName(host, out, 4000) == 1 && out != IPAddress(0,0,0,0)) {
      return true;
    }
  }
  return false;
}
