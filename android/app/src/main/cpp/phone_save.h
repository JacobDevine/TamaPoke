#pragma once
// The phone's save backup: the board's EXPORT block, as a .tpsave file.
//
// A board backs up over its serial console, and the web installer keeps what it
// reads as a .tpsave file. A phone has no console, so the app writes the same
// file itself and hands it to the share sheet. It is the SAME format, byte for
// byte (phone_test compares it against the console's EXPORT), so a backup taken
// on the phone restores on a board through the installer and the other way round.
//
// verify runs on BOTH directions, as web/savefile.js's verifyBackup() does: the
// file handed to the player is checked by the same function that checks the
// file they hand back. A backup is only worth having if it was checked when it
// was taken.
//
// No Android dependency, so tools/emu/tests/phone_test.cpp links it as is.
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// CRC-16/CCITT-FALSE, save.cpp's checksum. Pinned in phone_test to the published
// check value ("123456789" -> 0x29B1), not to a second copy of itself.
uint16_t tpsaveCrc16(const uint8_t *p, size_t n);

// The console's EXPORT block for a blob saveExport() produced.
std::string tpsaveFormat(const uint8_t *blob, size_t n);

// Parses a .tpsave and checks everything about it that can be checked without
// touching the save: the lines, the commit, the declared length (a short read
// still ends with a plausible commit line), the magic, the version and the
// checksum. On success fills blob; on failure says why, in English, for the log.
bool tpsaveVerify(const std::string &text, std::vector<uint8_t> *blob, std::string *why);

// The live save as a verified .tpsave. False (and why) if it could not be made,
// which a player must hear about: a backup that silently is not one is worse
// than none.
bool tpsaveExport(std::string *text, std::string *why);

// tamapoke-<trainer>-YYYY-MM-DD-HH-MM-SS.tpsave, web/savefile.js's
// backupFileName(). localEpoch is the board's local-time convention (rtcEpoch()).
std::string tpsaveFileName(const char *trainer, uint32_t localEpoch, const char *prefix = "tamapoke");
