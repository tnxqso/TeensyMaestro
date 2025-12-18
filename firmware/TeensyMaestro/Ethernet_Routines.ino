#include "tm_sketch_api.h"
#include <NativeEthernet.h>
#include <NativeDns.h>

extern byte   CFG_FlexIp[4];
extern String CFG_FlexHost;

extern int Myip[4];
extern int MyMask[4];
extern int MyGateway[4];
extern int MyDNS[4];

/*********************** getIpAddress *********************/
FLASHMEM void getIpAddress()
{
  auto is_apipa = [](const IPAddress &ip) {
    return (ip[0] == 169 && ip[1] == 254);
  };

#if DEBUG
  auto link_to_str = [](int v) -> const char* {
    switch (v) {
      case 1:  return "LinkON";
      case 0:  return "NoLink/Unknown";
      case 2:  return "Unknown";
      default: return "Unknown";
    }
  };
  auto hw_to_str = [](int v) -> const char* {
    switch (v) {
      case 0:  return "NoHardware";
      case 1:  return "W5100/Compat";
      case 2:  return "W5200/Compat";
      case 3:  return "W5500/NativeCompat";
      default: return "Unknown";
    }
  };
#endif

  debugln("=== Ethernet bring-up started ===");

  teensyMAC(mac);

#if DEBUG
  int link_pre = Ethernet.linkStatus();
  int hw_pre   = Ethernet.hardwareStatus();
  debug("Link (pre): ");      debug(link_to_str(link_pre)); debug(" ("); debug(link_pre); debugln(")");
  debug("Hardware (pre): ");  debug(hw_to_str(hw_pre));      debug(" ("); debug(hw_pre);  debugln(")");
#endif

  const bool have_ip    = (Myip[0] | Myip[1] | Myip[2] | Myip[3]) != 0;
  const bool have_mask  = (MyMask[0] | MyMask[1] | MyMask[2] | MyMask[3]) != 0;
  const bool have_gw    = (MyGateway[0] | MyGateway[1] | MyGateway[2] | MyGateway[3]) != 0;
  const IPAddress ini_ip(Myip[0], Myip[1], Myip[2], Myip[3]);
  const bool have_static = have_ip && have_mask && have_gw && !is_apipa(ini_ip);

  if (have_static) {
    debug("Static config found in INI (ip="); debug(ini_ip[0]); debug(".");
    debug(ini_ip[1]); debug("."); debug(ini_ip[2]); debug("."); debug(ini_ip[3]);
    debugln(") -> trying STATIC first");

    getFixedIpAddress();

    if (Ethernet.localIP() == IPAddress(0,0,0,0)) {
      debugln("STATIC resulted in 0.0.0.0 -> attempting DHCP fallback");
      TimeIt = millis();
      while (Ethernet.begin(mac, 6000) == 0) {
        if (millis() - TimeIt > 10000) {
          debugln("DHCP fallback failed -> interface remains down");
          break;
        }
      }
    }
  } else {
    if (have_ip && is_apipa(ini_ip)) {
      debugln("INI contains APIPA (169.254.x.x) -> treating as NO valid static, using DHCP first");
    } else {
      debugln("No valid static config in INI -> using DHCP first");
    }
    TimeIt = millis();
    while (Ethernet.begin(mac, 6000) == 0) {
      if (millis() - TimeIt > 10000) {
        debugln("DHCP failed within timeout -> attempting STATIC fallback");
        getFixedIpAddress();
        break;
      }
    }
  }

#if DEBUG
  int link_post = Ethernet.linkStatus();
  int hw_post   = Ethernet.hardwareStatus();
  debug("Link (post): ");      debug(link_to_str(link_post)); debug(" ("); debug(link_post); debugln(")");
  debug("Hardware (post): ");  debug(hw_to_str(hw_post));      debug(" ("); debug(hw_post);  debugln(")");
#endif

  IPAddress lip = Ethernet.localIP();

#if DEBUG
  IPAddress lmask = Ethernet.subnetMask();
  IPAddress lgw   = Ethernet.gatewayIP();
  IPAddress ldns  = Ethernet.dnsServerIP();
  debug("NIC: IP=");   debug(lip[0]);   debug("."); debug(lip[1]);   debug("."); debug(lip[2]);   debug("."); debug(lip[3]);
  debug("  MASK=");    debug(lmask[0]); debug("."); debug(lmask[1]); debug("."); debug(lmask[2]); debug("."); debug(lmask[3]);
  debug("  GW=");      debug(lgw[0]);   debug("."); debug(lgw[1]);   debug("."); debug(lgw[2]);   debug("."); debug(lgw[3]);
  debug("  DNS=");     debug(ldns[0]);  debug("."); debug(ldns[1]);  debug("."); debug(ldns[2]);  debug("."); debug(ldns[3]);
  debugln("");
#endif

  addr = String(lip[0]) + "." + String(lip[1]) + "." + String(lip[2]) + "." + String(lip[3]);
  debug(TM_PRODUCT_NAME);
  debug(" — ");
  debug(TM_EDITION_SHORT);
  debug(" IP: ");  debugln(addr);
  if (lip == IPAddress(0,0,0,0)) {
    debugln("WARNING: Interface has no IP address (0.0.0.0). Check link, DHCP, or static settings.");
  }
}

/*********************** getFixedIpAddress *********************/
FLASHMEM void getFixedIpAddress()
{
  IPAddress ip   (Myip[0],      Myip[1],      Myip[2],      Myip[3]);
  IPAddress mask (MyMask[0],    MyMask[1],    MyMask[2],    MyMask[3]);
  IPAddress gw   (MyGateway[0], MyGateway[1], MyGateway[2], MyGateway[3]);
  IPAddress dns  (MyDNS[0],     MyDNS[1],     MyDNS[2],     MyDNS[3]);

  debugln("=== Setting fixed IP address");
  debugf("Static cfg: ip=%u.%u.%u.%u mask=%u.%u.%u.%u gw=%u.%u.%u.%u dns=%u.%u.%u.%u\n",
                ip[0], ip[1], ip[2], ip[3],
                mask[0], mask[1], mask[2], mask[3],
                gw[0], gw[1], gw[2], gw[3],
                dns[0], dns[1], dns[2], dns[3]);

  // Bring up the interface unconditionally; link may settle a bit later.
  Ethernet.begin(mac, ip, dns, gw, mask);

  // Give the NIC a brief moment, then dump what it actually took.
  delay(200);
#if DEBUG == 1
  IPAddress lip   = Ethernet.localIP();
  IPAddress lmask = Ethernet.subnetMask();
  IPAddress lgw   = Ethernet.gatewayIP();
  IPAddress ldns  = Ethernet.dnsServerIP();

  debugf("NIC after static: ip=%u.%u.%u.%u gw=%u.%u.%u.%u mask=%u.%u.%u.%u dns=%u.%u.%u.%u\n",
                lip[0],   lip[1],   lip[2],   lip[3],
                lgw[0],   lgw[1],   lgw[2],   lgw[3],
                lmask[0], lmask[1], lmask[2], lmask[3],
                ldns[0],  ldns[1],  ldns[2],  ldns[3]);
#endif
}

/*********************** teensyMAC *********************/
FLASHMEM void teensyMAC(uint8_t *mac)
{
  for (uint8_t by = 0; by < 2; by++) mac[by] = (HW_OCOTP_MAC1 >> ((1 - by) * 8)) & 0xFF;
  for (uint8_t by = 0; by < 4; by++) mac[by + 2] = (HW_OCOTP_MAC0 >> ((3 - by) * 8)) & 0xFF;
  debugf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Resolve CFG_FlexHost into CFG_FlexIp[] once at connect time.
// Returns true if a valid IPv4 was produced.

extern String CFG_FlexHost;
extern byte   CFG_FlexIp[4];
extern int MyDNS[4];

FLASHMEM String tm_strip_inline_comment(const String &s) {
  int sc = s.indexOf(';');
  int hc = s.indexOf('#');
  int cut = -1;
  if (sc >= 0) cut = sc;
  if (hc >= 0 && (cut < 0 || hc < cut)) cut = hc;
  return (cut >= 0) ? s.substring(0, cut) : s;
}

static bool tm_dns_try(const char *host, const IPAddress &server, IPAddress &out, uint16_t timeout_ms = 4000) {
  DNSClient dns;
  dns.begin(server);
  int rc = dns.getHostByName(host, out, timeout_ms);
  debugf("DNS try server=%u.%u.%u.%u host='%s' rc=%d -> %u.%u.%u.%u\n",
                server[0], server[1], server[2], server[3],
                host, rc, out[0], out[1], out[2], out[3]);
  return (rc == 1 && out != IPAddress(0,0,0,0));
}

FLASHMEM bool TM_ComputeFlexTargetIP()
{
  // Nothing configured? Keep any existing IP.
  if (CFG_FlexHost.length() == 0) {
    return (CFG_FlexIp[0] | CFG_FlexIp[1] | CFG_FlexIp[2] | CFG_FlexIp[3]) != 0;
  }

  // Warm-up: give NIC a beat so ARP/DNS socket setup is ready
  delay(200);

  // Sanitize host string (trim + strip inline comments)
  String host = tm_strip_inline_comment(CFG_FlexHost);
  host.trim();

  IPAddress ip;

  // 1) Literal IPv4 fast path
  if (ip.fromString(host)) {
    CFG_FlexIp[0] = ip[0]; CFG_FlexIp[1] = ip[1]; CFG_FlexIp[2] = ip[2]; CFG_FlexIp[3] = ip[3];
    debugf("Flex Host is literal IPv4 -> %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
    return true;
  }

  auto _add_unique = [](IPAddress list[], int &n, const IPAddress &cand) {
    if (cand == IPAddress(0,0,0,0)) return;
    for (int i = 0; i < n; ++i) if (list[i] == cand) return; // already present
    list[n++] = cand;
  };

  IPAddress servers[4];
  int n = 0;
  _add_unique(servers, n, IPAddress(MyDNS[0], MyDNS[1], MyDNS[2], MyDNS[3]));     // INI
  _add_unique(servers, n, Ethernet.dnsServerIP());                                 // NIC
  _add_unique(servers, n, IPAddress(MyGateway[0], MyGateway[1], MyGateway[2], MyGateway[3])); // GW
  if (host.indexOf('.') >= 0) {
    _add_unique(servers, n, IPAddress(8,8,8,8)); // fallback
  }

  // 3) Try resolution against each server, with quick retries on each label
  String hostDot = host;
  if (!host.endsWith(".")) hostDot += ".";

  const uint8_t retries = 2;    // attempts per label
  const uint16_t to_ms = 3000;  // per-attempt timeout
  const uint16_t backoff_ms = 500;

  for (int i = 0; i < n; ++i) {
    if (servers[i] == IPAddress(0,0,0,0)) continue;

    // Try plain host with retries
    for (uint8_t att = 0; att <= retries; ++att) {
      if (tm_dns_try(host.c_str(), servers[i], ip, to_ms)) {
        CFG_FlexIp[0] = ip[0]; CFG_FlexIp[1] = ip[1]; CFG_FlexIp[2] = ip[2]; CFG_FlexIp[3] = ip[3];
        debugf("DNS '%s' -> %u.%u.%u.%u (server %u.%u.%u.%u)\n",
                      host.c_str(), ip[0],ip[1],ip[2],ip[3],
                      servers[i][0],servers[i][1],servers[i][2],servers[i][3]);
        return true;
      }
      delay(backoff_ms);
    }

    // Try FQDN with trailing dot with retries
    for (uint8_t att = 0; att <= retries; ++att) {
      if (tm_dns_try(hostDot.c_str(), servers[i], ip, to_ms)) {
        CFG_FlexIp[0] = ip[0]; CFG_FlexIp[1] = ip[1]; CFG_FlexIp[2] = ip[2]; CFG_FlexIp[3] = ip[3];
        debugf("DNS (FQDN with trailing dot) '%s' -> %u.%u.%u.%u (server %u.%u.%u.%u)\n",
                      hostDot.c_str(), ip[0],ip[1],ip[2],ip[3],
                      servers[i][0],servers[i][1],servers[i][2],servers[i][3]);
        return true;
      }
      delay(backoff_ms);
    }
  }

  debugf("DNS FAILED for '%s' (tried %d server(s))\n", host.c_str(), n);
  return false;
}