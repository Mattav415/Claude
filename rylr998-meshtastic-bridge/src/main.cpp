// ESP32 + RYLR998 <-> Meshtastic MQTT bridge.
//
// The RYLR998 can't speak Meshtastic over the air, so this bridges at the
// MQTT layer instead: the ESP32 joins the broker as a virtual Meshtastic node,
// publishing RYLR998 traffic as encrypted ServiceEnvelopes and relaying
// channel text messages back out over the RYLR998. A real Meshtastic node with
// MQTT downlink enabled on the same channel carries them onto the mesh.
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <deque>
#include <map>

#include "config.h"
#include "mesh_codec.h"
#include "rylr998.h"

namespace {

constexpr size_t kMaxMeshText = 200;
constexpr size_t kMaxQueue = 16;
constexpr size_t kMaxNames = 128;
constexpr size_t kSeenIds = 64;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Rylr998 radio;
mesh::Channel channel;

uint32_t nodeNum = 0;
char gatewayId[10];  // "!xxxxxxxx"
String meshPublishTopic, meshSubscribeTopic, rawRxTopic, rawTxTopic;

struct Outgoing {
  uint16_t address;
  String text;
};
std::deque<Outgoing> rylrQueue;
uint32_t lastRylrSend = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastNodeInfo = 0;
bool nodeInfoSent = false;

// A packet uplinked by several gateways arrives once per gateway.
uint32_t seenIds[kSeenIds];
size_t seenPos = 0;

std::map<uint32_t, String> shortNames;  // learned from NodeInfo broadcasts

bool alreadySeen(uint32_t id) {
  for (uint32_t seen : seenIds)
    if (seen == id) return true;
  seenIds[seenPos++ % kSeenIds] = id;
  return false;
}

String nodeName(uint32_t num) {
  auto it = shortNames.find(num);
  if (it != shortNames.end()) return it->second;
  char buf[10];
  snprintf(buf, sizeof(buf), "!%08x", num);
  return buf;
}

String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (uint8_t(c) < 0x20) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += c;
    }
  }
  return out;
}

void publishToMesh(uint32_t portnum, const std::string& payload) {
  if (!mqtt.connected()) {
    Serial.println("[mesh] MQTT down, dropping packet");
    return;
  }
  mesh::Packet p;
  p.from = nodeNum;
  p.to = mesh::BROADCAST;
  p.id = esp_random();
  p.hopLimit = p.hopStart = MESH_HOP_LIMIT;
  p.portnum = portnum;
  p.payload = payload;
  p.hasBitfield = true;
  p.bitfield = mesh::BITFIELD_OK_TO_MQTT;
  std::string envelope = mesh::encodeEnvelope(channel, p, gatewayId);
  mqtt.publish(meshPublishTopic.c_str(), reinterpret_cast<const uint8_t*>(envelope.data()), envelope.size());
}

void sendNodeInfo() {
  publishToMesh(mesh::PORT_NODEINFO, mesh::encodeUser(gatewayId, MESH_LONG_NAME, MESH_SHORT_NAME, mesh::HW_PRIVATE));
  lastNodeInfo = millis();
  nodeInfoSent = true;
}

void queueRylr(uint16_t address, const String& text) {
  if (rylrQueue.size() >= kMaxQueue) {
    Serial.println("[rylr] send queue full, dropping message");
    return;
  }
  rylrQueue.push_back({address, text});
}

void onRylrReceive(uint16_t address, const String& data, int rssi, int snr) {
  Serial.printf("[rylr] from %u (rssi %d, snr %d): %s\n", address, rssi, snr, data.c_str());
  if (mqtt.connected()) {
    String json = "{\"from\":" + String(address) + ",\"rssi\":" + String(rssi) + ",\"snr\":" + String(snr) +
                  ",\"text\":\"" + jsonEscape(data) + "\"}";
    mqtt.publish(rawRxTopic.c_str(), json.c_str());
  }
  String text = String(RYLR_SENDER_PREFIX) + address + ": " + data;
  if (text.length() > kMaxMeshText) text.remove(kMaxMeshText);
  publishToMesh(mesh::PORT_TEXT_MESSAGE, text.c_str());
}

void handleMeshEnvelope(const std::string& buf) {
  mesh::Packet p;
  std::string channelId, fromGateway;
  if (mesh::decodeEnvelope(buf, channel, p, channelId, fromGateway) != mesh::DecodeResult::OK) return;
  if (fromGateway == gatewayId || p.from == nodeNum || alreadySeen(p.id)) return;

  if (p.portnum == mesh::PORT_NODEINFO) {
    std::string longName, shortName;
    if (mesh::decodeUser(p.payload, longName, shortName) && !shortName.empty()) {
      if (shortNames.size() >= kMaxNames) shortNames.clear();
      shortNames[p.from] = shortName.c_str();
    }
  } else if (p.portnum == mesh::PORT_TEXT_MESSAGE && (p.to == mesh::BROADCAST || p.to == nodeNum)) {
    String text = nodeName(p.from) + (p.to == nodeNum ? " (DM): " : ": ") + p.payload.c_str();
    Serial.printf("[mesh] %s\n", text.c_str());
    queueRylr(RYLR_DEST_ADDRESS, text);
  }
}

void onMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  String t(topic);
  if (t == rawTxTopic || t.startsWith(rawTxTopic + "/")) {
    uint16_t address = t == rawTxTopic ? RYLR_DEST_ADDRESS : t.substring(rawTxTopic.length() + 1).toInt();
    String text;
    text.concat(reinterpret_cast<const char*>(payload), length);
    queueRylr(address, text);
    return;
  }
  handleMeshEnvelope(std::string(reinterpret_cast<const char*>(payload), length));
}

void ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (lastMqttAttempt && millis() - lastMqttAttempt < 5000) return;
  lastMqttAttempt = millis();

  String clientId = String("rylr-bridge-") + (gatewayId + 1);
  bool ok = strlen(MQTT_USER) ? mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)
                              : mqtt.connect(clientId.c_str());
  if (!ok) {
    Serial.printf("[mqtt] connect failed, state %d\n", mqtt.state());
    return;
  }
  Serial.println("[mqtt] connected");
  mqtt.subscribe(meshSubscribeTopic.c_str());
  mqtt.subscribe(rawTxTopic.c_str());
  mqtt.subscribe((rawTxTopic + "/+").c_str());
  sendNodeInfo();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  if (!mesh::initChannel(channel, MESH_CHANNEL_NAME, MESH_CHANNEL_PSK)) {
    Serial.println("[mesh] invalid MESH_CHANNEL_PSK, halting");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_STA);
  nodeNum = MESH_NODE_NUM;
  if (!nodeNum) {
    // Meshtastic derives node numbers from the last four MAC bytes too.
    uint8_t mac[6];
    WiFi.macAddress(mac);
    nodeNum = (uint32_t(mac[2]) << 24) | (uint32_t(mac[3]) << 16) | (uint32_t(mac[4]) << 8) | mac[5];
  }
  snprintf(gatewayId, sizeof(gatewayId), "!%08x", nodeNum);

  String channelRoot = String(MESH_ROOT_TOPIC) + "/2/e/" + MESH_CHANNEL_NAME;
  meshPublishTopic = channelRoot + "/" + gatewayId;
  meshSubscribeTopic = channelRoot + "/+";
  rawRxTopic = String(RAW_TOPIC_PREFIX) + "/" + gatewayId + "/rx";
  rawTxTopic = String(RAW_TOPIC_PREFIX) + "/" + gatewayId + "/tx";
  Serial.printf("[mesh] node %s, channel %s (hash %u), topic %s\n", gatewayId, MESH_CHANNEL_NAME, channel.hash,
                meshPublishTopic.c_str());

  radio.begin(Serial2, RYLR_RX_PIN, RYLR_TX_PIN, RYLR_BAUD, RYLR_RESET_PIN);
  radio.onReceive(onRylrReceive);
  Rylr998::Config rc = {RYLR_ADDRESS,   RYLR_NETWORK_ID,  RYLR_BAND_HZ,  RYLR_SPREADING_FACTOR,
                        RYLR_BANDWIDTH, RYLR_CODING_RATE, RYLR_PREAMBLE, RYLR_PASSWORD};
  Serial.println(radio.configure(rc) ? "[rylr] configured" : "[rylr] configuration failed, check wiring");

  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(onMqttMessage);
}

void loop() {
  radio.poll();
  ensureMqtt();
  mqtt.loop();

  if (!rylrQueue.empty() && millis() - lastRylrSend >= RYLR_MIN_SEND_GAP_MS) {
    Outgoing next = rylrQueue.front();
    rylrQueue.pop_front();
    if (!radio.send(next.address, next.text)) Serial.println("[rylr] send failed");
    lastRylrSend = millis();
  }

  if (mqtt.connected() && nodeInfoSent && millis() - lastNodeInfo >= NODEINFO_INTERVAL_MS) sendNodeInfo();
}
