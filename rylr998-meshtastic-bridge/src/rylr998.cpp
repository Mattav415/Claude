#include "rylr998.h"

void Rylr998::begin(HardwareSerial& serial, int rxPin, int txPin, uint32_t baud, int resetPin) {
  serial_ = &serial;
  serial_->begin(baud, SERIAL_8N1, rxPin, txPin);
  if (resetPin >= 0) {
    pinMode(resetPin, OUTPUT);
    digitalWrite(resetPin, LOW);
    delay(100);
    digitalWrite(resetPin, HIGH);
  }
  // The module prints +READY roughly 100-300 ms after power-up or reset.
  delay(500);
  String line;
  while (readLine(line)) handleLine(line);
}

bool Rylr998::configure(const Config& c) {
  bool alive = false;
  for (int i = 0; i < 3 && !alive; i++) alive = command("AT", 500);
  if (!alive) return false;

  char params[32];
  snprintf(params, sizeof(params), "AT+PARAMETER=%u,%u,%u,%u", c.spreadingFactor, c.bandwidth, c.codingRate,
           c.preamble);
  bool ok = command("AT+ADDRESS=" + String(c.address), 1000) &&
            command("AT+NETWORKID=" + String(c.networkId), 1000) &&
            command("AT+BAND=" + String(c.bandHz), 1000) && command(params, 1000);
  if (ok && c.password && c.password[0]) ok = command("AT+CPIN=" + String(c.password), 1000);
  return ok;
}

bool Rylr998::send(uint16_t address, const String& data) {
  String payload = data;
  payload.replace("\r", " ");
  payload.replace("\n", " ");
  if (payload.length() > kMaxPayload) payload.remove(kMaxPayload);
  if (payload.isEmpty()) return false;
  return command("AT+SEND=" + String(address) + "," + String(payload.length()) + "," + payload, 5000);
}

void Rylr998::poll() {
  String line;
  while (readLine(line)) handleLine(line);
}

bool Rylr998::command(const String& cmd, uint32_t timeoutMs) {
  serial_->print(cmd);
  serial_->print("\r\n");
  uint32_t start = millis();
  String line;
  while (millis() - start < timeoutMs) {
    if (!readLine(line)) {
      delay(1);
      continue;
    }
    Reply reply = handleLine(line);
    if (reply == Reply::OK) return true;
    if (reply == Reply::ERROR) {
      Serial.printf("[rylr] %s -> %s\n", cmd.substring(0, 24).c_str(), line.c_str());
      return false;
    }
  }
  Serial.printf("[rylr] %s -> timeout\n", cmd.substring(0, 24).c_str());
  return false;
}

bool Rylr998::readLine(String& out) {
  while (serial_->available()) {
    char c = char(serial_->read());
    if (c == '\n') {
      out = buffer_;
      buffer_ = "";
      if (!out.isEmpty()) return true;
    } else if (c != '\r' && buffer_.length() < 300) {
      buffer_ += c;
    }
  }
  return false;
}

Rylr998::Reply Rylr998::handleLine(const String& line) {
  if (line == "+OK") return Reply::OK;
  if (line.startsWith("+ERR")) return Reply::ERROR;
  if (line.startsWith("+RCV=")) handleReceive(line.substring(5));
  return Reply::NONE;
}

// +RCV=<address>,<length>,<data>,<rssi>,<snr>. The data may itself contain
// commas, so it is cut by length, falling back to the last two commas.
void Rylr998::handleReceive(const String& body) {
  int c1 = body.indexOf(',');
  int c2 = c1 < 0 ? -1 : body.indexOf(',', c1 + 1);
  if (c2 < 0) return;
  int dataEnd = c2 + 1 + body.substring(c1 + 1, c2).toInt();
  if (dataEnd >= int(body.length()) || body[dataEnd] != ',') {
    int last = body.lastIndexOf(',');
    dataEnd = body.lastIndexOf(',', last - 1);
    if (dataEnd <= c2) return;
  }
  int c4 = body.indexOf(',', dataEnd + 1);
  if (c4 < 0) return;
  if (handler_) {
    handler_(uint16_t(body.substring(0, c1).toInt()), body.substring(c2 + 1, dataEnd),
             body.substring(dataEnd + 1, c4).toInt(), body.substring(c4 + 1).toInt());
  }
}
