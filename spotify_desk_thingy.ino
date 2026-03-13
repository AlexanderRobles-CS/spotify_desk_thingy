#include <Arduino.h>
#include "esp_task_wdt.h"
#include "secrets.h"
// ======================== WIFI ======================= //
#include <WiFi.h>
#include <HTTPClient.h>

char* SSID = WIFI_SSID;
const char* PASSWORD = WIFI_PASSWORD;
// ===================================================== //

// ======================= SPOTIFY ===================== //
#include <SpotifyEsp32.h>

const char* CLIENT_ID = SPOTIFY_CLIENT_ID;
const char* CLIENT_SECRET = SPOTIFY_CLIENT_SECRET;
const char* REFRESH_TOKEN = SPOTIFY_REFRESH_TOKEN;

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

Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);
// ===================================================== //

// =================== TFT DISPLAY ===================== //
#include <TJpg_Decoder.h>
#define FS_NO_GLOBALS
#include <FS.h>
#include "SPIFFS.h"
#include "Web_Fetch.h"
#include "SPI.h"
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// ===================================================== //

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

bool updateSpotifyImage(String spotifyImageURL) {
  if (SPIFFS.exists("/SpotifyTrack.jpg")) SPIFFS.remove("/SpotifyTrack.jpg");

  if (!getFile(spotifyImageURL, "/SpotifyTrack.jpg")) {
    Serial.println("Image download failed");
    return false;
  }

  TJpgDec.drawFsJpg(0, 45, "/SpotifyTrack.jpg");
  return true;
}

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

void initSPIFFS(){
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield();
  }
  Serial.println("\r\nSPIFFS Initialisation done.");

  if (SPIFFS.exists("/SpotifyTrack.jpg")) {
    SPIFFS.remove("/SpotifyTrack.jpg");
  }
}

void initTFTScreen(){
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

}

void initTJpegDecoder(){
  TJpgDec.setJpgScale(2);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
}

void setup() {
  Serial.begin(115200);

  initSPIFFS();

  initTFTScreen();

  initTJpegDecoder();

  connect_to_wifi();

  sp.begin();
  Serial.println("Connected to Spotify API.");
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