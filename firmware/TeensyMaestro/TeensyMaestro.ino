/*
  TeensyMaestroPlusKeyer
   Len Koppl, KDØRC
   05/08/20
   Teensy 4.1 (with built-in Ethernet)

*/

// DEBUG is defined in tm_config.h to ensure a consistent setting
// across all .ino and .cpp files in the project. This guarantees
// that logging macros (debug, debugln, debugf) behave uniformly
// regardless of compilation unit.

// VS Code IntelliSense: pull in the other .ino files (no effect on Arduino build)

#include "tm_rig_alias.h"
#include "tm_sketch_api.h"
#include "tm_attr.h"

#include "tm_profile_select.h"

#if defined(__INTELLISENSE__)
  // Local IntelliSense-only aggregation header, safe to ignore for builds
  #include ".vscode/ino_unity_others.h"
#endif

const byte A = 0;  // Slice A
const byte B = 1;  // Slice B

#include <AceTime.h>
using ace_time::TimeZone;

#include "tm_config.h"
#include "tm_logging.h"
#include "tm_system_utils.h"
#include "ClockWidget.h"
#ifdef USE_CRASH_REPORT
  #include <CrashReport.h>
#endif
#include "Display_Driver.h"
#include "Display_Colors.h"
#include <map>
#include "T4_PowerButton.h"

#include <SPI.h>
#include <SD.h>          // must come before MTP_Teensy.h
#include <MTP_Teensy.h>  // MTP: expose SD card over USB using MTP

#include "NativeEthernet.h"
#include <FlexRigTeensy.h>
#include "tm_time.h"
#include "tm_netutil.h"
#include "Encoder.h"
#include "BandPlan.h"
#include "tm_wk_bridge.h"
#include "tm_wk_trace.h"
#include "tm_rcs_server.h"
#include "tm_flex_guard.h"
#include "tm_spot_pusher.h"

#include <EEPROM.h>

#include <Wire.h>

#include <ST7796_t3.h>
#include <st7735_t3_font_Arial.h>
#include <ST7735_t3_font_ArialBold.h>
//#include <st7735_t3_font_ComicSansMS.h>
#include <XPT2046_Touchscreen.h>
#include <Adafruit_MCP23X08.h>
#include <Adafruit_MCP23X17.h>  // NV0E - Mux C is now a MCP23017

#include "KickSort.h"
#include "tm_touch.h" //Keep #include "tm_touch.h" late

volatile bool g_systemReady = false;
void arm_power_down(void); // Provided by T4_PowerButton library

DMAMEM TimeZone utcTz;
DMAMEM TimeZone localTz;

//#include <InternalTemperature.h>

//extern "C" uint32_t set_arm_clock(uint32_t frequency); // required prototype
// set_arm_clock (528000000); // put this in the body of code to change MCU clock dynamically

extern void ClockWidget_Loop();

#if defined(__IMXRT1062__)
  // following only as long usb_mtp is not included in cores
  #if !__has_include("usb_mtp.h")
    #include "usb1_mtp.h"
  #endif
#else
  #ifndef BUILTIN_SDCARD
    #define BUILTIN_SDCARD 254
  #endif
void usb_mtp_configure(void)
{
}
#endif

#define SPI_SPEED SD_SCK_MHZ(33)  // adjust to sd card

#if defined(BUILTIN_SDCARD)
const char *sd_str[] = { "BUILTIN-SD", "AUDIO-SD" };  // edit to reflect your configuration
const int cs[]       = { BUILTIN_SDCARD, 10 };        // edit to reflect your configuration
#else
const char *sd_str[] = { "AUDIO-SD" };  // edit to reflect your configuration
const int cs[]       = { 10 };          // edit to reflect your configuration
#endif
const int nsd = sizeof(sd_str) / sizeof(const char *);

extern "C"
{
  typedef void (*genericHandlerFunction)(void);
}

#define BAUD_RATE 115200

IntervalTimer Accel;
static bool g_SpotsStarted = false;

// For Debounced MicSel processing (called from loop) 
static volatile bool     g_micSelPending = false;
static volatile uint32_t g_micSelStampMs = 0;
static const uint32_t    MICSEL_DEBOUNCE_MS = 20;

/***************************** VFOAccelISR ***************************/
volatile uint32_t g_vfoAccelPulseCount[2] = {0, 0};
volatile uint32_t g_vfoAccelLastIsrMs = 0;
volatile bool     g_vfoAccelPending    = false;

/***************************** TeensyMaestro Constants ***************************/

const byte AGCAPin1  = 25;
const byte AGCAPin2  = 24;
const byte AGCBPin1  = 27;
const byte AGCBPin2  = 26;

//const int BandList[11] = {160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6};

//const byte BandTable_Max = 38;
const int BtnClickDur    = 5;
const int BtnClickTone   = 1000;

// Button and button menu function definitions
const int BtnMuteA   = 0;
const int BtnNBA     = 1;
const int BtnNRA     = 2;
const int BtnRitSetA = 3;
const int BtnMuteB   = 4;
const int BtnNBB     = 5;
const int BtnNRB     = 6;
const int BtnRitSetB = 7;
const int BtnMenuSel = 8;
const int BtnRitA    = 9;
const int BtnXitA    = 10;
const int BtnRitB    = 11;
const int BtnXitB    = 12;
const int BtnPTT     = 13;
const int BtnTune    = 14;

const int BtnCW1 = 0;
const int BtnCW2 = 1;
const int BtnCW3 = 2;
const int BtnCW4 = 3;
const int BtnCW5 = 4;
const int BtnCW6 = 5;

const int BtnMenuNone            = 0;
const int BtnMenuProfLoad        = 1;
const int BtnMenuConfigReload    = 2;
const int BtnMenuFilter          = 3;
const int BtnMenuMem             = 4;
const int BtnMenuCWMsg           = 5;
const int BtnMenuCWMsgSource     = 6;
const int BtnMenuCWPaddleHand    = 7;
const int BtnMenuCWKeyMode       = 8;
const int BtnMenuOOBInd          = 9;
const int BtnMenuTeensyRestart   = 10;
const int BtnMenuCWSetSerNum     = 11;
const int BtnMenuCWResetSerNum   = 12;
const int BtnMenuPowerOff        = 13;
const int BtnMenuSnapToStep      = 14;
const int BtnMenuVFOTrack        = 15;
const int BtnMenuAtoB            = 16;
const int BtnMenuBtoA            = 17;
const int BtnMenuSideToneOnOff   = 18;
const int BtnMenuSideToneFreq    = 19;
const int BtnMenuSetMode         = 20;
const int BtnMenuSetRFPwr        = 21;
const int BtnMenuSetMicGain      = 22;
const int BtnMenuCWLocalEth      = 23;
const int BtnMenuSetBand         = 24;
const int BtnMenuClientBind      = 26;
const int BtnMenuShortPressClick = 27;
const int BtnMenuLongPressClick  = 28;
const int BtnMenuShowSplash      = 29;
const int BtnMenuCenterCntlFcn   = 30;
const int BtnMenuVFOaccel        = 31;
const int BtnMenuAccelFactor     = 32;


const int chipSelect = BUILTIN_SDCARD;

const byte CWMicPin1 = 29;
const byte CWMicPin2 = 28;

// *****************************    Display constants    *****************************
const int AGCT_X     = 100;
const int AGCT_Y     = 97;
const int AGCT_Width = 120;

const int CWHanded_X     = 120;
const int CWHanded_Y     = 290;
const int CWHanded_Width = 120;

const int CWMode_X     = 360;
const int CWMode_Y     = 290;
const int CWMode_Width = 120;

const int CWMsg_X     = 120;
const int CWMsg_Y     = 270;
const int CWMsg_Width = 120;

const int CWSerNum_X     = 360;
const int CWSerNum_Y     = 270;
const int CWSerNum_Width = 120;

const int Enc9Field_X     = 0;
const int Enc9Field_Y     = 250;
const int Enc9Field_Width = 120;

const int KeyerOnly_CWSpeed_Y = 170;  // centered WPM baseline in keyer-only

const int FiltHi_X     = 160;
const int FiltHi_Y     = 210;
const int FiltHi_Width = 60;

const int FiltLo_X     = 0;
const int FiltLo_Y     = 210;
const int FiltLo_Width = 60;

const int FiltShift_X     = 90;
const int FiltShift_Y     = 190;
const int FiltShift_Width = 60;

const int FiltW_X     = 80;
const int FiltW_Y     = 210;
const int FiltW_Width = 80;

const int FontHeight_A12 = 17;
const int FontHeight_A14 = 19;
const int FontHeight_A24 = 30;
const int FontHeight_A32 = 44;

const int Freq_X     = 0;
const int Freq_Y     = 55;
const int Freq_Width = 230;

const int License_X     = 0;
const int License_Y     = 270;
const int License_Width = 60;

const int Lock_X     = 169;
const int Lock_y     = 34;
const int Lock_Width = 50;

const int MidScreen = 250;  // Note that this is 10 pixels east of the actual center where the gray separator line is

const int Mode_X     = 35;
const int Mode_Y     = 0;
const int Mode_Width = 76;

const int Mute_X     = 115;
const int Mute_Y     = 30;
const int Mute_Width = 40;

const int NB_X     = 170;
const int NB_Y     = 2;
const int NB_Width = 57;

const int NR_X     = 170;
const int NR_Y     = 19;
const int NR_Width = 57;

const int Prof_X     = 120;
const int Prof_Y     = 250;
const int Prof_Width = 360;

const int RFPower_X     = 0;
const int RFPower_Y     = 290;
const int RFPower_Width = 110;

const int RIT_X     = 40;
const int RIT_Y     = 120;
const int RIT_Width = 49;

const int SliceA_X       = 0;
const int SliceA_Y       = 0;
const int SliceB_X       = 250;  // Normally the same as MidScreen
const int SliceB_Y       = 0;
const int SliceNameWidth = 25;

const int TXind_X = 115;
const int TXind_Y = 0;

const int Vol_X     = 35;
const int Vol_Y     = 97;
const int Vol_Width = 65;

const int XIT_X     = 135;
const int XIT_Y     = 120;
const int XIT_Width = 49;

// *****************************    End Display constants    *****************************

const int EEAddr         = 0;
const int Enc9_CWSpeed   = 0;
const int Enc9_MicGain   = 1;
const int Enc9_RFPower   = 2;
const int Enc9_TunePower = 3;
const int Enc9_WNBLevel  = 4;
const int Enc9_MonLevel  = 5;
const int Enc9_VOXLevel  = 6;
const int Enc9_VOXDelay  = 7;
const int Enc9_Band      = 8;
const int Enc9_Max       = 8;

const byte HighAPin1 = 7;
const byte HighAPin2 = 6;

const byte LowAPin1 = 5;
const byte LowAPin2 = 4;

const byte LowBPin1  = 16;
const byte LowBPin2  = 17;

const byte HighBPin1 = 14;
const byte HighBPin2 = 15;

const int MaxMenus     = 30;
const int MaxMenuItems = 15;  // includes space at top for menu title
const byte MicSelPin = 36;
const byte MUXEnaPin[2] = { 41, 35 };
const byte MUXIOPin     = 36;
const byte MUXPin[4]    = { 37, 38, 39, 40 };

const unsigned int RestartTime = 2000;  // Time to hold Menu Select button for restart

const byte SD_MOSI = 11;
const byte SD_MISO = 12;
const byte SD_SCK  = 13;

//const unsigned int SelectedTimeout = 5000;  // Leave 'selected' highlight on for 5 seconds
const int SMDelay                  = 350;
const unsigned long SMeterHoldTime = 1500;  // 1500 ms Smeter peak hold time
const int StepSize[9]              = { 1, 10, 50, 100, 500, 1000, 2000, 3000, 5000 };
const byte STPin                   = 34;

// SPI bus
const byte TFT_DC   = 9;
const byte TFT_RST  = 8;
const byte TFT_MOSI = 11;
const byte TFT_SCLK = 13;
const byte TFT_MISO = 12;

// TFT Chip Select
const byte TFT_CS = 10;

// Touch Screen Chip Select
const byte TS_CS = 37;

const byte VFOAPin1  = 1;  // Slice A VFO encoder
const byte VFOAPin2  = 0;  // Slice A VFO encoder

const byte VFOBPin1 = 22;  // Slice B VFO encoder
const byte VFOBPin2 = 23;  // Slice B VFO encoder

const byte VolAPin1  = 3;  // Slice A AF volume encoder
const byte VolAPin2  = 2;  // Slice A AF volume encoder

const byte VolBPin1 = 20;  // Slice B AF volume encoder
const byte VolBPin2 = 21;  // Slice B AF volume encoder
/***************************** End TeensyMaestro Constants ***************************/

/***************************** Keyer Constants ***************************/
const byte Dot  = 1;  // Element length
const byte Dash = 3;  // Element length

const byte ElementSpace = 0;

const byte KeyOutPin      = 33;
const int SerEEAdr        = 0;  // int at this address
const byte StraightKeyPin = 32;

/***************************** End Keyer Constants ***************************/

/***************************** Global Variables ***************************/
/***************************** TeensyMaestro Global Variables ***************************/
static bool s_gatedLogged = false;
int ActivePan   = 0;
int AccelFactor = 600;
int AccelFactorSave;
bool AccelMenuActive = false;
volatile int AccelStep[2];
bool AccelTimingActive[2]   = { false, false };
unsigned long AccelTimer[2] = { 0, 0 };
String addr;
bool gFixedEndpointUsed = false;
char gFixedEndpointLabel[64] = "";

int AGCVal[2];
int AGCValSave[2];

DMAMEM String AMFilterDisp[8] = { "5.6K", "6.0K", "8.0K", "10K", "12K", "14K", "16K", "20K" };
const int AMFilterHigh[8]    = { 2800, 3000, 4000, 5000, 6000, 7000, 8000, 10000 };
const int AMFilterLow[8]     = { -2800, -3000, -4000, -5000, -6000, -7000, -8000, -10000 };
int AMMaxFilterItems   = 8;
int AntennaMenu;
bool AntennaMenuOn = true;

int Band0;  // Panadapter 0
int Band1;  // Panadapter 1
int Band;
int BandSave;
const int BandMenu[13] = { 2200, 630, 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6 };
bool BandMenuOn  = true;

int BtnDebounce = 30;
int BTNidx;
unsigned long ButtonTime;

bool ClientMenuActive = false;
bool ClientMenuOn     = true;
int ClientMenuIDX     = 0;
int ClientMenuItem    = 0;
int Col;
String ConnectSerialNum = "ANY";

// ===== WinKeyer emulator config defaults =====
bool            CFG_WK_Enable       = true;
TM_WK_Transport CFG_WK_Transport    = TM_WK_TRANSPORT_TCP;
uint16_t        CFG_WK_TCP_Port     = 8891;

// ===== Remote Command Server defaults =====
bool            CFG_RCS_Enable      = false;
uint16_t        CFG_RCS_TCP_Port     = 5020;

// ---- FlexRadio connection config (from MMConfig.ini) ----
enum TM_ConnectionMode {
  TM_CONN_AUTO = 0,
  TM_CONN_FIXED,
  TM_CONN_FIXED_FAILOVER
};

// -----------------------------------------------------------------------------
// Profile Selector configuration (defaults)
// -----------------------------------------------------------------------------

// Timing (ms)
uint16_t CFG_Profile_Selector_Timeout_Ms     = 5000; // total idle timeout
uint16_t CFG_Profile_Selector_Close_Delay_Ms = 200;  // close-after-mode tap

// Mode visibility (default baseline; INI can override)
bool CFG_Mode_Visible_SSB  = true;
bool CFG_Mode_Visible_CW   = true;
bool CFG_Mode_Visible_FM   = true;
bool CFG_Mode_Visible_DIGU = false;  // keep off by default for a lean UI

// -----------------------------------------------------------------------------
// Explicit Global Profile name overrides (defaults)
// If any of these are set from MMConfig.ini, the overridden value is used.
// -----------------------------------------------------------------------------

// CW
DMAMEM String CFG_Profile_Map_CW_160M = "CW 160m";
DMAMEM String CFG_Profile_Map_CW_80M  = "CW 80m";
DMAMEM String CFG_Profile_Map_CW_60M  = "CW 60m";
DMAMEM String CFG_Profile_Map_CW_40M  = "CW 40m";
DMAMEM String CFG_Profile_Map_CW_30M  = "CW 30m";
DMAMEM String CFG_Profile_Map_CW_20M  = "CW 20m";
DMAMEM String CFG_Profile_Map_CW_17M  = "CW 17m";
DMAMEM String CFG_Profile_Map_CW_15M  = "CW 15m";
DMAMEM String CFG_Profile_Map_CW_12M  = "CW 12m";
DMAMEM String CFG_Profile_Map_CW_10M  = "CW 10m";
DMAMEM String CFG_Profile_Map_CW_6M   = "CW 6m";

// SSB
DMAMEM String CFG_Profile_Map_SSB_160M = "SSB 160m";
DMAMEM String CFG_Profile_Map_SSB_80M  = "SSB 80m";
DMAMEM String CFG_Profile_Map_SSB_60M  = "SSB 60m";
DMAMEM String CFG_Profile_Map_SSB_40M  = "SSB 40m";
DMAMEM String CFG_Profile_Map_SSB_30M  = "SSB 30m";   // selector will gray this out, but default exists
DMAMEM String CFG_Profile_Map_SSB_20M  = "SSB 20m";
DMAMEM String CFG_Profile_Map_SSB_17M  = "SSB 17m";
DMAMEM String CFG_Profile_Map_SSB_15M  = "SSB 15m";
DMAMEM String CFG_Profile_Map_SSB_12M  = "SSB 12m";
DMAMEM String CFG_Profile_Map_SSB_10M  = "SSB 10m";
DMAMEM String CFG_Profile_Map_SSB_6M   = "SSB 6m";

// FM (only meaningful on 10m and 6m, but defaults provided for completeness)
DMAMEM String CFG_Profile_Map_FM_160M = "FM 160m";
DMAMEM String CFG_Profile_Map_FM_80M  = "FM 80m";
DMAMEM String CFG_Profile_Map_FM_60M  = "FM 60m";
DMAMEM String CFG_Profile_Map_FM_40M  = "FM 40m";
DMAMEM String CFG_Profile_Map_FM_30M  = "FM 30m";
DMAMEM String CFG_Profile_Map_FM_20M  = "FM 20m";
DMAMEM String CFG_Profile_Map_FM_17M  = "FM 17m";
DMAMEM String CFG_Profile_Map_FM_15M  = "FM 15m";
DMAMEM String CFG_Profile_Map_FM_12M  = "FM 12m";
DMAMEM String CFG_Profile_Map_FM_10M  = "FM 10m";
DMAMEM String CFG_Profile_Map_FM_6M   = "FM 6m";

// DIGU (kept even if hidden by default)
DMAMEM String CFG_Profile_Map_DIGU_160M = "DIGU 160m";
DMAMEM String CFG_Profile_Map_DIGU_80M  = "DIGU 80m";
DMAMEM String CFG_Profile_Map_DIGU_60M  = "DIGU 60m";
DMAMEM String CFG_Profile_Map_DIGU_40M  = "DIGU 40m";
DMAMEM String CFG_Profile_Map_DIGU_30M  = "DIGU 30m";
DMAMEM String CFG_Profile_Map_DIGU_20M  = "DIGU 20m";
DMAMEM String CFG_Profile_Map_DIGU_17M  = "DIGU 17m";
DMAMEM String CFG_Profile_Map_DIGU_15M  = "DIGU 15m";
DMAMEM String CFG_Profile_Map_DIGU_12M  = "DIGU 12m";
DMAMEM String CFG_Profile_Map_DIGU_10M  = "DIGU 10m";
DMAMEM String CFG_Profile_Map_DIGU_6M   = "DIGU 6m";

TM_ConnectionMode CFG_ConnMode = TM_CONN_AUTO;
byte CFG_FlexIp[4] = {0, 0, 0, 0};
String CFG_FlexHost;

int CFG_FlexControlPort = UDP_DISCOVERING_PORT; // 4992 (TCP control)

// --- Time / Clock config (from INI) ---
bool   CFG_ShowDateTime     = true;
String CFG_TimeZone         = "UTC";
String CFG_DateFormat       = "%a %d %b %Y";
String CFG_TimeFormat       = "%H:%M:%S";
bool   CFG_ImperialUnits    = false;
String CFG_NTPServer        = "pool.ntp.org";

double CurFreq[2]       = { 0.0, 0.0 };
DMAMEM String CWFilterDisp[8]  = { "50", "100", "250", "400", "500", "800", "1.0K", "3.0K" };
const int CWFilterWidth[8]    = { 50, 100, 250, 400, 500, 800, 1000, 3000 };
int CWMaxFilterItems    = 8;
bool CWMenuOpt          = true;
bool CWMsgMenuOpt       = true;
volatile int CWVal      = 20;
volatile int CWValSave  = 20;
volatile int CWDelay    = 0; // Apply CW Delay only if configured (non-zero)
bool ShowContestSerialNumber = false;

// ---------- VFO Acceleration (runtime-tunable via config) ----------
float VFOAccel_OnFactor      = 1.6f;   // engage earlier when lower
float VFOAccel_OffFactor     = 1.5f;   // hold longer when higher
int   VFOAccel_MinDeadband   = 3;      // small-tail ignore zone on count EMA
int   VFOAccel_CountEmaDen   = 2;      // smoothing for counts (lower = snappier)
int   VFOAccel_AccelEmaDen   = 4;      // smoothing for accel term (higher = smoother)
int   VFOAccel_MaxUpMult     = 60;    // accel rise limit per tick (x Step)
int   VFOAccel_MaxDownMult   = 20;     // accel fall limit per tick (x Step)
int   VFOAccel_MaxAbsHz      = 20000;  // absolute safety cap for accel term

bool DidItOnce        = false;
bool DisableGUIClient = false;
int DEXPLevel;
int DEXPLevelSave;

DMAMEM String DFMFilterDisp[8] = { "6.0K", "8.0K", "10K", "12K", "14K", "16K", "18K", "20K" };
const int DFMFilterHigh[8]    = { 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
const int DFMFilterLow[8]     = { -3000, -4000, -5000, -6000, -7000, -8000, -9000, -10000 };
int DFMMaxFilterItems   = 8;

DMAMEM String DigiFilterDisp[8] = { "100", "300", "600", "1.0K", "1.5K", "2.0K", "3.0K", "6.0K" };
const int DigiFilterHigh[8]    = { 1550, 1650, 1800, 2000, 2250, 2500, 3000, 6000 };
const int DigiFilterLow[8]     = { 1450, 1350, 1200, 1000, 750, 500, 0, 0 };
int DigiMaxFilterItems   = 8;

DMAMEM String DispFreq[2];

int Encoder_9      = 0;  // Default = CW Speed
DMAMEM String Enc9_Text[] = { "CW Speed", "Mic Gain", "RF Power", "Tune Power", "WNB Level", "Mon Level", "VOX Level", "VOX Delay", "Band" };

int VolAEncSteps  = 1;
int VolBEncSteps  = 1;
int AGCAEncSteps  = 1;
int AGCBEncSteps  = 1;
int LowAEncSteps  = 1;
int LowBEncSteps  = 1;
int HighAEncSteps = 1;
int HighBEncSteps = 1;
int CWEncSteps    = 1;

bool FilterMenu = true;
int FilterMenuIDX;

bool GotBtn        = false;
bool GotCWMsgMenu  = false;
bool GotFilterMenu = false;
bool GotSpeedParm  = false;
bool GotTouch      = false;

int HighVal[2];
int HighValSave[2];

bool InBand = false;
String InBuf;
bool InSetup = true;

int LastMenuIDX          = -1;
int LastMenuItemIDX      = 0;
int LClass               = 0;
DMAMEM String LClassText[5]     = { "Extra", "Advanced", "General", "Technician", "Novice" };

unsigned int LongPress = 350;
bool LongPressClick    = false;
String LongPressInd;

int LowVal[2];
int LowValSave[2];

//Ethernet
byte mac[6];

int MemMenuStart;
DMAMEM String Memories[200];
bool MemoryMenu = true;
bool MenuActive = false;
int MenuIDX     = 0;
DMAMEM int    MenuAction[30][15];
DMAMEM String MenuItem[30][15];
int MenuItemIDX = 0;
DMAMEM String MenuTitle[30];
int MicGain;
int MicGainSave;
bool MicGainMenuActive  = false;
DMAMEM String MicProf[2]       = { "Len FHM-3", "Len CM500" };
bool MicProfOvr         = false;
volatile bool MicSelInt = true;
int MiscMenu;
DMAMEM String ModeMenu[11] = { "CW", "LSB", "USB", "DIGL", "DIGU", "RTTY", "AM", "SAM", "FM", "NFM", "DFM" };
bool ModeMenuOn     = true;
int MonLevel;
int MonLevelSave;
byte MuteVal[2] = { 0, 0 };
int MUXidx;
String MyCall;
String MyLicense;
int MyGateway[4] = { 169, 254, 0, 1 };
int Myip[4]      = { 169, 254, 0, 17 };
int MyMask[4]    = { 255, 255, 0, 0 };
int MyDNS[4]     = {0,0,0,0};

int NBVal[2];
int NBValSave[2];
int NRVal[2];
int NRValSave[2];

String OOBI;
bool OOBindicator = false;
int OOBSpotTime   = 5;
int Ovr[2];
int OvrLast[2];
int OvrPeak[2];
static bool     OOB_lastWasOOB        = false;
static uint32_t OOB_lastSpotMs        = 0;
static const uint32_t OOB_minInterval = 800;

unsigned long PingTimer = 0;
String PowerBtn         = "POWER";
String Profile;
bool ProfileMenu = true;

unsigned long QueryTimer;

char Rchar;
int RecentMenuIDX = 0;
int RFPower;
bool RFPowerMenuActive = false;
int RFPowerSave;
int RITVal[2];
int RITValSave[2];

DMAMEM String RTTYFilterDisp[8] = { "270", "300", "350", "400", "500", "1.0K", "1.5K", "3.0K" };
const int RTTYFilterHigh[8]    = { 50, 65, 90, 115, 165, 415, 665, 1415 };
const int RTTYFilterLow[8]     = { -220, -235, -260, -285, -335, -585, -835, -1585 };
const int RTTYMaxFilterItems   = 8;

String RXAnt = "ANT1";

unsigned long ScreenSave = 1200 * 1000;
bool ScreenSaveActive    = false;
unsigned long ScreenSaveTimer;
unsigned long SelectedTimer  = 0;
unsigned int SelectedTimeout = 5000;
bool SerialStarted           = false;
int SetFreq[2];
int SetFreqSave[2];
bool SetNB[2]  = { false, false };
bool SetNR[2]  = { false, false };
bool SetRIT[2] = { false, false };
bool SetXIT[2] = { false, false };

bool ShortPress;
bool ShortPressClick = false;
String ShortPressInd;
bool ShowSpots = true;
int SliceActiveVal;
int SMeter[2];
int SMeterHold[2];
int SMeterLast[2];
int SMeterPeak[2];
unsigned long SMeterRef;
unsigned long SMTimeIt;
bool SnapToStep       = false;
String SnapToSteptxt  = "OFF";
unsigned int SPIClock = 20000000;
bool Splash           = true;
bool SplashM          = false;
unsigned long SplashTimeIt;
unsigned int SplashTimer;
int SpotIDX     = 0;
bool StandAlone = false;
DMAMEM String SpotFreq[100];
DMAMEM String SpotText[100];
int StartUpDelay    = 250;
unsigned int STFreq = 800;
unsigned int STFreqSave;
bool STFreqMenuActive = false;
String STtxt          = "ON";

DMAMEM String SSBFilterDisp[8] = { "1.8K", "2.1K", "2.4K", "2.7K", "2.9K", "3.3K", "4.0K", "6.0K" };
const int SSBFilterHigh[8]    = { 1900, 2200, 2500, 2800, 3000, 3400, 4100, 6100 };
const int SSBFilterLow[8]     = { 100, 100, 100, 100, 100, 100, 100, 100 };
const int SSBMaxFilterItems   = 8;

float tempC;
float tempF;
unsigned long TempTimer;
unsigned long TimeIt;
String TMID;
int tmpAccelFactor;
int tmpSTFreq;
unsigned long TouchTime;
uint16_t TPX;
uint16_t TPY;
uint8_t TPZ;

uint16_t TSCX;
uint16_t TSCY;
uint16_t TSCZ;
uint16_t TSCZ1;
int TransmitMenu;
bool TransmitMenuOn = true;
int TunePower;
int TunePowerSave;
int TuningRateCW[2]  = { 10, 10 };
int TuningRateSSB[2] = { 40, 40 };
String TXAnt         = "ANT1";
int TXSlice;

bool VFOaccel      = true;
String VFOaccelInd = "ON";
volatile int VFOAccelCount[2];
int VFODir[2];
int VFODirSave[2];
int VFOStep[2]     = { 3, 3 };
int VFOStepSSB[2]  = { 3, 3 };
int VFOStepCW[2]   = { 1, 1 };
bool VFOTrack      = false;
String VFOTrackInd = "OFF";
volatile int VFOTuningRate[2];
volatile int VFOTuningRateSave[2];
long VFOVal[2] = { 0, 0 };

int VolVal[2];
int VolValSave[2];
int VOXDelay;
int VOXDelaySave;
int VOXLevel;
int VOXLevelSave;

int WNBLevel;
int WNBLevelSave;

int XITVal[2];
int XITValSave[2];

/***************************** End TeensyMaestro Global Variables ***************************/

/***************************** Begin Button Variables ***************************************/
const int IOX_TFT_LCD = 7;  // NV0E - IOExpander pin for TFT LCD backlight

// NV0E - GPIO direction definitions
// These are used to set the direction of the GPIO pins for the buttons
enum GpioDirection
{
  GPIO_NONE,
  GPIO_INPUT,
  GPIO_OUTPUT
};

// NV0E - Button names
// These are used to identify the button pressed and to map it to a GPIO pin
enum ButtonName
{
  BTN_NONE,
  BTN_VFO_A_MUTE_SLICE,
  BTN_VFO_A_RIT_SEL,
  BTN_VFO_A_NB,
  BTN_VFO_A_NR,
  BTN_VFO_A_RIT,
  BTN_VFO_A_XIT,
  BTN_MENU_SEL,
  BTN_VFO_B_MUTE_SLICE,
  BTN_VFO_B_RIT_SEL,
  BTN_VFO_B_NB,
  BTN_VFO_B_NR,
  BTN_VFO_B_RIT,
  BTN_VFO_B_XIT,
  BTN_PTT,
  BTN_TUNE,
  BTN_ACC_0,
  BTN_ACC_1,
  BTN_ACC_2,
  BTN_ACC_3,
  BTN_ACC_4,
  BTN_ACC_5,
  BTN_ACC_6,
  BTN_ACC_7,
  BTN_CW_MSG_1,
  BTN_CW_MSG_2,
  BTN_CW_MSG_3,
  BTN_CW_MSG_4,
  BTN_CW_MSG_5,
  BTN_CW_MSG_6,
  BTN_ACC_8,  // PTT A
  BTN_ACC_9   // PTT B
};

// NV0E - button names (if an input) and GPIO directions for each MUX pin
struct GpioType
{
  ButtonName name;
  GpioDirection gpioDirection;
};

// NV0E - GPIO pin definitions for each MUX pin
// These arrays define the GPIO device connected to each MUX pin
// Each array contains ButtonName (if an input) and GpioDirection pairs for an IOExpander.
GpioType muxA_arr[] = {
  {BTN_VFO_A_MUTE_SLICE,  GPIO_INPUT},
  {   BTN_VFO_A_RIT_SEL,  GPIO_INPUT},
  {        BTN_VFO_A_NB,  GPIO_INPUT},
  {        BTN_VFO_A_NR,  GPIO_INPUT},
  {       BTN_VFO_A_RIT,  GPIO_INPUT},
  {       BTN_VFO_A_XIT,  GPIO_INPUT},
  {        BTN_MENU_SEL,  GPIO_INPUT},
  {            BTN_NONE, GPIO_OUTPUT}
};
GpioType muxB_arr[] = {
  {BTN_VFO_B_MUTE_SLICE, GPIO_INPUT},
  {   BTN_VFO_B_RIT_SEL, GPIO_INPUT},
  {        BTN_VFO_B_NB, GPIO_INPUT},
  {        BTN_VFO_B_NR, GPIO_INPUT},
  {       BTN_VFO_B_RIT, GPIO_INPUT},
  {       BTN_VFO_B_XIT, GPIO_INPUT},
  {             BTN_PTT, GPIO_INPUT},
  {            BTN_TUNE, GPIO_INPUT}
};
GpioType muxC_arr[] = {
  {   BTN_ACC_0, GPIO_INPUT},
  {   BTN_ACC_1, GPIO_INPUT},
  {   BTN_ACC_2, GPIO_INPUT},
  {   BTN_ACC_3, GPIO_INPUT},
  {   BTN_ACC_4, GPIO_INPUT},
  {   BTN_ACC_5, GPIO_INPUT},
  {   BTN_ACC_6, GPIO_INPUT},
  {   BTN_ACC_7, GPIO_INPUT},
  {BTN_CW_MSG_1, GPIO_INPUT},
  {BTN_CW_MSG_2, GPIO_INPUT},
  {BTN_CW_MSG_3, GPIO_INPUT},
  {BTN_CW_MSG_4, GPIO_INPUT},
  {BTN_CW_MSG_5, GPIO_INPUT},
  {BTN_CW_MSG_6, GPIO_INPUT},
  {   BTN_ACC_8, GPIO_INPUT},
  {   BTN_ACC_9, GPIO_INPUT}
};

// NV0E - IOExpander pin definitions
// These are the GPIO pin numbers for each button
struct PinData
{
  int mux;
  int pin;
  String name;
};

// A map to look up the mux, pin and name using the ButtonName
std::map<ButtonName, PinData> buttonMap;


/***************************** End Button Variables *****************************************/

/***************************** Keyer Global Variables ***************************/
volatile bool AbortMsg = true;  // Prevent Dash being sent at startup
byte ActivePin;

volatile byte CurElement = 0;
bool CutZero    = false;
int CWMenu;
int CWMsgNum     = 0;
DMAMEM String CWMsg[12] = { " ", " ", " ", " ", " ", " ", " ", " ", " ", " ", " ", " " };
int CWMsgMenu;
DMAMEM String CWMsgSource = "Teensy";

volatile unsigned int CWIndex = 0;

volatile bool DashDown    = false;
byte DashPin              = 31;  // Not a const so that can be reassigned right/left
volatile byte DashPinVal  = HIGH;
volatile bool DashPressed = false;
volatile bool DotDown     = false;
byte DotPin               = 30;  // Not a const so that can be reassigned right/left
volatile byte DotPinVal   = HIGH;
volatile bool DotPressed  = false;

volatile long ElementLen  = 1;
volatile bool ElementWait = false;

bool GoodParm;
bool GotParm = false;

int Handed = 0;  // right = 0, left = 1
String HandedTxt;

volatile bool KeyDown    = false;
String KeyerOut          = "LOCAL";  // LOCAL, ETHERNET
String KeyMode           = "B";      //A, B, U
volatile bool KeyPressed = false;

int LastHanded;
char LastKeyMode;
int LastWPM = 0;

// To do: get rid of MorseLen and instead, add a start bit to MorseVal & reverse bit order
int MorseIndex;  //  0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5  6  7  8  9  0  1
//                   A  B  C  D  E  F  G  H  I  J  K  L  M  N  O  P  Q  R  S  T  U  V  W  X  Y  Z  0  1  2  3  4  5  6  7  8  9  /  =  ?  ,  .  _

// Extended Morse tables:
// A..Z (0..25), 0..9 (26..35), punctuation (36..42), '+' (43),
// Å(44), Ä(45), Ö(46), Ü(47), '@' (48).
// Encoding: LSB-first per element (DOT=0, DASH=1); MorseLen defines active bits.

static const uint8_t MorseLen[49] = {
  // A..Z
  2,4,4,3,1,4,3,4,2,4,3,4,2,2,3,4,4,3,3,1,3,4,3,4,4,4,
  // 0..9
  5,5,5,5,5,5,5,5,5,5,
  // '/', '=', '?', ',', '.', '_', '-'
  5,5,6,6,6,1,6,
  // '+'
  5,
  // Å, Ä, Ö, Ü, '@'
  5,4,4,4,6
};

static const uint8_t MorseVal[49] = {
  // A..Z
  0b10, 0b1, 0b101, 0b1, 0b0, 0b100, 0b11, 0b0, 0b0, 0b1110,
  0b101, 0b10, 0b11, 0b1, 0b111, 0b110, 0b1011, 0b10, 0b0, 0b1,
  0b100, 0b1000, 0b110, 0b1001, 0b1101, 0b11,

  // 0..9
  0b11111, 0b11110, 0b11100, 0b11000, 0b10000, 0b0, 0b1, 0b11, 0b111, 0b1111,

  // '/', '=', '?', ',', '.', '_', '-'
  0b1001, 0b10001, 0b1100, 0b110011, 0b101010, 0b1, 0b100001,

  // '+'
  0b10101,

  // Å (.--.-), Ä (.-.-), Ö (---.), Ü (..--), '@' (.-.-.-)
  0b10110, 0b1010, 0b111, 0b1100, 0b110101
};

static_assert(sizeof(MorseLen) == 49, "MorseLen size mismatch");
static_assert(sizeof(MorseVal) == 49, "MorseVal size mismatch");

DMAMEM String Msg[12];
volatile bool MsgActive = false;

int NextSerNum = 0;  // CW contest serial number

bool OldKeyDown = false;

bool ProSign = false;

unsigned int R = 0;
bool RptActive = false;

int SaveBkInDelay     = 0;
int SerNum            = 1;
int SerNumSave        = 1;
bool SerNumMenuActive = false;
//bool SetSideTone = false;
bool SideTone                   = true;
int SideToneFreq                = 800;
int SideToneFreqSave            = 800;
volatile bool StraightKeyActive = false;
long int StraightKeyDebounce;
volatile byte StraightKeyPinVal;

int tmpSerNum;
int WordSp       = 1;
volatile int WPM = 24;

bool XmitOn;

IntervalTimer DotTimer;
//IntervalTimer DebounceTimer;
//IntervalTimer SideToneTimer;

/***************************** End Keyer Global Variables ***************************/


// Use hardware SPI.  MOSI = pin 11, MISO = pin 12, SCK = pin 13
// Create the TFT
//ST7796_t3 tft = ST7796_t3(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
// tft instance is provided by Display_Driver.cpp
// (access it via #include "Display_Driver.h")
// Create the touch screen
XPT2046_Touchscreen touch = XPT2046_Touchscreen(TS_CS);
// Create the I/O Expanders
Adafruit_MCP23X08 muxA;
Adafruit_MCP23X08 muxB;
Adafruit_MCP23X17 muxC;
bool muxA_found = false;
bool muxB_found = false;
bool muxC_found = false;

File ConfigFile;

FlexRig* g_fRig = nullptr;
DMAMEM __attribute__((aligned(32))) uint8_t fRig_buf[sizeof(FlexRig)];

// Transmit Transmit;

Encoder AGCAEnc(AGCAPin1, AGCAPin2);
Encoder AGCBEnc(AGCBPin1, AGCBPin2);

Encoder CWMicEnc(CWMicPin1, CWMicPin2);

Encoder HighAEnc(HighAPin1, HighAPin2);
Encoder HighBEnc(HighBPin1, HighBPin2);

Encoder LowAEnc(LowAPin1, LowAPin2);
Encoder LowBEnc(LowBPin1, LowBPin2);

Encoder VFOAEnc(VFOAPin1, VFOAPin2);
Encoder VFOBEnc(VFOBPin1, VFOBPin2);

Encoder VolAEnc(VolAPin1, VolAPin2);
Encoder VolBEnc(VolBPin1, VolBPin2);

// Hook used by tm_touch.h to write the menu encoder position
void TM_MenuEncoderWrite(int value) {
  CWMicEnc.write(value);
}

// Guarantees panel/backlight is OFF and peripherals are quiet before cutting power.
// Never returns.

__attribute__((noreturn))
void power_down(const char* reason)
{
  // --- Last log to help post-mortem ---
  if (reason && *reason) {
    Serial.print("[POWER] Shutting down: ");
    Serial.println(reason);
  } else {
    Serial.println("[POWER] Shutting down");
  }
  Serial.flush();
  delayMicroseconds(50);     // Give USB/Serial a nudge to flush

  // --- Silence peripherals here (expand as needed) ---
  // 1) Force TFT backlight OFF via IO expander (HIGH = on, LOW = off)
  //    Uses existing global: const int IOX_TFT_LCD = 7;
  //    Assumes global 'muxA' IO-expander instance is available.
  muxA.digitalWrite(IOX_TFT_LCD, LOW);

  // 2) If your display driver supports sleep/off, do it here (optional).
  // Display_Sleep();                 // <-- uncomment if available
  // Display_Backlight(false);        // <-- uncomment if available

  // Short settle so rails/LED driver see the command before SNVS cut
  delay(2);

  // --- Kill power domains via SNVS (does not return) ---
  arm_power_down();

  // Safety fallback if the call would ever return (it shouldn't)
  while (1) { __asm__ volatile("wfi"); }
}

/***************************** setup ***************************/
FLASHMEM void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(StartUpDelay);  // Let the MUX and display power settle a bit.
  digitalWrite(LED_BUILTIN, LOW);

  g_fRig = new (fRig_buf) FlexRig();
  if (!g_fRig) { while (1) {} }  // should never happen, but prevents UB  

#ifdef USE_CRASH_REPORT
  Serial.begin(BAUD_RATE);
  pinMode(LED_BUILTIN, OUTPUT);
  const uint32_t wait_ms = 12000; // wait up to 12 seconds for a Serial Monitor connection, MTP needs a bit more time
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0 < wait_ms)) {
    digitalWriteFast(LED_BUILTIN, (millis() >> 6) & 1); // blink LED while waiting
    yield(); // allow background tasks to run
  }
  digitalWriteFast(LED_BUILTIN, LOW);
  delay(50); // let Serial stabilize
  if (CrashReport) {
    Serial.println("\n*** CrashReport detected on last reboot ***");
    Serial.print(CrashReport);   // visar fault typ, adress, stack etc
    delay(10000);
  }
#endif

  for (int i=0;i<3;i++){ digitalWrite(LED_BUILTIN,HIGH); delay(150); digitalWrite(LED_BUILTIN,LOW); delay(150); }

  tone(STPin, 1000, 100);  // power button beep

  storage_configure();

  GetConfigFile();

  TM_WK_Bridge::beginFromConfig();
  TM_WK_TraceBegin();
  delay(StartUpDelay);

  TM_RCS::begin();   // Start remote command TCP server if enabled in config
  TeensyMaestroSetup();
  KeyerSetup();

  // Reserve small buffers for all menu strings to reduce heap fragmentation in Debug builds
  for (int i = 0; i < 30; ++i) {
    MenuTitle[i].reserve(32);
    for (int j = 0; j < 15; ++j) {
      MenuItem[i][j].reserve(32);
    }
  }
  if (VFOaccel) {
    // Call VFOAccelISR every 1000 µs (~1 kHz)
    Accel.begin(VFOAccelISR, 1000);
    // Optional: set priority if needed (0..255, lower = higher priority on some Teensy cores)
    // Accel.priority(128);
  }

  g_systemReady = true;
  debugln("Leaving Setup");
}  // End setup

bool FlexIsHeadless() {
  // Headless = DisableGUIClient && no external GUI client
  return fRig.isEffectiveHeadless();
}

// Debounced MicSel processing (called from loop) ---
static void ProcessMicSelDebounce()
{
  if (!g_micSelPending) return;

  const uint32_t now = millis();
  if (now - g_micSelStampMs < MICSEL_DEBOUNCE_MS) return;

  // Clear pending once we are past the debounce window
  g_micSelPending = false;

  // Mirror the original behavior precisely:
  const bool inUse = (fRig.slice[TXSlice].in_use != 0);
  const String& mode = fRig.slice[TXSlice].mode;

  const bool isVoice =
      (mode == "LSB" || mode == "USB" || mode == "AM"  ||
       mode == "SAM" || mode == "FM"  || mode == "NFM" || mode == "DFM");

  if (inUse && isVoice)
  {
    // Read the switch AFTER debounce, same as original ISR did
    fRig.send("profile mic load \"" + MicProf[digitalRead(MicSelPin)] + "\"");
  }
  else
  {
    // Preserve legacy signaling in non-voice modes
    MicSelInt = true;
  }
}

#include "tm_enc9_handlers.h"
#include "tm_enc_agc_bw_handlers.h"
#include "tm_enc_vfo_vol_handlers.h"

/***************************** loop ***************************/
void loop()
{
  
  MTP.loop();  // Allows PC to edit SD card over USB.

  TMTime_loop();
  ClockWidget_Loop();

  KeyerLoop();
  
  TM_WK_Bridge::poll();
  TM_RCS::poll();
  
  ProcessMicSelDebounce();  // handle debounced MicSel outside ISR

  if (s_gatedLogged) ReadCWMicEnc();

  if (fRig.connected)
  {
    {
      fRig.process(); // Do process() first to ingest/parse bytes from the radio,
      fRig.fireEvents(); // then fireEvents() to dispatch handlers.
    }

    ReadAgcAEnc();
    ReadAgcBEnc();

    ReadLowAEnc();  // Poll Slice A Low/Shift encoder
    ReadLowBEnc();  // Poll Slice B Low/Shift encoder

    ReadHighAEnc();  // Poll Slice A High/Shift encoder
    ReadHighBEnc();  // Poll Slice B High/Shift encoder

    TM_VFOAccel_Process();

    ReadVFOEnc(A);  // Poll Slice A VFO encoder
    ReadVFOEnc(B);  // Poll Slice B VFO encoder

    ReadVolAEnc();  // Poll Slice A volume encoder
    ReadVolBEnc();  // Poll Slice B volume encoder

    if (MicSelInt)  // Mic switch was thrown while in a non-voice mode
    {
      if (MicProfOvr)
      {
        // clang-format off
        if (fRig.slice[TXSlice].in_use &&
          (fRig.slice[TXSlice].mode == "LSB" ||
          fRig.slice[TXSlice].mode == "USB" ||
          fRig.slice[TXSlice].mode == "AM" ||
          fRig.slice[TXSlice].mode == "SAM" ||
          fRig.slice[TXSlice].mode == "FM" ||
          fRig.slice[TXSlice].mode == "NFM" ||
          fRig.slice[TXSlice].mode == "DFM"))
        // clang-format on
        {
          fRig.send("profile mic load \"" + MicProf[digitalRead(MicSelPin)] + "\"");
          MicSelInt = false;
        }
      }
    }
  }

  // Scan for pressed buttons
  if (!StraightKeyActive)
  {
    GetIOExpanderButton();
  }

  if (GotTouch)  // GotTouch = true if STMPE610 controller is present
  {
    if (touch.touched())
    {
      TS_Point p = touch.getPoint();
      // Optional: peek coordinates/pressure for debugging spurious wakeups
      #if 0
      Serial.print("Touch raw: x=");
      Serial.print(p.x);
      Serial.print(" y=");
      Serial.print(p.y);
      Serial.print(" z=");
      Serial.println(p.z);
      #endif

      // Z-gate: ignore weak/ghost touches below threshold
      const int TOUCH_Z_THRESHOLD = 1600;
      if (p.z < TOUCH_Z_THRESHOLD)
      {
        debugln("Touch gate: z below threshold, ignoring touch");
        return;  // Do not wake or bump timer on weak/ghost touch
      }

      if (ScreenSaveActive)
      {
        // Wake from screensaver due to touch input
        ResetScreenSaver("Touch: wake from screensaver");
      }
      else
      {
        // Touch while screen is already active: bump screensaver timer
        ResetScreenSaver("Touch: active screen");
        TouchDispatch();
      }
    }
  }

  if (Splash && !SplashM)
  {
    // Leaving splash screen, treat as user activity + wake if needed
    ResetScreenSaver("Splash exit");
    Splash = false;
    RefreshScreen();
  }

  if (millis() - ScreenSaveTimer > ScreenSave)
  {
    if (!ScreenSaveActive)
    {
      ScreenSaveActive = true;
      tft.fillScreen(COLOR_BLACK);
      muxA.digitalWrite(IOX_TFT_LCD, LOW);  // NV0E
    }
  }

  if (KeyPressed && ScreenSaveActive)
  {
    KeyPressed = false;
    // Wake from screensaver due to keyer activity
    ResetScreenSaver("Main INO: Keyer wake from screensaver");
  }

/*
CW WPM sync – observed Flex/SmartSDR behavior and TM policy
-----------------------------------------------------------

1) Flex 'transmit.speed' often reports a default snapshot of 30 WPM at startup,
   at SmartSDR disconnect, and sometimes during mode churn. It does NOT always
   represent the user’s last CW speed. In contrast, 'cwx.wpm' reflects the real
   persisted CW speed (e.g. 15 WPM) and typically arrives shortly after login.

2) When TM starts BEFORE SmartSDR, 'transmit.speed' = 30 can arrive early and,
   if blindly adopted, TM gets "stuck" at 30 WPM. The UI may flicker because the
   loop re-applies this snapshot repeatedly until 'cwx.wpm' shows up.

3) Slice mode churns briefly on connect (CW -> LSB -> ...). Some early slice
   events have empty mode (“”). We must gate WPM sync on a stable CW mode.

Policy:
- Treat 'cwx.wpm' as the authoritative source for CW speed at init.
- Never adopt 'transmit.speed' unless:
    a) we are NOT headless,
    b) TX slice mode is confirmed "CW",
    c) value is sane (>0) AND != suspicious default 30, and
    d) we've already seen at least one 'cwx.wpm' OR we are actively in CW TX.
- Do NOT push CW speed back to radio while not in CW mode.
- On SmartSDR disconnect or when mode is non-CW, freeze local CWVal (no adopt).
- Debounce duplicate 'cwx.wpm' bursts (e.g. transient 5) by time or count.
*/

  if (fRig.connected)
  {
    int applied = -1;

    if (GotSpeedParm) {
      GotSpeedParm = false;
      // Force a resync now that we know a speed parameter was involved.
      TMU_SyncCwWpm(/*preserveBaseline=*/false, &applied, "loop:parm");
    } else {
      // Normal periodic sync; harmless if nothing changes (debounced inside).
      TMU_SyncCwWpm(/*preserveBaseline=*/false, &applied, "loop");
    }
    // 'applied' >= 0 means a change was applied

    if (fRig.Current_Profile != Profile)
    {
      DispProfile();
    }

    if (millis() > SMeterRef + 50)  // Update S Meter every 50 ms
    {
      DispSMeter();
      SMeterRef = millis();
    }

    if ((SelectedTimeout > 0) && (millis() - SelectedTimer > SelectedTimeout) && SelectedTimer != 0)
    {
      for (int Slice = 0; Slice < 2; Slice++)
      {
        SetNB[Slice]  = false;
        SetNR[Slice]  = false;
        SetRIT[Slice] = false;
        SetXIT[Slice] = false;

        LowAEnc.write(LowVal[A] * LowAEncSteps);
        LowBEnc.write(LowVal[B] * LowBEncSteps);
        HighAEnc.write(HighVal[A] * HighAEncSteps);
        HighBEnc.write(HighVal[B] * HighBEncSteps);
        AGCAEnc.write(fRig.slice[A].agc_threshold * AGCAEncSteps);
        AGCBEnc.write(fRig.slice[B].agc_threshold * AGCBEncSteps);


        DispNB(Slice);
        DispNR(Slice);

        DispRIT(Slice);
        DispXIT(Slice);

        SelectedTimer = 0;
      }
    }

  }
  else
  {
    if (!StandAlone)
    {
      TimeIt = millis();
      while (!fRig.connected)
      {
        fRig.connect();
        delay(500);

        if (millis() - TimeIt > 5000)  // Needs time before it will connect. Keep trying for up to 5 seconds.
        {
          power_down("connect timeout >5s");
        }
      }
    }
  }
  s_gatedLogged = true;

  GotBtn   = false;  // Don't leave button rtn hung after aborting a CW msg
  AbortMsg = false;

  // ---- start spot pusher once, after setup is complete and rig is connected ----
  if (!g_SpotsStarted && fRig.connected && ShowSpots) {
    SpotPusher::start(SpotFreq, SpotText, SpotIDX);
    g_SpotsStarted = true;
    debugf("SpotPusher: started (%d spots, 1/sec)\n", SpotIDX);
  }

  SpotPusher::tick();
  ProfileSel::tick();

}  // End loop

/***************************** Subroutines ***************************/


/*==============================================================================
  CheckInBand
  -----------
  Purpose
    Validate that the (TX) slice's transmit footprint is within band limits.

  Returns
    true  : the footprint is within the band plan
    false : out-of-band

  Behaviour
    - If sync_ui == true:
        * updates global InBand
        * may post the Out_Of_Band spot with debounce
        * refreshes UI fields (DispLicense/DispFrq)
      If sync_ui == false:
        * does NOT touch UI, does NOT post spots, does NOT change InBand
        * pure computation path for preflight checks (e.g., toggles)

  Notes
    - Only evaluates TX slice.
    - Edges are inclusive (>= / <=).
==============================================================================*/
bool CheckInBand(int Slice, bool sync_ui)
{
  // If not TX slice, treat as valid for the purpose of the caller.
  if (!fRig.slice[Slice].tx) return true;

  // Shorthands
  const String &mode = fRig.slice[Slice].mode;
  const long    F0   = fRig.slice[Slice].RF_frequency;

  // Compute transmit footprint
  long FreqL = F0;
  long FreqH = F0;

  // PHONE-like expansion (AM/FM/NFM both sides)
  if (mode == "LSB" || mode == "DIGL" || mode == "AM" || mode == "FM" || mode == "NFM")
    FreqL = F0 - fRig.transmit.hi;

  if (mode == "USB" || mode == "DIGU" || mode == "AM" || mode == "FM" || mode == "NFM")
    FreqH = F0 + fRig.transmit.hi;

  // RTTY: 170 Hz shift + 100 Hz guard
  if (mode == "RTTY") {
    FreqL = F0 - 270;
    FreqH = F0 + 100;
  }

  // CW: ±100 Hz
  if (mode == "CW") {
    FreqL = F0 - 100;
    FreqH = F0 + 100;
  }

  // PHONE rows = odd indices; CW/DIGI rows = even
  const bool phoneMode = (mode == "LSB" || mode == "USB" || mode == "AM" || mode == "FM" || mode == "NFM");
  const int  startRow  = phoneMode ? 1 : 0;

  bool inBand = false;
  for (int i = startRow; i < BandTable_Max; i += 2) {
    if ((FreqL >= BandTable[i][LClass]) && (FreqH <= BandTable[i][LClass + 1])) {
      inBand = true;
      break;
    }
  }

  // If we are not syncing UI, just return the computation
  if (!sync_ui) {
    return inBand;
  }

  // --- UI/spot side-effects (original behaviour) -----------------------------
  // Respect feature flag when syncing UI
  if (!OOBindicator) {
    InBand = true;    // de facto "consider in band" when indicator off
    return true;
  }

  // Update InBand global for display
  InBand = inBand;

  // Post Out_Of_Band spot with debounce if needed
  const bool oobNow    = !InBand;
  const uint32_t nowMs = millis();

  if (oobNow && (!OOB_lastWasOOB || (nowMs - OOB_lastSpotMs) >= OOB_minInterval)) {
    fRig.send("spot add rx_freq=" + DispFreq[Slice] +
              " callsign=**Out_Of_Band** color=#FFFFFFFF lifetime_seconds=" + OOBSpotTime +
              " priority=1 comment=Out_Of_Band");
    OOB_lastSpotMs = nowMs;
  }
  OOB_lastWasOOB = oobNow;

  // Refresh UI fields
  DispLicense();
  DispFrq(A);
  DispFrq(B);

  return inBand;
}

// Backward-compatible wrapper for legacy call sites
void CheckInBand(int Slice)
{
    (void)CheckInBand(Slice, /*sync_ui=*/true);
}

/***************************** GetEEPROM ***************************/
void GetEEPROM()
{
  EEPROM.get(EEAddr, SerNum);
  delay(50);

  if (SerNum < 1)
  {
    SerNum = 1;
    TM_SerNum_SaveEEPROM();
  }

  SerNumSave = SerNum;
}

/*********************** Interrupt Service Routines *********************/

// Keep ISR minimal: timestamp + flag only.
void MicSelISR()
{
  g_micSelStampMs = millis();
  g_micSelPending = true;
}

void VFOAccelISR()
{
  // Minimal ISR: flag only. Encoder code updates VFOAccelCount[] elsewhere.
  if (!VFOaccel) return;
  g_vfoAccelPending = true;
}
