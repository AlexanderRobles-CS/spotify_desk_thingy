// devices.h
#pragma once

#include <Arduino.h>

struct Device {
  String name;
  String type;
  String id;
  bool   active;
  int    volume;
};

extern Device* deviceList;
extern int     deviceCount;

const int ROW_HEIGHT = 50;
const int HEADER_H   = 30;
const int SCREEN_W   = 320;
const int SCREEN_H   = 240;