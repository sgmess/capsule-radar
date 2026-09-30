// WiFi provisioning — see wifi_setup.h.
#include "wifi_setup.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <nvs.h>

// The WiFi driver keeps the last AP in its own NVS namespace. On Arduino core 3.x both
// wm.resetSettings() and WiFi.disconnect(true,true) can silently no-op once the driver is
// off, so the reset also erases the namespace directly — that works in any driver state.
static void erase_driver_nvs() {
    nvs_handle_t h;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}

#if BOARD_HAS_WIFIMANAGER
// ============================ WiFiManager path ===============================
#include <WiFiManager.h>

static WiFiManager s_wm;

bool wifi_setup_begin(void (*onSaved)()) {
    s_wm.setConfigPortalBlocking(false);
    s_wm.setTitle("Capsule Radar");
    // light phosphor-green theme for the captive portal (small CSS, injected into <head>)
    s_wm.setCustomHeadElement(
        "<style>"
        "body{background:#06100a;color:#cdd6d1;font-family:system-ui,sans-serif}"
        "h1,h2,h3{color:#1dff86}"
        "button,input[type=submit],.btn{background:#1dff86!important;color:#04140b!important;"
        "border:0!important;border-radius:8px!important;font-weight:700}"
        "input,select{background:#0c1a12!important;color:#eafff3!important;"
        "border:1px solid #2a4a39!important;border-radius:8px!important}"
        "a{color:#1dff86}.q{filter:hue-rotate(90deg)}"
        "</style>");
    if (onSaved) s_wm.setSaveConfigCallback(onSaved);
    return s_wm.autoConnect(WIFI_SETUP_AP);
}

void wifi_setup_attach(WebServer &) {}          // WiFiManager serves its own portal
void wifi_setup_process()        { s_wm.process(); }
bool wifi_setup_portal_active()  { return s_wm.getConfigPortalActive(); }
bool wifi_setup_has_saved()      { return s_wm.getWiFiIsSaved(); }
void wifi_setup_retry()          { WiFi.begin(); }   // the driver's stored credentials

void wifi_setup_forget() {
    s_wm.resetSettings();           // best-effort driver-level erase first...
    WiFi.disconnect(false, true);   // ...keep WiFi up so the erase can actually run
    delay(100);
    erase_driver_nvs();             // ...then the guaranteed path
}

#else
// ======================== setup-AP path (ESP32-P4) ===========================
// The P4's radio is an ESP32-C6 over SDIO (esp_hosted). Station and SoftAP work through
// plain WiFi.h, but WiFiManager does not build there, so credentials live in our own NVS
// namespace and the setup page is served by the normal config web server.
#include <DNSServer.h>
#include <Preferences.h>

#define WIFI_CONNECT_TIMEOUT_MS 12000   // boot-time wait, like WiFiManager's connect timeout

static DNSServer s_dns;
static bool      s_portal = false;
static String    s_ssid, s_pass;
static void    (*s_onSaved)() = nullptr;
static WebServer *s_web = nullptr;

static void load_creds() {
    Preferences p;
    p.begin("wifisetup", true);
    s_ssid = p.getString("ssid", "");
    s_pass = p.getString("pass", "");
    p.end();
}

static void save_creds(const String &ssid, const String &pass) {
    Preferences p;
    p.begin("wifisetup", false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();
    s_ssid = ssid;
    s_pass = pass;
    Serial.printf("[wifi] credentials saved for \"%s\"\n", ssid.c_str());
    if (s_onSaved) s_onSaved();
}

static void open_portal() {
    if (s_portal) return;
    // AP+STA so the saved network can still be retried while the portal is up.
    if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(WIFI_SETUP_AP)) {
        Serial.println("[wifi] setup AP FAILED - set WiFi over serial: wifi <ssid> <password>");
        return;
    }
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", WiFi.softAPIP());       // every name -> us: phones pop the page up
    s_portal = true;
    Serial.printf("[wifi] setup AP '%s' up at http://%s/ (or serial: wifi <ssid> <password>)\n",
                  WIFI_SETUP_AP, WiFi.softAPIP().toString().c_str());
}

bool wifi_setup_begin(void (*onSaved)()) {
    s_onSaved = onSaved;
    load_creds();
    if (s_ssid.length()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(s_ssid.c_str(), s_pass.c_str());
        const uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) delay(100);
        if (WiFi.status() == WL_CONNECTED) return true;
        Serial.printf("[wifi] \"%s\" not reachable yet\n", s_ssid.c_str());
    }
    open_portal();
    return false;
}

static const char SETUP_PAGE[] =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Capsule Radar WiFi</title><style>"
    "body{background:#06100a;color:#cdd6d1;font-family:system-ui,sans-serif;padding:20px;max-width:420px;margin:auto}"
    "h2{color:#1dff86}label{display:block;margin:14px 0 4px}"
    "input{width:100%;box-sizing:border-box;padding:10px;background:#0c1a12;color:#eafff3;"
    "border:1px solid #2a4a39;border-radius:8px}"
    "button{margin-top:18px;width:100%;padding:12px;background:#1dff86;color:#04140b;border:0;"
    "border-radius:8px;font-weight:700}</style>"
    "<h2>Capsule Radar WiFi</h2><form method=post action=/wifisetup>"
    "<label>Network (SSID)</label><input name=ssid maxlength=32 autocomplete=off required>"
    "<label>Password</label><input name=pass type=password maxlength=63 autocomplete=off>"
    "<button>Save &amp; restart</button></form>";

void wifi_setup_attach(WebServer &server) {
    s_web = &server;
    server.on("/wifisetup", HTTP_GET, [&server]() { server.send(200, "text/html", SETUP_PAGE); });
    server.on("/wifisetup", HTTP_POST, [&server]() {
        const String ssid = server.arg("ssid");
        if (ssid.length() == 0 || ssid.length() > 32) {
            server.send(400, "text/plain", "SSID required");
            return;
        }
        server.send(200, "text/html",
            "<body style='background:#06100a;color:#1dff86;font-family:sans-serif;padding:24px'>"
            "Saved. Restarting&hellip; then find it at <b>http://capsuleradar.local/</b></body>");
        save_creds(ssid, server.arg("pass"));
    });
    // Captive-portal detection (phones probe e.g. /generate_204, /hotspot-detect.html):
    // anything unknown while the portal is up goes to the setup page.
    server.onNotFound([&server]() {
        if (s_portal) {
            server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/wifisetup");
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "not found");
        }
    });
}

// Serial fallback: "wifi <ssid> <password>" (password may contain spaces; ssid may not).
static void poll_serial() {
    static String line;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c != '\n') { if (line.length() < 128) line += c; continue; }
        line.trim();
        if (line.startsWith("wifi ")) {
            const String rest = line.substring(5);
            const int sp = rest.indexOf(' ');
            const String ssid = sp < 0 ? rest : rest.substring(0, sp);
            const String pass = sp < 0 ? String() : rest.substring(sp + 1);
            if (ssid.length()) save_creds(ssid, pass);
        }
        line = "";
    }
}

void wifi_setup_process() {
    if (s_portal) s_dns.processNextRequest();
    poll_serial();
}

bool wifi_setup_portal_active() { return s_portal; }
bool wifi_setup_has_saved()     { return s_ssid.length() > 0; }
void wifi_setup_retry()         { if (s_ssid.length()) WiFi.begin(s_ssid.c_str(), s_pass.c_str()); }

void wifi_setup_forget() {
    Preferences p;
    p.begin("wifisetup", false);
    p.clear();
    p.end();
    WiFi.disconnect(false, true);
    delay(100);
    erase_driver_nvs();             // harmless if the hosted driver keeps none
}

#endif  // BOARD_HAS_WIFIMANAGER
