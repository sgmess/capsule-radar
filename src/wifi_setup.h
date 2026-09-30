#pragma once
// WiFi provisioning: connect with saved credentials, or open the "CapsuleRadar-Setup"
// access point so they can be entered from a phone. Non-blocking after boot either way.
//
// Two implementations behind one API (wifi_setup.cpp):
//   BOARD_HAS_WIFIMANAGER 1 (every S3 board): tzapu's WiFiManager captive portal, as before.
//   BOARD_HAS_WIFIMANAGER 0 (the ESP32-P4 boards): WiFiManager cannot build without a
//     native radio PHY, so a small setup AP + captive DNS + form on the config web server
//     stands in, plus a serial fallback: `wifi <ssid> <password>`.
#include <stdbool.h>

class WebServer;

#define WIFI_SETUP_AP "CapsuleRadar-Setup"

// Blocking-ish connect attempt with the saved credentials; opens the setup AP if none are
// saved or they fail. onSaved fires when new credentials were stored (main reboots then).
// Returns true if connected.
bool wifi_setup_begin(void (*onSaved)());

// Register the setup-page handlers on the config web server (P4 path only; no-op with
// WiFiManager, which runs its own portal server). Call before server.begin().
void wifi_setup_attach(WebServer &server);

void wifi_setup_process();   // every loop(): services the portal / DNS / serial command
bool wifi_setup_portal_active();
bool wifi_setup_has_saved();
void wifi_setup_retry();     // try the saved credentials again (car hotspot came up late)
void wifi_setup_forget();    // erase saved credentials (web "Reset WiFi"); caller reboots
