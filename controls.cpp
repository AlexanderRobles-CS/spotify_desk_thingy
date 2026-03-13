#include <Arduino.h>
#include "controls.h"

const int playbackPin = 12;

volatile bool         playbackButtonPressed        = false;
volatile unsigned long last_playback_interrupt_time = 0;

void IRAM_ATTR handlePlaybackButtonPress() {
  if (digitalRead(playbackPin) == LOW) {
    unsigned long t = millis();
    if (t - last_playback_interrupt_time > 200) {
      playbackButtonPressed        = true;
      last_playback_interrupt_time = t;
    }
  }
}

void initControls() {
  pinMode(playbackPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(playbackPin), handlePlaybackButtonPress, FALLING);
}