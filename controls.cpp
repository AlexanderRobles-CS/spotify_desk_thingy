#include <Arduino.h>
#include "controls.h"

const int playbackPin = 12;
const int prevPin     = 13;
const int skipPin     = 14;

volatile bool          playbackButtonPressed        = false;
volatile unsigned long last_playback_interrupt_time = 0;

volatile bool          prevButtonPressed            = false;
volatile unsigned long last_prev_interrupt_time     = 0;

volatile bool          skipButtonPressed            = false;
volatile unsigned long last_skip_interrupt_time     = 0;

void IRAM_ATTR handlePlaybackButtonPress() {
  if (digitalRead(playbackPin) == LOW) {
    unsigned long t = millis();
    if (t - last_playback_interrupt_time > 200) {
      playbackButtonPressed        = true;
      last_playback_interrupt_time = t;
    }
  }
}

void IRAM_ATTR handlePrevButtonPress() {
  if (digitalRead(prevPin) == LOW) {
    unsigned long t = millis();
    if (t - last_prev_interrupt_time > 200) {
      prevButtonPressed        = true;
      last_prev_interrupt_time = t;
    }
  }
}

void IRAM_ATTR handleSkipButtonPress() {
  if (digitalRead(skipPin) == LOW) {
    unsigned long t = millis();
    if (t - last_skip_interrupt_time > 200) {
      skipButtonPressed        = true;
      last_skip_interrupt_time = t;
    }
  }
}

void initControls() {
  pinMode(playbackPin, INPUT_PULLUP);
  pinMode(prevPin, INPUT_PULLUP);
  pinMode(skipPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(playbackPin), handlePlaybackButtonPress, FALLING);
  attachInterrupt(digitalPinToInterrupt(prevPin), handlePrevButtonPress, FALLING);
  attachInterrupt(digitalPinToInterrupt(skipPin), handleSkipButtonPress, FALLING);
}