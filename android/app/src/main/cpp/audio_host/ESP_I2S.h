// The board's audio path on a phone: just enough FreeRTOS and I2S for the REAL
// audio.cpp to run unchanged, with the I2S "DMA" being SDL's audio queue.
//
// The same arrangement as tools/emu/tests/audio_stubs/, which is how audio_test
// runs audio.cpp on a PC -- but that one is a deterministic, single-stepped
// fake for assertions. This one is real time: a real thread, a real blocking
// queue, and a write() that blocks while enough audio is already queued, which
// is what paces the task exactly as the codec's DMA does on the board.
//
// Only audio.cpp includes this (as <ESP_I2S.h>), so these names stay out of
// every other translation unit.
#pragma once
#include "Arduino.h"
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

using QueueHandle_t = void *;
using TaskFunction_t = void (*)(void *);
constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int I2S_MODE_STD = 0;
constexpr int I2S_DATA_BIT_WIDTH_16BIT = 16;
constexpr int I2S_SLOT_MODE_STEREO = 2;
constexpr int I2S_STD_SLOT_BOTH = 3;

inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }   // a tick is a millisecond
inline void digitalWrite(int, int) {}   // the speaker amp's enable pin: nothing to switch

// Defined in android_main.cpp, which owns the SDL audio device. Blocks while
// the device already holds enough to play; returns the bytes taken.
size_t hostAudioWrite(const uint8_t *data, size_t length);

// One FreeRTOS queue: fixed-size items, a capacity, a sender that never waits
// (audio.cpp sends with 0 ticks and drops on a full queue) and a receiver that
// may wait up to a timeout.
struct HostQueue {
  std::mutex m;
  std::condition_variable cv;
  std::deque<std::vector<uint8_t>> items;
  size_t itemSize, capacity;
};

inline QueueHandle_t xQueueCreate(size_t count, size_t itemSize) {
  HostQueue *q = new HostQueue();   // lives as long as the app, as on the board
  q->itemSize = itemSize;
  q->capacity = count;
  return q;
}

inline int xQueueSend(QueueHandle_t h, const void *item, uint32_t) {
  HostQueue *q = static_cast<HostQueue *>(h);
  {
    std::lock_guard<std::mutex> lock(q->m);
    if (q->items.size() >= q->capacity) return pdFALSE;
    const uint8_t *b = static_cast<const uint8_t *>(item);
    q->items.emplace_back(b, b + q->itemSize);
  }
  q->cv.notify_one();
  return pdTRUE;
}

inline int xQueueReceive(QueueHandle_t h, void *item, uint32_t waitMs) {
  HostQueue *q = static_cast<HostQueue *>(h);
  std::unique_lock<std::mutex> lock(q->m);
  if (q->items.empty() && waitMs)
    q->cv.wait_for(lock, std::chrono::milliseconds(waitMs), [q] { return !q->items.empty(); });
  if (q->items.empty()) return pdFALSE;
  memcpy(item, q->items.front().data(), q->itemSize);
  q->items.pop_front();
  return pdTRUE;
}

inline int xTaskCreatePinnedToCore(TaskFunction_t task, const char *, uint32_t, void *arg,
                                   uint32_t, void *, int) {
  std::thread(task, arg).detach();   // the task never returns, as on the board
  return pdTRUE;
}

class I2SClass {
public:
  void setPins(int, int, int, int, int) {}
  bool begin(int, int, int, int, int) { return true; }
  size_t write(uint8_t *data, size_t length) { return hostAudioWrite(data, length); }
};
