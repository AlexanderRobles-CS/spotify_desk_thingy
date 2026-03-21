#include <Arduino.h>
#include "time.h"
#include "desk_idle.h"

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -28800;  // UTC-8
const int  daylightOffset_sec = 3600; // auto DST

struct tm timeinfo;
char timeStr[32];
char dateStr[32];

void printLocalTime() {
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  
  strftime(timeStr, sizeof(timeStr), "%I:%M %p", &timeinfo);  // 09:45 PM
  strftime(dateStr, sizeof(dateStr), "%B %d, %Y", &timeinfo); // March 20, 2026

  Serial.println(timeStr);
  Serial.println(dateStr);
}
void initTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  Serial.print("Waiting for NTP sync");
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nTime synced!");
  printLocalTime();
}