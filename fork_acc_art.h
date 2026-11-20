#pragma once
#include <stdint.h>

// ANDROID FORK: the accessories' art (fork.h section 14). Original pixel art,
// drawn for this fork -- nothing here is taken from another project.
//
// One character a pixel, '.' transparent. The letters index FORK_ACC_PAL
// below. Each map is drawn at the creature's own pixel scale, so an
// accessory's pixels are the same size as the sprite's.
//
// `sit` is how the map meets the head: HAT maps are centred on the head and
// sink `overlap` rows into it; SIDE maps sit on the head's upper right (a bow,
// a flower); BROW maps sink further, across the forehead (goggles).

enum : uint8_t { FORK_ACC_SIT_HAT = 0, FORK_ACC_SIT_SIDE, FORK_ACC_SIT_BROW };

struct ForkAccArt {
  uint8_t w, h, sit, overlap;
  const char *const *rows;
};

// RGB565, by letter.
struct ForkAccInk { char k; uint16_t c; };
static const ForkAccInk FORK_ACC_PAL[] = {
  { 'K', 0x18C3 },   // outline
  { 'R', 0xE124 },   // red
  { 'D', 0x9800 },   // dark red
  { 'P', 0xFB56 },   // pink
  { 'Y', 0xFE60 },   // yellow
  { 'O', 0xDC40 },   // gold shade
  { 'B', 0x2B7D },   // blue
  { 'L', 0x6D9F },   // light blue
  { 'V', 0x780F },   // purple
  { 'U', 0xA19F },   // light purple
  { 'N', 0x2945 },   // near black
  { 'G', 0x6B4D },   // grey
  { 'W', 0xFFFF },   // white
  { 'C', 0x07FF },   // cyan glass
  { 'E', 0x2589 },   // green gem
};

static const char *const ACC_BOW[] = {
  "KK.......KK",
  "KRRK...KRRK",
  "KRPRKKKRRRK",
  "KRRRKDKRRRK",
  "KRRRKKKRRDK",
  "KRRK...KDDK",
  "KK.......KK",
};
static const char *const ACC_FLOWER[] = {
  ".KK.KK.",
  "KPPKPPK",
  "KPPYPPK",
  ".KYYYK.",
  "KPPYPPK",
  "KPPKPPK",
  ".KK.KK.",
};
static const char *const ACC_CAP[] = {
  "....KKKKK.....",
  "..KKBBBBBKK...",
  ".KBBLLBBBBBK..",
  ".KBLLBBBBBBK..",
  ".KBBBBBBBBBKK.",
  ".KKKKKKKKKKLLK",
  "..........KKKK",
};
static const char *const ACC_WITCH[] = {
  "..........KK..",
  "........KKVK..",
  ".......KVVK...",
  "......KVVVK...",
  ".....KVVVUK...",
  ".....KVVVUK...",
  "....KVVVVVUK..",
  "....KYYYYYYK..",
  "...KVVVVVVVUK.",
  "KKKKKKKKKKKKKK",
  "KVVVVVVVVVVVUK",
  "KKKKKKKKKKKKKK",
};
static const char *const ACC_TOPHAT[] = {
  "..KKKKKKKK..",
  "..KNNNNNGK..",
  "..KNNNNNGK..",
  "..KNNNNNGK..",
  "..KNNNNNGK..",
  "..KRRRRRRK..",
  "..KNNNNNGK..",
  "KKKKKKKKKKKK",
  "KNNNNNNNNNGK",
  "KKKKKKKKKKKK",
};
static const char *const ACC_CROWN[] = {
  "K....K....K",
  "KK..KYK..KK",
  "KYK.KYK.KYK",
  "KYYKYYYKYYK",
  "KYYYYYYYYYK",
  "KYRYYEYYRYK",
  "KOOOOOOOOOK",
  "KKKKKKKKKKK",
};
static const char *const ACC_GOGGLES[] = {
  "..KKKK....KKKK..",
  ".KGCCCK..KGCCCK.",
  "KKGCWCKKKKGCWCKK",
  "KBKCCCKBBKKCCCKB",
  "..KKKK....KKKK..",
};
static const char *const ACC_SANTA[] = {
  "..........KK.",
  "........KKWWK",
  "......KKRKWWK",
  ".....KRRRKKK.",
  "....KRRRRK...",
  "...KRRRRRDK..",
  "..KRRRRRRDK..",
  ".KKKKKKKKKKK.",
  "KWWWWWWWWWWWK",
  "KWWWWWWWWWWWK",
  "KKKKKKKKKKKKK",
};

// By FORK_ACC_* number; [0] is NONE.
static const ForkAccArt FORK_ACC_ART[] = {
  { 0, 0, FORK_ACC_SIT_HAT, 0, nullptr },
  { 11, 7, FORK_ACC_SIT_SIDE, 4, ACC_BOW },
  { 7, 7, FORK_ACC_SIT_SIDE, 4, ACC_FLOWER },
  { 14, 7, FORK_ACC_SIT_HAT, 2, ACC_CAP },
  { 14, 12, FORK_ACC_SIT_HAT, 2, ACC_WITCH },
  { 12, 10, FORK_ACC_SIT_HAT, 2, ACC_TOPHAT },
  { 11, 8, FORK_ACC_SIT_HAT, 1, ACC_CROWN },
  { 16, 5, FORK_ACC_SIT_BROW, 4, ACC_GOGGLES },
  { 13, 11, FORK_ACC_SIT_HAT, 2, ACC_SANTA },
};
