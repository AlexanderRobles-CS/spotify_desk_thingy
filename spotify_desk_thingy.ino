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

// ---- Scroll Sprites ----
TFT_eSprite sTrack  = TFT_eSprite(&tft);
TFT_eSprite sArtist = TFT_eSprite(&tft);

const int SPRITE_X   = 165;
const int VISIBLE_W  = 320 - SPRITE_X;   // 155px
const int SPRITE_H_TRACK  = 26;          // font 4
const int SPRITE_H_ARTIST = 16;          // font 2

int trackX = 0,  artistX = 0;
int trackW = 0,  artistW = 0;
int trackSpriteW = 0, artistSpriteW = 0;

bool trackScrollLeft  = true, artistScrollLeft  = true;
bool trackDone        = false, artistDone        = false;
bool trackPaused      = true,  artistPaused      = true;

unsigned long trackPauseStart = 0, artistPauseStart = 0;
volatile bool spritesReady = false, songChanged = false;

long avgR = 0, avgG = 0, avgB = 0;
int avgSamples = 0;
uint16_t bgColor, textColor;
// ===================================================== //

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

bool sample_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (x == 0 && y == 0) {
    Serial.printf("first pixel raw=0x%04X r=%d g=%d b=%d\n", 
      bitmap[0],
      (bitmap[0] >> 11) & 0x1F,
      (bitmap[0] >> 5)  & 0x3F,
      (bitmap[0])       & 0x1F);
  }
  for (int i = 0; i < w * h; i += 5) {
    uint16_t color = bitmap[i];
    avgR += (color >> 11) & 0x1F;
    avgG += (color >> 5)  & 0x3F;
    avgB += (color)       & 0x1F;
    avgSamples++;
  }
  return 1;
}

uint16_t getTextColor(uint16_t bgColor) {
  // extract RGB from 565
  uint8_t r = (bgColor >> 11) & 0x1F;
  uint8_t g = (bgColor >> 5)  & 0x3F;
  uint8_t b = (bgColor)       & 0x1F;

  // scale to 8bit
  r = r << 3;
  g = g << 2;
  b = b << 3;

  // calculate luminance (human eye sensitivity weighting)
  float luminance = 0.299 * r + 0.587 * g + 0.114 * b;

  return (luminance > 128) ? TFT_BLACK : TFT_WHITE;
}

uint16_t getAverageColor() {
  avgR = 0; avgG = 0; avgB = 0; avgSamples = 0;

  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(sample_output);
  TJpgDec.drawFsJpg(0, 45, "/SpotifyTrack.jpg");
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  if (avgSamples == 0) return TFT_BLACK;

  avgR /= avgSamples;
  avgG /= avgSamples;
  avgB /= avgSamples;

  return tft.color565(avgR << 3, avgG << 2, avgB << 3);
}

bool updateSpotifyImage(String spotifyImageURL) {
  spritesReady = false;

  if (SPIFFS.exists("/SpotifyTrack.jpg")) SPIFFS.remove("/SpotifyTrack.jpg");
  if (!getFile(spotifyImageURL, "/SpotifyTrack.jpg")) {
    Serial.println("Image download failed");
    return false;
  }

  TJpgDec.drawFsJpg(0, 45, "/SpotifyTrack.jpg");

  bgColor = getAverageColor(); 
  textColor = getTextColor(bgColor);
  tft.fillScreen(bgColor); 
  TJpgDec.drawFsJpg(0, 45, "/SpotifyTrack.jpg");

  // ---- Track sprite ----
  sTrack.deleteSprite();
  sTrack.setColorDepth(16);
  sTrack.createSprite(800, SPRITE_H_TRACK);
  sTrack.setTextColor(textColor);
  trackW = sTrack.textWidth(track, 4);
  trackDone = (trackW <= VISIBLE_W);
  trackSpriteW = VISIBLE_W + trackW;
  sTrack.deleteSprite();
  sTrack.createSprite(trackSpriteW, SPRITE_H_TRACK);
  sTrack.fillSprite(bgColor);
  sTrack.setTextColor(textColor);
  sTrack.drawString(track, 0, 0, 4);

  // ---- Artist sprite ----
  sArtist.deleteSprite();
  sArtist.setColorDepth(16);
  sArtist.createSprite(800, SPRITE_H_ARTIST);
  sArtist.setTextColor(textColor);
  artistW = sArtist.textWidth(artists, 2);
  artistDone = (artistW <= VISIBLE_W);
  artistSpriteW = VISIBLE_W + artistW;
  sArtist.deleteSprite();
  sArtist.createSprite(artistSpriteW, SPRITE_H_ARTIST);
  sArtist.fillSprite(bgColor);
  sArtist.setTextColor(textColor);
  sArtist.drawString(artists, 0, 0, 2);

  // ---- Reset scroll state ----
  trackX = 0;  artistX = 0;
  trackScrollLeft  = true;  artistScrollLeft  = true;
  trackDone        = false; artistDone        = false;
  trackPauseStart  = millis(); artistPauseStart = millis();
  trackPaused      = true;  artistPaused      = true;

  if (trackW <= VISIBLE_W) {
    trackDone = true;
    trackPaused = false;
  } else {
    trackDone = false;
  }

  if (artistW <= VISIBLE_W) {
    artistDone = true;
    artistPaused = false;
  } else {
    artistDone = false;
  } 

  spritesReady = true;
  return true;
}

#define TRANSPARENT_KEY 0xF81F

void updateScrollSprites() {
  if (songChanged) return;
  if (!spritesReady) return;
  static unsigned long lastScroll = 0;
  if (millis() - lastScroll < 50) return;
  lastScroll = millis();

  // --- Track ---
  tft.fillRect(SPRITE_X, 45, VISIBLE_W, SPRITE_H_TRACK, bgColor);
  sTrack.fillSprite(TRANSPARENT_KEY);
  sTrack.fillSprite(bgColor);
  sTrack.setTextColor(textColor);
  sTrack.drawString(track, trackX, 0, 4);
  sTrack.pushSprite(SPRITE_X, 45);

  if (!trackDone) {
    if (trackPaused) {
      if (millis() - trackPauseStart >= 500) trackPaused = false;
    } else {
      if (trackScrollLeft) {
        trackX--;
        if (trackX <= -(trackW - VISIBLE_W)) {
          trackScrollLeft = false;
          trackPauseStart = millis();
          trackPaused = true;
        }
      } else {
        trackX++;
        if (trackX >= 0) { trackX = 0; trackDone = true; }
      }
    }
  }

  // --- Artists ---
  tft.fillRect(SPRITE_X, 45 + SPRITE_H_TRACK + 5, VISIBLE_W, SPRITE_H_ARTIST, bgColor);
  sArtist.fillSprite(TRANSPARENT_KEY);
  sArtist.fillSprite(bgColor);
  sArtist.setTextColor(textColor);
  sArtist.drawString(artists, artistX, 0, 2);
  sArtist.pushSprite(SPRITE_X, 45 + SPRITE_H_TRACK + 5);

  if (!artistDone) {
    if (artistPaused) {
      if (millis() - artistPauseStart >= 500) artistPaused = false;
    } else {
      if (artistScrollLeft) {
        artistX--;
        if (artistX <= -(artistW - VISIBLE_W)) {
          artistScrollLeft = false;
          artistPauseStart = millis();
          artistPaused = true;
        }
      } else {
        artistX++;
        if (artistX >= 0) { artistX = 0; artistDone = true; }
      }
    }
  }
}

void connect_to_wifi() {
  WiFi.disconnect(true);
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

void scrollTask(void* parameter) {
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    updateScrollSprites();
    
    // update progress bar and time every second
    static unsigned long lastBarUpdate = 0;
    unsigned long now = millis();
    if (now - lastBarUpdate > 1000) {
      lastBarUpdate = now;

      int displayProgress = progress_ms;
      if (playing) displayProgress += (now - lastProgressSync);

      int progress_sec = displayProgress / 1000;
      int duration_sec = duration_ms / 1000;
      int progress_min = progress_sec / 60;
      int progress_rem = progress_sec % 60;
      int duration_min = duration_sec / 60;
      int duration_rem = duration_sec % 60;

      if (!songChanged && spritesReady) {
        int barW = map(displayProgress, 0, duration_ms, 0, 155);
        tft.fillRect(165, 125, barW, 6, textColor);
        tft.fillRect(165 + barW, 125, 155 - barW, 6, bgColor);

        char timeStr[20];
        sprintf(timeStr, "%d:%02d / %d:%02d", progress_min, progress_rem, duration_min, duration_rem);
        tft.setTextColor(textColor, bgColor);
        tft.drawString(timeStr, 165, 110, 2);
      }
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}
void setup() {
  Serial.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield();
  }
  Serial.println("\r\nSPIFFS Initialisation done.");

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  TJpgDec.setJpgScale(2);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  connect_to_wifi();

  if (SPIFFS.exists("/SpotifyTrack.jpg")) {
    SPIFFS.remove("/SpotifyTrack.jpg");
  }

  sp.begin();

  // Start scroll on core 0 (Spotify runs on core 1 by default)
  xTaskCreatePinnedToCore(
    scrollTask,    // function
    "scrollTask",  // name
    8192,          // stack size
    NULL,          // parameter
    1,             // priority
    NULL,          // handle
    0              // core 0
  );
}

void loop() {
  unsigned long now = millis();

  // ===== Sync with Spotify every second =====
  if (now - lastApiCall > 1000) {
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

    if (lastSong != id) {
      songChanged = true;
      if (updateSpotifyImage(imageUrl)) {
        lastSong = id;
        songChanged = false;
      }
    }

      lastProgressSync = now;
    }

    lastApiCall = now;
  }

  // ===== Smooth progress =====
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