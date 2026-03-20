#include <Arduino.h>
#include "time.h"
#include "desk_idle.h"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 0;

struct tm timeinfo;

void printLocalTime() {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
}

void initTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  Serial.print("Waiting for NTP sync");
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nTime synced!");
  printLocalTime();
}