/*
  Library support for FlexRadio 6000 Rigs
  Copyright (C)2015 Vincenzo Stefanazzi - IW7DMH. All right reserved

  This library is free software; you can redistribute it and/or
  modify it under the terms of the CC BY-NC-SA 3.0 license.
  https://creativecommons.org/licenses/by-nc-sa/3.0/

  The license applies to all part of the library including the
  examples and tools supplied with the library.
*/
#pragma once

// --- Debug default for the library (sketch can override via build flags) ---
#ifndef DEBUG
  #define DEBUG 0
#endif

// Centralized identity strings for this library.
// These may be overridden at compile time if needed.
#ifndef TM_PRODUCT_NAME
  #define TM_PRODUCT_NAME   "TeensyMaestro"    // Main product name (immutable upstream identity)
#endif

#ifndef TM_EDITION_LONG
  #define TM_EDITION_LONG   "Community Edition" // Human-friendly edition label for UI
#endif

#ifndef TM_EDITION_SHORT
  #define TM_EDITION_SHORT  "CE"               // Short edition tag for compact displays/logs
#endif

#ifndef TM_VERSION
  #define TM_VERSION        "0.9.40"     // Semantic version with prerelease stage
#endif

#ifndef TM_FULL_NAME
  #define TM_FULL_NAME TM_PRODUCT_NAME " — " TM_EDITION_LONG " (" TM_EDITION_SHORT ")"
#endif

#ifndef TM_FULL_NAME_WITH_VERSION
  #define TM_FULL_NAME_WITH_VERSION TM_FULL_NAME " v" TM_VERSION
#endif

#ifndef TM_SHORT_NAME
  #define TM_SHORT_NAME TM_PRODUCT_NAME " (" TM_EDITION_SHORT ")"
#endif

#include <Arduino.h>     // must precede Print-based declarations
#include "tm_logging.h"  // your debug()/debugln()/debugf() macros

#include <SPI.h>
#include <NativeEthernet.h>
#include "Radio.h"
#include "Transmit.h"
#include "Interlock.h"
#include "Panadapter.h"
#include "Waterfall.h"
#include "Slice.h"
#include "Eq.h"
#include "CommandList.h"
#include "Cwx.h"
#include "Atu.h"
#include "Spots.h"

#define UDP_DISCOVERING_PORT        4992
#define UDP_VITA49_PORT             4991
#define UDP_VITA49_PACKET_MAX_SIZE  2048  // V3 = 2048, V2 = 4992
#define RESPONSE_PACKET_LIST_SIZE   1024  // V3 = 1024, V2 = 25

// --- Null Serial type declaration (used only in .cpp when DEBUG==0) --------
#if !DEBUG
struct __fr_serial_null__ : public Print {
  size_t write(uint8_t) override { return 1; }
};
extern __fr_serial_null__ __fr_serial_sink__;
#endif

// ---------------------------------------------------------------------------


//COMMAND (CIRCULAR) LIST
//#define COMMAND_LIST_SIZE 20
//#define COMMAND_LEGTH 80
#define UPDATE_VFO_TIMER            25  //millis

enum UNITS
{
  dBFS = 0,
  Volts,
  dBm,
  SWR,
  degC
};

enum VITA_PACKET_TYPE
{
  VITA_IF_DATA = 0x0,                    // 0
  VITA_IF_DATA_WITH_STREAM = 0x1,        // 1
  VITA_EXT_DATA = 0x2,                   // 2
  VITA_EXT_DATA_WITH_STREAM = 0x3,       // 3
  VITA_IF_CONTEXT = 0x4,                 // 4
  VITA_EXT_CONTEXT = 0x5                 // 5
};

enum METERS_ID
{
  MET_S_A = 0,
  MET_S_B,
  MET_SWR,
  MET_FPWR,
  MET_MIC_PEAK,
  MET_COMP_PEAK,
  PATEMP,
  VOLTAGE_BEFORE,
  VOLTAGE_AFTER
};


struct rPacket
{
  int id;        //Command id to match with
  int parserId;  //Message parser id
};

struct Meter  //Meters Structure
{
  String name;  //Meter Name
  int id;       //Meter id
  float low;    //Min meter value
  float hi;     //Max meter value
  float value;  //Meter value
  int num;      //Meter source object num
  int unit;     //Meter unit
};

class FlexRig
{
public:
  //properties
  byte ipAddress[4];  		//IP Address
  uint16_t tcpPort;      // TCP control port (defaults to UDP_DISCOVERING_PORT)
  char modelName[36];    //Model Name
  char serial[34];       //Serial Number
  char nickName[36];     //Nick Name
  char softVersion[32];  //Software Version
  char handle[8];        //Handle assigned to client
  int C;                 //Command id counter
  boolean connected;     //Connection status
  int preampList[6];
  int preampListSize;
  //Embedded objects
  Radio radio;
  Transmit transmit;
  Interlock interlock;
  Cwx cwx;
  Panadapter panadapter[2];
  Spots spots[2];
  Waterfall waterfall[2];
  static constexpr int MAX_SLICES = 2;
  Slice slice[MAX_SLICES];
  int nMaxSlice = MAX_SLICES;
  Eq eq;
  Atu atu;
  boolean vfo;
  int activeSlice;
  int nMaxPanadapter;
  int activePanadapter;
  int nMaxWaterfall;
  int activeWaterfall;
  float metersValue[9];  //meters values
  //bool Client_Found = false;
  int Max_Clients          = 0;
  String Client_ID[4]      = { "", "" };
  String Client_Handle[4]  = { "", "" };
  String Client_Program[4] = { "", "" };
  String Client_Station[4] = { "", "" };
  String Client_Status[4];  // connected, disconnected
  String Current_Profile;
  String Profile[200];
  bool Global_Prof_Applied = false;
  static constexpr int MEM_CAP = 200;

  int MaxMemNum            = 0;
  static int Mem[MEM_CAP];          // memory number kept in Flex (may not be consecutive)
  static String MemName[MEM_CAP];
  static String MemFreq[MEM_CAP];

  int TSpotNum[100] = {};
  static String TSpotFreq[100];
  static String TSpotCall[100];

  //methods
  FlexRig();
  void connect();
  void connect(const byte ip[4], uint16_t port = UDP_DISCOVERING_PORT);
  void connect(IPAddress ip,     uint16_t port = UDP_DISCOVERING_PORT);
  bool ensureIdentity(uint16_t wait_ms = 1500);
  bool hasExternalGuiClient() const;
  bool isEffectiveHeadless() const;
  void process();
  void disconnect();
  void fireEvents();
  void ping();
  void setPreampList(char m);
  //void setVFOA(float freq);
  void parseStatus(String msg);
  //void initBuffer();
  FLASHMEM void send(String cmd);
  FLASHMEM void send(const char* cmd);

  //static FlexRig findAFlex(const char *serial);
	static FlexRig findAFlex(String serial);
  void startReceiver();
  void stopReceiver();
  void updateObject(int objectId, int value, int activeSlice, int activePanadapter, int activeWaterfall);
  //String removeFromCommandList();
  void addToCommandList(String entry);

  //GUI METHODS
  void set_headphone_gain(int val);

  void set_sidetone_onoff(int value);
  void set_breakin_onoff(int value);
  void set_iambic_onoff(int value);
  void set_vox_onoff(int value);
  void set_dexp_onoff(int value);
  void set_proc_onoff(int value);
  void set_monitor_onoff(int value);

  void setRxAntenna(int sliceId, String value);
  void setTxAntenna(int sliceId, String value);
  void setPreampGain(int panId, int value);
  void setTxEqMode(int value);
  void setRxEqMode(int value);

  void setBand(int panId, int value);
  void setMode(int sliceId, String value);

  bool loadGlobalProfile(const String &profileName);

  void setTx(int sliceId, int active);
  void setActiveSlice(int sliceId, int active);
  void removeSlice(int sliceId);
  //void createSlice(float freq,String antenna,String mode);
  void createSlice(double freq, String antenna, String mode);

  // --- SmartSDR-style split helper -------------------------------------------
  // Performs a "split" by cloning slice A onto the same panadapter,
  // assigning TX to the new slice (B), and applying mode-dependent offset.
  // Returns true if the split command sequence was queued, false otherwise.
  bool split();

  bool toggleCW_SSB();

  // Called by event handlers when a slice becomes active.
  // It will internally check if a pending split should be finalized.
  //void handleSliceActiveEvent(int senderId);

  void removePanadapter(int panId);
  //void createPanadapter(float freq);
  void createPanadapter(double freq);
  void setFreq(int sliceId, int freq);

  void setAgcMode(int sliceId, String value);

  void setDisplayPanWeightAverage(int panId, int value);
  void setPanafallAutoBlack(int panFallId, int value);

  void setNb(int sliceId, int active);
  void setNr(int sliceId, int active);
  void setWnb(int sliceId, int active);
  void setApf(int sliceId, int active);
  void setSquelch(int sliceId, int active);
  void setAnf(int sliceId, int active);

  void setRit(int sliceId, int active);
  void setXit(int sliceId, int active);

  void setAudioMute(int sliceId, int active);

  void setAgcOffLevel(int sliceId, int active);
  void setAgcThreshold(int sliceId, int active);

  void setLineoutMute(int active);
  void setHeadphoneMute(int active);

  void setBandWidth(int sliceId, int low, int high);

  //Encoders methods
  void setWnbLevel(int sliceId, int value);
  void setNbLevel(int sliceId, int value);
  void setNrLevel(int sliceId, int value);
  void setApfLevel(int sliceId, int value);
  void setSquelchLevel(int sliceId, int value);
  void setAnfLevel(int sliceId, int value);
  void setPanAverage(int panId, int value);
  void setRitFreq(int sliceId, int value);
  void setXitFreq(int sliceId, int value);
  void setPanFps(int panId, int value);
  void setRxFiltLow(int sliceId, int value);
  void setRxFiltShift(int sliceId, int value);
  void setWatBlackLevel(int watId, int value);
  void setRxFiltHigh(int sliceId, int value);
  void setRxFiltWidth(int sliceId, int value);
  void setWatGradientId(int watId, int value);
  void setAudioGain(int sliceId, int value);
  void setAudioPan(int sliceId, int value);
  void setWatLineDuration(int watId, int value);
  void setWatColorGain(int watId, int value);
  void setCwSpeed(int value);
  void setCwPitch(int value);
  void setCwBreakinInDelay(int value);
  void setCwMonitorGain(int value);
  void setCwMonitorPan(int value);
  void setMicLevel(int value);
  void setSbMonitorGain(int value);
  void setCompanderLevel(int value);
  void setVoxLevel(int value);
  void setVoxDelay(int value);
  void setAmCarrier(int value);
  void setTransmitLow(int value);
  void setTransmitHigh(int value);
  void setPanBandwidth(int panId, float value);
  void setLineoutGain(int value);
  void setHeadphoneGain(int value);
  void setRfPower(int value);
  void setTunePower(int value);
  void setPanMinDbm(int panId, float value);
  void setPanMaxDbm(int panId, float value);
  void setPanCenter(int panId, float value);
  void setEqControl(String type, String freq, int value);
  void setMoxState(int value);
  void setTuneState(int value);
  void enableAtu(int value);
  void setAtuMemMode(int value);
  void setStep(int sliceId, int value);
  int sendCwxMsg(String value);
  int sendCwxMacro(int value);
  void clearCwx();
  void setSpotTriggered(int value);

private:
  //properties
  EthernetClient client;
  String buffer;  //tcp-ip buffer

  CommandList commandList;
  void sendAllCommands();

  int metersId[9];  //meters id

  // Pending split transaction; finalized asynchronously (e.g., from process()).
  bool   mSplitPending   = false;

  //Vita-49 variables
  EthernetClient vitaClient;
  EthernetUDP vitaUdp;
  static char vitaPacketBuffer[UDP_VITA49_PACKET_MAX_SIZE];

  static rPacket packetElement[RESPONSE_PACKET_LIST_SIZE];  //circular queque
  int front;
  int rear;
  int size;  //pointers to response list array


  //String CommandEntry[COMMAND_LIST_SIZE];
  //int cFront=0, cRear=-1,cSize=0;   //pointers to commands circular list

  //accessory methods
  static int charIndexOf(const char *string, const char *key);
  //static char *valueOf(char *string, char *key);

  //methods
  void readEthernetData();
  void readVita49Data();
  void parseMessage(String msg);
  
  FLASHMEM int  send(String cmd, int parserId);
  FLASHMEM int  send(const char* cmd, int parserId);
  
  void configureClient();
  void addToResponseList(int id, int parserId);
  rPacket removeFromResponseList();
  //void ping();
  //static boolean isAFlex(const char *model, const char *serial, const char *serialKey);
  unsigned int getUIntValue2Bytes(char nb1, char nb2);
  short getMeterValue(char nb1, char nb2);
  void setMeterField(Meter *m, String fld);
  void parseMeter(String msg);
	String ParseDiscovery(String discKey, String &strBuf);

  //Parser Methods
  void parseReplay(String msg);
  void parseDummy(String msg);        //0
  void parseInfo(String msg);         //0
  void parseAntennaList(String msg);  //1
  void parseMicList(String msg);      //2
  void finalizePendingSplitIfReady();
};
