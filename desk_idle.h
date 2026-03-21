#pragma once

extern const char* ntpServer;
extern const long  gmtOffset_sec;
extern const int   daylightOffset_sec;
extern struct tm timeinfo;
extern char timeStr[32];
extern char dateStr[32];

void printLocalTime();
void initTime();