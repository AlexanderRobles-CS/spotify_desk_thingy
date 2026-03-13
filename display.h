#pragma once

#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <HTTPClient.h> 

extern TFT_eSPI tft;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
bool updateSpotifyImage(String spotifyImageURL);
void updateTrackInfo(String trackName, String artistName);
void initSPIFFS();
void initTFTScreen();
void initTJpegDecoder();
String truncate(String text, int maxChars);