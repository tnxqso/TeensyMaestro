#pragma once
#include <stdint.h>
#include <Arduino.h>

/*
  ui_boot.h
  Boot UI module for TeensyMaestro.
  Handles startup banner, splash information, and simple progress display.

  Notes:
  - attachTFT() uses void* to avoid a hard dependency in this header.
  - Implementation in ui_boot.cpp casts it to TFT_Driver* from Display_Driver.h.
*/

struct BootInfo {
  // Use Arduino String for convenient ownership and type interoperability
  String myCall;                 // e.g. "KD0RC"
  String version;                // e.g. TM_VERSION
  bool   isDebugBuild;           // DEBUG == 1

  uint8_t mac[6];                // Teensy MAC address
  String  ipStr;                 // Printed IP string (e.g. "192.168.1.23")

  bool   rigConnected;           // fRig.connected
  bool   fixedEndpointUsed;      // gFixedEndpointUsed
  String fixedEndpointLabel;     // e.g. "host:port" if used
  uint8_t rigIp[4];              // fRig.ipAddress[4] if you want raw bytes

  String rigNick;                // fRig.nickName (char[36] -> copied into String)
  String rigSerial;              // fRig.serial
  String rigModel;               // fRig.modelName
  String rigVersion;             // fRig.softVersion

  // Displayed only if not in setup mode
  String clientStation;          // fRig.Client_Station[ClientMenuItem]
  String clientProgram;          // fRig.Client_Program[ClientMenuItem]

  bool inSetup;                  // InSetup flag
};

enum class BootStage : uint8_t {
  Start,
  InitDisplay,
  InitNetwork,
  InitStorage,
  DiscoverFlex,
  ConnectFlex,
  LoadConfig,
  Done
};

namespace UI_Boot {
  void attachTFT(void* tftPtr);
  void showInitialBanner(uint16_t bgColor = 0xFFFF, const char* byline = nullptr);
  void showInfo();
  void showSplash(const BootInfo& info);
  void showProgress(BootStage stage, const char* note = nullptr);

  // Convenience helpers for boot progress
  void Prog(BootStage st, const char* s);
  void Prog(BootStage st, const __FlashStringHelper* s);
  void Progf(BootStage st, const char* fmt, ...);  
}
