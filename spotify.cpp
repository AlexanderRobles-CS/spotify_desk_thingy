#include <Arduino.h>
#include <SpotifyEsp32.h>
#include "spotify.h"
#include "secrets.h"
#include "display.h"

const char* CLIENT_ID     = SPOTIFY_CLIENT_ID;
const char* CLIENT_SECRET = SPOTIFY_CLIENT_SECRET;
const char* REFRESH_TOKEN = SPOTIFY_REFRESH_TOKEN;

Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);

// State
static unsigned long lastApiCall      = 0;
static unsigned long lastProgressSync = 0;
static int           progress_ms      = 0;
static int           duration_ms      = 0;
static bool          playing          = false;
static String        track            = "";
static String        id               = "";
static String        artists          = "";
static String        imageUrl         = "";
static String        playlistURI      = "";
static String        lastSong         = "";

void initSpotify() {
  sp.begin();
  Serial.println("Connected to Spotify API.");
}

void updatePlayback() {
  unsigned long now = millis();

  if (now - lastApiCall > 5000) {
    response data = sp.current_playback_state();

    if (data.status_code == 200 && !data.reply.isNull()) {
      track = data.reply["item"]["name"].as<String>();
      id    = data.reply["item"]["id"].as<String>();

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
        if (updateSpotifyImage(imageUrl)) {
          updateTrackInfo(track, artists);
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