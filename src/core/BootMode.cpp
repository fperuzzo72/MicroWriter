#include "BootMode.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>

#include <cstring>

#include "../ThemeManager.h"
#include "../config.h"
#include "Core.h"
#include "SumiSettings.h"

// Access global renderer from main.cpp
extern GfxRenderer renderer;

namespace sumi {

// Access global core from main.cpp
extern Core core;

// Cached transition for current boot
static ModeTransition cachedTransition = {};
static bool transitionCached = false;

// ─── Dedicated transition state file (audit #41) ──────────────────────
//
// Pre-Batch-3b, BootMode::saveTransition / clearTransition / detectBootMode
// each rewrote the entire settings.bin (1+ KB, 35 fields) to flip 2 bytes.
// A single Reader-mode dual-boot path went through 2-3 full settings
// rewrites just to set pendingTransition + transitionReturnTo, hammering
// the same SD sectors thousands of times per year.
//
// Now: transition state lives in its own /.sumi/transition.bin (208 bytes).
// settings.bin is no longer rewritten on transitions. The Settings struct
// still carries pendingTransition / transitionReturnTo in RAM so the rest
// of the codebase doesn't change; those fields just don't round-trip to
// settings.bin any more (existing settings.bin files leave them at
// whatever value they last carried — irrelevant once transition.bin
// becomes the source of truth).
//
// Migration: on first boot after upgrade, transition.bin doesn't exist
// yet. If settings.bin has a non-zero pendingTransition, we honour it
// once via the legacy-fallback branch in detectBootMode, then write
// transition.bin going forward.
namespace {
constexpr const char* TRANSITION_FILE   = SUMI_DIR "/transition.bin";
constexpr uint32_t   TRANSITION_MAGIC   = 0x4E535254u;  // 'TRSN' little-endian
constexpr uint8_t    TRANSITION_VERSION = 1;
constexpr size_t     BOOK_PATH_BYTES    = sizeof(cachedTransition.bookPath);
constexpr size_t     TRANSITION_BYTES   = 4 /*magic*/ + 1 /*ver*/ + 1 /*reserved*/ +
                                          1 /*pending*/ + 1 /*returnTo*/ +
                                          BOOK_PATH_BYTES;

// Read transition.bin into out. Returns false (and clears out) on any
// read / format issue — caller falls back to legacy settings.bin path.
bool readTransitionFile(uint8_t& outPending, uint8_t& outReturnTo,
                        char (&outBookPath)[BOOK_PATH_BYTES]) {
  outPending = 0;
  outReturnTo = 0;
  outBookPath[0] = '\0';

  FsFile f;
  if (!SdMan.openFileForRead("BOOT", TRANSITION_FILE, f)) {
    return false;  // No file → no pending transition.
  }
  if (f.size() != static_cast<int>(TRANSITION_BYTES)) {
    Serial.printf("[BOOT] transition.bin wrong size %u (want %u)\n",
                  static_cast<unsigned>(f.size()),
                  static_cast<unsigned>(TRANSITION_BYTES));
    f.close();
    return false;
  }

  uint8_t buf[TRANSITION_BYTES];
  if (f.read(buf, TRANSITION_BYTES) != static_cast<int>(TRANSITION_BYTES)) {
    f.close();
    return false;
  }
  f.close();

  const uint32_t magic = static_cast<uint32_t>(buf[0])        |
                        (static_cast<uint32_t>(buf[1]) << 8)  |
                        (static_cast<uint32_t>(buf[2]) << 16) |
                        (static_cast<uint32_t>(buf[3]) << 24);
  if (magic != TRANSITION_MAGIC) {
    Serial.printf("[BOOT] transition.bin bad magic 0x%08X\n", magic);
    return false;
  }
  if (buf[4] != TRANSITION_VERSION) {
    Serial.printf("[BOOT] transition.bin unknown version %u\n", buf[4]);
    return false;
  }

  outPending = buf[6];
  outReturnTo = buf[7];
  std::memcpy(outBookPath, buf + 8, BOOK_PATH_BYTES);
  outBookPath[BOOK_PATH_BYTES - 1] = '\0';  // defensive
  return true;
}

bool writeTransitionFile(uint8_t pending, uint8_t returnTo,
                         const char* bookPath) {
  SdMan.mkdir(SUMI_DIR);

  FsFile f;
  if (!SdMan.atomicOpenWrite("BOOT", TRANSITION_FILE, f)) {
    Serial.printf("[BOOT] atomicOpenWrite failed for %s\n", TRANSITION_FILE);
    return false;
  }

  uint8_t buf[TRANSITION_BYTES] = {};
  buf[0] = static_cast<uint8_t>(TRANSITION_MAGIC);
  buf[1] = static_cast<uint8_t>(TRANSITION_MAGIC >> 8);
  buf[2] = static_cast<uint8_t>(TRANSITION_MAGIC >> 16);
  buf[3] = static_cast<uint8_t>(TRANSITION_MAGIC >> 24);
  buf[4] = TRANSITION_VERSION;
  buf[5] = 0;  // reserved
  buf[6] = pending;
  buf[7] = returnTo;
  // Copy bookPath into the body (bounded by BOOK_PATH_BYTES, NUL-padded).
  if (bookPath && bookPath[0] != '\0') {
    const size_t n = strnlen(bookPath, BOOK_PATH_BYTES - 1);
    std::memcpy(buf + 8, bookPath, n);
    // remaining bytes already zeroed via designated zero-init above
  }

  if (f.write(buf, TRANSITION_BYTES) != static_cast<int>(TRANSITION_BYTES)) {
    SdMan.atomicAbort(f, TRANSITION_FILE);
    return false;
  }
  if (!SdMan.atomicCommit(f, TRANSITION_FILE)) {
    SdMan.atomicAbort(f, TRANSITION_FILE);
    return false;
  }
  return true;
}

void removeTransitionFile() {
  if (SdMan.exists(TRANSITION_FILE)) {
    SdMan.remove(TRANSITION_FILE);
  }
}
}  // namespace

BootMode detectBootMode() {
  Serial.printf("[BOOT] Checking boot mode...\n");

  // Source-of-truth read for the transition state. transition.bin is the
  // primary channel; settings.bin's pendingTransition / transitionReturnTo
  // are kept in the Settings struct for legacy migration only — see the
  // namespace doc-comment above.
  uint8_t pending = 0;
  uint8_t returnTo = 0;
  char    bookPath[BOOK_PATH_BYTES] = {};
  bool    transitionFromFile = readTransitionFile(pending, returnTo, bookPath);

  // Legacy migration: first boot after upgrading to Batch 3b — transition.bin
  // doesn't exist yet, but settings.bin still carries pendingTransition from
  // the pre-upgrade firmware run. Honour it once and write transition.bin
  // going forward.
  if (!transitionFromFile && core.settings.pendingTransition != 0) {
    Serial.printf("[BOOT] Migrating legacy pendingTransition=%u from settings.bin\n",
                  static_cast<unsigned>(core.settings.pendingTransition));
    pending = core.settings.pendingTransition;
    returnTo = core.settings.transitionReturnTo;
    utf8SafeCopy(bookPath, core.settings.lastBookPath, BOOK_PATH_BYTES);
    transitionFromFile = true;  // treat as if read from file, will clear below
  }

  // Pending Emulator transition (3=SumiBoy). The dual-boot lets the GB
  // emulator have the entire ~120 KB heap budget to itself — no plugins
  // / fonts / themes / stats / achievements competing for fragmented
  // contig blocks. Critical where the v2 arena is permanently
  // in .bss.
  if (pending == 3 && bookPath[0] != '\0' && SdMan.exists(bookPath)) {
    Serial.printf("[BOOT] Pending Emulator transition: rom=%s\n", bookPath);
    cachedTransition.magic = ModeTransition::MAGIC;
    cachedTransition.mode = BootMode::EMULATOR;
    cachedTransition.returnTo = static_cast<ReturnTo>(returnTo);
    utf8SafeCopy(cachedTransition.bookPath, bookPath, sizeof(cachedTransition.bookPath));
    transitionCached = true;
    clearTransition();
    return BootMode::EMULATOR;
  }

  // Pending UI transition (1=UI mode)
  if (pending == 1) {
    Serial.printf("[BOOT] Pending UI transition, returnTo=%u\n",
                  static_cast<unsigned>(returnTo));
    cachedTransition.magic = ModeTransition::MAGIC;
    cachedTransition.mode = BootMode::UI;
    cachedTransition.returnTo = static_cast<ReturnTo>(returnTo);
    cachedTransition.bookPath[0] = '\0';
    transitionCached = true;
    clearTransition();
    return BootMode::UI;
  }

  // Pending Reader transition (2=Reader mode)
  if (pending == 2 && bookPath[0] != '\0' && SdMan.exists(bookPath)) {
    Serial.printf("[BOOT] Pending Reader transition: path=%s, returnTo=%u\n",
                  bookPath, static_cast<unsigned>(returnTo));
    cachedTransition.magic = ModeTransition::MAGIC;
    cachedTransition.mode = BootMode::READER;
    cachedTransition.returnTo = static_cast<ReturnTo>(returnTo);
    utf8SafeCopy(cachedTransition.bookPath, bookPath, sizeof(cachedTransition.bookPath));
    transitionCached = true;
    clearTransition();
    return BootMode::READER;
  }

  // No pending transition - check "Last Document" startup behavior setting.
  // This path uses Settings::lastBookPath (a real persisted setting), NOT
  // the transition channel — and the lastBookPath clear that prevents a
  // crash-loop on a bad book is one settings.bin write per boot. That's
  // unavoidable since lastBookPath itself lives in settings.
  if (core.settings.startupBehavior == Settings::StartupLastDocument &&
      core.settings.lastBookPath[0] != '\0' &&
      SdMan.exists(core.settings.lastBookPath)) {
    Serial.printf("[BOOT] 'Last Document' startup: %s\n", core.settings.lastBookPath);

    cachedTransition.magic = ModeTransition::MAGIC;
    cachedTransition.mode = BootMode::READER;
    cachedTransition.returnTo = ReturnTo::HOME;
    utf8SafeCopy(cachedTransition.bookPath, core.settings.lastBookPath,
                 sizeof(cachedTransition.bookPath));
    transitionCached = true;

    // Clear lastBookPath to prevent boot loop if reader fails. ReaderState
    // will re-save on successful open.
    core.settings.lastBookPath[0] = '\0';
    core.settings.saveToFile();

    return BootMode::READER;
  }

  Serial.printf("[BOOT] No transition pending, using default UI mode\n");
  return BootMode::UI;
}

const ModeTransition& getTransition() { return cachedTransition; }

void saveTransition(BootMode mode, const char* bookPath, ReturnTo returnTo) {
  // Mode → encoded byte: 1=UI, 2=Reader, 3=Emulator. Same encoding
  // settings.bin's pendingTransition used pre-Batch-3b so the legacy
  // migration in detectBootMode picks up the same values.
  uint8_t pending = 1;
  if (mode == BootMode::READER)        pending = 2;
  else if (mode == BootMode::EMULATOR) pending = 3;

  // Reader and Emulator modes pass a path (book or ROM). UI mode passes
  // nullptr; we pass through `core.settings.lastBookPath` so the
  // post-restart code still has access to the user's last document.
  const char* persistedPath = "";
  if ((mode == BootMode::READER || mode == BootMode::EMULATOR)
      && bookPath && bookPath[0] != '\0') {
    // Mirror into settings so the "Last Document" path stays current
    // (this is the existing semantics — saveTransition has always done
    // this for non-UI modes). Settings.bin write is tolerable here
    // because it's user-driven (one click on the Reader / dual-boot
    // tile), not boot-time hot-path.
    utf8SafeCopy(core.settings.lastBookPath, bookPath,
                 sizeof(core.settings.lastBookPath));
    core.settings.saveToFile();
    persistedPath = core.settings.lastBookPath;
  } else {
    persistedPath = core.settings.lastBookPath;  // already set; keep for the file
  }

  // Mirror the new state into the in-RAM struct so any code that reads
  // pendingTransition / transitionReturnTo before the next reboot sees
  // a consistent value. These fields no longer round-trip via settings.bin
  // but the schema visitor still serialises them; that's harmless.
  core.settings.pendingTransition = pending;
  core.settings.transitionReturnTo = static_cast<uint8_t>(returnTo);

  if (writeTransitionFile(pending, static_cast<uint8_t>(returnTo), persistedPath)) {
    Serial.printf("[BOOT] Saved transition: mode=%d, returnTo=%d, path=%s\n",
                  static_cast<int>(mode), static_cast<int>(returnTo),
                  persistedPath);
  } else {
    Serial.printf("[BOOT] Saved transition FAILED — falling back to settings.bin\n");
    // Fallback: legacy path. Should be rare (SD full / disk error); if
    // it happens, the next boot will pick up via the legacy-migration
    // branch in detectBootMode.
    core.settings.saveToFile();
  }
}

void clearTransition() {
  // Drop the file rather than write a zeroed version — fewer SD ops,
  // and detectBootMode treats "no file" as "no pending transition"
  // identically. Also clear the in-RAM mirrors so the legacy migration
  // path doesn't accidentally trigger on a subsequent detectBootMode
  // call within the same boot.
  removeTransitionFile();
  core.settings.pendingTransition = 0;
  core.settings.transitionReturnTo = 0;
  Serial.printf("[BOOT] Cleared pending transition\n");
}

bool peekEmulatorTransition(char* outPath, size_t outSize) {
  if (!outPath || outSize == 0) return false;
  outPath[0] = '\0';

  uint8_t pending = 0;
  uint8_t returnTo = 0;
  char    bookPath[BOOK_PATH_BYTES] = {};
  if (!readTransitionFile(pending, returnTo, bookPath)) {
    return false;
  }
  if (pending != 3 || bookPath[0] == '\0') {
    return false;
  }
  utf8SafeCopy(outPath, bookPath, outSize);
  return true;
}

void showTransitionNotification(const char* message) {
  const Theme& theme = THEME_MANAGER.current();

  renderer.clearScreen(theme.backgroundColor);

  // Draw centered message
  const int screenHeight = renderer.getScreenHeight();
  const int y = screenHeight / 2 - 20;

  renderer.drawCenteredText(theme.uiFontId, y, message, theme.primaryTextBlack, REGULAR);

  // Display immediately (partial refresh for speed)
  renderer.displayBuffer();

  Serial.printf("[BOOT] Displayed notification: %s\n", message);
}

}  // namespace sumi
