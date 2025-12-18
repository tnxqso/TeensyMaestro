#pragma once
#include <Arduino.h>

// -------------------- Core init / config --------------------
void storage_configure();
void GetConfigFile();
void TeensyMaestroSetup();

// -------------------- Keyer API / ISRs --------------------
void KeyerSetup();
void KeyerLoop();
void Keyer_Apply_Wpm(int newWpm, bool preserveBaseline);

// These are used as ISR callbacks (Accel.begin / attachInterrupt / timers etc)
void VFOAccelISR();
void UnKeyISR();
void StopDotTimerISR();
void DotKeyISR();
void DashKeyISR();
void StraightKeyISR();

// -------------------- Buttons / IO expander --------------------
void GetIOExpanderButton();
void processIOExpanderBtn(int pressedBtn);   // adjust type if ButtonName is an enum typedef
void ProcessButtons();

// -------------------- Networking helpers --------------------
void teensyMAC(uint8_t *mac);                // adjust if your signature differs
void getFixedIpAddress();

// -------------------- UI / Display --------------------
void RefreshScreen();
void UI_SyncSliceFromRig(int sliceId);

void DispSlice();
void DispProfile();
void DispSMeter();
void DispSMeterScale();
void DispLicense();

void DispMicGain();
void DispRF();
void DispWNB();
void DispMonLevel();
void DispVOXLevel();
void DispVOXDelay();
void DispCWSpeed();
void DispSTFreq();
void DispAccelFactor();
void DispSerNum();
void DispTune();
void DispKeyerOnlyScreen();

void DispTX(int slice);
void DispNB(int slice);
void DispNR(int slice);
void DispRIT(int slice);
void DispXIT(int slice);
void DispMute(int slice);
void DispMode(int slice);
void DispLock(int slice);
void DispAGC(int slice);
void DispFilter(int slice);
void DispStep(int slice);
void DispVol(int slice);

// -------------------- Menus / misc --------------------
void LoadClientMenu();
FLASHMEM void LoadFilterMenu(String Mode);

// -------------------- Rig event callbacks --------------------
// These signatures may need adjustment depending on your event system.
// If they are all `void fn(int)` then this is correct.
void onInterlock_state(int);
void onInterlock_state(void);

void onTransmit_lo(void);
void onTransmit_hi(void);
void onTransmit_rfpower(void);
void onTransmit_tunepower(void);
void onTransmit_vox_level(void);
void onTransmit_vox_delay(void);
void onTransmit_mic_level(void);
void onTransmit_speed(void);
void onTransmit_break_in(void);
void onTransmit_break_in_delay(void);
void onTransmit_mon_gain_sb(void);
void onTransmit_tune(void);

void onPanadapter_pan(int);
void onPanadapter_band(int);
void onPanadapter_xvtr(int);

void onSlice_in_use(int);
void onSlice_index_letter(int);
void onSlice_RF_frequency(int);
void onSlice_rit_on(int);
void onSlice_rit_freq(int);
void onSlice_xit_on(int);
void onSlice_xit_freq(int);
void onSlice_filter_lo(int);
void onSlice_filter_hi(int);
void onSlice_agc_threshold(int);
void onSlice_pan(int);
void onSlice_lock(int);
void onSlice_tx(int);
void onSlice_active(int);
void onSlice_audio_gain(int);
void onSlice_audio_pan(int);
void onSlice_audio_mute(int);
void onSlice_nr(int);
void onSlice_nr_level(int);
void onSlice_wnb(int);
void onSlice_wnb_level(int);
void onSlice_nb(int);
void onSlice_nb_level(int);
void onSlice_apf(int);
void onSlice_apf_level(int);
void onSlice_squelch(int);
void onSlice_squelch_level(int);
void onSlice_filter_shift(int);
void onSlice_filter_width(int);
void onSlice_rxant(int);
void onSlice_mode(int);
void onSlice_step_list(int);
void onSlice_agc_mode(int);
void onSlice_txant(int);
void onSlice_play(int);
void onSlice_ant_list(int);

// Keyer.ino
static inline void Keyer_HardUnkeyNow();
static inline void _KeyerAbortAndCleanup();
void Keyer();
void KeyerLoop();
void KeyerSetup();
void KeyTrans();

void SendMsgRaw(const char* p, size_t n);
void SendChar(char c);
void WordSpace();
void CharSpace();
int  ParseParm(String& s, unsigned int& idx);
void DelayMillis(uint32_t ms);

// Process_Buttons.ino handlers
void HandleBtnPTTBefore();
void HandleBtnPTTAfter();
void HandleBtnTuneBefore();
void HandleBtnTuneAfter();

void HandleBtnMuteA();
void HandleBtnMuteB();
void HandleBtnNoiseBlankA();
void HandleBtnNoiseBlankB();
void HandleBtnNoiseReductionA();
void HandleBtnNoiseReductionB();
void HandleBtnRitSetA();
void HandleBtnRitSetB();
void HandleBtnSelect();
void HandleBtnRitA();
void HandleBtnRitB();
void HandleBtnXitA();
void HandleBtnXitB();

void CWButton(int msg, bool longPress);

void ShutDownCB();
void TM_AttemptFlexConnect();