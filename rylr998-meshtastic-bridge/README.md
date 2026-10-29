# RYLR998 ↔ Meshtastic MQTT bridge (ESP32)

Firmware that connects a network of REYAX RYLR998 modules to a Meshtastic mesh
by way of MQTT.

The RYLR998 can't be a Meshtastic radio. It only exposes AT commands and wraps
every frame in REYAX's own format, so Meshtastic nodes can't hear it over the
air. This firmware bridges at the MQTT layer instead. The ESP32 joins your
broker as a **virtual Meshtastic node**: it publishes and decodes the same
encrypted `ServiceEnvelope` protobufs that real Meshtastic gateways use.

```
RYLR998 nodes ──LoRa (REYAX)──► RYLR998 + ESP32 ──WiFi/MQTT──► broker
                                                                  │
Meshtastic nodes ◄──LoRa (Meshtastic)── real Meshtastic node with MQTT
                                         uplink + downlink enabled
```

- A message from RYLR address 12 shows up on the Meshtastic channel as
  `R12: <text>`, sent from the bridge node (`RYLR998 Bridge` / `RYLR`).
- Channel text messages from the mesh, and DMs to the bridge node that aren't
  PKI-encrypted, go out over the RYLR998 as `<sender short name>: <text>`.
- The bridge broadcasts its own NodeInfo so Meshtastic apps show its name, and
  remembers other nodes' short names from their NodeInfo.
- Plain topics (no Meshtastic encoding) are also available:
  `rylr/!<nodeid>/rx` gets JSON for every RYLR frame received, and publishing
  text to `rylr/!<nodeid>/tx` (or `.../tx/<address>`) sends it out over the
  RYLR998.

## What you need

- An ESP32 dev board and a RYLR998.
- An MQTT broker, ideally your own (e.g. Mosquitto). The public
  `mqtt.meshtastic.org` broker filters and rate-limits downlink, so traffic
  injected there won't reliably reach the radio side.
- **At least one real Meshtastic node** (any WiFi-capable board) on the same
  broker, with MQTT enabled and **uplink and downlink** turned on for the
  channel. It is the node that actually puts bridged messages on the air.

## Wiring

| RYLR998 | ESP32            |
|---------|------------------|
| VDD     | 3V3              |
| GND     | GND              |
| TXD     | GPIO16 (RX2)     |
| RXD     | GPIO17 (TX2)     |
| NRST    | optional GPIO (`RYLR_RESET_PIN`) |

The RYLR998 is a 3.3 V part, so don't power it from 5 V.

## Setup

1. Edit `include/config.h`: WiFi, broker, and the Meshtastic root topic,
   channel name and PSK. These must match your gateway node; the defaults are
   `msh/US`, `LongFast` and `AQ==`.
2. Set the RYLR998 parameters (address, network ID, band, SF/BW/CR/preamble)
   to match your other RYLR998 nodes. Choose a frequency away from your
   Meshtastic slot (US LongFast is 906.875 MHz) so the two networks don't step
   on each other.
3. Build and flash with PlatformIO: `pio run -t upload && pio device monitor`.

The serial log prints the bridge's node ID (`!xxxxxxxx`) and its MQTT topic.

## Limitations

- Text messages and NodeInfo only. Positions, telemetry and other port numbers
  are ignored.
- PKI-encrypted DMs can't be decrypted (the bridge has no key pair). Channel
  messages work.
- Messages longer than 200 bytes (mesh-bound) or 240 bytes (RYLR-bound) are
  truncated.
- All RYLR998 nodes share the bridge's single Meshtastic identity.
- Mind your region's duty-cycle limits on both radios.

## Tests

`test/verify_codec.py` builds the codec on a host (g++ + OpenSSL) and checks it
in both directions against the official `meshtastic` Python protobufs and an
independent AES-CTR implementation:

```
pip install meshtastic cryptography
python3 test/verify_codec.py
```
