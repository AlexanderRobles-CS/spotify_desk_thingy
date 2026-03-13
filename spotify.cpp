#include <Arduino.h>
#include <SpotifyEsp32.h>
#include "spotify.h"
#include "secrets.h"

const char* CLIENT_ID     = SPOTIFY_CLIENT_ID;
const char* CLIENT_SECRET = SPOTIFY_CLIENT_SECRET;
const char* REFRESH_TOKEN = SPOTIFY_REFRESH_TOKEN;

Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);

void initSpotify() {
  sp.begin();
  Serial.println("Connected to Spotify API.");
}