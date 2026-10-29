// Driver for the REYAX RYLR998 LoRa module (UART AT-command interface).
#pragma once

#include <Arduino.h>

#include <functional>

class Rylr998 {
 public:
  using ReceiveHandler = std::function<void(uint16_t address, const String& data, int rssi, int snr)>;

  struct Config {
    uint16_t address;
    uint8_t networkId;  // 3..15 or 18
    uint32_t bandHz;
    uint8_t spreadingFactor;  // 5..11
    uint8_t bandwidth;        // 7 = 125 kHz, 8 = 250 kHz, 9 = 500 kHz
    uint8_t codingRate;       // 1..4
    uint8_t preamble;         // must be 12 unless networkId is 18
    const char* password;     // 8 hex chars for AT+CPIN, or "" for none
  };

  static constexpr size_t kMaxPayload = 240;

  void begin(HardwareSerial& serial, int rxPin, int txPin, uint32_t baud, int resetPin = -1);
  bool configure(const Config& config);
  // Blocks until the module accepts the frame. Address 0 broadcasts.
  bool send(uint16_t address, const String& data);
  // Call often from loop(); dispatches +RCV lines to the receive handler.
  void poll();
  void onReceive(ReceiveHandler handler) { handler_ = handler; }

 private:
  enum class Reply { NONE, OK, ERROR };

  bool command(const String& cmd, uint32_t timeoutMs);
  bool readLine(String& out);
  Reply handleLine(const String& line);
  void handleReceive(const String& body);

  HardwareSerial* serial_ = nullptr;
  String buffer_;
  ReceiveHandler handler_;
};
