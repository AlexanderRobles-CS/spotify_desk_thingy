#include <Arduino.h>
#include "spotify.h"
#include "secrets.h"
#include "display.h"
#include "controls.h"
#include "desk_idle.h"
// ======================== WIFI ======================= //
#include <WiFi.h>
#include <HTTPClient.h>

char* SSID = WIFI_SSID;
const char* PASSWORD = WIFI_PASSWORD;
// ===================================================== //

void connect_to_wifi() {
  WiFi.disconnect(true);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
}

void setup() {
  Serial.begin(115200);

  initSPIFFS();

  initTFTScreen();

  initTJpegDecoder();

  connect_to_wifi();

  initSpotify();

  initControls();

  initTime();
}

void loop() {
  updatePlayback();
}