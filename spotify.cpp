#include <Arduino.h>
#include <SpotifyEsp32.h>
#include "esp_task_wdt.h"
#include "spotify.h"
#include "secrets.h"
#include "display.h"
#include "controls.h"
#include "devices.h"

const char* CLIENT_ID     = SPOTIFY_CLIENT_ID;
const char* CLIENT_SECRET = SPOTIFY_CLIENT_SECRET;
const char* REFRESH_TOKEN = SPOTIFY_REFRESH_TOKEN;

Spotify sp(CLIENT_ID, CLIENT_SECRET, REFRESH_TOKEN);

// ─── Device list (owned here, declared extern in devices.h) ───────
Device* deviceList = nullptr;
int     deviceCount = 0;

// ─── State machine ────────────────────────────────────────────────

enum SpotifyState {
  STATE_IDLE,
  STATE_FETCHING,
  STATE_TOGGLING,
  STATE_SHOWING_DEVICES
};

static SpotifyState state = STATE_IDLE;

// ─── Local playback state ─────────────────────────────────────────

static unsigned long lastApiCall      = 0;
static unsigned long lastProgressSync = 0;
static unsigned long lastButtonPress  = 0;
static unsigned long lastVolumeChange = 0;
static int           lastSentVolume   = -1;
static int           s_volumeToSend   = 0;
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

// ─── Shared fetch state (written by task, read by main loop) ──────

static volatile bool fetchDone        = false;
static volatile bool toggleDone       = false;
static volatile bool prevDone         = false;
static volatile bool skipDone         = false;
static volatile bool deviceFetchDone  = false;
static volatile bool volumeDone       = false;
static char s_track[128]              = "";
static char s_artists[256]            = "";
static char s_imageUrl[128]           = "";
static char s_playlist[128]           = "";
static char s_id[64]                  = "";
static char s_deviceId[64]            = "";
static int  s_volume                  = 0;
static int  s_progress_ms             = 0;
static int  s_duration_ms             = 0;
static bool s_playing                 = false;

// ─── Tasks ────────────────────────────────────────────────────────

void spotifyFetchTask(void* param) {
  Serial.println("[FETCH] task started");
  esp_task_wdt_add(NULL);

  response* result = new response();
  *result = sp.current_playback_state();
  esp_task_wdt_reset();

  Serial.printf("[FETCH] status: %d\n", result->status_code);

  if (result->status_code == 200 && !result->reply.isNull()) {
    strlcpy(s_track,    result->reply["item"]["name"].as<const char*>(),                      sizeof(s_track));
    strlcpy(s_id,       result->reply["item"]["id"].as<const char*>(),                        sizeof(s_id));
    strlcpy(s_imageUrl, result->reply["item"]["album"]["images"][1]["url"].as<const char*>(), sizeof(s_imageUrl));
    strlcpy(s_playlist, result->reply["context"]["uri"].as<const char*>(),                    sizeof(s_playlist));
    strlcpy(s_deviceId, result->reply["device"]["id"].as<const char*>(),                      sizeof(s_deviceId));
    s_volume      = result->reply["device"]["volume_percent"] | 0;
    s_progress_ms = result->reply["progress_ms"];
    s_duration_ms = result->reply["item"]["duration_ms"];
    s_playing     = result->reply["is_playing"];

    int artistCount = result->reply["item"]["artists"].size();
    s_artists[0] = '\0';
    for (int i = 0; i < artistCount; i++) {
      if (i > 0) strlcat(s_artists, ", ", sizeof(s_artists));
      strlcat(s_artists, result->reply["item"]["artists"][i]["name"].as<const char*>(), sizeof(s_artists));
    }
  } else {
    Serial.println("[FETCH] bad/empty response");
  }

  fetchDone = true;
  delete result;
  esp_task_wdt_delete(NULL);
  Serial.println("[FETCH] task ending");
  vTaskDelete(NULL);
}

void deviceFetchTask(void* param) {
  Serial.println("[DEVICES] task started");
  esp_task_wdt_add(NULL);

  response* result = new response();
  *result = sp.available_devices();
  esp_task_wdt_reset();

  Serial.printf("[DEVICES] status: %d\n", result->status_code);

  if (result->status_code == 200 && !result->reply.isNull()) {
    parseDevices(result->reply);
  } else {
    Serial.println("[DEVICES] bad/empty response");
  }

  deviceFetchDone = true;
  delete result;
  esp_task_wdt_delete(NULL);
  Serial.println("[DEVICES] task ending");
  vTaskDelete(NULL);
}

void skipControlTask(void* param) {
  Serial.printf("[SKIP] task started");
  sp.skip();
  skipDone = true;
  Serial.println("[SKIP] task ending");
  vTaskDelete(NULL);
}

void prevControlTask(void* param) {
  Serial.printf("[PREVIOUS] task started");
  sp.previous();
  prevDone = true;
  Serial.println("[PREVIOUS] task ending");
  vTaskDelete(NULL);
}

void playbackControlTask(void* param) {
  Serial.printf("[TOGGLE] task started, playing=%d\n", playing);
  if (playing) {
    sp.start_resume_playback(s_deviceId[0] != '\0' ? s_deviceId : nullptr);
  } else {
    sp.pause_playback();
  }
  toggleDone = true;
  Serial.println("[TOGGLE] task ending");
  vTaskDelete(NULL);
}

void volumeControlTask(void* param) {
  Serial.printf("[VOLUME] task started, sending %d\n", s_volumeToSend);
  sp.set_volume(s_volumeToSend);
  volumeDone = true;
  Serial.println("[VOLUME] task ending");
  vTaskDelete(NULL);
}

// ─── Helpers ──────────────────────────────────────────────────────

void parseDevices(JsonDocument& doc) {
  delete[] deviceList;
  deviceList  = nullptr;
  deviceCount = 0;

  JsonArray arr = doc["devices"].as<JsonArray>();
  deviceCount = arr.size();
  if (deviceCount == 0) return;

  deviceList = new Device[deviceCount];

  int i = 0;
  for (JsonObject d : arr) {
    deviceList[i].name   = d["name"]           | "Unknown";
    deviceList[i].type   = d["type"]           | "?";
    deviceList[i].id     = d["id"]             | "";
    deviceList[i].active = d["is_active"]      | false;
    deviceList[i].volume = d["volume_percent"] | 0;
    i++;
  }

  Serial.printf("[DEVICES] parsed %d device(s)\n", deviceCount);
  for (int i = 0; i < deviceCount; i++) {
    Serial.printf("  [%d] %s (%s)\n", i, deviceList[i].name.c_str(), deviceList[i].id.c_str());
  }
}

static void startFetch() {
  fetchDone   = false;
  state       = STATE_FETCHING;
  lastApiCall = millis();
  Serial.println("[STATE] → FETCHING");
  xTaskCreatePinnedToCore(spotifyFetchTask, "spotify", 16384, NULL, 1, NULL, 0);
}

static void startDeviceFetch() {
  deviceFetchDone = false;
  state           = STATE_TOGGLING;
  Serial.println("[STATE] → FETCHING_DEVICES");
  xTaskCreatePinnedToCore(deviceFetchTask, "devices", 16384, NULL, 1, NULL, 0);
}

static void startSkip() {
  skipDone         = false;
  progress_ms      = 0;
  lastProgressSync = millis();
  state            = STATE_TOGGLING;
  Serial.println("[STATE] → SKIP");
  xTaskCreatePinnedToCore(skipControlTask, "skip", 8192, NULL, 1, NULL, 0);
}

static void startPrev() {
  prevDone         = false;
  progress_ms      = 0;
  lastProgressSync = millis();
  state            = STATE_TOGGLING;
  Serial.println("[STATE] → PREVIOUS");
  xTaskCreatePinnedToCore(prevControlTask, "previous", 8192, NULL, 1, NULL, 0);
}

static void startToggle() {
  toggleDone      = false;
  playing         = !playing;
  lastButtonPress = millis();

  if (!playing) {
    progress_ms      += (int)(millis() - lastProgressSync);
    lastProgressSync  = millis();
  } else {
    lastProgressSync = millis();
  }

  state = STATE_TOGGLING;
  Serial.printf("[STATE] → TOGGLING (playing=%d)\n", playing);
  xTaskCreatePinnedToCore(playbackControlTask, "playback", 8192, NULL, 1, NULL, 0);
}

static void startVolume() {
  volumeDone     = false;
  s_volumeToSend = lastSentVolume;
  lastVolumeChange = 0;
  state          = STATE_TOGGLING;
  Serial.printf("[STATE] → VOLUME (%d)\n", s_volumeToSend);
  xTaskCreatePinnedToCore(volumeControlTask, "volume", 8192, NULL, 1, NULL, 0);
}

static void handleVolume() {
  int diff = abs(count - lastSentVolume);
  if (diff > 1 && diff < 50) {
    lastVolumeChange = millis();
    lastSentVolume   = count;
  }
}

void handleDeviceTouch() {
  uint16_t x, y;
  if (!tft.getTouch(&x, &y, 20)) return;

  int availableH = SCREEN_H - HEADER_H - 12;
  int rowH       = min(ROW_HEIGHT, availableH / deviceCount);
  int row        = (y - HEADER_H) / rowH;
  if (row < 0 || row >= deviceCount) return;

  Serial.printf("[TOUCH] tapped device %d: %s\n", row, deviceList[row].name.c_str());

  sp.transfer_playback(deviceList[row].id.c_str());

  // flash tap indicator
  int tapY = HEADER_H + row * rowH + rowH / 2;
  tft.fillCircle(300, tapY, 6, TFT_YELLOW);
  delay(250);

  // update active flags locally before redraw
  for (int i = 0; i < deviceCount; i++) {
    deviceList[i].active = (i == row);
  }

  drawDevices();
  delay(1000);

  // go back to main screen
  tft.fillScreen(bgColor);
  lastSong = "";
  displayReady = true;
  updateSpotifyImage(imageUrl);
  bgColor   = getAverageColor();
  textColor = getTextColor(bgColor);
  buildScrollSprites(track, artists, bgColor, textColor);
  lastApiCall = millis();
  state = STATE_IDLE;
}

static void applyFetchResult() {
  static bool volumeInitialized = false;
  if (!volumeInitialized) {
    Serial.printf("[VOLUME] initializing encoder to %d\n", s_volume);
    count          = s_volume;
    lastSentVolume = s_volume;
    volumeInitialized = true;
  }

  track       = s_track;
  id          = s_id;
  artists     = s_artists;
  imageUrl    = s_imageUrl;
  playlistURI = s_playlist;
  progress_ms = s_progress_ms;
  duration_ms = s_duration_ms;

  if (millis() - lastButtonPress > 2000) {
    playing = s_playing;
  }

  if (lastSong != id) {
    displayReady = false;
    if (updateSpotifyImage(imageUrl)) {
      bgColor   = getAverageColor();
      textColor = getTextColor(bgColor);
      buildScrollSprites(track, artists, bgColor, textColor);
      lastSong  = id;
    }
  }

  lastProgressSync = millis();
  Serial.println("[STATE] → IDLE");
}

// ─── Init ─────────────────────────────────────────────────────────

void initSpotify() {
  sp.begin();
  Serial.println("Connected to Spotify API.");
}

// ─── Main loop ────────────────────────────────────────────────────

void updatePlayback() {
  unsigned long now = millis();

  if (!displayReady) return;

  switch (state) {

    case STATE_SHOWING_DEVICES:
      handleDeviceTouch();
      return;

    case STATE_IDLE:
      if (playbackButtonPressed) { playbackButtonPressed = false; startToggle(); break; }
      if (prevButtonPressed)     { prevButtonPressed = false;     startPrev();   break; }
      if (skipButtonPressed)     { skipButtonPressed = false;     startSkip();   break; }

      handleVolume();
      if (millis() - lastVolumeChange > 200 && lastVolumeChange != 0) {
        startVolume();
        break;
      }

      if (now - lastApiCall > 5000) {
        startFetch();
      }
      break;

    case STATE_FETCHING:
      handleVolume();
      if (fetchDone) {
        fetchDone = false;
        applyFetchResult();
        state = STATE_IDLE;
        if (playbackButtonPressed) {
          playbackButtonPressed = false;
          startToggle();
        }
      }
      break;

    case STATE_TOGGLING:
      handleVolume();
      if (toggleDone)     { toggleDone = false;     startFetch(); }
      if (prevDone)       { prevDone   = false;      startFetch(); }
      if (skipDone)       { skipDone   = false;      startFetch(); }
      if (volumeDone)     { volumeDone = false;      startFetch(); }
      if (deviceFetchDone) {
        deviceFetchDone = false;
        drawDevices();
        state = STATE_SHOWING_DEVICES;
      }
      break;
  }

  // ─── Progress (only runs when NOT showing devices) ────────────
  int displayProgress = progress_ms;
  if (playing) displayProgress += (int)(now - lastProgressSync);

  int progress_sec = displayProgress / 1000;
  int duration_sec = duration_ms / 1000;
  int progress_min = progress_sec / 60;
  int progress_rem = progress_sec % 60;
  int duration_min = duration_sec / 60;
  int duration_rem = duration_sec % 60;

  if (state == STATE_IDLE && duration_ms > 0 && displayProgress >= duration_ms) {
    startFetch();
    return;
  }

  static int lastProgressSec = -1;
  if (progress_sec != lastProgressSec) {
    lastProgressSec = progress_sec;
    markProgressDirty();
  }

  updateScrollSprites();
  int clampedProgress = min(displayProgress, duration_ms);
  updateProgressBar(clampedProgress, duration_ms, bgColor, textColor);

  static unsigned long lastPrint = 0;
  if (now - lastPrint > 1000) {
    lastPrint = now;
    Serial.printf("Currently playing: %s - %s\n", track.c_str(), artists.c_str());
    Serial.printf("Time: %d:%02d / %d:%02d\n", progress_min, progress_rem, duration_min, duration_rem);
  }
}