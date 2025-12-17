// tz_setup.cpp
#include <AceTime.h>
#include <zonedbx/zone_registry.h>  // extended database (zones + links available)
using namespace ace_time;

// Keep this small; 2 is usually enough on Teensy 4.1
static const uint8_t CACHE_SIZE = 2;
static DMAMEM ExtendedZoneProcessorCache<CACHE_SIZE> zoneProcessorCache;

// Use the full Zone+Link registry so aliases like "Europe/Stockholm" always resolve
ExtendedZoneManager zoneManager(
  zonedbx::kZoneAndLinkRegistrySize,
  zonedbx::kZoneAndLinkRegistry,
  zoneProcessorCache
);
