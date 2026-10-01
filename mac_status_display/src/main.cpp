// MiniDisplay USB status screen: ESP8266 NodeMCU v2 + 240 x 240 ST7789.
// The display state machine lives in MiniDisplayApp; this file only wires the
// serial port and the panel to it.
#include <Arduino.h>
#include <TFT_eSPI.h>

#include "line_reader.h"
#include "minidisplay_app.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;

TFT_eSPI display;
minidisplay::MiniDisplayApp app(display);
minidisplay::LineReader<1024> serialLines;

}  // namespace

void setup() {
  Serial.setRxBufferSize(2048);
  Serial.begin(kSerialBaud);
  app.begin(millis());
  Serial.println("MSD4 READY AI1");
}

void loop() {
  while (Serial.available() > 0) {
    char *line = serialLines.push(static_cast<char>(Serial.read()));
    if (line != nullptr) app.handleLine(line, millis());
  }
  app.tick(millis());
  delay(2);
}
