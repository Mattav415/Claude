// Bridge settings. Edit before flashing, and don't commit real credentials.
#pragma once

// ---- WiFi / MQTT ----
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"
#define MQTT_HOST "192.168.1.10"
#define MQTT_PORT 1883
#define MQTT_USER ""  // empty = anonymous
#define MQTT_PASSWORD ""

// ---- Meshtastic side ----
// Must match the root topic, channel name and PSK used by the real
// Meshtastic gateway node(s) on the same broker.
#define MESH_ROOT_TOPIC "msh/US"
#define MESH_CHANNEL_NAME "LongFast"
#define MESH_CHANNEL_PSK "AQ=="  // base64; "AQ==" is the default key
#define MESH_NODE_NUM 0          // 0 = derive from the ESP32's MAC
#define MESH_LONG_NAME "RYLR998 Bridge"
#define MESH_SHORT_NAME "RYLR"  // up to 4 characters
#define MESH_HOP_LIMIT 3
#define NODEINFO_INTERVAL_MS (3UL * 60 * 60 * 1000)

// ---- RYLR998 side ----
#define RYLR_RX_PIN 16  // ESP32 RX <- RYLR998 TXD
#define RYLR_TX_PIN 17  // ESP32 TX -> RYLR998 RXD
#define RYLR_RESET_PIN -1  // GPIO wired to NRST, or -1
#define RYLR_BAUD 115200
#define RYLR_ADDRESS 1  // this bridge's address on the RYLR network
#define RYLR_NETWORK_ID 18
#define RYLR_BAND_HZ 915000000  // keep clear of your Meshtastic frequency slot
#define RYLR_SPREADING_FACTOR 9
#define RYLR_BANDWIDTH 7  // 125 kHz
#define RYLR_CODING_RATE 1
#define RYLR_PREAMBLE 12
#define RYLR_PASSWORD ""      // 8 hex chars to set AT+CPIN, or ""
#define RYLR_DEST_ADDRESS 0   // where mesh messages go; 0 = all RYLR nodes
#define RYLR_MIN_SEND_GAP_MS 1500
// Mesh text for a message from RYLR address 12 reads "R12: <text>".
#define RYLR_SENDER_PREFIX "R"

// ---- Plain MQTT topics (no Meshtastic encoding) ----
// <prefix>/<!nodeid>/rx gets JSON for every RYLR frame received;
// publishing text to <prefix>/<!nodeid>/tx[/<address>] sends it over RYLR.
#define RAW_TOPIC_PREFIX "rylr"
