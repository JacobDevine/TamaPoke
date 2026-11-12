// TamaPoke on Android: the desktop emulator's platform layer, for a phone.
//
// Same arrangement as tools/emu/main_sdl.cpp -- the firmware sources compile
// UNMODIFIED against the stubs in tools/emu/, and only the edges are replaced:
//
//   window/input   SDL2 (its Android backend), finger events as the touch INT
//   NVS            the emulator's file-backed store, in the app's private dir
//   SD card        the committed web/sprites-*.pak bundles, read in place
//                  out of the APK -- the shared TPK2 parser in host_impl.cpp
//                  does the rest via hostSlurp()
//   RTC            the phone's wall clock (TAMA_HOST_WALLCLOCK)
//   serial         stdout piped to logcat, tag "TamaPoke"
//
// Backgrounding the app is treated as switching the board OFF: the save is
// flushed, and on return the gap goes through Pet::syncClock() -- the offline
// catch-up path with its floors -- exactly as a power cycle would. See
// emuClockSkipGap() in clock.cpp for why the gap must not ALSO reach millis().
#include <SDL.h>
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <deque>
#include <thread>
#include <chrono>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "dex.h"
#include "pet.h"
#include "rtcbat.h"
#include "save.h"
#include "i18n.h"
#include "phone_save.h"
#include "phone_remind.h"
#include "phone_clock.h"
#include "fork.h"

// --- Arduino runtime globals (main_sdl.cpp's twins) ---
uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

static std::string g_savePath;

static void saveNow() {
  if (g_savePath.empty()) return;
  // NVS on the board commits key by key; here the whole store is one file, so
  // make that file's replacement atomic. A kill mid-write leaves the old save.
  std::string tmp = g_savePath + ".tmp";
  nvsSave(tmp.c_str());
  if (rename(tmp.c_str(), g_savePath.c_str()) != 0)
    __android_log_print(ANDROID_LOG_ERROR, "TamaPoke", "save: rename failed");
}

// --- calling MainActivity, from this (the SDL) thread ---
static jobject activityRef(JNIEnv **env) {
  *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  return *env ? (jobject)SDL_AndroidGetActivity() : nullptr;
}
static jmethodID activityMethod(JNIEnv *env, jobject act, const char *name, const char *sig) {
  jclass cls = env->GetObjectClass(act);
  jmethodID m = env->GetMethodID(cls, name, sig);
  env->DeleteLocalRef(cls);
  if (!m) {
    env->ExceptionClear();
    __android_log_print(ANDROID_LOG_ERROR, "TamaPoke", "MainActivity.%s%s missing", name, sig);
  }
  return m;
}
// Strings are passed as jstrings built here; ints and longs as they are.
static void callVoid(const char *name, const char *sig, ...) {
  JNIEnv *env;
  jobject act = activityRef(&env);
  if (!act) return;
  if (jmethodID m = activityMethod(env, act, name, sig)) {
    va_list ap;
    va_start(ap, sig);
    env->CallVoidMethodV(act, m, ap);
    va_end(ap);
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
  }
  env->DeleteLocalRef(act);
}

// ESP.restart() ends a WIPE, a RESET GAME and a restore. The store holds what
// the next boot must find, so persisting it is what makes each one stick; the
// app then starts afresh, as the board would reboot.
void FakeESP::restart() {
  Serial.println("android: ESP.restart() -> relaunching");
  saveNow();
  callVoid("relaunch", "()V");
  exit(0);
}

// --- serial in: none yet. The board's console is a debug tool; a phone has no
// stdin. Kept as an empty queue so handleSerial() runs its ordinary path.
static std::deque<std::string> g_lines;
int FakeSerial::available() { return g_lines.empty() ? 0 : 1; }
String FakeSerial::readStringUntil(char) {
  if (g_lines.empty()) return String("");
  String s(g_lines.front());
  g_lines.pop_front();
  return s;
}

// --- serial out -> logcat. Serial.printf is std::printf in the stub, and an
// Android process's stdout goes nowhere, so pipe it into the log a line at a time.
static void *logPump(void *fdp) {
  int fd = *(int *)fdp;
  char buf[1024];
  std::string line;
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\n') { __android_log_write(ANDROID_LOG_INFO, "TamaPoke", line.c_str()); line.clear(); }
      else line += buf[i];
    }
  }
  return nullptr;
}
static void redirectStdio() {
  static int fds[2];
  if (pipe(fds) != 0) return;
  setvbuf(stdout, nullptr, _IOLBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  dup2(fds[1], 1);
  dup2(fds[1], 2);
  pthread_t t;
  pthread_create(&t, nullptr, logPump, &fds[0]);
  pthread_detach(t);
}

// --- sprites: an index over the .pak bundles inside the APK ---
//
// TPAK, little-endian (tools/pack_bundle.py):
//   "TPAK" u16 count, count x { u8 nameLen, name, u32 size }, then the data
// The bundles are stored uncompressed (noCompress in build.gradle), so a seek
// into one is a seek, not a re-inflate from the start.
struct PakEntry { int pak; uint32_t offset, size; };
static std::vector<SDL_RWops *> g_paks;
static std::map<std::string, PakEntry> g_pakIndex;

static bool readExact(SDL_RWops *rw, void *dst, size_t n) {
  return SDL_RWread(rw, dst, 1, n) == n;
}

static void indexPak(const std::string &asset) {
  SDL_RWops *rw = SDL_RWFromFile(asset.c_str(), "rb");
  if (!rw) return;   // this build did not ship the region: it stays locked
  char magic[4];
  uint16_t count = 0;
  if (!readExact(rw, magic, 4) || memcmp(magic, "TPAK", 4) != 0 || !readExact(rw, &count, 2)) {
    SDL_RWclose(rw);
    return;
  }
  std::vector<std::pair<std::string, uint32_t>> names;
  for (uint16_t i = 0; i < count; i++) {
    uint8_t len = 0;
    if (!readExact(rw, &len, 1)) { SDL_RWclose(rw); return; }
    std::string name(len, '\0');
    uint32_t size = 0;
    if ((len && !readExact(rw, &name[0], len)) || !readExact(rw, &size, 4)) { SDL_RWclose(rw); return; }
    names.push_back({ name, size });
  }
  const int pak = (int)g_paks.size();
  g_paks.push_back(rw);
  uint32_t at = (uint32_t)SDL_RWtell(rw);
  for (auto &n : names) {
    g_pakIndex[n.first] = { pak, at, n.second };
    at += n.second;
  }
  printf("android: %s -> %u files\n", asset.c_str(), (unsigned)count);
}

static void indexAllPaks() {
  // Derived from REGIONS, never a second list of region names (CLAUDE.md:
  // "the same trap wearing a table instead of a width").
  for (uint8_t r = 0; r < REGION_COUNT; r++) {
    if (r == REGION_ALL) continue;
    std::string name = REGIONS[r].name;
    for (auto &c : name) c = (char)tolower((unsigned char)c);
    indexPak("sprites-" + name + ".pak");
  }
}

// host_impl.cpp asks for "<spriteDir>/pNNN.bin"; spriteDir is "mons", which is
// exactly how the bundles name their entries.
uint8_t *hostSlurp(const std::string &path, uint32_t *size) {
  auto it = g_pakIndex.find(path);
  if (it == g_pakIndex.end() || it->second.size == 0) return nullptr;
  const PakEntry &e = it->second;
  SDL_RWops *rw = g_paks[e.pak];
  if (SDL_RWseek(rw, e.offset, RW_SEEK_SET) < 0) return nullptr;
  uint8_t *b = (uint8_t *)malloc(e.size);
  if (!b) return nullptr;
  if (!readExact(rw, b, e.size)) { free(b); return nullptr; }
  *size = e.size;
  return b;
}

// The firmware's sdScanRegionArt() (sdmon.cpp), asking the index instead of
// SD_MMC. The probe species come from the same regionArtProbe() in pet.cpp,
// so a region is unlocked here on exactly the evidence a card would need.
void sdScanRegionArt(bool verbose) {
  uint16_t mask = 0;
  for (uint8_t r = 0; r < REGION_COUNT; r++) {
    if (r == REGION_ALL) continue;
    bool all = true;
    for (int i = 0; i < 3 && all; i++) {
      int16_t probe = regionArtProbe(r, (uint8_t)i);
      if (!probe) { all = false; break; }
      char path[24];
      snprintf(path, sizeof(path), "mons/p%03u.bin", (unsigned)probe);
      if (!g_pakIndex.count(path)) all = false;
    }
    if (all) mask |= (uint16_t)(1u << r);
    if (verbose) printf("art: %-6s %s\n", REGIONS[r].name, all ? "yes" : "NO (pack not in this build)");
  }
  gRegionArt = mask;
}

// --- the battery: the phone's own, read through SDL (host_impl.cpp leaves
// these to us under TAMA_HOST_BATTERY). Polled at most every 30 s.
static int g_batPct = -1;
static bool g_batChg = false;
static void batRefresh() {
  static uint32_t last = 0;
  if (last && SDL_GetTicks() - last < 30000) return;
  last = SDL_GetTicks();
  int pct = -1;
  SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
  g_batPct = (st == SDL_POWERSTATE_NO_BATTERY || st == SDL_POWERSTATE_UNKNOWN) ? -1 : pct;
  g_batChg = st == SDL_POWERSTATE_CHARGING || st == SDL_POWERSTATE_CHARGED;
}
int batPercent() { batRefresh(); return g_batPct; }
bool batCharging() { batRefresh(); return g_batChg; }

// --- the sketch ---
void emuSetSpriteDir(const char *);
void emuClockSkipGap();
void setup();
void loop();
uint8_t uiCurrentScreen();
void uiBackKey();
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
void flushBeforePowerLoss(const char *why);

#define PANEL 466
// The panel's shape, owned by the sketch (TamaPoke.ino § "the panel's shape").
extern int16_t gPanelH, gInsetTop, gInsetBot, gGfxOriginY, gFrameSquareY;
extern bool gPanelRound;

// The system UI's insets, in physical pixels, read from MainActivity (which
// takes them off the window's display cutout). They arrive after the window is
// laid out, so this is polled rather than read once.
static void readInsets(int *top, int *bot) {
  *top = *bot = 0;
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  jobject activity = (jobject)SDL_AndroidGetActivity();
  if (!env || !activity) return;
  jclass cls = env->GetObjectClass(activity);
  jfieldID ft = env->GetStaticFieldID(cls, "insetTop", "I");
  jfieldID fb = env->GetStaticFieldID(cls, "insetBottom", "I");
  if (ft && fb) {
    *top = env->GetStaticIntField(cls, ft);
    *bot = env->GetStaticIntField(cls, fb);
  } else {
    env->ExceptionClear();
  }
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
}

// --- touch: every state is held long enough to be SEEN ---
//
// handleTouch() polls the panel at most every 20 ms ("50 Hz is plenty for a
// finger") and this loop runs at vsync, ~16 ms. A quick tap delivers DOWN and
// UP inside one SDL_PollEvent batch; applied together, or even one frame apart,
// the press can fall between two polls and the tap simply vanishes. Found with
// `adb shell input tap`, whose press lasts ~17 ms -- a brisk finger is no slower.
// The board never needed this: the CST9217 reports a physical finger, which
// cannot be that brief.
//
// So every touch goes through one queue, and each event carries how long the
// PREVIOUS one must have been visible before it may replace it. A real finger's
// motion waits for nothing; a press change waits TOUCH_HOLD_MS, comfortably over
// the poll interval. One queue, one timing rule -- the Back button's drag below
// first shipped as its own per-frame script and failed for exactly this reason.
#define TOUCH_HOLD_MS 45
struct TouchEv { int x, y; int8_t press; uint16_t afterMs; };   // press: 1 down, 0 up, -1 move
static std::deque<TouchEv> g_touchQ;

static void touchApply() {
  static uint32_t lastApplied = 0;
  while (!g_touchQ.empty()) {
    const TouchEv t = g_touchQ.front();
    if (millis() - lastApplied < t.afterMs) break;
    g_touchQ.pop_front();
    lastApplied = millis();
    g_touchX = t.x; g_touchY = t.y;
    if (t.press == 1) g_touchDown = true;
    else if (t.press == 0) g_touchDown = false;
    emuFireInterrupt();   // the sketch ignores the panel until INT fires
    if (t.afterMs) break;   // a timed event gets at least one loop() to itself
  }
}

// --- the system back button ---
//
// Back leaves the screen; it never turns a page. It used to be played in as a
// downward drag, but on a rectangle a down-swipe pages back first
// (uiSwipePage), so Back on a Pokedex's third view would have shown the second.
// uiBackKey() is the down-swipe's BACK path with the finger's wake rules. On
// the main screen there is nothing to back out of, so Back does what it does in
// any Android app and sends the task to the background.
static bool onMainScreen() { return !strcmp(SCREEN_NAME[uiCurrentScreen()], "main"); }

static void moveTaskToBack() {
  JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  jobject activity = (jobject)SDL_AndroidGetActivity();
  if (!env || !activity) return;
  jclass cls = env->GetObjectClass(activity);
  jmethodID m = env->GetMethodID(cls, "moveTaskToBack", "(Z)Z");
  if (m) env->CallBooleanMethod(activity, m, JNI_TRUE);
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(activity);
}

// --- the speaker ---
//
// audio.cpp runs here unchanged: its task renders 16 kHz stereo and writes it
// to "I2S", which audio_host/ESP_I2S.h hands to hostAudioWrite(). On the board
// the write blocks on the codec's DMA; here it blocks while the device already
// holds AUDIO_AHEAD_MS, which paces the task the same way. Short, because an
// effect cuts into the music only after what is already queued has played;
// long enough to ride out the audio thread being scheduled late.
#define AUDIO_RATE 16000        // audio.cpp's SAMPLE_RATE and gbsynth's GB_RATE
#define AUDIO_AHEAD_MS 60
static SDL_AudioDeviceID g_audioDev = 0;

static void audioOpen() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    __android_log_print(ANDROID_LOG_WARN, "TamaPoke", "no audio: %s", SDL_GetError());
    return;
  }
  SDL_AudioSpec want = {}, have = {};
  want.freq = AUDIO_RATE;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = nullptr;   // queued, not pulled: the task pushes, as to I2S
  // No allowed changes: SDL converts to whatever the phone's output really is.
  g_audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!g_audioDev) {
    __android_log_print(ANDROID_LOG_WARN, "TamaPoke", "no audio device: %s", SDL_GetError());
    return;
  }
  SDL_PauseAudioDevice(g_audioDev, 0);
}

size_t hostAudioWrite(const uint8_t *data, size_t length) {
  if (!g_audioDev) {
    // No device: keep the task's timing anyway, or it would spin.
    std::this_thread::sleep_for(std::chrono::microseconds((uint64_t)(length / 4) * 1000000 / AUDIO_RATE));
    return length;
  }
  const Uint32 ahead = AUDIO_RATE * 4 * AUDIO_AHEAD_MS / 1000;
  while (SDL_GetQueuedAudioSize(g_audioDev) > ahead) SDL_Delay(4);
  SDL_QueueAudio(g_audioDev, data, (Uint32)length);
  return length;
}

// Backgrounded is switched off: nothing plays, and what was queued is dropped
// rather than heard as a stale fragment on return. A paused device does not
// drain, so the task simply blocks in hostAudioWrite() until it resumes.
static void audioPause(bool off) {
  if (!g_audioDev) return;
  SDL_PauseAudioDevice(g_audioDev, off ? 1 : 0);
  if (off) SDL_ClearQueuedAudio(g_audioDev);
}

// --- the save backup (phone_save.h): the console's EXPORT, as a file ---
//
// The settings screen's BACK UP and RESTORE call these (gHostSaveBackup and
// gHostSaveRestore in the sketch). The share sheet and the file picker are
// MainActivity's; everything that reads or changes the save happens here, on
// the game's own thread. What the UI thread hands back is only queued.
extern void (*gHostSaveBackup)();
extern void (*gHostSaveRestore)();
extern bool saveInhibited;
static_assert(LANG_ES == 0 && LANG_EN == 1 && LANG_FR == 2 && LANG_DE == 3 && LANG_IT == 4 &&
              LANG_PT == 5 && LANG_COUNT == 6, "MainActivity.LANGS lists the languages in this order");
enum { TOAST_BACKUP_FAILED, TOAST_NOT_A_SAVE, TOAST_RESTORE_FAILED, TOAST_NOT_KEPT };   // MainActivity.toast()
static void toast(int which) { callVoid("toast", "(II)V", (jint)gLang, (jint)which); }

static void hostSaveBackup() {
  flushBeforePowerLoss("backup");   // what is on screen is what is backed up
  std::string text, why;
  if (!tpsaveExport(&text, &why)) {
    printf("backup: FAILED: %s\n", why.c_str());
    toast(TOAST_BACKUP_FAILED);
    return;
  }
  const std::string name = tpsaveFileName(pet.trainerName, rtcEpoch());
  printf("backup: %s, %u bytes of text\n", name.c_str(), (unsigned)text.size());
  JNIEnv *env;
  jobject act = activityRef(&env);
  if (!act) return;
  if (jmethodID m = activityMethod(env, act, "shareBackup", "(Ljava/lang/String;Ljava/lang/String;I)V")) {
    jstring jn = env->NewStringUTF(name.c_str()), jt = env->NewStringUTF(text.c_str());
    env->CallVoidMethod(act, m, jn, jt, (jint)gLang);
    env->DeleteLocalRef(jn);
    env->DeleteLocalRef(jt);
  }
  env->DeleteLocalRef(act);
}

static void hostSaveRestore() { callVoid("pickRestore", "()V"); }

// A minigame hit (gameBuzz() in the sketch): MainActivity.haptic().
extern void (*gHostHaptic)(uint8_t kind);
static void hostHaptic(uint8_t kind) { callVoid("haptic", "(I)V", (jint)kind); }

static std::mutex g_restoreMu;
static std::string g_restoreText;
static bool g_restoreOffered = false, g_restoreConfirmed = false;
static std::vector<uint8_t> g_restoreBlob;   // verified, waiting for the player's yes

extern "C" JNIEXPORT void JNICALL
Java_org_tamapoke_app_MainActivity_nativeRestoreOffered(JNIEnv *env, jclass, jstring text) {
  const char *c = text ? env->GetStringUTFChars(text, nullptr) : nullptr;
  std::lock_guard<std::mutex> lock(g_restoreMu);
  g_restoreText = c ? c : "";
  g_restoreOffered = true;
  if (c) env->ReleaseStringUTFChars(text, c);
}
extern "C" JNIEXPORT void JNICALL
Java_org_tamapoke_app_MainActivity_nativeRestoreConfirmed(JNIEnv *, jclass) {
  std::lock_guard<std::mutex> lock(g_restoreMu);
  g_restoreConfirmed = true;
}

// Once a frame. A picked file is verified BEFORE the player is asked anything:
// the dialog must never offer a restore that would then be refused. A yes keeps
// the current save in Downloads first, and does not go ahead if it could not;
// saveImport() then validates the whole blob again before it touches the store.
static void restorePoll() {
  std::string text;
  bool offered, confirmed;
  {
    std::lock_guard<std::mutex> lock(g_restoreMu);
    offered = g_restoreOffered;
    confirmed = g_restoreConfirmed;
    g_restoreOffered = g_restoreConfirmed = false;
    text.swap(g_restoreText);
  }
  if (offered) {
    std::string why;
    g_restoreBlob.clear();
    if (!tpsaveVerify(text, &g_restoreBlob, &why)) {
      printf("restore: refused: %s\n", why.c_str());
      g_restoreBlob.clear();
      toast(TOAST_NOT_A_SAVE);
      return;
    }
    printf("restore: %u-byte save verified, asking\n", (unsigned)g_restoreBlob.size());
    callVoid("confirmRestore", "(I)V", (jint)gLang);
  }
  if (!confirmed || g_restoreBlob.empty()) return;
  std::vector<uint8_t> blob;
  blob.swap(g_restoreBlob);

  flushBeforePowerLoss("restore");
  std::string cur, why;
  bool kept = false;
  if (tpsaveExport(&cur, &why)) {
    const std::string name = tpsaveFileName(pet.trainerName, rtcEpoch(), "tamapoke-replaced");
    JNIEnv *env;
    if (jobject act = activityRef(&env)) {
      if (jmethodID m = activityMethod(env, act, "keepReplaced", "(Ljava/lang/String;Ljava/lang/String;)Z")) {
        jstring jn = env->NewStringUTF(name.c_str()), jt = env->NewStringUTF(cur.c_str());
        kept = env->CallBooleanMethod(act, m, jn, jt);
        env->DeleteLocalRef(jn);
        env->DeleteLocalRef(jt);
      }
      env->DeleteLocalRef(act);
    }
    printf("restore: current save %s as %s\n", kept ? "kept" : "NOT kept", name.c_str());
  } else {
    printf("restore: could not export the current save: %s\n", why.c_str());
  }
  if (!kept) { toast(TOAST_NOT_KEPT); return; }
  if (!saveImport(blob.data(), blob.size())) {
    printf("restore: saveImport refused it\n");
    toast(TOAST_RESTORE_FAILED);
    return;
  }
  // The creature in RAM is the one just replaced: nothing may write it back
  // between here and the relaunch (the console's IMPORT and WIPE, the same).
  printf("restore: done, relaunching\n");
  saveInhibited = true;
  ESP.restart();
}

// --- the care reminder (phone_remind.h) ---
//
// Scheduled when the app leaves, from the state the absence will start from,
// and cancelled when it comes back. Its words are the game's own: the
// creature's name and statusMsg()'s line for the first need, in the game's
// language.
static void reminderSchedule() {
  StrId need = S_HUNGRY;
  const int32_t mins = remindDelayMinutes(pet, rtcEpoch(), &need);
  if (mins < 0) { callVoid("cancelReminder", "()V"); return; }
#ifdef TAMA_REMIND_NOW
  // A testing build (-Ptamapoke.remindNow=true): whenever a reminder is due at
  // all, post it a few seconds after leaving rather than waiting for it.
  const jlong delayMs = 5000;
#else
  const jlong delayMs = (jlong)mins * 60000;
#endif
  const char *name = pet.nick[0] ? pet.nick
                   : (pet.speciesId > 0 && pet.speciesId <= DEX_COUNT) ? DEX_TBL[pet.speciesId].name : "TamaPoke";
  printf("reminder: %s \"%s\" in %d min, posting in %lld s\n", name, T(need), (int)mins, (long long)delayMs / 1000);
  JNIEnv *env;
  jobject act = activityRef(&env);
  if (!act) return;
  if (jmethodID m = activityMethod(env, act, "scheduleReminder", "(JLjava/lang/String;Ljava/lang/String;)V")) {
    jstring jn = env->NewStringUTF(name), jt = env->NewStringUTF(T(need));
    env->CallVoidMethod(act, m, delayMs, jn, jt);
    env->DeleteLocalRef(jn);
    env->DeleteLocalRef(jt);
  }
  env->DeleteLocalRef(act);
}

// --- going to the background is switching the board off ---
static void onBackground() {
  audioPause(true);
  phoneClockAdopt(pet);             // the absence starts in today's zone
  pet.lastSeenEpoch = rtcEpoch();   // what syncClock() measures the absence from
  flushBeforePowerLoss("app backgrounded");
  saveNow();
  reminderSchedule();
}
static void onForeground() {
  callVoid("cancelReminder", "()V");
  audioPause(false);
  emuClockSkipGap();
  pet.syncClock(rtcEpoch());         // measured in the zone it left in...
  phoneClockAdopt(pet);              // ...then the new zone, without aging
  saveNow();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  redirectStdio();
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");      // fingers only, no doubled taps
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, "TamaPoke", "SDL: %s", SDL_GetError());
    return 1;
  }
  audioOpen();   // before setup(), whose audioBegin() starts the task
  // The fork's features (android/FEATURES_PLAN.md) exist only here: the board,
  // the desktop emulator and its --shot screenshots all leave this off, which
  // is what keeps the board's shots byte-identical. Before setup(), whose
  // forkData.begin() reads the switch.
  forkSetFeatures(true);

  const char *dir = SDL_AndroidGetInternalStoragePath();
  g_savePath = std::string(dir ? dir : ".") + "/tamapoke.nvs";
  nvsLoad(g_savePath.c_str());
  phoneClockBegin();   // the zone the save was written in, for setup()'s syncClock()
  emuSetSpriteDir("mons");
  indexAllPaks();

  SDL_Window *win = SDL_CreateWindow("TamaPoke", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  // The canvas: the full width at the board's 466 px, and as tall as the
  // screen's shape allows -- about 1010 on a 1440x3120 phone. Set BEFORE
  // setup(), so the sketch never sees another shape. The board keeps 466x466.
  int outW = PANEL, outH = PANEL;
  SDL_GetRendererOutputSize(ren, &outW, &outH);
  const int canvasH = outH > outW ? (outH * PANEL + outW / 2) / outW : PANEL;
  gfx->resize(PANEL, canvasH);
  gPanelH = canvasH;
  gPanelRound = false;
  gGfxOriginY = gFrameSquareY = (canvasH - PANEL) / 2;
  int insetPxTop = -1, insetPxBot = -1;
  auto applyInsets = [&]() {
    int t, b;
    readInsets(&t, &b);
    if (t == insetPxTop && b == insetPxBot) return;
    insetPxTop = t; insetPxBot = b;
    // physical px -> canvas px, rounded up: clearing the cutout is the point
    gInsetTop = (int16_t)((t * PANEL + outW - 1) / outW);
    gInsetBot = (int16_t)((b * PANEL + outW - 1) / outW);
    printf("android: insets %d/%d px -> %d/%d canvas\n", t, b, gInsetTop, gInsetBot);
  };
  applyInsets();

  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, PANEL, canvasH);
  SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);

  gHostSaveBackup = hostSaveBackup;
  gHostSaveRestore = hostSaveRestore;
  gHostHaptic = hostHaptic;
  setup();
  phoneClockAdopt(pet);   // setup() has measured the absence; now today's zone
  // A reminder left from the last run is moot: the creature is on screen.
  callVoid("cancelReminder", "()V");

  // The panel is round; the phone is not. The board hides the square's corners
  // behind its bezel, and the first build masked them black here -- which left
  // a small disc floating in a tall black screen. Now the frame is shown
  // square, edge to edge, and the bands above and below continue it (below).
  //
  // The scene screens already paint all 466x466. Most menus do NOT: they clear
  // to black and fill a disc of radius 231, so their corners would show as
  // black wedges. A frame of that shape is recognised and its corners given the
  // disc's own colour. Only black pixels outside the disc change, so anything
  // the firmware draws out there (the rim scrollbar, the axis dots) survives.
  std::vector<uint32_t> px(PANEL * canvasH);
  std::vector<uint8_t> offDisc(PANEL * PANEL);
  for (int y = 0; y < PANEL; y++)
    for (int x = 0; x < PANEL; x++) {
      int dx = x - 233, dy = y - 233;
      offDisc[y * PANEL + x] = dx * dx + dy * dy > 231 * 231;
    }
  // Four points just inside the disc's rim. Chrome can sit on one of them, so
  // the disc colour is whatever at least two agree on.
  auto discColour = [](const uint16_t *fb, uint16_t *out) -> bool {
    if (fb[0] != 0 || fb[PANEL * PANEL - 1] != 0) return false;   // corners not black
    const int r = 226;
    const uint16_t s[4] = { fb[(233 - r) * PANEL + 233], fb[(233 + r) * PANEL + 233],
                            fb[233 * PANEL + 233 - r],   fb[233 * PANEL + 233 + r] };
    for (int i = 0; i < 4; i++)
      for (int j = i + 1; j < 4; j++)
        if (s[i] == s[j] && s[i] != 0) { *out = s[i]; return true; }
    return false;
  };
  auto to8888 = [](uint16_t c) -> uint32_t {
    uint32_t r = ((c >> 11) & 31) * 255 / 31;
    uint32_t g = ((c >> 5) & 63) * 255 / 63;
    uint32_t b = (c & 31) * 255 / 31;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
  };

  // Sharp scaling. 466 -> 1440 is x3.09, and nearest-neighbour at a fractional
  // ratio makes every third pixel a column wider: lines wobble and text looks
  // chunky. Linear alone is blurry. So: nearest up to the next WHOLE multiple
  // (x4, every pixel identical), then linear down the last fraction. Edges stay
  // crisp and every pixel comes out the same size.
  SDL_Texture *up = nullptr;
  int upK = 0;
  SDL_Rect dst = { 0, 0, PANEL, canvasH };
  auto layout = [&]() {
    SDL_GetRendererOutputSize(ren, &outW, &outH);
    // the canvas at full width; its height was derived from this shape, so it
    // lands within a pixel of the screen's -- centred, in case of rounding
    const int h = canvasH * outW / PANEL;
    dst = { 0, (outH - h) / 2, outW, h };
    int k = (outW + PANEL - 1) / PANEL;
    if (k < 1) k = 1;
    if (!up || upK != k) {
      if (up) SDL_DestroyTexture(up);
      upK = k;
      up = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, k * PANEL, k * canvasH);
      if (up) SDL_SetTextureScaleMode(up, SDL_ScaleModeLinear);
      else printf("android: no render target (%s), scaling nearest\n", SDL_GetError());
    }
    printf("android: screen %dx%d, canvas %dx%d, x%d then linear\n", outW, outH, PANEL, canvasH, k);
  };
  layout();
  uint32_t lastInsetPoll = 0;

  SDL_FingerID finger = 0;
  bool haveFinger = false;
  uint32_t lastSave = SDL_GetTicks();
  bool run = true;
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: run = false; break;
        case SDL_APP_WILLENTERBACKGROUND: onBackground(); break;
        case SDL_APP_DIDENTERFOREGROUND: onForeground(); layout(); break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) layout();
          break;
        // A GL context lost behind the app's back takes texture contents with
        // it; the next present would show garbage until the screen changed.
        case SDL_RENDER_TARGETS_RESET:
        case SDL_RENDER_DEVICE_RESET:
          if (gfx) gfx->frameReady = true;
          break;
        case SDL_KEYDOWN:
          if (e.key.keysym.sym == SDLK_AC_BACK && !haveFinger && !g_touchDown && g_touchQ.empty()) {
            const bool root = onMainScreen();
            printf("android: back (%s)\n", root ? "to background" : "leave screen");
            if (root) moveTaskToBack();
            else uiBackKey();
          }
          break;
        // One finger is the panel's one touch point. A second finger is
        // ignored until the first lifts, as the CST9217 reports a single point
        // to this firmware.
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
        case SDL_FINGERUP: {
          if (e.type == SDL_FINGERDOWN && haveFinger) break;
          if (e.type != SDL_FINGERDOWN && (!haveFinger || e.tfinger.fingerId != finger)) break;
          int w = 0, h = 0;
          SDL_GetRendererOutputSize(ren, &w, &h);
          float sx = e.tfinger.x * w, sy = e.tfinger.y * h;
          // canvas coordinates; the sketch maps them for a square screen itself
          int px_ = (int)((sx - dst.x) * PANEL / dst.w);
          int py_ = (int)((sy - dst.y) * canvasH / dst.h);
          px_ = px_ < 0 ? 0 : px_ >= PANEL ? PANEL - 1 : px_;
          py_ = py_ < 0 ? 0 : py_ >= canvasH ? canvasH - 1 : py_;
          int8_t press = -1;
          if (e.type == SDL_FINGERDOWN) { finger = e.tfinger.fingerId; haveFinger = true; press = 1; }
          else if (e.type == SDL_FINGERUP) { haveFinger = false; press = 0; }
          g_touchQ.push_back({ px_, py_, press, (uint16_t)(press >= 0 ? TOUCH_HOLD_MS : 0) });
          break;
        }
      }
    }
    touchApply();
    restorePoll();
    loop();

    // The game saves on its own schedule into the in-memory store; this puts
    // that store on disk. Android may kill a backgrounded process without
    // another word, so do not rely on the background hook alone.
    if (SDL_GetTicks() - lastSave > 30000) {
      lastSave = SDL_GetTicks();
      phoneClockAdopt(pet);   // a zone change with the app open (a landing, DST)
      saveNow();
    }

    // The cutout's insets arrive once the window is laid out; keep asking for
    // the first few seconds, then rarely.
    if (SDL_GetTicks() - lastInsetPoll > (SDL_GetTicks() < 5000 ? 250u : 5000u)) {
      lastInsetPoll = SDL_GetTicks();
      applyInsets();
    }

    if (gfx && gfx->frameReady) {
      gfx->frameReady = false;
      const uint16_t *fb = gfx->buffer();
      const int sq = gFrameSquareY;
      if (sq >= 0 && sq + PANEL <= canvasH) {
        // A SQUARE frame, centred. Fill a disc-shaped menu's corners in its
        // own colour, then continue the square's first and last rows into the
        // bands above and below: the sky runs up, the ground runs down.
        const uint16_t *sfb = fb + (size_t)sq * PANEL;
        uint16_t disc = 0;
        const bool fillCorners = discColour(sfb, &disc);
        for (int i = 0; i < PANEL * PANEL; i++) {
          uint16_t c = sfb[i];
          if (fillCorners && c == 0 && offDisc[i]) c = disc;
          px[(size_t)sq * PANEL + i] = to8888(c);
        }
        // Each band is ONE colour, the commonest in that edge row. Stretching
        // the row itself turned any lone pixel on it -- a star, a cloud's
        // edge -- into a line running the height of the band.
        auto dominant = [&](size_t row) -> uint32_t {
          std::map<uint32_t, int> n;
          uint32_t best = px[row * PANEL];
          int bestN = 0;
          for (int x = 0; x < PANEL; x++) {
            int c = ++n[px[row * PANEL + x]];
            if (c > bestN) { bestN = c; best = px[row * PANEL + x]; }
          }
          return best;
        };
        const uint32_t topC = dominant((size_t)sq), botC = dominant((size_t)(sq + PANEL - 1));
        std::fill(px.begin(), px.begin() + (size_t)sq * PANEL, topC);
        std::fill(px.begin() + (size_t)(sq + PANEL) * PANEL, px.end(), botC);
      } else {
        for (int i = 0; i < PANEL * canvasH; i++) px[i] = to8888(fb[i]);
      }
      SDL_UpdateTexture(tex, nullptr, px.data(), PANEL * 4);
    }
    SDL_Texture *src = tex;
    if (up && SDL_SetRenderTarget(ren, up) == 0) {
      SDL_RenderCopy(ren, tex, nullptr, nullptr);   // nearest, whole multiple
      SDL_SetRenderTarget(ren, nullptr);
      src = up;
    }
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, src, nullptr, &dst);
    SDL_RenderPresent(ren);   // vsync paces the loop
  }

  flushBeforePowerLoss("app quit");
  saveNow();
  if (up) SDL_DestroyTexture(up);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
