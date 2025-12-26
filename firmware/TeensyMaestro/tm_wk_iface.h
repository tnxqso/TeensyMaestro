/*
  tm_wk_iface.h

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

// Interface that decouples the WinKey protocol from the actual keyer.
class TM_IKeyer {
public:
  virtual ~TM_IKeyer() {}

  virtual void startPTT() = 0;
  virtual void stopPTT()  = 0;

  // Enqueue ASCII text for CW transmission, returns accepted length.
  virtual size_t enqueueText(const char* text, size_t len) = 0;

  // Speed controls
  virtual void setWpm(uint8_t wpm) = 0;          // Buffered (Queued)
  virtual void setWpmImmediate(uint8_t wpm) = 0; // NEW: Immediate (Direct)
  virtual uint8_t getWpm() const = 0;

  // Weight, ratio, sidetone
  virtual void setWeightPermille(uint16_t per_mille) = 0; 
  virtual void setDitDahRatio(uint8_t ratio) = 0;         
  virtual void setSidetone(bool on) = 0;
  
  // Pro Features (K3NG / WinKeyer 2)
  virtual void setKeyCompensation(uint8_t ms) = 0;
  virtual void setFirstExtension(uint8_t ms) = 0;
  virtual void setFarnsworth(uint8_t wpm) = 0;

  // Status
  virtual bool isBusy() const = 0;
  virtual bool isPaddlePressed() const = 0;
  virtual bool isKeyDown() const = 0;

  // Returns how many bytes are currently queued in the keyer's own TX queue.
  virtual uint16_t txqUsed() const = 0;
  virtual uint16_t txqCapacityBytes() const = 0;

  virtual void keyImmediate(bool down) = 0;  // Manual key line control
  virtual void abortNow() = 0;
  virtual void clearTextQueue() = 0;
};

// TeensyMaestro adapter, wire these to Keyer Engine
class TM_KeyerAdapter : public TM_IKeyer {
public:
  TM_KeyerAdapter();
  ~TM_KeyerAdapter() override;

  void startPTT() override;
  void stopPTT() override;
  size_t enqueueText(const char* text, size_t len) override;

  void setWpm(uint8_t wpm) override;
  void setWpmImmediate(uint8_t wpm) override; // NEW
  uint8_t getWpm() const override;

  void setWeightPermille(uint16_t per_mille) override;
  void setDitDahRatio(uint8_t ratio) override;
  void setSidetone(bool on) override;
  
  // Pro Features
  void setKeyCompensation(uint8_t ms) override;
  void setFirstExtension(uint8_t ms) override;
  void setFarnsworth(uint8_t wpm) override;

  bool isBusy() const override;
  bool isPaddlePressed() const override;
  bool isKeyDown() const override;
  uint16_t txqUsed() const override;
  uint16_t txqCapacityBytes() const override;
  void keyImmediate(bool down) override;
  void abortNow() override;
  void clearTextQueue() override;
};