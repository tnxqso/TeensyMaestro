/*
  Library support for FlexRadio 6000 Rigs
  Copyright (C)2015 Vincenzo Stefanazzi - IW7DMH. All right reserved

  This library is free software; you can redistribute it and/or
  modify it under the terms of the CC BY-NC-SA 3.0 license.
  https://creativecommons.org/licenses/by-nc-sa/3.0/

  The license applies to all part of the library including the
  examples and tools supplied with the library.
*/
#include <FlexRigTeensy.h>
#include "tm_attr.h"
#include "tm_utils.h"

#if !DEBUG
__fr_serial_null__ __fr_serial_sink__;
#endif

// Place heavy static members in OCRAM (RAM2)
DMAMEM int    FlexRig::Mem[MEM_CAP];
DMAMEM String FlexRig::MemName[MEM_CAP];
DMAMEM String FlexRig::MemFreq[MEM_CAP];

DMAMEM String FlexRig::TSpotFreq[100];
DMAMEM String FlexRig::TSpotCall[100];

// VITA-49 UDP workspace buffer (align for DMA safety)
DMAMEM char   FlexRig::vitaPacketBuffer[UDP_VITA49_PACKET_MAX_SIZE] __attribute__((aligned(32)));

// Response packet ring buffer (align to at least 16)
DMAMEM rPacket FlexRig::packetElement[RESPONSE_PACKET_LIST_SIZE] __attribute__((aligned(16)));

// Safe compile-time array length helper (works on plain C-arrays)
#ifndef ARRLEN
#define ARRLEN(a) (static_cast<int>(sizeof(a) / sizeof((a)[0])))
#endif

extern int CFG_FlexControlPort; // default = UDP_DISCOVERING_PORT (4992)
extern int STUN_ExternalVitaPort; // default = 0, set by STUN if used
int lastUdpportCmdId = -1;
static bool vita_first_logged = false;
static bool g_vita_seen = false;
static unsigned long vita_accept_ts = 0;
static constexpr uint16_t RADIO_VITA_SRC_PORT = 4993;
static constexpr unsigned long VITA_PROBE_INTERVAL_MS = 10000;

extern bool DisableGUIClient;
extern bool CheckInBand(int slice, bool sync_ui);

bool FlexRig::hasExternalGuiClient() const {
  for (int i = 0; i < Max_Clients && i < 4; ++i) {
    if (Client_Status[i] == "connected" && Client_Program[i] != TM_PRODUCT_NAME) {
      return true;
    }
  }
  return false;
}

bool FlexRig::isEffectiveHeadless() const {
  return DisableGUIClient && !hasExternalGuiClient();
}

// --- Probe sender with built-in logging ---
// Sends one small UDP probe ("TMVITA") to the radio.
// Guard clauses prevent invalid destination or null IP access.
// Logging is inside this function so any caller path will emit logs.
inline void sendVitaProbe(EthernetUDP& udp, const byte* ip, uint16_t dstPort) {
  // Validate arguments first
  if (!ip) {
#if DEBUG == 1
    debugln("VITA probe: ip is null, abort");
#endif
    return;
  }
  if (dstPort == 0 || dstPort > 65535) {
#if DEBUG == 1
    debug("VITA probe: invalid dstPort=");
    debugln((unsigned)dstPort);
#endif
    return;
  }
  if (ip[0]==0 && ip[1]==0 && ip[2]==0 && ip[3]==0) {
#if DEBUG == 1
    debugln("VITA probe: ip=0.0.0.0, abort");
#endif
    return;
  }

  IPAddress dst(ip[0], ip[1], ip[2], ip[3]);

#if DEBUG == 1
  debug("VITA probe: ip=");
  debug(dst);
  debug(" dstPort=");
  debug((unsigned)dstPort);
  debug(" payloadLen=6");
#endif

  // Always send the probe (even when DEBUG != 1)
  // In non-debug builds we ignore return codes to avoid -Wunused-variable.
  int rc1 = udp.beginPacket(dst, dstPort);
  int rc2 = udp.write((const uint8_t*)"TMVITA", 6);
  int rc3 = udp.endPacket();

#if DEBUG == 1
  debug("  beginPacket="); debug(rc1);
  debug(" write=");       debug(rc2);
  debug(" endPacket=");   debugln(rc3);
#else
  (void)rc1; (void)rc2; (void)rc3;  // silence unused warnings if any toolchain inlines away prints
#endif
}

FlexRig::FlexRig()
{

  tcpPort = UDP_DISCOVERING_PORT;
  connected        = false;
  vfo              = false;
  nMaxSlice        = 2;
  activeSlice      = 0;
  nMaxPanadapter   = 2;
  activePanadapter = 0;
  nMaxWaterfall    = 2;
  activeWaterfall  = 0;
  C                = 0;
  front            = 0;
  rear             = -1;
  size             = 0;

  //sprintf(handle,"3144B418")); //dummy initialization for offline test
  commandList = CommandList();
  radio       = Radio();
  transmit    = Transmit();
  interlock   = Interlock();
  eq          = Eq();
  cwx         = Cwx();
  atu         = Atu();
  for (int i = 0; i < nMaxPanadapter; i++)
  {
    panadapter[i] = Panadapter();
    panadapter[i].set_pan(i);
  }
  //panadapter[1]=Panadapter();
  for (int i = 0; i < nMaxWaterfall; i++)
  {
    waterfall[i] = Waterfall();
    waterfall[i].set_waterfall(i);
  }
  //waterfall[1]=Waterfall();
  for (int i = 0; i < nMaxSlice; i++)
  {
    slice[i] = Slice();
    slice[i].set_sliceId(i);
  }
  //slice[1]=Slice();

  for (int i = 0; i < 9; i++) metersId[i] = -1;
  
  // NV0E - Initialize character arrays to empty strings for safe rendering
  modelName[0] = '\0';
  serial[0] = '\0';
  nickName[0] = '\0';
  softVersion[0] = '\0';
  handle[0] = '\0';

  // Derive max counts from fixed-size arrays (always valid, even with no GUI client connected to Flex)
  nMaxSlice      = ARRLEN(slice);
  nMaxPanadapter = ARRLEN(panadapter);
  nMaxWaterfall  = ARRLEN(waterfall);

  // Deterministic, safe defaults so UI/events never see garbage
  for (int i = 0; i < nMaxSlice; ++i) {
    slice[i].in_use     = 0;
    slice[i].tx         = 0;
    slice[i].active     = 0;
    slice[i].audio_gain = 0;   // 0..100
    slice[i].audio_pan  = 50;  // center
    slice[i].audio_mute = 0;   // 0/1
    slice[i].filter_lo  = 0;
    slice[i].filter_hi  = 0;
    slice[i].mode[0]    = '\0';
    slice[i].pan        = -1;  // unknown pan handle until radio reports one
  }

  for (int i = 0; i < nMaxPanadapter; ++i) {
    panadapter[i].band = 0;
  }

  // Initialize memory tables to deterministic state
  MaxMemNum = 0;
  for (int i = 0; i < MEM_CAP; ++i) {
    Mem[i] = -1;
    MemName[i].reserve(24);   // small reserve reduces heap churn in Debug
    MemFreq[i].reserve(16);
  }

}

FLASHMEM void FlexRig::ping()
{
  send(F("ping"));
}

FLASHMEM void FlexRig::process()
{
  if (!client.connected())
  {
    client.stop();
    connected = false;
    radio.fire_disconnected_event();
  }

  sendAllCommands();
  readEthernetData();
  readVita49Data();

  // Try finalize strictly based on model state (no event dependency)
  if (mSplitPending) {

    finalizePendingSplitIfReady();
  }
}


FLASHMEM void FlexRig::disconnect()
{
  client.stop();
}

FLASHMEM void FlexRig::fireEvents()
{
  //run events handlers
  radio.fireEvents();
  eq.fireEvents();
  interlock.fireEvents();
  transmit.fireEvents();
  cwx.fireEvents();
  atu.fireEvents();
  for (int i = 0; i < nMaxPanadapter; i++)
    panadapter[i].fireEvents();
  for (int i = 0; i < nMaxWaterfall; i++)
    waterfall[i].fireEvents();
  for (int i = 0; i < nMaxSlice; i++)
    slice[i].fireEvents();
}

FLASHMEM void FlexRig::send(String cmd)
{
  send(cmd, 0);
}

FLASHMEM void FlexRig::send(const char* cmd)
{
  send(cmd, 0);
}

FLASHMEM int FlexRig::send(String cmd, int parserId)
{
  // Build the wire line safely without fixed-size stack buffers.
  // Format expected by the radio: "C<id>|<cmd>\n"
  String line;
  line.reserve(cmd.length() + 16);   // Reserve enough space for ID, separators, and newline
  line += 'C';
  line += C++;                       // Append current command ID, then increment it
  line += '|';
  line += cmd;
  line += '\n';

  // Register this command so the corresponding reply can be routed to the right parser.
  addToResponseList(C - 1, parserId);

  // Send atomically with interrupts disabled to avoid ISR interleaving during socket I/O.
  noInterrupts();
  client.print(line);
  interrupts();

  // Return the command ID that was just sent.
  return C - 1;
}

FLASHMEM int FlexRig::send(const char* cmd, int parserId)
{
  if (!cmd) return -1;

  size_t cmdLen = strlen(cmd);

  String line;
  line.reserve(cmdLen + 16);
  line += 'C';
  line += C++;
  line += '|';
  line += cmd;
  line += '\n';

  addToResponseList(C - 1, parserId);

  noInterrupts();
  client.print(line);
  interrupts();

  return C - 1;
}

FLASHMEM void FlexRig::setPreampList(char m) {
  // Preamp tables live in flash (read-only). We only copy the one we need.
  static constexpr int PREAMP_6300[] = { 0, 20 };
  static constexpr int PREAMP_6400[] = { -8, 0, 8, 16, 24, 32 };
  static constexpr int PREAMP_6500[] = { -10, 0, 10, 20 };
  static constexpr int PREAMP_6600[] = { -8, 0, 8, 16, 24, 32 };
  static constexpr int PREAMP_6700[] = { -10, 0, 10, 20, 30 };

  // Choose table based on model code 'm'.
  // Typical mapping used in this project:
  // '3' -> 6300, '4' -> 6400, '5' -> 6500, '6' -> 6600, '7' -> 6700
  const int* src = nullptr;
  int n = 0;
  switch (m) {
    case '3': src = PREAMP_6300; n = (int)(sizeof(PREAMP_6300)/sizeof(PREAMP_6300[0])); break;
    case '4': src = PREAMP_6400; n = (int)(sizeof(PREAMP_6400)/sizeof(PREAMP_6400[0])); break;
    case '6': src = PREAMP_6600; n = (int)(sizeof(PREAMP_6600)/sizeof(PREAMP_6600[0])); break;
    case '7': src = PREAMP_6700; n = (int)(sizeof(PREAMP_6700)/sizeof(PREAMP_6700[0])); break;
    case '5':
    default:  src = PREAMP_6500; n = (int)(sizeof(PREAMP_6500)/sizeof(PREAMP_6500[0])); break;
  }

  preampListSize = n;
  // Copy to the instance RAM list used elsewhere in the UI/control path
  for (int i = 0; i < n; ++i) preampList[i] = src[i];
}

FLASHMEM void FlexRig::readEthernetData()
{

  //reading response
  while (client.available())
  {
    //debug(F("numero di byte:"));debugln(client.available());
    char c = client.read();
    if (c == 0)
    {
      //debugln(F("0!!!"));
      continue;
    }
    //debug(c);

    if (c == '\n')
    {

      buffer.trim();

      //debug(F("===Buffer address: ")); debugln((long)&buffer,HEX);

      switch (buffer[0])
      {
        case 'R':
          //debugln(F("==RESPONSE=="));
          parseReplay(buffer);
          break;
        case 'S':
          //debugln(F("==STATUS=="));
          parseStatus(buffer);
          break;
        case 'M':
          //debugln(F("==MESSAGE=="));
          parseMessage(buffer);
          break;
        default:
          debugln(F("\n==UNKNOWN=="));
          debug(F("Buffer length:"));
          debugln(buffer.length());
          unsigned int i = 0;
          for (i = 0; i < buffer.length(); i++)
          {
            debug(buffer[0]);
            debug(F("-"));
            debugf("%u\r\n", (unsigned)buffer[0]);;
          }
          //debug(F(">")); debug(buffer);debugln(F("<"));
      }
      buffer = "";
    }
    else
    {
      buffer += c;
    }
  }
}

FLASHMEM void FlexRig::parseStatus(String msg)
{
  //debug(F("FlexRig::parseStatus(msg): "));
  //debugln(msg);
  //if (msg.indexOf(handle)) {

  int s       = msg.indexOf(' ');
  int h       = msg.indexOf('|') + 1;
  String type = msg.substring(h, s);

  //debugln(type);

  if (type.equals(F("memory")))
  {
    //debug("Memory: "); debugln(MaxMemNum);

    int sepIDX = msg.indexOf(" ", s + 1);
    int mIDX;
    int MNum;
    String MFreq;
    String MName;

    MNum = msg.substring(s, sepIDX).toInt();

    if (msg.indexOf("freq=") >= 0)
    {
      mIDX   = msg.indexOf("freq=");
      sepIDX = msg.indexOf(" ", mIDX);
      if (sepIDX != -1)
      {
        mIDX += 5;
        MFreq = msg.substring(mIDX, sepIDX);
      }
    }

    if (msg.indexOf("name=") >= 0)
    {
      mIDX   = msg.indexOf("name=");
      sepIDX = msg.indexOf(" ", mIDX);
      if (sepIDX != -1)
      {
        mIDX += 5;
        MName = msg.substring(mIDX, sepIDX);
        MName.replace(char(127), " ");
      }
    }

    //debug("Memory: "); debug(MaxMemNum); debug(Mem[MaxMemNum]); debug(" "); debugln(MemFreq[MaxMemNum]);
    //debug("Memory (From Flex): "); debug(MaxMemNum); debug(" "); debug(MNum); debug(" "); debug(MFreq); debug(" "); debugln(MName);

    // Find existing slot first, bounded by capacity
    int found = -1;
    for (mIDX = 0; mIDX < MaxMemNum && mIDX < MEM_CAP; ++mIDX) {
      if (MNum == Mem[mIDX]) { found = mIDX; break; }
    }

    // Choose write index, overwrite existing or append at tail
    int wr = (found >= 0) ? found : MaxMemNum;

    // Final guard, write only inside array
    if (wr >= 0 && wr < MEM_CAP) {
      Mem[wr]     = MNum;
      MemFreq[wr] = MFreq;
      MemName[wr] = MName;

      // Increase count only when appending and still within capacity
      if (found < 0 && MaxMemNum < MEM_CAP) {
        MaxMemNum += 1;
      }
    }

  }

  // -----------------------------------------------------------------------------
  // PROFILE messages
  //   Examples:
  //     "profile global current=SSB - 60m"
  //     "profile global list=CW -  6m^CW - 10m^..."
  // -----------------------------------------------------------------------------
  if (type.equals(F("profile")))
  {
    // Handle "profile global current=..."
    // This is our primary trigger that a global profile was selected.
    int idx_current = msg.indexOf(F("global current="));
    if (idx_current >= 0)
    {
      idx_current += 15; // move past "global current="
      String profile_value = msg.substring(idx_current);
      profile_value.trim();

      Current_Profile = profile_value;
      Global_Prof_Applied = true;

      debugf("Profile: global current = %s\n", profile_value.c_str());
    }

    // Handle "profile global list=..."
    // Populates the Profile[] array with bounds checking and trim().
    int idx_list = msg.indexOf(F("global list="));
    if (idx_list >= 0)
    {
      idx_list += 12; // move past "global list="
      const int PROFILE_MAX = 200;
      int count = 0;

      while (count < PROFILE_MAX && idx_list >= 0 && idx_list < (int)msg.length())
      {
        int sep = msg.indexOf('^', idx_list);
        if (sep == -1)
        {
          // last item (no trailing '^') -> take remainder and stop
          String item = msg.substring(idx_list);
          item.trim();
          if (item.length() > 0)
          {
            Profile[count++] = item;
            // debugf("Profile[%d] = %s\n", count - 1, item.c_str());
          }
          break;
        }
        else
        {
          // delimited item
          String item = msg.substring(idx_list, sep);
          item.trim();
          if (item.length() > 0)
          {
            Profile[count++] = item;
            // debugf("Profile[%d] = %s\n", count - 1, item.c_str());
          }
          idx_list = sep + 1; // advance to next candidate
        }
      }

    }

    // We consumed this line entirely; nothing else to do in this branch.
    // Return early to avoid falling through to other handlers.
    return;
  }


  if (type.equals(F("client")))
  {
    int CurClient;
    int spIDX;
    //debugln("Got Client");
    CurClient = Max_Clients;

    //if(msg.indexOf("client ") >= 0 && Client_Handle[CurClient] == "")
    if (msg.indexOf("client ") >= 0)
    {
      int chidIDX = msg.indexOf("client ");
      spIDX       = msg.indexOf(" ", chidIDX + 7);
      //Client_Handle[CurClient] = msg.substring(chidIDX + 7, spIDX);
      //spIDX=msg.indexOf(" ",spIDX + 1);
      //Client_Status[CurClient] = msg.substring(chidIDX + 18, spIDX);
      //debug("FlexRigTeensy.cpp Client_Handle: "); debugln(Client_Handle[Max_Clients]);
      String tmp = msg.substring(chidIDX + 7, spIDX);

      for (int i = 0; i < 4; i++)
      {
        if (tmp == Client_Handle[i])
        {
          CurClient = i;
          Max_Clients--;
          break;
        }
      }

      Client_Handle[CurClient] = msg.substring(chidIDX + 7, spIDX);
      spIDX                    = msg.indexOf(" ", spIDX + 1);
      Client_Status[CurClient] = msg.substring(chidIDX + 18, spIDX);
    }

    if (msg.indexOf("client_id=") >= 0)
    {
      int cidIDX           = msg.indexOf("client_id=");
      spIDX                = msg.indexOf(" ", cidIDX);
      Client_ID[CurClient] = msg.substring(cidIDX + 10, spIDX);
    }

    if (msg.indexOf("program=") >= 0 && Client_Program[CurClient] == "")
    {
      int cpgmIDX               = msg.indexOf("program=");
      spIDX                     = msg.indexOf(" ", cpgmIDX);
      Client_Program[CurClient] = msg.substring(cpgmIDX + 8, spIDX);
      //debug("FlexRigTeensy.cpp Client_Program: "); debugln(Client_Program[Max_Clients]);
    }

    if (msg.indexOf("station=") >= 0 && Client_Station[CurClient] == "")
    {
      int cstnIDX               = msg.indexOf("station=");
      spIDX                     = msg.indexOf(" ", cstnIDX);
      Client_Station[CurClient] = msg.substring(cstnIDX + 8, spIDX);
      Client_Station[CurClient].replace(char(127), " ");
      //debug("FlexRigTeensy.cpp Client_Station: "); debugln(Client_Station);
    }

    //		    debug("Max Clients: ");debugln(Max_Clients);
    //		    debug("CurClient: ");debugln(CurClient);
    //		    debug("Client_ID: ");debugln(Client_ID[CurClient]);
    //		    debug("Client_Handle: ");debugln(Client_Handle[CurClient]);
    //		    debug("Client_Program: ");debugln(Client_Program[CurClient]);
    //		    debug("Client_Station: ");debugln(Client_Station[CurClient]);
    //		    debug("Client_Status: ");debugln(Client_Status[CurClient]);

    //Client_Found = true;

    // Compact client array safely when a disconnected entry is found
    const int MAX_CLIENTS = ARRLEN(Client_ID);

    // Shift-away *all* disconnected entries (not just one)
    int w = 0; // write index
    for (int r = 0; r < MAX_CLIENTS; ++r) {
      const bool alive = (Client_Handle[r].length() > 0) && (Client_Status[r] != "disconnected");
      if (alive) {
        if (w != r) {
          Client_ID[w]      = Client_ID[r];
          Client_Handle[w]  = Client_Handle[r];
          Client_Program[w] = Client_Program[r];
          Client_Station[w] = Client_Station[r];
          Client_Status[w]  = Client_Status[r];
        }
        ++w;
      }
    }
    // Clear the tail
    for (int i = w; i < MAX_CLIENTS; ++i) {
      Client_ID[i]      = "";
      Client_Handle[i]  = "";
      Client_Program[i] = "";
      Client_Station[i] = "";
      Client_Status[i]  = "";
    }

    // Recompute Max_Clients from current, non-disconnected entries
    Max_Clients = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
      if (Client_Handle[i].length() && Client_Status[i] != "disconnected") {
        ++Max_Clients;
      }
    }
    // NOTE: no manual ++ and no hard clamp here – the array size already enforces the cap.

    return;
  }

  if (type.equals(F("radio")))
  {
    radio.updateStatus(msg.substring(s + 1));
    return;
  }
  if (type.equals(F("transmit")))
  {
    transmit.updateStatus(msg.substring(s + 1));
    return;
  }
  if (type.equals(F("interlock")))
  {
    interlock.updateStatus(msg.substring(s + 1));
    return;
  }

  if (type.equals(F("display")))
  {
    int s1     = msg.indexOf(' ', s + 1);
    String obj = msg.substring(s + 1, s1);

    // Parse the 0x40xxxxxx / 0x42xxxxxx handle as decimal substring already in your code
    int handle = (msg.substring(s1 + 3, s1 + 11)).toInt();   // existing pattern kept

    if (obj.equals(F("pan")))
    {
        // Determine the start of the handle token right after "<obj>"
        int hstart = msg.indexOf(' ', s1 + 1);
        while (hstart >= 0 && hstart < (int)msg.length() && msg[hstart] == ' ') { ++hstart; }
        if (hstart < 0 || hstart >= (int)msg.length()) return;

        // Parse "0x40000000" or decimal safely without libc
        const char* raw = msg.c_str() + hstart;
        const uint32_t handle = tm_parse_hex32(raw);

        // Map Flex handle (0x40000000 + index) to panadapter index
        const uint32_t base = 0x40000000u;
        int idx = (handle >= base) ? (int)(handle - base) : -1;

        if (idx >= 0 && idx < (int)nMaxPanadapter)
        {
            // (1) Store the actual handle in the panadapter object
            panadapter[idx].set_pan((int)handle);

            // (2) Determine payload start (first space after the handle token)
            int payload_start = msg.indexOf(' ', hstart);
            String payload = (payload_start >= 0 && (payload_start + 1) < (int)msg.length())
                            ? msg.substring(payload_start + 1)
                            : String();

            // (3) Update remaining panadapter fields and mark as active
            panadapter[idx].updateStatus(payload);
            activePanadapter = idx;
        }
        return;
    }

    if (obj.equals(F("waterfall")))
    {
      // Flex waterfall handles start at 0x42000000
      const int base = 42000000;
      int idx = handle - base;
      if (idx >= 0 && idx < nMaxWaterfall)
      {
        waterfall[idx].updateStatus(msg.substring(s1 + 12));
      }
      else
      {
        // optional debug:
        // debugf("WATERFALL handle out of range: handle=%d idx=%d\n", handle, idx);
      }
      return;
    }

    return;
  }

  if (type.equals(F("slice")))
  {
    // Parse slice id
    const int s1 = msg.indexOf(' ', s);
    const int id = (msg.substring(s1 + 1, s1 + 2)).toInt();

    if (id < nMaxSlice)
    {
      // Snapshot old state BEFORE update
      // We only care about B (id == 1) here
      int old_in_use_B = 0;
      int old_tx_B     = 0;
      int old_tx_A     = 0;

      if (id == 1) {
        old_in_use_B = slice[1].in_use;
        // If your Slice class uses another field name than 'tx', change next line accordingly.
        old_tx_B     = slice[1].tx;
        // Snapshot A's TX so we can avoid redundant command if A is already TX
        old_tx_A     = (0 < nMaxSlice) ? slice[0].tx : 0;
      }

      // Update the slice model from Flex status
      slice[id].updateStatus(msg.substring(s1 + 1));

      // Safety handover when B closes:
      // Condition: B existed and was TX, now B is not in_use, A still exists, and A is not already TX.
      if (id == 1) {
        const bool b_was_present_and_tx = (old_in_use_B == 1) && (old_tx_B == 1);
        const bool b_is_now_closed      = (slice[1].in_use == 0);
        const bool a_is_present         = (nMaxSlice > 0) && (slice[0].in_use == 1);
        const bool a_not_already_tx     = (nMaxSlice > 0) && (slice[0].tx != 1) && (old_tx_A != 1);

        if (b_was_present_and_tx && b_is_now_closed && a_is_present && a_not_already_tx) {
          // Defer the action: queue command, actual send happens in the normal send loop
          commandList.add(F("slice set 0 tx=1"));
        }
      }
    }

    return;
  }

  if (type.equals(F("spot")))
  {
    //debugln(msg);
    //debug(F("============>spot "));
    int parmIDX;
    int sepIDX = msg.indexOf(" ", s + 1);
    static int sIDX;
    int SNum;
    //String SFreq;
    //String SName;
    //String SComment;

    SNum = msg.substring(s, sepIDX).toInt();
    //debugln(SNum);

    parmIDX = msg.indexOf("triggered");
    if (parmIDX >= 0)
    {
      //spot_triggered.updateStatus(msg.substring(s, sepIDX).toInt());
      //set_spot_triggered(SNum);
      //debugln("triggered");
      return;
    }

    // parse comment looking for TM_PRODUCT_NAME
    parmIDX = msg.indexOf(TM_PRODUCT_NAME);
    if (parmIDX < 0)
    {
      return;  // Only capturing TeensyMaestro spots
    }

    for (int i = 0; i < sIDX; i++)
    {
      if (SNum == TSpotNum[i])
      {
        return;  // already have this spot
      }
    }

    parmIDX = msg.indexOf("rx_freq=");
    if (parmIDX >= 0)
    {
      parmIDX += 8;
      sepIDX          = msg.indexOf(" ", parmIDX);
      TSpotFreq[sIDX] = msg.substring(parmIDX, sepIDX);
      //debugln(msg.substring(parmIDX, sepIDX));
    }

    parmIDX = msg.indexOf("callsign=");
    if (parmIDX >= 0)
    {
      parmIDX += 9;
      sepIDX          = msg.indexOf(" ", parmIDX);
      TSpotCall[sIDX] = msg.substring(parmIDX, sepIDX);
      //debugln(msg.substring(parmIDX, sepIDX));
    }



    if (sIDX > 100)
    {
      return;
    }

    TSpotNum[sIDX] = SNum;

    //debug(F("============>sIDX "));
    //debugln(sIDX);

    //debug(F("============>TSpotNum[sIDX] "));
    //debugln(TSpotNum[sIDX]);

    //debug(F("============>TSpotFreq[sIDX] "));
    //debugln(TSpotFreq[sIDX]);

    //debug(F("============>TSpotCall[sIDX] "));
    //debugln(TSpotCall[sIDX]);


    sIDX++;

    return;
  }

  if (type.equals(F("eq")))
  {
    //debugln(F("============>eq"));
    eq.updateStatus(msg.substring(s + 1));
    return;
  }

  if (type.equals(F("waveform")))
  {
    //debugln(F("waveform"));
    return;
  }

  if (type.equals(F("atu")))
  {
    //debugln(F("atu"));
    atu.updateStatus(msg.substring(s + 1));
    return;
  }

  if (type.equals(F("cwx")))
  {
    //debugln(F("cwx"));
    cwx.updateStatus(msg.substring(s + 1));
    return;
  }

  if (type.equals(F("meter")))
  {
    parseMeter(msg.substring(s + 1));
    //for (int i=0;i<6;i++) {
    //	debug(F("==> Meter id:"));debug(i);debug(F("-"));debugln(metersId[i]);
    //}
    return;
  }
  /////////////////////////////////////////////////////////////debug(F("FlexRig::parseStatus() - Unhandled type->")); debugln(type + "<"));
  //}
}

FLASHMEM void FlexRig::readVita49Data()
{

  int vitaPktSize = vitaUdp.parsePacket();
  static unsigned long last_probe_ms = 0;
  static bool vita_wait_started = false;
  static unsigned long vita_wait_start_ms = 0;
  static bool vita_warned_once = false;
  
  //debugln(vitaPktSize);

  if (vitaPktSize)
  {

    if (!vita_first_logged) {
      vita_first_logged = true;
      debug(F("VITA49: first packet len="));
      debugln(vitaPktSize);
    }    
    g_vita_seen = true;

    vitaUdp.read(vitaPacketBuffer, vitaPktSize);

    vita_wait_started = false;
    vita_warned_once  = false;

    //debugln(vitaPacketBuffer);

    // DEBUG
    /*
	    debug(F("VITA49 PKT - len:")); debugln(vitaPktSize);
	    char vb[3];
	    int x=0;
	    for (int i=0; i < vitaPktSize; i++) {
	      sprintf(vb,"0x%02x, ",vitaPacketBuffer[i]);
		  if (x==16) {x=0; debugln();}
          debug(vb);
		  x++;
        }
        debugln(); */
    // DEBUG


    //char pattern[24];
    char pattern[25];
    sprintf(pattern, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
            vitaPacketBuffer[4], vitaPacketBuffer[5], vitaPacketBuffer[6], vitaPacketBuffer[7],
            vitaPacketBuffer[8], vitaPacketBuffer[9], vitaPacketBuffer[10], vitaPacketBuffer[11],
            vitaPacketBuffer[12], vitaPacketBuffer[13], vitaPacketBuffer[14], vitaPacketBuffer[15]);

    if (!String(pattern).equals(F("0000070000001C2D534C8002")))
    {
      //0000070000001C2D534C8002
      //debugln(pattern);
      //debugln(F("NON VITA METERING PACKET !!!!"));
      return;
    }

    int packet_size = getUIntValue2Bytes(vitaPacketBuffer[2], vitaPacketBuffer[3]);
    //debug(F("VITA==>packet_size: "));debugln(packet_size);

    int payload_bytes = (packet_size - 1) * 4;
    //debug(F("VITA==>payload_bytes: "));debugln(payload_bytes);

    int num_meters = (payload_bytes - 24) / 4;
    //debug(F("VITA==>num_meters: "));debugln(num_meters);

    int offset = 28;
    //for (int i = 0; i < num_meters; i++)
    for (int i = 0; i < num_meters; i++)
    {
      offset = 28 + i * 4;

      int meterId      = getUIntValue2Bytes(vitaPacketBuffer[offset], vitaPacketBuffer[offset + 1]);
      short meterValue = getMeterValue(vitaPacketBuffer[offset + 2], vitaPacketBuffer[offset + 3]);

      /*
			if (meterId==12 || meterId==7 || meterId==8) {
				debug(F("Meter id:"));debug(meterId);
				debug(F("-Value:"));debugln(meterValue);
			} */

      /*
			if (meterId==15) {
			    debug(F("Meter id:"));debug(meterId);
				debug(F("-Value:"));debugln(meterValue);
			}
			*/

      /*
			if (meterId==19 || meterId==20 || meterId==21 || meterId==22 ||
			    meterId==23 || meterId==24 || meterId==25 || meterId==26) {
				debug(F("Meter id:"));debug(meterId);
				debug(F("-Value:"));debugln(meterValue);
			}
			*/
      //debug(F("meterId:"));debugln(meterId);

      if (meterId == metersId[MET_S_A])
      {
        //debug(F(" Meter MET_S_A ")); debugln(millis());
        metersValue[MET_S_A] = (float)meterValue / 128.0f;
        continue;
      }
      if (meterId == metersId[MET_S_B])
      {
        //debugln(F(" Meter MET_S_B"));
        metersValue[MET_S_B] = (float)meterValue / 128.0f;
        continue;
      }
      if (meterId == metersId[MET_SWR])
      {
        //debug(F("meterValue:"));debugln(meterValue);
        metersValue[MET_SWR] = (float)meterValue / 128.0f;
        //debug(F("metersValue[MET_SWR]:"));debugln(metersValue[MET_SWR]);

        continue;
      }
      if (meterId == metersId[MET_FPWR])
      {
        //debugln(F("Meter MET_FPWR"));
        metersValue[MET_FPWR] = pow(10, (((float)meterValue / 128.0f) / 10)) / 1000;
        continue;
      }
      /*
			if (meterId==metersId[MET_MIC_PEAK]) {
				//debugln(F("Meter MET_MIC_PEAK"));
				metersValue[MET_MIC_PEAK]=(float)meterValue/128.0f;
				continue;
			}
			if (meterId==metersId[MET_COMP_PEAK]) {
				//debugln(F("Meter MET_COMP_PEAK"));
				metersValue[MET_COMP_PEAK]=(float)meterValue/128.0f;
				continue;
			}*/
      if (meterId == metersId[PATEMP])
      {
        //debugln(F("Meter MET_COMP_PEAK"));
        metersValue[PATEMP] = ((float)meterValue / 64.0f) / 100;
        continue;
      }
      if (meterId == metersId[VOLTAGE_BEFORE])
      {
        //debugln(F("Meter MET_COMP_PEAK"));
        metersValue[VOLTAGE_BEFORE] = ((float)meterValue) / 1000;
        continue;
      }

      if (meterId == metersId[VOLTAGE_AFTER])
      {
        //debugln(F("Meter MET_COMP_PEAK"));
        metersValue[VOLTAGE_AFTER] = ((float)meterValue) / 1000;
        continue;
      }
    }
  }
  else if (!g_vita_seen) {
    if (!vita_wait_started) {
      vita_wait_start_ms = millis();
      vita_wait_started = true;
    }

    if (!vita_warned_once && (millis() - vita_wait_start_ms > 30000UL)) {
      debugln(F("WARN: No VITA49 received yet (30s). Check panadapter active, NAT punch, and ports."));
      vita_warned_once = true;
    }

    unsigned long now = millis();
    if (now - last_probe_ms >= VITA_PROBE_INTERVAL_MS) {
      last_probe_ms = now;
      sendVitaProbe(vitaUdp, ipAddress, RADIO_VITA_SRC_PORT);
    }
  }
}

FLASHMEM void FlexRig::parseMessage(String msg)
{
  //debugln(msg);
}


FLASHMEM void FlexRig::connect()
{

  debug(F("Connecting to Flex rig: "));
  debug(ipAddress[0]);
  debug(F("."));
  debug(ipAddress[1]);
  debug(F("."));
  debug(ipAddress[2]);
  debug(F("."));
  debug(ipAddress[3]);
  debugln(F("."));

  if (client.connect(ipAddress, tcpPort))
  {
    connected = true;
    debugln("Connected");

    // Drain early server banners without a hard 1s sleep.
    uint32_t t0   = millis();
    uint32_t idle = 0;
    while ((millis() - t0) < 300 && idle < 50) {
      int n = client.available();
      if (n <= 0) { idle++; delay(1); continue; }
      idle = 0;
      while (n-- > 0) {
        char c = client.read();
        if (c == '\n') {
          switch (buffer[0]) {
            case 'H': /* handle */ for (unsigned int i = 1; i < buffer.length(); i++) { handle[i - 1] = buffer[i]; } debugln(); break;
            case 'S': parseStatus(buffer); break;
            case 'M': parseMessage(buffer); break;
          }
          buffer = "";
        } else {
          buffer += c;
        }
      }
    }

    //Listening on port 4991 for VITA-49 protocol
    g_vita_seen = false;
    vitaUdp.begin(UDP_VITA49_PORT);
    debugln(F("==>LISTENING FOR VITA49 PACKETS"));
    configureClient();

    //
    //startReceiver();
    //
  }
  else
  {
    debugln(F("failed"));
  }
}

FLASHMEM void FlexRig::connect(const byte ip[4], uint16_t port)
{
  // copy IP and set desired port, then reuse the existing connect()
  ipAddress[0] = ip[0];
  ipAddress[1] = ip[1];
  ipAddress[2] = ip[2];
  ipAddress[3] = ip[3];
  tcpPort = port;
  connect();
}

FLASHMEM void FlexRig::connect(IPAddress ip, uint16_t port)
{
  ipAddress[0] = ip[0];
  ipAddress[1] = ip[1];
  ipAddress[2] = ip[2];
  ipAddress[3] = ip[3];
  tcpPort = port;
  connect();
}

FLASHMEM bool FlexRig::ensureIdentity(uint16_t wait_ms)
{
  // If we’re not connected, we cannot query the radio.
  if (!connected) return false;

  // If identity is already populated (e.g., via UDP discovery), we’re done.
  if (modelName[0] != '\0' && serial[0] != '\0' && softVersion[0] != '\0') {
    return true;
  }

  debugln(F("Send the info request over the existing TCP control session"));
  // Send the "info" request over the existing TCP control session.
  // The library’s send() should format this correctly (e.g., "cN|info").
  send("info");

  // Process incoming data for up to wait_ms, giving the radio time to reply.
  const unsigned long t0 = millis();
  while (millis() - t0 < wait_ms) {
    // Run the normal processing path so incoming "R|..." replies are parsed.
    process();

    // As soon as all identity fields are filled, we can return success.
    if (modelName[0] != '\0' && serial[0] != '\0' && softVersion[0] != '\0') {
      debugln(F("Identity fields are filled"));
      return true;
    }
    delay(20);
  }

  debugln(F("Timed out waiting for identity fields"));
  // Partial identity is still useful; report success only if all 3 are present.
  return (modelName[0] != '\0' && serial[0] != '\0' && softVersion[0] != '\0');
}

/*
  findAFlex() validates UDP packets in this order:

  * Packet size (minimum 16 bytes for VITA-49 header)
  * VITA packet type (must be ExtDataWithStream = 0x3)
  * Class ID presence (must be present for FlexRadio packets)
  * Packet size consistency (VITA size vs actual UDP packet size)
  * Stream ID (must be 0x00000800 for Discovery)
  * FlexRadio OUI (must be 0x00001C2D)
  * Class ID Info (must be 0x534CFFFF)
*/

FLASHMEM FlexRig FlexRig::findAFlex(String serialKey)
{
  EthernetUDP Udp;
  Udp.begin(UDP_DISCOVERING_PORT);
  FlexRig f = FlexRig();
  char headerBuffer[16];  // NV0E - Allocate the header buffer once

  debugln(F("===Looking for Flex Rig (UDP)==="));

  unsigned long TimeIt = millis();
  boolean found        = false;
  while (!found)  // will wait forever if a serial number is specified and not found on the network
  {
    String discValue;
    int packetSize = Udp.parsePacket();

    debug(F("Received packet size: "));
    debugln(packetSize);

    if (packetSize > 0)
    {
      // NV0E - Stage 1: Read VITA-49 header first (minimum 16 bytes)
      if (packetSize < 16)  // Minimum header size check
      {
        debugln(F("Packet too small for VITA-49 header"));
        continue;
      }

      Udp.read(headerBuffer, 16);

      // Extract header fields (first 4 bytes)
      uint32_t header = ((uint32_t)headerBuffer[0] << 24) | 
                       ((uint32_t)headerBuffer[1] << 16) | 
                       ((uint32_t)headerBuffer[2] << 8) | 
                       (uint32_t)headerBuffer[3];
      
      uint8_t pkt_type = (header >> 28) & 0xF;
      uint8_t class_id_present = (header >> 27) & 0x1;
      uint8_t tsi = (header >> 22) & 0x3;
      uint8_t tsf = (header >> 20) & 0x3;
      uint16_t vita_packet_size = header & 0xFFFF;

      // NV0E - Check if this is ExtDataWithStream packet type (0x3)
      if (pkt_type != VITA_EXT_DATA_WITH_STREAM)
      {
        debug(F("Not ExtDataWithStream packet type, got: 0x"));
        debugf("%02X\r\n", (unsigned)pkt_type);
        continue;
      }

      // NV0E - Skip if no class ID present
      if (!class_id_present)
      {
        debugln(F("No class ID present in packet"));
        continue;
      }

      // NV0E - Validate packet size makes sense
      int expectedPacketSize = vita_packet_size * 4;  // VITA-49 packet size is in 32-bit words
      if (expectedPacketSize != packetSize || expectedPacketSize > 4096)  // Sanity check
      {
        debug(F("Packet size mismatch or too large. Expected: "));
        debug(expectedPacketSize);
        debug(F(", Actual: "));
        debugln(packetSize);
        continue;
      }

      // NV0E - Read Stream ID (bytes 4-7 of header)
      uint32_t stream_id = ((uint32_t)headerBuffer[4] << 24) | 
                          ((uint32_t)headerBuffer[5] << 16) | 
                          ((uint32_t)headerBuffer[6] << 8) | 
                          (uint32_t)headerBuffer[7];

      // NV0E - Check if this is a Discovery stream (0x00000800)
      if (stream_id != 0x00000800)
      {
        debug(F("Not a discovery stream, stream_id: 0x"));
        debugf("%08lX\r\n", (unsigned long)stream_id);
        continue;
      }

      // NV0E - Read Class ID OUI (bytes 8-11 of header)
      uint32_t class_oui = ((uint32_t)headerBuffer[8] << 24) | 
                          ((uint32_t)headerBuffer[9] << 16) | 
                          ((uint32_t)headerBuffer[10] << 8) | 
                          (uint32_t)headerBuffer[11];

      // NV0E - Check if this is a FlexRadio packet (OUI = 0x00001C2D)
      if (class_oui != 0x00001C2D)
      {
        debug(F("Not a FlexRadio packet, OUI: 0x"));
        debugf("%06lX\r\n", (unsigned long)class_oui);
        continue;
      }

      // NV0E - Read Class ID Info + Packet Class (bytes 12-15 of header)
      uint32_t class_info_and_packet = ((uint32_t)headerBuffer[12] << 24) | 
                                      ((uint32_t)headerBuffer[13] << 16) | 
                                      ((uint32_t)headerBuffer[14] << 8) | 
                                      (uint32_t)headerBuffer[15];

      // NV0E - Check if this is the correct Class ID Info + Packet Class (0x534CFFFF)
      if (class_info_and_packet != 0x534CFFFF)
      {
        debug(F("Incorrect Class ID Info, expected 0x534CFFFF, got: 0x"));
        debugf("%08lX\r\n", (unsigned long)class_info_and_packet);
        continue;
      }

      debug(F("Found FlexRadio discovery packet from "));
      debugln(Udp.remoteIP());

      // NV0E - Stage 2: Now we know it's a valid FlexRadio packet, allocate exact buffer size and read full packet
      char* packetBuffer = new char[packetSize];
      if (packetBuffer == NULL)
      {
        debugln(F("Failed to allocate memory for packet"));
        continue;
      }

      // NV0E - Copy header data to full buffer
      memcpy(packetBuffer, headerBuffer, 16);
      
      // NV0E - Read remaining packet data
      int remainingBytes = packetSize - 16;
      if (remainingBytes > 0)
      {
        Udp.read(packetBuffer + 16, remainingBytes);
      }

      debug("packetSize: ");
      debugln(packetSize);
      debugln(" ");

      // NV0E - Calculate payload offset after timestamps
      int offset = 16;  // Start after the 16-byte header we already processed
      
      // NV0E - Skip timestamps if present
      if (tsi != 0)
      {
        offset += 4;  // NV0E - Integer timestamp
      }
      if (tsf != 0)
      {
        offset += 8;  // NV0E - Fractional timestamp
      }

      // NV0E - Extract payload
      if (packetSize <= offset)
      {
        debugln(F("No payload data in packet"));
        delete[] packetBuffer;
        continue;
      }

      String strBuf;
      // NV0E - Remove null padding and build payload string
      for (int i = offset; i < packetSize; i++)
      {
        if (packetBuffer[i] != 0)  // Skip null padding
        {
          strBuf += (char)packetBuffer[i];
        }
      }

      // NV0E - Log the discovery payload found
      debug(F("UDP Discovery | "));
      debugln(strBuf);

      // serial number
      discValue = f.ParseDiscovery("serial=", strBuf);
      //debugln(serialKey);
      //debugln(discValue);

      if (serialKey == "" || serialKey == discValue)
      {
        found = true;
        discValue.toCharArray(f.serial, 20);
      }

      if (found)
      {
        debugln(F("===========Flex Rig============="));
        debug("Serial Number: ");
        debug(f.serial);
        debugln("<");

        // nickname
        discValue = f.ParseDiscovery("nickname=", strBuf);

        if (discValue != "")
        {
          discValue.toCharArray(f.nickName, 36);
        }

        debug(F("NickName: "));
        debug(f.nickName);
        debugln("<");

        // model
        discValue = f.ParseDiscovery("model=", strBuf);

        if (discValue != "")
        {
          discValue.toCharArray(f.modelName, 36);
        }

        debug(F("Model: "));
        debug(f.modelName);
        debugln("<");

        // IP address
        debug(F("IP Address: "));
        IPAddress remote = Udp.remoteIP();
        for (int i = 0; i < 4; i++)
        {
          debugf("%u", (unsigned)remote[i]);
          f.ipAddress[i] = remote[i];  //Storing data into Flex Struct
          if (i < 3) debug(F("."));
        }
        debugln("<");

        // version
        discValue = f.ParseDiscovery(" version=", strBuf);  // Need leading blank to tell it from other version numbers

        if (discValue != "")
        {
          discValue.toCharArray(f.softVersion, 32);
        }

        debug(F("Version: "));
        debug(f.softVersion);
        debugln("<");
        debugln();
      }

      //debugln(f.modelName[6]);
      f.setPreampList(f.modelName[6]);  // Use the second digit of the model number to populate the preamp values.
      
      // NV0E - Clean up allocated memory
      delete[] packetBuffer;
    }                                     // packetsize > 0

    if (millis() - TimeIt > 10000)
    {
      for (unsigned int i = 0; i < sizeof(f.serial); i++) 
      {
        f.serial[i] = 0x00;
      }
 
      return f;
    }

    delay(100);
  }  //main loop - it is executed until a Flex rig is found

  return f;
}

FLASHMEM String FlexRig::ParseDiscovery(String discKey, String &strBuf)
{
  int discKeyIDX = strBuf.indexOf(discKey) + discKey.length();
  return strBuf.substring(discKeyIDX, strBuf.indexOf(" ", discKeyIDX));
}

FLASHMEM int FlexRig::charIndexOf(const char *string, const char *key)
{
  int val = -1;
  char *test;
  test = strstr(string, key);
  if (test)
  {
    val = test - string;
    debug("test:");
    debugln(test);
  }
  return val;
}

// char *FlexRig::valueOf(char *string, char *key)
// {
//   char *ret = NULL;

//   char *pch;
//   pch = strtok(string, " ");
//   while (pch != NULL)
//   {
//     //debug(">"); debug(pch); debugln("<");
//     //int pos=charIndexOf(pch,"=");
//     int i = charIndexOf(pch, key);
//     //debug(pos);debug(" # ");debugln(i);
//     if (i == 0)
//     {
//       debug("  found key:");
//       debugln(key);
//       char *tmp = strstr(pch, "=");
//       debugln(tmp);
//       ret = tmp + 1;
//       break;
//     }
//     /*if (charIndexOf(pch,key)) {
//       char *tmp=strstr(pch,"=");
//       ret=tmp+1;
//       debug("  ret: ");debugln(ret);
//     }*/
//     pch = strtok(NULL, " ");
//   }
//   return (char *)ret;
// }

FLASHMEM  void FlexRig::configureClient()
{
  // --- Short, non-blocking settle instead of delay(500) ---
  {
    // Pump I/O for ~500 ms so we read any early server lines immediately.
    uint32_t t0 = millis();
    while (millis() - t0 < 500) {
      process();      // handle incoming TCP + VITA (if any) and send queued cmds
      delay(1);       // yield a tick to keep system responsive
    }
  }

  commandList.add(F("client ip"));
  send(F("client program TeensyMaestro")); // direct send is fine; could also be add()
  commandList.add(F("sub client all"));
  commandList.add(F("sub radio all"));
  commandList.add(F("sub tx all"));
  commandList.add(F("sub atu all"));
  commandList.add(F("sub meter all"));
  commandList.add(F("sub pan all"));
  commandList.add(F("sub slice all"));
  commandList.add(F("sub spot all"));
  commandList.add(F("info"));
  send(F("profile global info"));
  send(F("sub memories all"));
  commandList.add(F("eq RXsc info"));
  commandList.add(F("eq TXsc info"));

  vita_accept_ts    = millis();
  vita_first_logged = false;

  // Tell the radio which UDP port to send VITA-49 to (WAN/NAT-visible port).
  if (!g_vita_seen) {
    const bool stun_valid = (STUN_ExternalVitaPort > 0) && (STUN_ExternalVitaPort <= 65535);
    const uint16_t port_for_radio = stun_valid
                                    ? static_cast<uint16_t>(STUN_ExternalVitaPort)
                                    : static_cast<uint16_t>(UDP_VITA49_PORT);

    char cmd[40];
    snprintf(cmd, sizeof(cmd), "client udpport %u", port_for_radio);
    lastUdpportCmdId = send(String(cmd), -1);

    debug(F("UDPPORT CMD ID: ")); debugln(lastUdpportCmdId);
    debug(F("VITA49: announced udpport "));
    debug(port_for_radio);
    debugln(stun_valid ? F(" (from STUN)") : F(" (default/config)"));

    // Punch NAT / confirm path (always send; logs may be conditional).
    sendVitaProbe(vitaUdp, ipAddress, RADIO_VITA_SRC_PORT);
  } else {
    debugln(F("VITA already flowing; skip client udpport + probe."));
  }

  commandList.add(F("keepalive disable"));

  // --- Short, non-blocking settle instead of delay(500) ---
  {
    // Give the radio ~500 ms to acknowledge early subs/udpport/etc. while we keep processing.
    uint32_t t0 = millis();
    while (millis() - t0 < 500) {
      process();      // continue draining incoming responses/events
      delay(1);       // brief yield
    }
  }
}

FLASHMEM void FlexRig::startReceiver()
{
  commandList.add(F("display pan c 7.020000 1 800 400"));
  commandList.add(F("slice C 7.020000 ant1 cw"));
}

void FlexRig::stopReceiver()
{
  send(F("slice r 0"));
  send(F("slice r 1"));
  send(F("display pan r 0x40000000"));
  send(F("display pan r 0x40000001"));
}

void FlexRig::addToResponseList(int id, int parserId)
{

  if ((front == 0 && rear == RESPONSE_PACKET_LIST_SIZE - 1) || (front > 0 && rear == front - 1))
  {
    debugln(F("\nResponse List overflow"));
    return;
  }
  else
  {
    if (rear == RESPONSE_PACKET_LIST_SIZE - 1 && front > 0)
    {
      rear = 0;
    }
    else
    {
      if ((front == 0 && rear == -1) || (rear != front - 1))
        rear++;
    }
  }
  packetElement[rear].id       = id;
  packetElement[rear].parserId = parserId;
  size++;
}

rPacket FlexRig::removeFromResponseList()
{
  rPacket element;
  if ((front == 0) && (rear == -1))
  {
    debugln(F("Response List underflow\n"));
    return element;
  }
  if (front == rear)
  {
    element = packetElement[front];
    rear    = -1;
    front   = 0;
  }
  else
  {
    if (front == RESPONSE_PACKET_LIST_SIZE - 1)
    {
      element = packetElement[front];
      front   = 0;
    }
    else
    {
      element = packetElement[front];
      front++;
    }
  }
  size--;
  return element;
}

FLASHMEM void FlexRig::parseReplay(String msg)
{

  //debugln(msg);
  int p1 = msg.indexOf(F("|"));
  int id = msg.substring(1, p1).toInt();              // existing logic (slightly refactored to keep p1)
  int p2 = (p1 >= 0) ? msg.indexOf(F("|"), p1 + 1) : -1;
  int code = (p2 > p1) ? msg.substring(p1 + 1, p2).toInt() : -1;  // new: parse return code

  // new: log outcome for "client udpport ..." command
  if (id == lastUdpportCmdId) {
    if (code == 0) {
      debugln(F("client udpport: accepted by radio (R|0|)"));
    } else if (code == 500000) {
      debugln(F("client udpport already set (500000) – continuing..."));
    } else {
      debug(F("client udpport: rejected by radio, code="));
      debugln(code);
    }
  }

  //debugln(id);
  //debug("size:"); debugln(size);

  while (size > 0)
  {
    rPacket tmp = removeFromResponseList();
    //debug(tmp.id); debug(" - "); debug(tmp.parserId);
    if (tmp.id == id)
    {
      switch (tmp.parserId)
      {
        case 0:
          //parseDummy(msg.substring(msg.lastIndexOf(F("|"))+1));
          parseInfo(msg.substring(msg.lastIndexOf(F("|")) + 1));
          break;
        case 1:
          //parseAntennaList(msg.substring(msg.lastIndexOf(F("|"))+1));
          break;
        case 2:
          //parseMicList();
          break;
        case 3:
          //debug("First Cwx CharId:");
          //debugln(msg.substring(msg.lastIndexOf(F("|"))+1));
          {
            String t = msg.substring(msg.lastIndexOf(F("|")) + 1);
            //debug("ParseReplay - commandId"); debug(tmp.id);
            //debug(" - FirstCharId:" );
            //debugln(t.toInt());
            cwx.setFromToValues(tmp.id, t.toInt());  //commandId, firstCharId
          }
          break;
      }
      break;
    }
  }
}

FLASHMEM void FlexRig::parseInfo(String msg)
{
  // --- Early exit: if identity is already populated, do nothing
  if (modelName[0] != '\0' && serial[0] != '\0' && softVersion[0] != '\0' && nickName[0] != '\0') {
    return;
  }

  // Trim off "R<id>|0|" so we only have key=value,... left
  int p = msg.indexOf('|');
  if (p >= 0) msg.remove(0, p + 1);
  p = msg.indexOf('|');
  if (p >= 0) msg.remove(0, p + 1);

  // --- small helpers (no lambdas, no sscanf) -----------------
  auto trimQuotes = [](String s) -> String {
    s.trim();
    if (s.length() >= 2 && s.startsWith("\"") && s.endsWith("\"")) {
      s.remove(0, 1);
      s.remove(s.length() - 1);
    }
    return s;
  };

  // Extract value for key=... (comma-separated list). Returns "" if not found.
  auto getVal = [&](const char *key) -> String {
    String k = String(key) + "=";
    int i = msg.indexOf(k);
    if (i < 0) return "";
    i += k.length();
    int j = msg.indexOf(',', i);
    String v = (j < 0) ? msg.substring(i) : msg.substring(i, j);
    return trimQuotes(v);
  };

  // Parse dotted IPv4 without sscanf
  auto parseIPv4 = [](const String &s, byte out[4]) -> bool {
    int a = -1, b = -1, c = -1, d = -1;
    int p1 = s.indexOf('.');
    if (p1 <= 0) return false;
    int p2 = s.indexOf('.', p1 + 1);
    if (p2 <= p1 + 1) return false;
    int p3 = s.indexOf('.', p2 + 1);
    if (p3 <= p2 + 1) return false;

    a = s.substring(0,     p1    ).toInt();
    b = s.substring(p1 + 1, p2   ).toInt();
    c = s.substring(p2 + 1, p3   ).toInt();
    d = s.substring(p3 + 1       ).toInt();

    if ((unsigned)a <= 255 && (unsigned)b <= 255 &&
        (unsigned)c <= 255 && (unsigned)d <= 255) {
      out[0] = (byte)a; out[1] = (byte)b; out[2] = (byte)c; out[3] = (byte)d;
      return true;
    }
    return false;
  };
  // ------------------------------------------------------------

  // Extract strings
  String m  = getVal("model");
  String sn = getVal("chassis_serial");
  String nm = getVal("name");
  String sv = getVal("software_ver");
  String ip = getVal("ip");

  // Populate fixed-size char buffers (guard + null-terminate)
  if (m.length())  { m.toCharArray(modelName,   sizeof(modelName));   modelName[sizeof(modelName)-1]    = '\0'; }
  if (sn.length()) { sn.toCharArray(serial,     sizeof(serial));      serial[sizeof(serial)-1]           = '\0'; }
  if (nm.length()) { nm.toCharArray(nickName,   sizeof(nickName));    nickName[sizeof(nickName)-1]       = '\0'; }
  if (sv.length()) { sv.toCharArray(softVersion,sizeof(softVersion)); softVersion[sizeof(softVersion)-1] = '\0'; }

  // Update ipAddress[] if present, but only if we don't already have a target
  if (ip.length()) {
    byte tmp[4];
    if (parseIPv4(ip, tmp)) {
      // treat current ipAddress as "unset" only if 0.0.0.0
      bool ip_is_zero = (ipAddress[0] | ipAddress[1] | ipAddress[2] | ipAddress[3]) == 0;
      if (ip_is_zero) {
        ipAddress[0] = tmp[0];
        ipAddress[1] = tmp[1];
        ipAddress[2] = tmp[2];
        ipAddress[3] = tmp[3];
      }
      // else: keep the existing target IP (don’t let INFO override it)
    }
  }

  // --- Only print once when all fields are filled --------------
  if (modelName[0] && serial[0] && softVersion[0] && nickName[0]) {
    debugf(
      "INFO parsed: model=%s serial=%s ver=%s name=%s ip=%u.%u.%u.%u\n",
      modelName, serial, softVersion, nickName,
      ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]
    );
  }
}

FLASHMEM void FlexRig::parseDummy(String msg)
{
  //debugln(F("=== Dummy ==="));
  //debugln(msg+"<"));
  //debug(F("===front:")); debug(front);debug(F(" - rear:")); debug(rear);debug(F(" - size:"));   debug(size);
  //debugln();
}

FLASHMEM void FlexRig::parseAntennaList(String msg)
{
  //debugln(F("=== Antenna List ==="));
  debugln(msg);
}

FLASHMEM void FlexRig::parseMicList(String msg)
{
  //debugln(F("=== Mic List ==="));
  debugln(msg);
}


FLASHMEM void FlexRig::updateObject(int objectId, int value, int activeSlice, int activePanadapter, int activeWaterfall)
{

  int i = (int)objectId / 1000;
  /* do not use this code - it locks the console!!!!
	debug(F("updateStatus id="));debug(objectId);
	debug(F(" - value="));debug(value);
	debug(F(" - activeSlice="));debugln(activeId);
	*/
  switch (i)
  {
    case 1:  //Radio
      this->radio.updateObject(objectId, value);
      break;
    case 2:  //Interlock
      this->interlock.updateObject(objectId, value);
      break;
    case 3:  //Transmit
      this->transmit.updateObject(objectId, value);
      break;
    case 4:  //Waterfall (0/1)
      this->waterfall[activeWaterfall].updateObject(objectId, value);
      break;
    case 5:  //Panadapter (0/1)
      this->panadapter[activePanadapter].updateObject(objectId, value);
      break;
    case 6:  //Slice (0/1)
      this->slice[activeSlice].updateObject(objectId, value);
      break;
    case 7:  //Eq
      this->eq.updateObject(objectId, value);
      break;
    default:
      debug(F("FlexRig::updateObject - Unhandled Status:"));
      debugln(i);
  }
}

FLASHMEM void FlexRig::sendAllCommands()
{
  while (commandList.getCount() > 0)
  {
    send(commandList.remove());
  }
}

//DA cancellare
FLASHMEM void FlexRig::addToCommandList(String entry)
{
  commandList.add(entry);
}

/*
 * Return (and remove) the first command in the list
 */
/*
String FlexRig::removeFromCommandList()
{
	return commandList.remove();

 String element;
  if((cFront==0)&&(cRear==-1)) {
      debugln(F("\nCommand List underflow"));
      return element;
  }
  if(cFront==cRear){
      element=CommandEntry[cFront];
      cRear=-1;
      cFront=0;
  } else {
      if(cFront==COMMAND_LIST_SIZE-1) {
           element=CommandEntry[cFront];
           cFront=0;
      } else {
        element=CommandEntry[cFront];
        cFront++;
      }
  }
  cSize--;
  //debug(F("->")); debugln(element);
  return element;
}
*/

FLASHMEM unsigned int FlexRig::getUIntValue2Bytes(char nb1, char nb2)
{

  return nb2 | nb1 << 8;
}

FLASHMEM short FlexRig::getMeterValue(char nb1, char nb2)
{

  short var = nb2 | nb1 << 8;
  return var;
}

FLASHMEM void FlexRig::setMeterField(Meter *m, String fld)
{

  // debugln(fld);

  int s      = fld.indexOf('.', 0);
  int e      = fld.indexOf('=', 0);
  String var = fld.substring(s + 1, e);

  if (var.equals(F("src")))
  {
    m->id = fld.substring(0, s).toInt();
  }


  if (var.equals(F("nam"))) m->name = fld.substring(e + 1, fld.length());
  if (var.equals(F("low"))) m->low = fld.substring(e + 1, fld.length()).toFloat();
  if (var.equals(F("hi"))) m->hi = fld.substring(e + 1, fld.length()).toFloat();
  if (var.equals(F("num"))) m->num = fld.substring(e + 1, fld.length()).toInt();
  if (var.equals(F("unit")))
  {
    String val = fld.substring(e + 1, fld.length());
    if (val.equals(F("dBFS"))) m->unit = dBFS;
    if (val.equals(F("Volts"))) m->unit = Volts;
    if (val.equals(F("dBm"))) m->unit = dBm;
    if (val.equals(F("SWR"))) m->unit = SWR;
    if (val.equals(F("degC"))) m->unit = degC;
  }
}
FLASHMEM void FlexRig::parseMeter(String msg)
{
  Meter met;

  // Split on '#', feed tokens to setMeterField()
  int oldi = 0;
  int i    = msg.indexOf('#', oldi);
  while (i > 0) {
    setMeterField(&met, msg.substring(oldi, i));
    oldi = i + 1;
    // search next '#' starting exactly at 'oldi'
    i = msg.indexOf('#', oldi);
  }

  // Normalize LEVEL → LEVEL-0/LEVEL-1 (keeps your original mapping)
  if (met.name.equals(F("LEVEL"))) {
    if (met.num == 0) met.name = F("LEVEL-0");
    else if (met.num == 1) met.name = F("LEVEL-1");
  }

  // Map meter name → metersId[] index (fast path first-char switch)
  int index = -1;
  if (!met.name.length()) {
    index = -1;
  } else {
    switch (met.name.charAt(0)) {
      case 'M': // MICPEAK
        if (met.name.equals(F("MICPEAK"))) index = MET_MIC_PEAK;
        break;
      case 'C': // COMPPEAK
        if (met.name.equals(F("COMPPEAK"))) index = MET_COMP_PEAK;
        break;
      case 'S': // SWR, LEVEL-0/1 handled below
        if (met.name.equals(F("SWR"))) index = MET_SWR;
        else if (met.name.equals(F("S"))) {
          // If you ever get raw 'S' here, you could choose A/B by met.num or other hints
          // but your current code bases S-meter mapping on LEVEL-0/1, so leave unmapped.
        }
        break;
      case 'F': // FWDPWR
        if (met.name.equals(F("FWDPWR"))) index = MET_FPWR;
        break;
      case 'L': // LEVEL-0 / LEVEL-1 (S-meter A/B)
        if (met.name.equals(F("LEVEL-0"))) index = MET_S_A;
        else if (met.name.equals(F("LEVEL-1"))) index = MET_S_B;
        break;
      case 'P': // PATEMP
        if (met.name.equals(F("PATEMP"))) index = PATEMP;
        break;
      case '+': // +13.8A / +13.8B
        if (met.name.equals(F("+13.8A"))) index = VOLTAGE_BEFORE;
        else if (met.name.equals(F("+13.8B"))) index = VOLTAGE_AFTER;
        break;
      default:
    // NOTE: Known meter names we currently skip (no mapping yet):
    // MIC, HWALC, PACURRENT, MAINFAN, REFPWR, 24kHz, OSC, ANF, TNF, SQUELCH,
    // NR, AGC+, CODEC, SC_MIC, SC_FILT_1, ALC, PRE_WAVE_AGC, SC_FILT_2,
    // PRE_WAVE, B4RAMP, AFRAMP, POST_P, GAIN
    //
    // These can be mapped to spare indices in metersId[] later if needed.
        break;
    }
  }

  // Safety guard (same as your original)
  const int N = (int)(sizeof(metersId) / sizeof(metersId[0]));
  if (index >= 0 && index < N) {
    metersId[index] = met.id;
  } else {
    // Optional debug:
    // debug(F("Unknown meter name, skipped: "));
    // debugln(met.name);
  }
}

//GUI METHODS IMPLEMENTATION
/************************************ SLICE ****************************************/

/************************************ RADIO ****************************************/
FLASHMEM void FlexRig::set_headphone_gain(int val)
{
  char cmd[30];
  sprintf(cmd, "mixer headphone gain %d", val);
  //sprintf(cmd,"audio client 0 slice 0 gain %d",val);

  addToCommandList(cmd);
  radio.set_headphone_gain(val);
}

/************************************ RADIO ****************************************/
FLASHMEM void FlexRig::set_sidetone_onoff(int value)
{
  char cmd[40];
  sprintf(cmd, "cw sidetone %d", value);
  addToCommandList(cmd);
  transmit.set_sidetone(value);
}

FLASHMEM void FlexRig::set_breakin_onoff(int value)
{
  char cmd[40];
  sprintf(cmd, "cw break_in %d", value);
  addToCommandList(cmd);
  transmit.set_break_in(value);
}

FLASHMEM void FlexRig::set_iambic_onoff(int value)
{
  char cmd[40];
  sprintf(cmd, "cw iambic %d", value);
  addToCommandList(cmd);
  transmit.set_iambic(value);
}

FLASHMEM void FlexRig::set_vox_onoff(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set vox_enable=%d", value);
  addToCommandList(cmd);
  transmit.set_vox_enable(value);
}

FLASHMEM void FlexRig::set_dexp_onoff(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set compander=%d", value);
  addToCommandList(cmd);
  transmit.set_compander(value);
}

FLASHMEM void FlexRig::set_proc_onoff(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set speech_processor_enable=%d", value);
  addToCommandList(cmd);
  transmit.set_speech_processor_enable(value);
}

FLASHMEM void FlexRig::set_monitor_onoff(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set mon=%d", value);
  addToCommandList(cmd);
  transmit.set_sb_monitor(value);
}

FLASHMEM void FlexRig::setRxAntenna(int sliceId, String value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d rxant=%s", sliceId, value.c_str());
  addToCommandList(cmd);
  slice[sliceId].set_rxant(value);
}

FLASHMEM void FlexRig::setTxAntenna(int sliceId, String value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d txant=%s", sliceId, value.c_str());
  addToCommandList(cmd);
  slice[sliceId].set_txant(value);
}

FLASHMEM void FlexRig::setPreampGain(int panId, int value)
{
  char cmd[50];
  sprintf(cmd, "display pan set 0x%d rfgain=%d.0", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_rfgain(value);
}

FLASHMEM void FlexRig::setTxEqMode(int value)
{
  char cmd[40];
  if (value)
    sprintf(cmd, "eq TXsc mode=True");
  else
    sprintf(cmd, "eq TXsc mode=False");
  addToCommandList(cmd);
  eq.set_TX_mode(value);
}

FLASHMEM void FlexRig::setRxEqMode(int value)
{
  char cmd[40];
  if (value)
    sprintf(cmd, "eq RXsc mode=True");
  else
    sprintf(cmd, "eq RXsc mode=False");
  addToCommandList(cmd);
  eq.set_RX_mode(value);
}

FLASHMEM void FlexRig::setBand(int panId, int value)
{
  char cmd[50];
  //debugln(panId+40000000);
  //debugln(panId+value);
  sprintf(cmd, "display pan set 0x%d band=%d", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_band(value);
}

FLASHMEM void FlexRig::setMode(int sliceId, String value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d mode=%s", sliceId, value.c_str());
  addToCommandList(cmd);
  slice[sliceId].set_mode(value);
}

FLASHMEM bool FlexRig::loadGlobalProfile(const String &profileName)
{
  // Guard: must be connected
  if (!connected) return false;

  // Guard: empty names are a no-op
  if (!profileName.length()) return false;

  // Escape any quotes (defensive) and send quoted name
  String name = profileName;
  name.replace("\"", "\\\"");

  // Flex expects: profile global load "<name>"
  String cmd = "profile global load \"" + name + "\"";

  // Non-blocking: queue/send the command; completion is detected asynchronously
  // when parseStatus() receives "profile global current=...".
  Global_Prof_Applied = false;
  send(cmd);
  return true;
}

FLASHMEM void FlexRig::setTx(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d tx=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_tx(active);
}

FLASHMEM void FlexRig::setActiveSlice(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d active=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_active(active);
}

FLASHMEM void FlexRig::removeSlice(int sliceId)
{
  char cmd[30];
  sprintf(cmd, "slice r %d", sliceId);
  addToCommandList(cmd);
  slice[sliceId].set_in_use(0);
}

FLASHMEM void FlexRig::createSlice(double freq, String antenna, String mode)
{
  char cmd[50];
  sprintf(cmd, "slice c %f %s %s", freq, antenna.c_str(), mode.c_str());
  addToCommandList(cmd);
}

/*************************************************************
 * Toggle between CW and SSB according to amateur band rules
 * Returns:
 *   true  = mode changed (or no-op by rule)
 *   false = denied by band check (no mode change)
 *
 * Rules:
 * - If mode == CW:
 *     - 30 m (10.1–10.15 MHz): stay CW (no switch) → return true
 *     - < 10.0 MHz: LSB, except 60 m (5.0–5.5 MHz) = USB
 *     - ≥ 10.0 MHz: USB
 *   Before changing, simulate new mode and call CheckInBand(..., false).
 *   If false → deny and return false.
 * - If mode == USB or LSB: switch to CW (always allowed) → return true
 *************************************************************/
FLASHMEM bool FlexRig::toggleCW_SSB()
{
  if (!connected) {
#if DEBUG
    Serial.println(F("toggleCW_SSB(): rig not connected"));
#endif
    return false;
  }

  // Find active TX slice
  int txSlice = -1;
  for (int i = 0; i < nMaxSlice; ++i) {
    if (slice[i].tx == 1 && slice[i].in_use == 1) {
      txSlice = i;
      break;
    }
  }
  if (txSlice < 0) {
#if DEBUG
    Serial.println(F("toggleCW_SSB(): no TX slice active"));
#endif
    return false;
  }

  // Current mode and frequency (RF_frequency is in Hz)
  String mode = slice[txSlice].mode;
  mode.toUpperCase();

  long   rf_raw_hz = slice[txSlice].RF_frequency;   // e.g. 21275000 for 21.275 MHz
  double freqMHz   = (double)rf_raw_hz / 1e6;

#if DEBUG
  Serial.print(F("toggleCW_SSB(): mode="));
  Serial.print(mode);
  Serial.print(F("  freq(MHz)="));
  Serial.println(freqMHz, 6);
#endif

  // If currently CW → choose SSB by rules, but validate with CheckInBand(..., false)
  if (mode == "CW") {
    // 30 m stays CW: treat as success (requested toggle is a no-op by policy)
    if (freqMHz >= 10.1 && freqMHz <= 10.15) {
#if DEBUG
      Serial.println(F("toggleCW_SSB(): 30m band → remain CW (no-op)"));
#endif
      return true;
    }

    // Decide target SSB mode
    String newMode = "USB";  // default for >=10 MHz
    if (freqMHz < 10.0) {
      if (freqMHz >= 5.0 && freqMHz <= 5.5) {
        newMode = "USB";     // 60 m always USB
      } else {
        newMode = "LSB";     // <10 MHz → LSB
      }
    }

    // Simulate: temporarily override slice mode, run band check without UI sync, then restore.
    String oldMode = slice[txSlice].mode;
    slice[txSlice].mode = newMode;   // local simulation only
    bool ok = CheckInBand(txSlice, /*sync_ui=*/false);
    slice[txSlice].mode = oldMode;

    if (!ok) {
#if DEBUG
      Serial.println(F("toggleCW_SSB(): denied by CheckInBand (would be OOB)"));
#endif
      return false; // deny toggle
    }

#if DEBUG
    Serial.print(F("toggleCW_SSB(): switching to "));
    Serial.println(newMode);
#endif
    setMode(txSlice, newMode);
    return true;
  }

  // If currently SSB → switch to CW (CW footprint is minimal; allow)
  if (mode == "USB" || mode == "LSB") {
#if DEBUG
    Serial.println(F("toggleCW_SSB(): switching to CW"));
#endif
    setMode(txSlice, "CW");
    return true;
  }

#if DEBUG
  Serial.println(F("toggleCW_SSB(): mode not CW/SSB → ignored"));
#endif
  return false;
}

// === [TM_PATCH BEGIN] FlexRig::split() robust guards + safe fallbacks ===
FLASHMEM bool FlexRig::split()
{
  // Guards for slice presence
  if (slice[0].in_use != 1) return false;   // Slice A missing
  if (slice[1].in_use == 1) return false;   // Slice B already present

  // Try primary source: slice A pan handle
  uint32_t panHandle = (uint32_t)slice[0].pan;

  // Validate: require true Flex handle (>= 0x40000000) and not 0xFFFFFFFF sentinel
  auto is_valid_pan = [](uint32_t h) -> bool {
    return (h >= 0x40000000u) && (h != 0xFFFFFFFFu);
  };

  if (!is_valid_pan(panHandle)) {
    // Fallback 1: use active panadapter's handle if known
    if (activePanadapter >= 0 && activePanadapter < (int)nMaxPanadapter) {
      uint32_t h2 = (uint32_t)panadapter[activePanadapter].pan;
      if (is_valid_pan(h2)) {
        panHandle = h2;
      }
    }
  }

  if (!is_valid_pan(panHandle)) {
    // Fallback 2: scan all panadapters for the first valid handle
    for (int i = 0; i < (int)nMaxPanadapter; ++i) {
      uint32_t h3 = (uint32_t)panadapter[i].pan;
      if (is_valid_pan(h3)) {
        panHandle = h3;
        break;
      }
    }
  }

  // If still invalid, abort cleanly
  if (!is_valid_pan(panHandle)) {
    return false;
  }

  // Optional one-time sync: map handle to its panadapter index and patch if zero
  {
    const uint32_t base = 0x40000000u;
    int pidx = (panHandle >= base) ? (int)(panHandle - base) : -1;

    if (pidx >= 0 && pidx < (int)nMaxPanadapter) {
      if ((uint32_t)panadapter[pidx].pan == 0u) {
        panadapter[pidx].set_pan((int)panHandle);
      }
    }
  }

  mSplitPending = true;

  // Build: "slice create clone_slice=0 pan=0xHHHHHHHH load_from=clone"
  char hex[11];
  tm_hex32_to_cstr(panHandle, hex);

  char cmd[96];
  cmd[0] = '\0';
  strcat(cmd, "slice create clone_slice=0 pan=");
  strcat(cmd, hex);
  strcat(cmd, " load_from=clone");

  send(String(cmd));
  return true;
}

FLASHMEM void FlexRig::finalizePendingSplitIfReady()
{
  // Abort if not pending
  if (!mSplitPending) return;

  // Only finalize when B is reported present by the model
  if (slice[1].in_use != 1) return;

  // Queue TX handover; sending happens via sendAllCommands()
  commandList.add(F("slice set 1 tx=1"));  // make B TX
  commandList.add(F("slice set 0 tx=0"));  // clear TX on A

  mSplitPending = false;
}

FLASHMEM void FlexRig::removePanadapter(int panId)
{
  //char cmd[30];
  //sprintf(cmd,"display pan r %dx40000000",panId);
  //addToCommandList(cmd);
}


FLASHMEM void FlexRig::createPanadapter(double freq)
{
  //char cmd[50];
  //sprintf(cmd,"display pan c %f 1 800 400",freq);
  //addToCommandList(cmd);
}

FLASHMEM void FlexRig::setFreq(int sliceId, int freq)
{
  char cmd[40];
  sprintf(cmd, "slice t %d %f \n", sliceId, (double)freq / 1000000.0);
  //sprintf(cmd, "slice t %d %f autopan=0\n",sliceId,(double)freq/1000000.0);
  addToCommandList(String(cmd));
  slice[sliceId].RF_frequency = freq;
}

FLASHMEM void FlexRig::setAgcMode(int sliceId, String value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d agc_mode=%s", sliceId, value.c_str());
  addToCommandList(cmd);
  slice[sliceId].set_agc_mode(value);
}

FLASHMEM void FlexRig::setDisplayPanWeightAverage(int panId, int value)
{
  char cmd[60];
  sprintf(cmd, "display pan set 0x%d weighted_average=%d", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_weighted_average(value);
}

FLASHMEM void FlexRig::setPanafallAutoBlack(int panFallId, int value)
{
  char cmd[60];
  sprintf(cmd, "display panafall set 0x%d auto_black=%d", panFallId + 42000000, value);
  addToCommandList(cmd);
  waterfall[panFallId].set_auto_black(value);
}

FLASHMEM void FlexRig::setNr(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d nr=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_nr(active);
}

FLASHMEM void FlexRig::setWnb(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d wnb=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_wnb(active);
}

FLASHMEM void FlexRig::setNb(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d nb=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_nb(active);
}

FLASHMEM void FlexRig::setApf(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d apf=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_apf(active);
}

FLASHMEM void FlexRig::setSquelch(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d squelch=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_squelch(active);
}

FLASHMEM void FlexRig::setAnf(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d anf=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_anf(active);
}

FLASHMEM void FlexRig::setRit(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d rit_on=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_rit_on(active);
}

FLASHMEM void FlexRig::setXit(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d xit_on=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_xit_on(active);
}

FLASHMEM void FlexRig::setAudioMute(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "audio client 0 slice %d mute %d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_audio_mute(active);
}

FLASHMEM void FlexRig::setAgcOffLevel(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d agc_off_level=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_agc_off_level(active);
}

FLASHMEM void FlexRig::setAgcThreshold(int sliceId, int active)
{
  char cmd[40];
  sprintf(cmd, "slice set %d agc_threshold=%d", sliceId, active);
  addToCommandList(cmd);
  slice[sliceId].set_agc_threshold(active);
}

FLASHMEM void FlexRig::setLineoutMute(int active)
{
  char cmd[40];
  sprintf(cmd, "mixer lineout mute %d", active);
  addToCommandList(cmd);
  radio.set_lineout_mute(active);
}

FLASHMEM void FlexRig::setHeadphoneMute(int active)
{
  char cmd[40];
  sprintf(cmd, "mixer headphone mute %d", active);
  addToCommandList(cmd);
  radio.set_headphone_mute(active);
}

FLASHMEM void FlexRig::setBandWidth(int sliceId, int low, int high)
{
  char cmd[40];
  sprintf(cmd, "filt %d %d %d", sliceId, low, high);
  addToCommandList(cmd);
  slice[sliceId].set_filter_lo(low);
  slice[sliceId].set_filter_hi(high);
  slice[sliceId].calcShiftWidthValue();
}

FLASHMEM void FlexRig::setWnbLevel(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d wnb_level=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_wnb_level(value);
}

FLASHMEM void FlexRig::setNbLevel(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d nb_level=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_nb_level(value);
}

FLASHMEM void FlexRig::setNrLevel(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d nr_level=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_nr_level(value);
}

FLASHMEM void FlexRig::setApfLevel(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d apf_level=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_apf_level(value);
}

FLASHMEM void FlexRig::setSquelchLevel(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d squelch_level=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_squelch_level(value);
}

FLASHMEM void FlexRig::setAnfLevel(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d anf_level=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_anf_level(value);
}

FLASHMEM void FlexRig::setPanAverage(int panId, int value)
{
  char cmd[40];
  sprintf(cmd, "display pan set 0x%d average=%d", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_average(value);
}

FLASHMEM void FlexRig::setRitFreq(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d rit_freq=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_rit_freq(value);
}

FLASHMEM void FlexRig::setXitFreq(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d xit_freq=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_xit_freq(value);
}

FLASHMEM void FlexRig::setPanFps(int panId, int value)
{
  char cmd[50];
  sprintf(cmd, "display pan set 0x%d fps=%d", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_fps(value);
}

FLASHMEM void FlexRig::setRxFiltLow(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "filt %d %d %d", sliceId, value, slice[sliceId].filter_hi);
  addToCommandList(cmd);
  slice[sliceId].set_filter_lo(value);
  slice[sliceId].calcShiftWidthValue();
}

FLASHMEM void FlexRig::setRxFiltShift(int sliceId, int value)
{
  char cmd[40];
  slice[sliceId].set_filter_shift(value);
  slice[sliceId].calcHighLowValues();
  sprintf(cmd, "filt %d %d %d", sliceId, slice[sliceId].filter_lo, slice[sliceId].filter_hi);
  addToCommandList(cmd);
}

FLASHMEM void FlexRig::setWatBlackLevel(int watId, int value)
{
  char cmd[60];
  sprintf(cmd, "display panafall set 0x%d black_level=%d", watId + 42000000, value);
  addToCommandList(cmd);
  waterfall[watId].set_black_level(value);
}

FLASHMEM void FlexRig::setRxFiltHigh(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "filt %d %d %d", sliceId, slice[sliceId].filter_lo, value);
  addToCommandList(cmd);
  slice[sliceId].set_filter_hi(value);
  slice[sliceId].calcShiftWidthValue();
}

FLASHMEM void FlexRig::setRxFiltWidth(int sliceId, int value)
{
  char cmd[40];
  slice[sliceId].set_filter_width(value);
  slice[sliceId].calcHighLowValues();
  sprintf(cmd, "filt %d %d %d", sliceId, slice[sliceId].filter_lo, slice[sliceId].filter_hi);
  addToCommandList(cmd);
}

FLASHMEM void FlexRig::setWatGradientId(int watId, int value)
{
  char cmd[60];
  sprintf(cmd, "display panafall set 0x%d gradient_index=%d", watId + 42000000, value);
  addToCommandList(cmd);
  waterfall[watId].set_gradient_index(value);
}

FLASHMEM void FlexRig::setAudioGain(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "audio client 0 slice %d gain %d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_audio_gain(value);
}

FLASHMEM void FlexRig::setAudioPan(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "audio client 0 slice %d pan %d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_audio_pan(value);
}

FLASHMEM void FlexRig::setWatLineDuration(int watId, int value)
{
  char cmd[60];
  sprintf(cmd, "display panafall set 0x%d line_duration=%d", watId + 42000000, value);
  addToCommandList(cmd);
  waterfall[watId].set_line_duration(value);
}

FLASHMEM void FlexRig::setWatColorGain(int watId, int value)
{
  char cmd[60];
  sprintf(cmd, "display panafall set 0x%d color_gain=%d", watId + 42000000, value);
  addToCommandList(cmd);
  waterfall[watId].set_color_gain(value);
}

FLASHMEM void FlexRig::setCwSpeed(int value)
{
  char cmd[30];
  sprintf(cmd, "cw wpm %d", value);
  addToCommandList(cmd);
  transmit.set_speed(value);
}

FLASHMEM void FlexRig::setCwPitch(int value)
{
  char cmd[30];
  sprintf(cmd, "cw pitch %d", value);
  addToCommandList(cmd);
  transmit.set_pitch(value);
}

FLASHMEM void FlexRig::setCwBreakinInDelay(int value)
{
  char cmd[30];
  sprintf(cmd, "cw break_in_delay %d", value);
  addToCommandList(cmd);
  transmit.set_break_in_delay(value);
}

FLASHMEM void FlexRig::setCwMonitorGain(int value)
{
  char cmd[30];
  sprintf(cmd, "transmit set mon_gain_cw=%d", value);
  addToCommandList(cmd);
  transmit.set_mon_gain_cw(value);
}

FLASHMEM void FlexRig::setCwMonitorPan(int value)
{
  char cmd[30];
  sprintf(cmd, "transmit set mon_pan_cw=%d", value);
  addToCommandList(cmd);
  transmit.set_mon_pan_cw(value);
}

FLASHMEM void FlexRig::setMicLevel(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set miclevel=%d", value);
  addToCommandList(cmd);
  transmit.set_mic_level(value);
}

FLASHMEM void FlexRig::setSbMonitorGain(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set mon_gain_sb=%d", value);
  addToCommandList(cmd);
  transmit.set_mon_gain_sb(value);
}

FLASHMEM void FlexRig::setCompanderLevel(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set compander_level=%d", value);
  addToCommandList(cmd);
  transmit.set_compander_level(value);
}

FLASHMEM void FlexRig::setVoxLevel(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set vox_level=%d", value);
  addToCommandList(cmd);
  transmit.set_vox_level(value);
}

FLASHMEM void FlexRig::setVoxDelay(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set vox_delay=%d", value);
  addToCommandList(cmd);
  transmit.set_vox_delay(value);
}

FLASHMEM void FlexRig::setAmCarrier(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set am_carrier=%d", value);
  addToCommandList(cmd);
  transmit.set_am_carrier_level(value);
}

FLASHMEM void FlexRig::setTransmitLow(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set filter_low=%d filter_high=%d", value, transmit.hi);
  addToCommandList(cmd);
  transmit.set_lo(value);
}

FLASHMEM void FlexRig::setTransmitHigh(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set filter_low=%d filter_high=%d", transmit.lo, value);
  addToCommandList(cmd);
  transmit.set_hi(value);
}

FLASHMEM void FlexRig::setPanBandwidth(int panId, float value)
{
  char cmd[70];
  sprintf(cmd, "display pan set 0x%d bandwidth=%f autocenter=1", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_bandwidth(value * 1000000);
}

FLASHMEM void FlexRig::setLineoutGain(int value)
{
  char cmd[40];
  sprintf(cmd, "mixer lineout gain %d", value);
  addToCommandList(cmd);
  radio.set_lineout_gain(value);
}

FLASHMEM void FlexRig::setHeadphoneGain(int value)
{
  char cmd[40];
  sprintf(cmd, "mixer headphone gain %d", value);
  addToCommandList(cmd);
  radio.set_headphone_gain(value);
}

FLASHMEM void FlexRig::setRfPower(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set rfpower=%d", value);
  addToCommandList(cmd);
  transmit.set_rfpower(value);
}

FLASHMEM void FlexRig::setTunePower(int value)
{
  char cmd[40];
  sprintf(cmd, "transmit set tunepower=%d", value);
  addToCommandList(cmd);
  transmit.set_tunepower(value);
}

FLASHMEM void FlexRig::setPanMinDbm(int panId, float value)
{
  char cmd[50];
  sprintf(cmd, "display pan set 0x%d min_dbm=%f", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_min_dbm(value);
}

FLASHMEM void FlexRig::setPanMaxDbm(int panId, float value)
{
  char cmd[50];
  sprintf(cmd, "display pan set 0x%d max_dbm=%f", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_max_dbm(value);
}

FLASHMEM void FlexRig::setPanCenter(int panId, float value)
{
  char cmd[50];
  sprintf(cmd, "display pan set 0x%d center=%f", panId + 40000000, value);
  addToCommandList(cmd);
  panadapter[panId].set_center(value);
}

FLASHMEM void FlexRig::setEqControl(String type, String freq, int value)
{
  char cmd[30];
  sprintf(cmd, "eq %ssc %s=%d", type.c_str(), freq.c_str(), value);
  addToCommandList(cmd);
  if (type.equals(F("RX")))
  {
    if (freq.equals(F("63Hz")))
    {
      eq.set_RX_63Hz(value);
      return;
    }
    if (freq.equals(F("125Hz")))
    {
      eq.set_RX_125Hz(value);
      return;
    }
    if (freq.equals(F("250Hz")))
    {
      eq.set_RX_250Hz(value);
      return;
    }
    if (freq.equals(F("500Hz")))
    {
      eq.set_RX_500Hz(value);
      return;
    }
    if (freq.equals(F("1000Hz")))
    {
      eq.set_RX_1000Hz(value);
      return;
    }
    if (freq.equals(F("2000Hz")))
    {
      eq.set_RX_2000Hz(value);
      return;
    }
    if (freq.equals(F("4000Hz")))
    {
      eq.set_RX_4000Hz(value);
      return;
    }
    if (freq.equals(F("8000Hz")))
    {
      eq.set_RX_8000Hz(value);
      return;
    }
  }
  else
  {
    if (freq.equals(F("63Hz")))
    {
      eq.set_TX_63Hz(value);
      return;
    }
    if (freq.equals(F("125Hz")))
    {
      eq.set_TX_125Hz(value);
      return;
    }
    if (freq.equals(F("250Hz")))
    {
      eq.set_TX_250Hz(value);
      return;
    }
    if (freq.equals(F("500Hz")))
    {
      eq.set_TX_500Hz(value);
      return;
    }
    if (freq.equals(F("1000Hz")))
    {
      eq.set_TX_1000Hz(value);
      return;
    }
    if (freq.equals(F("2000Hz")))
    {
      eq.set_TX_2000Hz(value);
      return;
    }
    if (freq.equals(F("4000Hz")))
    {
      eq.set_TX_4000Hz(value);
      return;
    }
    if (freq.equals(F("8000Hz")))
    {
      eq.set_TX_8000Hz(value);
      return;
    }
  }
}

FLASHMEM void FlexRig::setMoxState(int value)
{
  char cmd[30];
  sprintf(cmd, "xmit %d", value);
  addToCommandList(cmd);
}

FLASHMEM void FlexRig::setTuneState(int value)
{
  char cmd[30];
  sprintf(cmd, "transmit tune %d", value);
  addToCommandList(cmd);
  transmit.set_tune(value);
}

FLASHMEM void FlexRig::enableAtu(int value)
{
  char cmd[30];
  if (value == 0)
    sprintf(cmd, "atu bypass");
  else
    sprintf(cmd, "atu start");
  addToCommandList(cmd);
}
void FlexRig::setAtuMemMode(int value)
{
  char cmd[40];
  sprintf(cmd, "atu set memories_enabled=%d", value);
  addToCommandList(cmd);
}


FLASHMEM void FlexRig::setStep(int sliceId, int value)
{
  char cmd[40];
  sprintf(cmd, "slice set %d step=%d", sliceId, value);
  addToCommandList(cmd);
  slice[sliceId].set_step(value);
}

int FlexRig::sendCwxMsg(String value)
{
  // Hard cap to avoid pathological sizes
  if (value.length() > 400) value.remove(400);

  String cmd = "cwx send \"" + value + "\"";
  return send(cmd, 3);
}

FLASHMEM int FlexRig::sendCwxMacro(int value)
{
  char cmd[250];
  switch (value)
  {
    case 1:
      sprintf(cmd, "cwx send \"%s\"", cwx.macro1.c_str());
      break;
    case 2:
      sprintf(cmd, "cwx send \"%s\"", cwx.macro2.c_str());
      break;
    case 3:
      sprintf(cmd, "cwx send \"%s\"", cwx.macro3.c_str());
      break;
  }
  int c = send(cmd, 3);
  cwx.addToInfoList(value, c);
  //debug("Cwx Response command:");debugln(c);
  return c;
}

FLASHMEM void FlexRig::clearCwx()
{
  char *cmd = (char *)"cwx clear";
  addToCommandList(cmd);
}

FLASHMEM void FlexRig::setSpotTriggered(int value)
{
  char cmd[50];
  //	sprintf(cmd,"display pan set 0x%d fps=%d",panId+40000000,value);
  sprintf(cmd, "display pan set 0x%d fps=%d", 40000000, value);
  addToCommandList(cmd);
  //	panadapter[panId].set_fps(value);
}