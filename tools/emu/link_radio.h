#pragma once
#include "linknow.h"

// The emulator's and the phone's stand-in for linknow.cpp (the board's ESP-NOW).
//
// host_impl.cpp defines the linkNow*() functions the sketch calls. With nothing
// set here they answer "no radio", which is what every test sees and what the
// tests were written against (tall_test checks the "no radio" message). The
// Android app and the desktop emulator's --net set it to phone_net.cpp's UDP
// transport (android/app/src/main/cpp/phone_net.h, plan step 12e).
struct LinkRadio {
  bool (*begin)(Link *l) = nullptr;
  void (*end)() = nullptr;
  bool (*up)() = nullptr;
  void (*poll)() = nullptr;
};
extern LinkRadio gLinkRadio;
