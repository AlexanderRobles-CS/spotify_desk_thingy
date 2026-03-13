#include "display.h"
#include <TJpg_Decoder.h>
#define FS_NO_GLOBALS
#include <FS.h>
#include "SPIFFS.h"
#include "Web_Fetch.h"
#include "SPI.h"
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

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

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield();
  }
  Serial.println("\r\nSPIFFS Initialisation done.");

  if (SPIFFS.exists("/SpotifyTrack.jpg")) {
    SPIFFS.remove("/SpotifyTrack.jpg");
  }
}

void initTFTScreen() {
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
}

void initTJpegDecoder() {
  TJpgDec.setJpgScale(2);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
}