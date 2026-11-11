// The Android app's audio path: the REAL audio.cpp over the phone's FreeRTOS
// and I2S layer (android/app/src/main/cpp/audio_host/ESP_I2S.h), in real time,
// on real threads.
//
// audio_test covers the task's logic with a single-stepped fake; this covers
// the layer the phone actually runs it on -- the queue a game-loop thread
// sends into, the task thread that waits on it, and the receive timeout that
// keeps an idle task from spinning. hostAudioWrite() here stands in for SDL's
// audio queue: it records what arrives and blocks for the buffer's duration,
// as a device draining at 16 kHz would. Whether it SOUNDS right is ears-only;
// whether anything reaches the speaker at all is not.
#include "Arduino.h"
#include "../../../audio.cpp"
#include <atomic>
#include <ctime>

uint32_t g_seed = 17;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

static std::atomic<size_t> gSamples{0}, gAudible{0}, gWrites{0};
static std::atomic<bool> gBadBuf{false};

size_t hostAudioWrite(const uint8_t *data, size_t length) {
  if (!length || length % 4) gBadBuf = true;   // stereo 16-bit frames only
  const int16_t *pcm = reinterpret_cast<const int16_t *>(data);
  size_t loud = 0;
  for (size_t i = 0; i < length / 2; i++) if (pcm[i]) loud++;
  gSamples += length / 4;
  gAudible += loud;
  gWrites++;
  std::this_thread::sleep_for(std::chrono::microseconds((uint64_t)(length / 4) * 1000000 / 16000));
  return length;
}

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  fflush(stdout);
  if (!ok) bad++;
}
static void waitMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
// Waits until no audio has been written for a while: the task has gone idle.
static bool settle(int limitMs) {
  for (int t = 0; t < limitMs; t += 100) {
    const size_t w = gWrites;
    waitMs(100);
    if (gWrites == w) return true;
  }
  return false;
}

int main() {
  audioBegin();   // starts the task on its own thread, and queues the boot jingle
  ck(gReady && gQ != nullptr, "audioBegin() comes up: the fake codec answers and the queue exists");
  waitMs(300);
  ck(gAudible > 0, "the boot jingle reaches the speaker from the task's own thread");
  ck(settle(3000), "and the task goes quiet after it");

  // Idle: the task waits on the queue with a timeout and writes nothing.
  size_t s0 = gSamples;
  const std::clock_t c0 = std::clock();
  waitMs(400);
  const double cpuMs = 1000.0 * (std::clock() - c0) / CLOCKS_PER_SEC;
  ck(gSamples == s0, "an idle task writes nothing");
  // ... and does not spin. The task polls the queue with a 40 ms wait; a
  // receive that returned at once would burn a core, which on a phone is the
  // battery. Process CPU time over 400 ms idle: a few ms waiting, ~400 spinning.
  printf("      idle CPU over 400 ms: %.1f ms\n", cpuMs);
  ck(cpuMs < 100, "an idle task sleeps rather than spinning");

  // An effect sent from this (the game loop's) thread is heard.
  s0 = gSamples; size_t a0 = gAudible;
  sfxPlay(SFX_LEVEL);
  waitMs(300);
  ck(gAudible > a0, "sfxPlay() from another thread is heard");
  ck(settle(3000), "and ends");

  // A cry too: the same queue, a wider request.
  a0 = gAudible;
  audioCry(25);
  waitMs(300);
  ck(gAudible > a0, "a cry is heard");
  ck(settle(3000), "and ends");

  // Music plays in real time: about a second of samples a second, neither
  // stalled nor racing ahead of the device.
  audioMusic(MUS_BATTLE);
  waitMs(200);
  s0 = gSamples; a0 = gAudible;
  waitMs(1000);
  const size_t got = gSamples - s0;
  printf("      1 s of battle music: %zu samples\n", got);
  ck(got > 16000 * 7 / 10 && got < 16000 * 13 / 10, "battle music runs at the device's rate");
  ck(gAudible > a0, "and is audible");
  audioMusic(MUS_NONE);
  ck(settle(2000), "MUS_NONE stops it");

  // Volume 0 keeps the timing and makes no sound.
  gVol = 0;
  s0 = gSamples; a0 = gAudible;
  audioMusic(MUS_BATTLE);
  waitMs(500);
  ck(gSamples > s0 && gAudible == a0, "volume 0 keeps playing in silence");
  audioMusic(MUS_NONE);
  settle(2000);
  gVol = 7;

  // Sound off is no sound at all, and no writes.
  gOn = false;
  s0 = gSamples;
  audioMusic(MUS_BATTLE);
  sfxPlay(SFX_TAP);
  waitMs(400);
  ck(gSamples == s0, "with sound off nothing is written");
  audioMusic(MUS_NONE);

  ck(!gBadBuf, "every buffer is whole stereo frames");
  if (bad) { printf("\nFAIL phoneaudio_test (%d)\n", bad); fflush(stdout); std::_Exit(1); }
  printf("\nPASS phoneaudio_test\n");
  fflush(stdout);
  std::_Exit(0);   // the task thread never returns, as on the board
}
