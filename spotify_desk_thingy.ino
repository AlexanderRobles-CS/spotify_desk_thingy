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
  unsigned long now = millis();

  // ===== Sync with Spotify every 5 seconds =====
  if (now - lastApiCall > 5000) {
    response data = sp.current_playback_state();

    if (data.status_code == 200 && !data.reply.isNull()) {
      track   = data.reply["item"]["name"].as<String>();
      id      = data.reply["item"]["id"].as<String>();

      int artistCount = data.reply["item"]["artists"].size();
      artists = "";
      for (int i = 0; i < artistCount; i++) {
        if (i > 0) artists += ", ";
        artists += data.reply["item"]["artists"][i]["name"].as<String>();
      }

      imageUrl    = data.reply["item"]["album"]["images"][1]["url"].as<String>();
      playlistURI = data.reply["context"]["uri"].as<String>();
      progress_ms = data.reply["progress_ms"];
      duration_ms = data.reply["item"]["duration_ms"];
      playing     = data.reply["is_playing"];

      if (lastSong != id){
        if(updateSpotifyImage(imageUrl)){
          lastSong = id;
        }
      }

      lastProgressSync = now;
    }

    lastApiCall = now;
  }

  int displayProgress = progress_ms;
  if (playing) displayProgress += (now - lastProgressSync);

  int progress_sec = displayProgress / 1000;
  int duration_sec = duration_ms / 1000;
  int progress_min = progress_sec / 60;
  int progress_rem = progress_sec % 60;
  int duration_min = duration_sec / 60;
  int duration_rem = duration_sec % 60;

  static unsigned long lastPrint = 0;
  if (now - lastPrint > 1000) {
    lastPrint = now;
    Serial.printf("Image URL: %s\n", imageUrl.c_str());
    Serial.printf("Currently playing: %s - %s\n", track.c_str(), artists.c_str());
    Serial.printf("Time: %d:%02d / %d:%02d\n", progress_min, progress_rem, duration_min, duration_rem);
  }
}