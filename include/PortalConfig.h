#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// ============================================================
//  PortalConfig.h — Skylander Portal Settings & Definitions
// ============================================================

// ── Figure & Slot Dimensions ──────────────────────────────────
#define FIGURE_SIZE  1024   // Dump size per Skylander figure (bytes)
#define NUM_SLOTS    8      // Active RAM slots (simultaneous figures)
#define MAX_SLOTS    8      // Total slots (no virtual slots)

// ── Network Configuration ─────────────────────────────────────
#define HTTP_PORT    8080   // HTTP server port (aligned with Flutter AppSettings.ipAddress)

// WiFi Station Mode (Connects to your home WiFi)
#define WIFI_SSID     "NOS-C364"
#define WIFI_PASSWORD "NXCMHZNM"

// Static IP Configuration (Home Network)
const IPAddress PORTAL_IP(192, 168, 1, 200);
const IPAddress PORTAL_GATEWAY(192, 168, 1, 1);
const IPAddress PORTAL_SUBNET(255, 255, 255, 0);
const IPAddress PORTAL_DNS(192, 168, 1, 1);

// Fallback Access Point (Used if Home WiFi is unreachable)
#define AP_SSID       "Skylander-Portal-S3"
#define AP_PASSWORD   "skylander123"
const IPAddress AP_IP(192, 168, 4, 1);

// ── USB HID Identifiers (Traptanium Portal) ───────────────────────────────────────
#define PORTAL_USB_VID          0x1430
#define PORTAL_USB_PID          0x0150   // Traptanium Portal
#define PORTAL_USB_MANUFACTURER "Activision"
#define PORTAL_USB_PRODUCT      "Traptanium Portal"
#define PORTAL_USB_SERIAL       "00000001"
