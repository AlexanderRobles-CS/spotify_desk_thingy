#pragma once
#include <SpotifyEsp32.h>

extern Spotify sp;

void initSpotify();
void updatePlayback();
void fetchAndDisplay();
void parseDevices(JsonDocument& doc);
void handleDeviceTouch();