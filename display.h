#pragma once

#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <HTTPClient.h> 

extern TFT_eSPI tft;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
bool updateSpotifyImage(String spotifyImageURL);
uint16_t getAverageColor();
uint16_t getTextColor(uint16_t bgColor);
void buildScrollSprites(String track, String artists, uint16_t bgColor, uint16_t textColor);
void updateScrollSprites();
void resetScroll();
bool sample_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
void initSPIFFS();
void initTFTScreen();
void initTJpegDecoder();
void updateProgressBar(int progress_ms, int duration_ms, uint16_t bgColor, uint16_t textColor);
void markProgressDirty();
void markTrackDirty();