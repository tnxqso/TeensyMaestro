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
#include "tm_sketch_api.h"
#include "StunHelper.h"
#include "tm_netutil.h"
#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>
#include <NativeDns.h>
#include <Arduino.h>

int STUN_ExternalVitaPort = 0;

bool isPrivateIp(const IPAddress &a) {
  if (a[0] == 10) return true;                               // 10.0.0.0/8
  if (a[0] == 172 && a[1] >= 16 && a[1] <= 31) return true;  // 172.16.0.0/12
  if (a[0] == 192 && a[1] == 168) return true;               // 192.168.0.0/16
  if (a[0] == 100 && a[1] >= 64 && a[1] <= 127) return true; // 100.64.0.0/10 CGNAT
  if (a[0] == 169 && a[1] == 254) return true;               // 169.254.0.0/16 link-local
  if (a[0] == 127) return true;                              // 127.0.0.0/8 loopback
  return false;
}

// ---------- Build-time toggles ----------
// Set to 1 to print detailed STUN parsing logs (attributes, etc.)
#ifndef STUN_DEBUG
#define STUN_DEBUG 0
#endif

// Set to 1 to strictly require Transaction ID to match (RFC-accurate & safer).
// If you observe some NATs/servers dropping TxID echo, set this to 0 to be lenient.
#ifndef STUN_STRICT_TXID
#define STUN_STRICT_TXID 1
#endif

// ---------- Static receive buffer ----------
// Keep big buffers off the main stack on Teensy 4.x.
// 576 bytes is well below typical MTU and enough for common STUN replies.
DMAMEM __attribute__((aligned(32))) static uint8_t g_stun_rxbuf[576];

// Manual forward declaration so Arduino won't invent a wrong one.
static bool stunQueryOne(EthernetUDP& udpSock,
                         const char* host,
                         uint16_t port,
                         uint16_t localPort,
                         StunMapping& out);

// -------------------- Config --------------------

// A small, diverse set of public STUN servers (Google + Twilio + generic)
static const char* STUN_HOSTS[] = {
  "stun.l.google.com",
  "stun1.l.google.com",
  "stun2.l.google.com",
  "stun3.l.google.com",
  "stun4.l.google.com",
  "global.stun.twilio.com",
  "stun.stunprotocol.org"
};
static const uint16_t STUN_PORTS[] = { 19302, 19302, 19302, 19302, 19302, 3478, 3478 };

// Filled by STUN; external mapped UDP port to announce via "client udpport".

extern int MyDNS[4];          // Parsed from INI
extern int MyGateway[4];      // Parsed from INI

// -------------------- Helpers --------------------

// Build a minimal RFC5389 Binding Request with random 12-byte transaction ID.
// Returns a pointer to a static buffer and sets outLen + fills txid.
static uint8_t* buildStunBindingRequest(size_t& outLen, uint8_t txid[12]) {
  static uint8_t buf[20];
  // Type: 0x0001 (Binding Request), Length: 0x0000 (no attributes)
  buf[0] = 0x00; buf[1] = 0x01;
  buf[2] = 0x00; buf[3] = 0x00;
  // Magic cookie 0x2112A442
  buf[4] = 0x21; buf[5] = 0x12; buf[6] = 0xA4; buf[7] = 0x42;

  // Random TxID (12 bytes) — write to BOTH the packet and the txid[] we return
  for (int i = 0; i < 12; ++i) {
    uint8_t r = (uint8_t)random(0, 256);
    buf[8 + i] = r;
    txid[i] = r;           // keep TxID used in packet and the expected one in sync
  }

  outLen = 20;
  return buf;
}

/**
 * Parse STUN Binding Response; supports MAPPED-ADDRESS (0x0001) and
 * XOR-MAPPED-ADDRESS (0x0020 / 0x8020) for IPv4.
 * Defensive bounds checks ensure we don't read past the received UDP payload.
 */
FLASHMEM static bool parseStunResponse(const uint8_t* p, int len, const uint8_t txid[12],
                              IPAddress& outIP, uint16_t& outPort) {
  if (len < 20) return false;

  // Verify magic cookie and (optionally) transaction ID echo
  const uint32_t cookie = (uint32_t)p[4]<<24 | (uint32_t)p[5]<<16 | (uint32_t)p[6]<<8 | p[7];
  const bool cookieOK = (cookie == 0x2112A442);

  bool txid_ok = true;
  if (cookieOK) {
    for (int i = 0; i < 12; ++i) if (p[8+i] != txid[i]) { txid_ok = false; break; }
  } else {
    txid_ok = false;
  }

#if STUN_DEBUG
  debugf("STUN parse: type=0x%02X%02X len=%u cookie=%08lx cookieOK=%d txid_ok=%d\n",
                p[0], p[1], (unsigned)((uint16_t)p[2]<<8 | p[3]),
                (unsigned long)cookie, cookieOK, txid_ok);
#endif

#if STUN_STRICT_TXID
  if (!cookieOK || !txid_ok) {
#if STUN_DEBUG
    debugf("STUN parse: reject, cookieOK=%d txid_ok=%d\n", cookieOK, txid_ok);
#endif
    return false;
  }
#endif

  // msgLen is the length of the attributes following the 20-byte header
  const uint16_t msgLen = (uint16_t)p[2]<<8 | p[3];
  int end = 20 + (int)msgLen;
  if (end > len) end = len;  // never read beyond the UDP payload we actually got

  // Walk attributes (each: 2B type, 2B length, value, 32-bit padded)
  int ofs = 20;
  while ((ofs + 4) <= end) {
    uint16_t attrType = (uint16_t)p[ofs]<<8 | p[ofs+1];
    uint16_t attrLen  = (uint16_t)p[ofs+2]<<8 | p[ofs+3];
    int valOfs = ofs + 4;

    if ((valOfs + attrLen) > end) break;  // malformed; stop parsing safely

#if STUN_DEBUG
    debugf("STUN attr 0x%04X len=%u ofs=%d\n", attrType, attrLen, ofs);
#endif

    // MAPPED-ADDRESS (0x0001)
    if (attrType == 0x0001 && attrLen >= 8) {
      uint8_t family = p[valOfs+1];
      uint16_t port  = (uint16_t)p[valOfs+2]<<8 | p[valOfs+3];
      if (family == 0x01) { // IPv4
        outIP   = IPAddress(p[valOfs+4], p[valOfs+5], p[valOfs+6], p[valOfs+7]);
        outPort = port;
        return true;
      }
    }

    // XOR-MAPPED-ADDRESS (0x0020 or 0x8020)
    if ((attrType == 0x0020 || attrType == 0x8020) && attrLen >= 8 && cookieOK) {
      uint8_t family = p[valOfs+1];
      uint16_t xPort = (uint16_t)p[valOfs+2]<<8 | p[valOfs+3];
      uint16_t port  = xPort ^ 0x2112;
      if (family == 0x01) { // IPv4
        uint8_t xip0 = p[valOfs+4] ^ 0x21;
        uint8_t xip1 = p[valOfs+5] ^ 0x12;
        uint8_t xip2 = p[valOfs+6] ^ 0xA4;
        uint8_t xip3 = p[valOfs+7] ^ 0x42;
        outIP   = IPAddress(xip0, xip1, xip2, xip3);
        outPort = port;
        return true;
      }
    }

    // 32-bit padding per RFC5389 (attributes are padded to a multiple of 4 bytes)
    uint16_t pad = (4 - (attrLen & 3)) & 3;
    ofs += 4 + attrLen + pad;
  }

  return false;
}

// -------------------- Core --------------------

/**
 * Perform one STUN transaction on 'localPort' toward 'host:port'.
 * Binds the socket to the exact local VITA49 port (so NAT pinholes are relevant).
 * If success: fills 'out' with mapping and returns true.
 */
FLASHMEM static bool stunQueryOne(EthernetUDP& udpSock,
                         const char* host,
                         uint16_t port,
                         uint16_t localPort,
                         StunMapping& out) {
  IPAddress srv;
  if (!TM_ResolveHost(host, srv)) {
    debug("STUN DNS failed for "); debugln(host);
    return false;
  }

  // Bind UDP on the exact local port we care about (VITA-49)
  udpSock.stop();
  if (!udpSock.begin(localPort)) {
    debug("UDP begin failed on "); debugln(localPort);
    return false;
  }

  // Build and send a minimal Binding Request
  size_t reqLen = 0;
  uint8_t txid[12];
  uint8_t* req = buildStunBindingRequest(reqLen, txid);

  if (!udpSock.beginPacket(srv, port)) {
    debugln("beginPacket() failed");
    return false;
  }
  udpSock.write(req, reqLen);
  udpSock.endPacket();

  // Non-blocking poll loop with a short timeout (give server room for multiple replies)
  const uint32_t t0 = millis();
  while (millis() - t0 < 1500) { // ~1.5s budget
    int n = udpSock.parsePacket();
    if (n > 0) {
      // Only accept replies from the server we contacted (defensive)
      IPAddress rip = udpSock.remoteIP();
      uint16_t rpo  = udpSock.remotePort();
      if (!(rip == srv && rpo == port)) {
        // Drain unexpected packets on this bound port, but ignore them.
        (void)udpSock.read(g_stun_rxbuf, min(n, (int)sizeof(g_stun_rxbuf)));
#if STUN_DEBUG
        debugf("STUN ignore: packet from %u.%u.%u.%u:%u (expecting %u.%u.%u.%u:%u)\n",
                      rip[0],rip[1],rip[2],rip[3], rpo, srv[0],srv[1],srv[2],srv[3], port);
#endif
        continue;
      }

      // Read the STUN payload into our static buffer
      n = udpSock.read(g_stun_rxbuf, sizeof(g_stun_rxbuf));

      IPAddress extIP; uint16_t extPort = 0;
      if (parseStunResponse(g_stun_rxbuf, n, txid, extIP, extPort)) {
        out.ok = true;
        out.externalIP = extIP;
        out.externalPort = extPort;
        out.serverUsed = host;
        out.serverAddr = srv;
        return true;
      } else {
        // Keep waiting within the timeout; some servers may send multiple replies.
        debugf("STUN reply from %u.%u.%u.%u:%u but no mapped address (n=%d)\n",
                      rip[0],rip[1],rip[2],rip[3], rpo, n);
        continue;
      }
    }
    delay(10);
  }

  debug("No STUN response from "); debug(srv); debug(":"); debugln(port);
  return false;
}

// -------------------- Public API --------------------

/**
 * Run STUN mapping and remember the mapped external port for Flex VITA-49.
 * Call this after Ethernet is up, before connecting to the Flex radio.
 *
 * Returns true if at least one STUN server produced a valid mapping.
 * On success, STUN_ExternalVitaPort is updated; the local bind port
 */
FLASHMEM bool DoStunAndAdoptVitaPort(uint16_t localPort /* usually UDP_VITA49_PORT */) {
  EthernetUDP sock;
  StunMapping m;
  m.ok = false;

  const size_t N = sizeof(STUN_HOSTS) / sizeof(STUN_HOSTS[0]);
  for (size_t i = 0; i < N; ++i) {
    if (stunQueryOne(sock, STUN_HOSTS[i], STUN_PORTS[i], localPort, m)) {
      debug("STUN OK via "); debug(m.serverUsed);
      debug(" ("); debug(m.serverAddr); debugln(")");
      debug("External: "); debug(m.externalIP);
      debug(":"); debugln(m.externalPort);

      // Do NOT change the local bind port here.
      // Only store the external mapped port for the "client udpport" announce.
      STUN_ExternalVitaPort = (int)m.externalPort;

      if (m.externalPort != localPort) {
        debug("NAT remapped local ");
        debug(localPort);
        debug(" -> external ");
        debugln(m.externalPort);
      } else {
        debugln("NAT kept the same UDP port (no remap).");
      }
      sock.stop();
      return true;
    }
  }

  sock.stop();
  debugln("STUN failed on all servers, sticking with default UDP port.");
  return false;
}
