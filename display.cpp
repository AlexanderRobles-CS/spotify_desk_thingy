#include "display.h"
#include "devices.h"
#include "desk_idle.h"
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

static int trackScrollX  = 158;
static int artistScrollX = 158;
static unsigned long lastScroll = 0;

unsigned long volumeOverlayShownAt = 0;
bool volumeOverlayVisible = false;

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
  static String lastImageURL = "";

  // Only fetch and redraw if the image URL has changed (e.g. new song)
  if (spotifyImageURL != lastImageURL) {
    if (SPIFFS.exists("/SpotifyTrack.jpg")) SPIFFS.remove("/SpotifyTrack.jpg");
    if (!getFile(spotifyImageURL, "/SpotifyTrack.jpg")) return false;
    lastImageURL = spotifyImageURL;
  }
  uint16_t avgColor = getAverageColor();
  uint16_t textColor = getTextColor(avgColor);

  tft.fillScreen(avgColor);

  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  TJpgDec.drawFsJpg(0, 45, "/SpotifyTrack.jpg");

  tft.fillRect(0, 0, 150, 45, avgColor);
  tft.fillRect(0, 195, 150, 45, avgColor);
  displayReady = true;
  return true;
}

// ---- Sprite constants ----
#define SPRITE_X        158
#define TRACK_Y         70
#define ARTIST_Y        100
#define VISIBLE_W       162
#define SPRITE_H_TRACK  28
#define SPRITE_H_ARTIST 18

TFT_eSprite sTrack  = TFT_eSprite(&tft);
TFT_eSprite sArtist = TFT_eSprite(&tft);

static int  trackW = 0,  artistW = 0;
static int  trackX = 0,  artistX = 0;
static int  trackSpriteW = 0, artistSpriteW = 0;
static bool trackScrollLeft  = true, artistScrollLeft  = true;
static bool trackDone        = false, artistDone        = false;
static bool trackPaused      = true,  artistPaused      = true;
static unsigned long trackPauseStart = 0, artistPauseStart = 0;
static bool spritesReady = false;
bool displayReady = true;

static String  spriteTrack   = "";
static String  spriteArtists = "";
static uint16_t bgColor      = TFT_BLACK;
static uint16_t textColor    = TFT_WHITE;

void clearScrollSprites() {
  spritesReady = false;
  sTrack.deleteSprite();
  sArtist.deleteSprite();
}

void drawIdleScreen() {
  Serial.printf("[IDLE] drawing - time: '%s' date: '%s'\n", timeStr, dateStr);
  tft.fillScreen(TFT_BLACK);
  clearScrollSprites();

  // ── TIME ─────────────────────────────────────────────────────────
  char hours[3] = { timeStr[0], timeStr[1], '\0' };
  char mins[3]  = { timeStr[3], timeStr[4], '\0' };

  tft.setTextColor(0xEF5D, TFT_BLACK);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(hours, 150, 65, 7);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(":", 160, 61, 7);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(mins, 170, 65, 7);

  // ── DATE ─────────────────────────────────────────────────────────
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0x6B4D, TFT_BLACK);
  tft.drawString(dateStr, 160, 114, 2);

  // ── DIVIDER ──────────────────────────────────────────────────────
  tft.drawFastHLine(40, 132, 240, 0x2945);

  // ── WEATHER ROW ──────────────────────────────────────────────────
  const char* tempStr = "72F";
  const char* hlStr   = "H:76  L:58";
  const char* condStr = "SUNNY";

  int sunWidth  = 36;
  int leftWidth = max(tft.textWidth(tempStr, 4),
                      tft.textWidth(hlStr, 1));
  int sepGap    = 16;
  int condWidth = tft.textWidth(condStr, 2);
  int totalWidth = sunWidth + leftWidth + sepGap + condWidth;

  int wx = (320 - totalWidth) / 2;
  int wy = 190;

  // Sun icon
  tft.fillCircle(wx + 10, wy, 9, 0xD4A0);
  for (int i = 0; i < 8; i++) {
    float angle = i * PI / 4;
    int x1 = wx + 10 + cos(angle) * 13;
    int y1 = wy + sin(angle) * 13;
    int x2 = wx + 10 + cos(angle) * 17;
    int y2 = wy + sin(angle) * 17;
    tft.drawLine(x1, y1, x2, y2, 0xD4A0);
  }

  // Temp
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(0xC618, TFT_BLACK);
  tft.drawString(tempStr, wx + sunWidth, wy - 8, 4);

  // High
  tft.setTextColor(0x528A, TFT_BLACK);
  tft.drawString(hlStr, wx + sunWidth, wy + 10, 1);

  // Vertical separator
  int sepX = wx + sunWidth + leftWidth + 6;
  tft.drawFastVLine(sepX, wy - 14, 28, 0x2945);

  // Condition
  tft.setTextColor(0x528A, TFT_BLACK);
  tft.drawString(condStr, sepX + 10, wy, 2);
}

void drawDevices() {
  tft.fillScreen(TFT_BLACK);
  clearScrollSprites(); 

  // Header
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, ILI9341_DARKGREEN);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 7);
  tft.print("Spotify Devices");

  if (deviceCount == 0) {
    tft.setTextColor(ILI9341_YELLOW);
    tft.setCursor(10, 50);
    tft.println("No devices found.");
    return;
  }

  int availableH = SCREEN_H - HEADER_H - 12;
  int rowH       = min(50, availableH / deviceCount);
  int fontSize   = (rowH >= 40) ? 2 : 1;

  for (int i = 0; i < deviceCount; i++) {
    Device& dev = deviceList[i];
    int y = HEADER_H + i * rowH;

    uint16_t rowColor = dev.active ? ILI9341_NAVY : (i % 2 == 0 ? 0x1082 : ILI9341_BLACK);
    tft.fillRect(0, y, SCREEN_W, rowH - 1, rowColor);

    if (dev.active) tft.fillCircle(7, y + rowH / 2, 4, ILI9341_GREEN);

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(fontSize);
    tft.setCursor(16, y + 4);
    tft.print(dev.name);

    if (rowH >= 30) {
      tft.setTextSize(1);
      tft.setTextColor(ILI9341_CYAN);
      tft.setCursor(16, y + (fontSize == 2 ? 24 : 14));
      tft.printf("%s | Vol: %d%%", dev.type.c_str(), dev.volume);
    }
  }

  // Footer
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF);
  tft.setCursor(8, SCREEN_H - 10);
  tft.printf("%d device(s)", deviceCount);
}

void buildScrollSprites(String track, String artists, uint16_t bg, uint16_t fg) {
  spritesReady = false;
  spriteTrack   = track;
  spriteArtists = artists;
  bgColor       = bg;
  textColor     = fg;

  // ---- Track sprite ----
  sTrack.setColorDepth(16);
  sTrack.createSprite(VISIBLE_W, SPRITE_H_TRACK);
  sTrack.setTextColor(fg);
  trackW = sTrack.textWidth(track, 4);
  sTrack.deleteSprite();

  trackSpriteW = max((int)VISIBLE_W, trackW);
  sTrack.createSprite(trackSpriteW, SPRITE_H_TRACK);
  sTrack.fillSprite(bg);
  sTrack.setTextColor(fg);
  sTrack.drawString(track, 0, 0, 4);

  // ---- Artist sprite ----
  sArtist.setColorDepth(16);
  sArtist.createSprite(VISIBLE_W, SPRITE_H_ARTIST);
  sArtist.setTextColor(fg);
  artistW = sArtist.textWidth(artists, 2);
  sArtist.deleteSprite();

  artistSpriteW = max((int)VISIBLE_W, artistW);
  sArtist.createSprite(artistSpriteW, SPRITE_H_ARTIST);
  sArtist.fillSprite(bg);
  sArtist.setTextColor(fg);
  sArtist.drawString(artists, 0, 0, 2);

  // ---- Reset scroll state ----
  trackX = 0;  artistX = 0;
  trackScrollLeft = true;  artistScrollLeft = true;
  trackPauseStart = millis(); artistPauseStart = millis();
  trackPaused = true;  artistPaused = true;
  trackDone  = (trackW  <= VISIBLE_W);
  artistDone = (artistW <= VISIBLE_W);

  if (trackDone) {
    sTrack.pushSprite(SPRITE_X, TRACK_Y);
  }
  if (artistDone) {
    sArtist.pushSprite(SPRITE_X, ARTIST_Y);
  }

  spritesReady = true;
}

void updateScrollSprites() {
  if (!displayReady) return; 
  if (!spritesReady) return;

  static unsigned long lastScroll = 0;
  static unsigned long scrollDoneTime = 0;

  if (trackDone && artistDone) {
    if (scrollDoneTime == 0) {
      scrollDoneTime = millis();
    }
    if (millis() - scrollDoneTime >= 30000) {
      trackX = 0; artistX = 0;
      trackScrollLeft = true; artistScrollLeft = true;
      trackPaused = true; artistPaused = true;
      trackPauseStart = millis(); artistPauseStart = millis();
      trackDone  = (trackW  <= VISIBLE_W);
      artistDone = (artistW <= VISIBLE_W);
      scrollDoneTime = 0;
    }
    return;
  }

  if (trackDone && artistDone) return;
  if (millis() - lastScroll < 50) return;
  lastScroll = millis();

  tft.startWrite();

  // ---- Track ----
  if (!trackDone) {
    sTrack.fillSprite(bgColor);
    sTrack.setTextColor(textColor);
    sTrack.drawString(spriteTrack, trackX, 0, 4);
    sTrack.pushSprite(SPRITE_X, TRACK_Y);

    if (trackPaused) {
      if (millis() - trackPauseStart >= 1000) trackPaused = false;
    } else {
      if (trackScrollLeft) {
        trackX--;
        if (trackX <= -(trackW - VISIBLE_W)) {
          trackScrollLeft = false;
          trackPauseStart = millis();
          trackPaused     = true;
        }
      } else {
        trackX++;
        if (trackX >= 0) {
          trackX = 0;
          sTrack.fillSprite(bgColor);
          sTrack.setTextColor(textColor);
          sTrack.drawString(spriteTrack, 0, 0, 4);
          sTrack.pushSprite(SPRITE_X, TRACK_Y);
          trackDone = true;
        }
      }
    }
  }

  // ---- Artist ----
  if (!artistDone) {
    sArtist.fillSprite(bgColor);
    sArtist.setTextColor(textColor);
    sArtist.drawString(spriteArtists, artistX, 0, 2);
    sArtist.pushSprite(SPRITE_X, ARTIST_Y);

    if (artistPaused) {
      if (millis() - artistPauseStart >= 1000) artistPaused = false;
    } else {
      if (artistScrollLeft) {
        artistX--;
        if (artistX <= -(artistW - VISIBLE_W)) {
          artistScrollLeft = false;
          artistPauseStart = millis();
          artistPaused     = true;
        }
      } else {
        artistX++;
        if (artistX >= 0) {
          artistX = 0;
          sArtist.fillSprite(bgColor);
          sArtist.setTextColor(textColor);
          sArtist.drawString(spriteArtists, 0, 0, 2);
          sArtist.pushSprite(SPRITE_X, ARTIST_Y);
          artistDone = true;
        }
      }
    }
  }

  tft.endWrite();
}

void resetScroll() {
  trackX = 0; artistX = 0;
  trackScrollLeft = true; artistScrollLeft = true;
  trackPaused = true; artistPaused = true;
  trackPauseStart = millis(); artistPauseStart = millis();
  trackDone = false; artistDone = false;
  spritesReady = false;
}

void updateProgressBar(int progress_ms, int duration_ms, uint16_t bgColor, uint16_t textColor) {
  if (!displayReady) return; 
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

void showVolumeOverlay(int volumePct, uint16_t bgCol, uint16_t textCol) {
  if (volumePct < 0 || volumePct > 100) return;

  int barX = 158;
  int barW = 154;
  int barH = 4;
  int barY = 168;

  int filled = (int)((volumePct / 100.0f) * barW);

  char volStr[5];
  snprintf(volStr, sizeof(volStr), "%d%%", volumePct);

  tft.startWrite(); // hold SPI for entire overlay draw
  
  // label
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(textCol, bgCol);
  tft.drawString("VOL", barX, barY - 10, 1);

  // percentage
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(textCol, bgCol);
  tft.drawString(volStr, barX + barW, barY - 10, 1);

  // bar
  for (int row = barY; row < barY + barH; row++) {
    tft.drawFastHLine(barX, row, filled, textCol);
    tft.drawFastHLine(barX + filled, row, barW - filled, tft.color565(80, 80, 80));
  }

  tft.endWrite();

  volumeOverlayShownAt = millis();
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
  uint16_t calData[5] = { 463, 3404, 209, 3557, 7 };
  tft.setTouch(calData);
}

void initTJpegDecoder() {
  TJpgDec.setJpgScale(2);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
}