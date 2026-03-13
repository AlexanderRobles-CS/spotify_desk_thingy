#include "display.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#define FS_NO_GLOBALS
#include <FS.h>
#include "SPIFFS.h"
#include "Web_Fetch.h"
#include "SPI.h"
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

static uint32_t rTotal = 0, gTotal = 0, bTotal = 0, pixelCount = 0;
long avgR = 0, avgG = 0, avgB = 0;
int avgSamples = 0;

static bool dirtyProgressBar = false;
static bool dirtyTrackInfo   = false;

void markProgressDirty() { dirtyProgressBar = true; }
void markTrackDirty()    { dirtyTrackInfo   = true; }

void resetColorSample() {
  rTotal = 0; gTotal = 0; bTotal = 0; pixelCount = 0;
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

uint16_t getAverageTFTColor() {
  if (pixelCount == 0) return TFT_BLACK;
  uint8_t r = rTotal / pixelCount;
  uint8_t g = gTotal / pixelCount;
  uint8_t b = bTotal / pixelCount;
  return tft.color565(r, g, b);
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;

  for (int i = 0; i < w * h; i++) {
    uint16_t px = bitmap[i];
    rTotal += (px >> 11) & 0x1F;
    gTotal += (px >> 5)  & 0x3F;
    bTotal += (px)       & 0x1F;
    pixelCount++;
  }

  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

bool updateSpotifyImage(String spotifyImageURL) {
  if (SPIFFS.exists("/SpotifyTrack.jpg")) SPIFFS.remove("/SpotifyTrack.jpg");
  if (!getFile(spotifyImageURL, "/SpotifyTrack.jpg")) return false;

  uint16_t avgColor = getAverageColor();
  uint16_t textColor = getTextColor(avgColor);

  tft.fillScreen(avgColor);

  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  TJpgDec.drawFsJpg(0, 45, "/SpotifyTrack.jpg");

  tft.fillRect(0, 0, 150, 45, avgColor);
  tft.fillRect(0, 195, 150, 45, avgColor);

  return true;
}

String truncate(String text, int maxChars) {
  if (text.length() > maxChars) return text.substring(0, maxChars - 3) + "...";
  return text;
}

void updateTrackInfo(String track, String artists, uint16_t bgColor, uint16_t textColor) {
  if (!dirtyTrackInfo) return;
  dirtyTrackInfo = false;

  tft.startWrite();
  tft.fillRect(150, 0, 170, 240, bgColor);
  tft.setTextColor(textColor, bgColor);

  tft.setTextSize(2);
  tft.setCursor(158, 80);
  tft.println(truncate(track, 13));

  tft.setTextSize(1);
  tft.setCursor(158, 104);
  tft.println(truncate(artists, 25));
  tft.endWrite();
}

void updateProgressBar(int progress_ms, int duration_ms, uint16_t bgColor, uint16_t textColor) {
  if (!dirtyProgressBar) return;
  if (duration_ms == 0) return;
  dirtyProgressBar = false;

  int barX = 158;
  int barY = 125;
  int barW = 154;
  int barH = 6;

  float ratio = (float)progress_ms / (float)duration_ms;
  int filled = (int)(ratio * barW);

  tft.startWrite();
  for (int row = barY; row < barY + barH; row++) {
    tft.drawFastHLine(barX, row, filled, textColor);
    tft.drawFastHLine(barX + filled, row, barW - filled, tft.color565(80, 80, 80));
  }

  int progress_sec = progress_ms / 1000;
  int duration_sec = duration_ms / 1000;
  tft.setTextSize(1);
  tft.setTextColor(textColor, bgColor);
  tft.setCursor(barX, barY + 10);
  tft.printf("%d:%02d / %d:%02d", progress_sec / 60, progress_sec % 60, duration_sec / 60, duration_sec % 60);
  tft.endWrite();
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialisation failed!");
    while (1) yield();
  }
  Serial.println("\r\nSPIFFS Initialisation done.");
  if (SPIFFS.exists("/SpotifyTrack.jpg")) SPIFFS.remove("/SpotifyTrack.jpg");
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