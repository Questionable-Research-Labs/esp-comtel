// ============================================================================
//  secrets.example.h -- template for include/secrets.h (the launcher build's
//  network settings). Copy this file to include/secrets.h and fill it in:
//
//      cp include/secrets.example.h include/secrets.h
//
//  include/secrets.h is gitignored; never commit real credentials. Solo
//  (single-plugin) builds have no networking and don't need it.
// ============================================================================
#pragma once

#define COMTEL_WIFI_SSID  "your-network"
#define COMTEL_WIFI_PASS  "your-password"

// mDNS name: the control page is at http://<COMTEL_HOSTNAME>.local/ and
// `python tools/flash.py launcher --ota` uploads to <COMTEL_HOSTNAME>.local.
#define COMTEL_HOSTNAME   "comtel"

// Optional: require this password for over-the-air uploads. tools/flash.py
// reads it from here too. Leave commented out for no password.
// #define COMTEL_OTA_PASS   "change-me"
