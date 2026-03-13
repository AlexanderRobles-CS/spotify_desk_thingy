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
static uint16_t      bgColor          = TFT_BLACK;
static uint16_t      textColor        = TFT_WHITE;

// FreeRTOS async fetch
static volatile bool fetchPending = false;
static volatile bool fetchDone    = false;
static response      fetchedData;

void spotifyFetchTask(void* param) {
  fetchedData = sp.current_playback_state();
  fetchDone    = true;
  fetchPending = false;
  vTaskDelete(NULL);
}

void initSpotify() {
  sp.begin();
  Serial.println("Connected to Spotify API.");
}

void updatePlayback() {
  unsigned long now = millis();

  if (now - lastApiCall > 5000 && !fetchPending) {
    fetchPending = true;
    fetchDone    = false;
    xTaskCreatePinnedToCore(spotifyFetchTask, "spotify", 8192, NULL, 1, NULL, 0);
    lastApiCall  = now;
  }

  if (fetchDone) {
    fetchDone = false;
    response data = fetchedData;

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
          bgColor   = getAverageColor();
          textColor = getTextColor(bgColor);
          updateTrackInfo(track, artists, bgColor, textColor);
          lastSong  = id;
        }
      }

      lastProgressSync = now;
    }
  }

  int displayProgress = progress_ms;
  if (playing) displayProgress += (now - lastProgressSync);

  int progress_sec = displayProgress / 1000;
  int duration_sec = duration_ms / 1000;
  int progress_min = progress_sec / 60;
  int progress_rem = progress_sec % 60;
  int duration_min = duration_sec / 60;
  int duration_rem = duration_sec % 60;

  updateProgressBar(displayProgress, duration_ms, bgColor, textColor);

  static unsigned long lastPrint = 0;
  if (now - lastPrint > 1000) {
    lastPrint = now;
    Serial.printf("Image URL: %s\n", imageUrl.c_str());
    Serial.printf("Currently playing: %s - %s\n", track.c_str(), artists.c_str());
    Serial.printf("Time: %d:%02d / %d:%02d\n", progress_min, progress_rem, duration_min, duration_rem);
  }
}