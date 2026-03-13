#include <Arduino.h>
#include "spotify.h"
#include "esp_task_wdt.h"
#include "secrets.h"
#include "display.h"
// ======================== WIFI ======================= //
#include <WiFi.h>
#include <HTTPClient.h>

char* SSID = WIFI_SSID;
const char* PASSWORD = WIFI_PASSWORD;
// ===================================================== //

unsigned long lastApiCall = 0;
unsigned long lastProgressSync = 0;

int progress_ms = 0;
int duration_ms = 0;
bool playing = false;

String track;
String id;
String artists = "";
String imageUrl;
String playlistURI;
String lastSong = "";

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
}

void loop() {
  updatePlayback();
}