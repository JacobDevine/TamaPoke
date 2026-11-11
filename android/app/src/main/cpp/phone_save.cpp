#include "phone_save.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "save.h"

uint16_t tpsaveCrc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)p[i] << 8;
    for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

// The layout of TamaPoke.ino's EXPORT handler: a header, 48 bytes of hex a
// line, then the bare IMPORT that commits. phone_test compares the two outputs.
std::string tpsaveFormat(const uint8_t *blob, size_t n) {
  std::string out;
  char line[64];
  snprintf(line, sizeof(line), "# TamaPoke save, %u bytes. Paste this whole block back.\n", (unsigned)n);
  out += line;
  for (size_t i = 0; i < n; i += 48) {
    out += "IMPORT ";
    for (size_t j = i; j < i + 48 && j < n; j++) {
      snprintf(line, sizeof(line), "%02X", blob[j]);
      out += line;
    }
    out += "\n";
  }
  out += "IMPORT\n";
  return out;
}

static int nybble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

static bool fail(std::string *why, const std::string &msg) {
  if (why) *why = msg;
  return false;
}

bool tpsaveVerify(const std::string &text, std::vector<uint8_t> *blob, std::string *why) {
  std::vector<uint8_t> b;
  long declared = -1;
  bool committed = false;
  int dataLines = 0;
  size_t at = 0;
  while (at <= text.size()) {
    size_t nl = text.find('\n', at);
    if (nl == std::string::npos) nl = text.size();
    const std::string line = trim(text.substr(at, nl - at));
    at = nl + 1;
    if (line.empty()) continue;
    if (line[0] == '#') {
      // "# TamaPoke save, N bytes" -- other comments (the installer's board
      // line) are ignored, as savefile.js ignores them
      unsigned n = 0;
      if (sscanf(line.c_str(), "# TamaPoke save, %u bytes", &n) == 1) declared = (long)n;
      continue;
    }
    if (committed) return fail(why, "there is content after the final IMPORT line");
    if (line == "IMPORT") { committed = true; continue; }
    if (line.compare(0, 7, "IMPORT ") != 0) return fail(why, "unrecognised line: " + line.substr(0, 32));
    const std::string hex = trim(line.substr(7));
    if (hex.empty() || (hex.size() & 1)) return fail(why, "a data line has an odd number of hex digits");
    for (size_t i = 0; i < hex.size(); i += 2) {
      const int hi = nybble(hex[i]), lo = nybble(hex[i + 1]);
      if (hi < 0 || lo < 0) return fail(why, "a data line holds a non-hex digit");
      b.push_back((uint8_t)((hi << 4) | lo));
    }
    dataLines++;
    if (b.size() > SAVE_TRANSFER_MAX) return fail(why, "larger than any save");
  }
  if (!committed) return fail(why, "missing its final IMPORT line");
  if (!dataLines) return fail(why, "no data, only a commit line");
  if (declared >= 0 && (long)b.size() != declared) {
    char m[80];
    snprintf(m, sizeof(m), "%u bytes but declares %ld", (unsigned)b.size(), declared);
    return fail(why, m);
  }
  if (b.size() < SAVE_HDR + 2) return fail(why, "too short to be a save");
  if (b[0] != SAVE_MAGIC0 || b[1] != SAVE_MAGIC1 || b[2] != SAVE_MAGIC2 || b[3] != SAVE_MAGIC3)
    return fail(why, "not a TamaPoke save");
  if (b[4] != SAVE_VERSION) return fail(why, "an unknown save version");
  const uint16_t want = (uint16_t)(b[b.size() - 2] | (b[b.size() - 1] << 8));
  if (tpsaveCrc16(b.data(), b.size() - 2) != want) return fail(why, "checksum mismatch");
  if (blob) *blob = b;
  return true;
}

bool tpsaveExport(std::string *text, std::string *why) {
  static uint8_t buf[SAVE_TRANSFER_MAX];
  const size_t n = saveExport(buf, sizeof(buf));
  if (!n) return fail(why, "saveExport failed (larger than SAVE_TRANSFER_MAX?)");
  std::string t = tpsaveFormat(buf, n);
  std::vector<uint8_t> back;
  if (!tpsaveVerify(t, &back, why)) return false;
  if (back.size() != n || memcmp(back.data(), buf, n) != 0) return fail(why, "did not read back as written");
  if (text) *text = t;
  return true;
}

std::string tpsaveFileName(const char *trainer, uint32_t localEpoch, const char *prefix) {
  std::string who;
  for (const char *p = trainer ? trainer : ""; *p; p++)
    if (isalnum((unsigned char)*p)) who += *p;
  if (who.empty()) who = "save";
  // localEpoch already IS local wall-clock time, so it is read as UTC
  const time_t t = (time_t)localEpoch;
  struct tm tm;
  gmtime_r(&t, &tm);
  char stamp[32];
  strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H-%M-%S", &tm);
  return std::string(prefix) + "-" + who + "-" + stamp + ".tpsave";
}
