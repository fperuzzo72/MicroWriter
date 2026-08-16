#include "SDCardManager.h"

namespace {
constexpr uint8_t SD_CS = 12;
constexpr uint32_t SPI_FQ = 40000000;

// RAII guard for the recursive SD bus mutex. Acquired at the entry of every
// public SDCardManager method. Recursive because public methods call other
// public methods (writeFile → openFileForWrite → syncAndClose), and a
// non-recursive mutex would deadlock the second take from the same task.
//
// `mutex == nullptr` is a no-op (early boot before begin(), or a build that
// failed to create the semaphore). The pre-mutex behavior matches the v1
// SUMI runtime, so callers that race begin() see the same semantics they
// did before Batch 4.
class SdLockGuard {
 public:
  explicit SdLockGuard(SemaphoreHandle_t m) : mutex_(m) {
    if (mutex_) xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
  }
  ~SdLockGuard() {
    if (mutex_) xSemaphoreGiveRecursive(mutex_);
  }
  SdLockGuard(const SdLockGuard&) = delete;
  SdLockGuard& operator=(const SdLockGuard&) = delete;

 private:
  SemaphoreHandle_t mutex_;
};
}  // namespace

SDCardManager SDCardManager::instance;

SDCardManager::SDCardManager() : sd() {}

SDCardManager::~SDCardManager() {
  // The singleton's lifetime is the program's; the destructor runs at
  // process exit, which on ESP32 means never. Defensive cleanup for
  // future host-build mocks that construct + destruct.
  if (sdMutex_) {
    vSemaphoreDelete(sdMutex_);
    sdMutex_ = nullptr;
  }
}

bool SDCardManager::begin() {
  // Lazy-create the recursive bus mutex on first begin(). FreeRTOS is
  // expected to be running by the time begin() is called (Arduino's
  // setup() is itself a FreeRTOS task), so xSemaphoreCreateRecursiveMutex
  // is safe here. Static-init order with FreeRTOS readiness is more
  // fragile, so we do it here rather than in the constructor.
  if (!sdMutex_) {
    sdMutex_ = xSemaphoreCreateRecursiveMutex();
    if (!sdMutex_ && Serial) {
      Serial.printf("[%lu] [SD] WARN: failed to create bus mutex; "
                    "falling back to unlocked I/O\n", millis());
    }
  }
  // begin() itself is single-shot from main; no concurrent caller to
  // serialize against here.

  if (!sd.begin(SD_CS, SPI_FQ)) {
    if (Serial) Serial.printf("[%lu] [SD] SD card not detected\n", millis());
    initialized = false;
  } else {
    if (Serial) Serial.printf("[%lu] [SD] SD card detected\n", millis());
    initialized = true;
  }

  return initialized;
}

bool SDCardManager::ready() const { return initialized; }

std::vector<String> SDCardManager::listFiles(const char* path, const int maxFiles) {
  SdLockGuard lock(sdMutex_);
  std::vector<String> ret;
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] not initialized, returning empty list\n", millis());
    return ret;
  }

  auto root = sd.open(path);
  if (!root) {
    if (Serial) Serial.printf("[%lu] [SD] Failed to open directory\n", millis());
    return ret;
  }
  if (!root.isDirectory()) {
    if (Serial) Serial.printf("[%lu] [SD] Path is not a directory\n", millis());
    root.close();
    return ret;
  }

  int count = 0;
  char name[128];
  for (auto f = root.openNextFile(); f && count < maxFiles; f = root.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    f.getName(name, sizeof(name));
    ret.emplace_back(name);
    f.close();
    count++;
  }
  root.close();
  return ret;
}

String SDCardManager::readFile(const char* path) {
  SdLockGuard lock(sdMutex_);
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] not initialized; cannot read file\n", millis());
    return {""};
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    return {""};
  }

  constexpr size_t maxSize = 50000;  // Limit to 50KB
  const size_t fileSize = f.size();
  const size_t toRead = (fileSize < maxSize) ? fileSize : maxSize;

  String content;
  content.reserve(toRead);

  uint8_t buf[256];
  size_t readSize = 0;
  while (f.available() && readSize < toRead) {
    const size_t chunkSize = min(sizeof(buf), toRead - readSize);
    const int n = f.read(buf, chunkSize);
    if (n <= 0) break;
    content.concat(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    readSize += static_cast<size_t>(n);
  }
  f.close();
  return content;
}

bool SDCardManager::readFileToStream(const char* path, Print& out, const size_t chunkSize) {
  SdLockGuard lock(sdMutex_);
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] SD card not initialized\n", millis());
    return false;
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    return false;
  }

  constexpr size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  const size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    const int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, static_cast<size_t>(r));
    } else {
      break;
    }
  }

  f.close();
  return true;
}

size_t SDCardManager::readFileToBuffer(const char* path, char* buffer, const size_t bufferSize, const size_t maxBytes) {
  SdLockGuard lock(sdMutex_);
  if (!buffer || bufferSize == 0) return 0;
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] SD card not initialized\n", millis());
    buffer[0] = '\0';
    return 0;
  }

  FsFile f;
  if (!openFileForRead("SD", path, f)) {
    buffer[0] = '\0';
    return 0;
  }

  const size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;

  while (f.available() && total < maxToRead) {
    constexpr size_t chunk = 64;
    const size_t want = maxToRead - total;
    const size_t readLen = (want < chunk) ? want : chunk;
    const int r = f.read(buffer + total, readLen);
    if (r > 0) {
      total += static_cast<size_t>(r);
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool SDCardManager::writeFile(const char* path, const String& content) {
  SdLockGuard lock(sdMutex_);
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] SD card not initialized\n", millis());
    return false;
  }

  // openFileForWrite uses O_CREAT | O_TRUNC, which already truncates an
  // existing file to zero on open. The previous explicit sd.remove()
  // here was redundant and widened the no-file window between the
  // remove and the create — a power loss in that window would leave the
  // file gone with no replacement. Audit #21.
  FsFile f;
  if (!openFileForWrite("SD", path, f)) {
    return false;
  }

  const size_t written = f.print(content);
  // syncAndClose instead of bare close() so a multi-sector settings
  // or config write isn't silently truncated on dirty shutdown.
  // Everywhere else in the codebase uses this convention; writeFile
  // was the lone outlier.
  syncAndClose(f);
  return written == content.length();
}

bool SDCardManager::ensureDirectoryExists(const char* path) {
  SdLockGuard lock(sdMutex_);
  if (!initialized) {
    if (Serial) Serial.printf("[%lu] [SD] SD card not initialized\n", millis());
    return false;
  }

  // Check if directory already exists
  if (sd.exists(path)) {
    FsFile dir = sd.open(path);
    if (dir && dir.isDirectory()) {
      dir.close();
      return true;
    }
    dir.close();
  }

  // Create the directory
  if (sd.mkdir(path)) {
    if (Serial) Serial.printf("[%lu] [SD] Created directory: %s\n", millis(), path);
    return true;
  } else {
    if (Serial) Serial.printf("[%lu] [SD] Failed to create directory: %s\n", millis(), path);
    return false;
  }
}

bool SDCardManager::openFileForRead(const char* moduleName, const char* path, FsFile& file) {
  SdLockGuard lock(sdMutex_);
  if (!sd.exists(path)) {
    if (Serial) Serial.printf("[%lu] [%s] File does not exist: %s\n", millis(), moduleName, path);
    return false;
  }

  file = sd.open(path, O_RDONLY);
  if (!file) {
    if (Serial) Serial.printf("[%lu] [%s] Failed to open file for reading: %s\n", millis(), moduleName, path);
    return false;
  }
  return true;
}

bool SDCardManager::openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForRead(const char* moduleName, const String& path, FsFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
  SdLockGuard lock(sdMutex_);
  file = sd.open(path, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    if (Serial) Serial.printf("[%lu] [%s] Failed to open file for writing: %s\n", millis(), moduleName, path);
    return false;
  }
  return true;
}

bool SDCardManager::openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool SDCardManager::openFileForWrite(const char* moduleName, const String& path, FsFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

void SDCardManager::syncAndClose(FsFile& file) {
  SdLockGuard lock(sdMutex_);
  if (!file) return;
  file.sync();
  file.close();
}

// ─── Inline forwarders (de-inlined in Batch 4 to take the bus mutex) ────

FsFile SDCardManager::open(const char* path, const oflag_t oflag) {
  SdLockGuard lock(sdMutex_);
  return sd.open(path, oflag);
}

bool SDCardManager::mkdir(const char* path, const bool pFlag) {
  SdLockGuard lock(sdMutex_);
  return sd.mkdir(path, pFlag);
}

bool SDCardManager::exists(const char* path) {
  SdLockGuard lock(sdMutex_);
  return sd.exists(path);
}

bool SDCardManager::remove(const char* path) {
  SdLockGuard lock(sdMutex_);
  return sd.remove(path);
}

bool SDCardManager::rmdir(const char* path) {
  SdLockGuard lock(sdMutex_);
  return sd.rmdir(path);
}

bool SDCardManager::rename(const char* path, const char* newPath) {
  SdLockGuard lock(sdMutex_);
  return sd.rename(path, newPath);
}

// ─── Atomic write protocol ────────────────────────────────────────────
//
// 3-rename rotation per docs/ATOMIC_WRITE_DESIGN.md.
// Suffixes (".tmp" / ".bak") share the same 4-byte budget so a single
// MAX_ATOMIC_PATH buffer holds either decoration.

namespace {
// SdFat's path limit is 255 bytes. Real consumers stay under 200
// (longest is /.sumi/<book-stem>/progress.bin or /.sumi/<plugin>.bin).
// 256 leaves headroom for the 4-byte ".tmp"/".bak" suffix.
constexpr size_t MAX_ATOMIC_PATH = 256;

bool buildSuffixedPath(char* out, size_t outSize, const char* path,
                       const char* suffix) {
  if (!out || !path || !suffix || outSize == 0) return false;
  const size_t plen = strlen(path);
  const size_t slen = strlen(suffix);
  if (plen + slen + 1 > outSize) return false;  // +1 for NUL
  memcpy(out, path, plen);
  memcpy(out + plen, suffix, slen);
  out[plen + slen] = '\0';
  return true;
}
}  // namespace

bool SDCardManager::atomicOpenWrite(const char* moduleName, const char* path,
                                    FsFile& file) {
  SdLockGuard lock(sdMutex_);
  if (!initialized) {
    if (Serial)
      Serial.printf("[%lu] [%s] atomicOpenWrite: SD not initialized (%s)\n",
                    millis(), moduleName, path);
    return false;
  }

  char tmpPath[MAX_ATOMIC_PATH];
  if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp")) {
    if (Serial)
      Serial.printf("[%lu] [%s] atomicOpenWrite: path too long (%s)\n",
                    millis(), moduleName, path);
    return false;
  }

  // O_TRUNC the .tmp — if a stale one is sitting around (crash during a
  // previous attempt), it's discarded. recoverAtomicWrites at next boot
  // would have removed it anyway; this saves a redundant unlink.
  file = sd.open(tmpPath, O_RDWR | O_CREAT | O_TRUNC);
  if (!file) {
    if (Serial)
      Serial.printf("[%lu] [%s] atomicOpenWrite: failed to open %s\n",
                    millis(), moduleName, tmpPath);
    return false;
  }
  return true;
}

bool SDCardManager::atomicOpenWrite(const char* moduleName,
                                    const std::string& path, FsFile& file) {
  return atomicOpenWrite(moduleName, path.c_str(), file);
}

bool SDCardManager::atomicOpenWrite(const char* moduleName, const String& path,
                                    FsFile& file) {
  return atomicOpenWrite(moduleName, path.c_str(), file);
}

bool SDCardManager::atomicCommit(FsFile& file, const char* path) {
  SdLockGuard lock(sdMutex_);
  if (!file) {
    if (Serial)
      Serial.printf("[%lu] [SD] atomicCommit: file handle invalid (%s)\n",
                    millis(), path);
    return false;
  }

  // Step 1: durable .tmp on media.
  file.sync();
  file.close();

  if (!initialized) {
    if (Serial)
      Serial.printf("[%lu] [SD] atomicCommit: SD not initialized (%s)\n",
                    millis(), path);
    return false;
  }

  char tmpPath[MAX_ATOMIC_PATH];
  char bakPath[MAX_ATOMIC_PATH];
  if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp") ||
      !buildSuffixedPath(bakPath, sizeof(bakPath), path, ".bak")) {
    if (Serial)
      Serial.printf("[%lu] [SD] atomicCommit: path too long (%s)\n",
                    millis(), path);
    return false;
  }

  // Step 2: rotate canonical → .bak so the .tmp can be renamed in.
  const bool hadCanonical = sd.exists(path);
  if (hadCanonical) {
    // Clear any leftover .bak first — sd.rename() fails if dest exists.
    if (sd.exists(bakPath)) {
      sd.remove(bakPath);
    }
    if (!sd.rename(path, bakPath)) {
      if (Serial)
        Serial.printf("[%lu] [SD] atomicCommit: rename %s → %s failed\n",
                      millis(), path, bakPath);
      sd.remove(tmpPath);  // best-effort cleanup
      return false;
    }
  }

  // Step 3: promote .tmp to canonical.
  if (!sd.rename(tmpPath, path)) {
    if (Serial)
      Serial.printf("[%lu] [SD] atomicCommit: rename %s → %s failed\n",
                    millis(), tmpPath, path);
    // Roll back: try to put the previous canonical back. If that
    // fails too, recoverAtomicWrites at next boot will promote .bak.
    if (hadCanonical) {
      if (!sd.rename(bakPath, path) && Serial) {
        Serial.printf("[%lu] [SD] atomicCommit: rollback %s → %s failed; "
                      "recovery at next boot will promote\n",
                      millis(), bakPath, path);
      }
    }
    sd.remove(tmpPath);
    return false;
  }

  // Step 4: cleanup .bak. Best-effort — recovery clears any leftover.
  if (hadCanonical) {
    sd.remove(bakPath);
  }
  return true;
}

bool SDCardManager::atomicCommit(FsFile& file, const std::string& path) {
  return atomicCommit(file, path.c_str());
}

bool SDCardManager::atomicCommit(FsFile& file, const String& path) {
  return atomicCommit(file, path.c_str());
}

void SDCardManager::atomicAbort(FsFile& file, const char* path) {
  SdLockGuard lock(sdMutex_);
  if (file) {
    file.close();
  }
  if (!initialized || !path) return;
  char tmpPath[MAX_ATOMIC_PATH];
  if (buildSuffixedPath(tmpPath, sizeof(tmpPath), path, ".tmp")) {
    if (sd.exists(tmpPath)) {
      sd.remove(tmpPath);
    }
  }
}

void SDCardManager::atomicAbort(FsFile& file, const std::string& path) {
  atomicAbort(file, path.c_str());
}

void SDCardManager::atomicAbort(FsFile& file, const String& path) {
  atomicAbort(file, path.c_str());
}

namespace {
// Inner helper: scan one directory for .tmp/.bak orphans and apply the
// 8-state recovery rule. Mutating the directory while iterating is
// unsafe in SdFat, so we walk twice — pass 1 collects orphan canonical
// paths into a small stack array, pass 2 acts on each.
void recoverOrphansInOneDir(SdFat& sd, const char* dirPath) {
  if (!sd.exists(dirPath)) return;

  FsFile dir = sd.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  // 32 candidates is well above realistic ceiling for /.sumi root or a
  // single per-book cache directory.
  constexpr int MAX_CANDIDATES = 32;
  char candidates[MAX_CANDIDATES][MAX_ATOMIC_PATH];
  int candidateCount = 0;

  char name[64];
  for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    f.getName(name, sizeof(name));
    f.close();

    const size_t nlen = strlen(name);
    const bool isTmp = (nlen > 4 && strcmp(name + nlen - 4, ".tmp") == 0);
    const bool isBak = (nlen > 4 && strcmp(name + nlen - 4, ".bak") == 0);
    if (!isTmp && !isBak) continue;

    if (candidateCount >= MAX_CANDIDATES) {
      if (Serial)
        Serial.printf("[%lu] [SD] recover: too many orphans in %s, "
                      "stopping at %d\n", millis(), dirPath, MAX_CANDIDATES);
      break;
    }

    const size_t baseLen = nlen - 4;
    char* slot = candidates[candidateCount];
    const size_t prefixLen = strlen(dirPath);
    if (prefixLen + 1 + baseLen + 1 > MAX_ATOMIC_PATH) continue;
    memcpy(slot, dirPath, prefixLen);
    slot[prefixLen] = '/';
    memcpy(slot + prefixLen + 1, name, baseLen);
    slot[prefixLen + 1 + baseLen] = '\0';

    // Dedupe — a file with both .tmp and .bak siblings shows up twice.
    bool already = false;
    for (int i = 0; i < candidateCount; ++i) {
      if (strcmp(candidates[i], slot) == 0) { already = true; break; }
    }
    if (!already) {
      candidateCount++;
    } else {
      slot[0] = '\0';
    }
  }
  dir.close();

  // Pass 2: apply the 8-state recovery rule (docs/ATOMIC_WRITE_DESIGN.md).
  for (int i = 0; i < candidateCount; ++i) {
    const char* canonical = candidates[i];
    if (canonical[0] == '\0') continue;

    char tmpPath[MAX_ATOMIC_PATH];
    char bakPath[MAX_ATOMIC_PATH];
    if (!buildSuffixedPath(tmpPath, sizeof(tmpPath), canonical, ".tmp") ||
        !buildSuffixedPath(bakPath, sizeof(bakPath), canonical, ".bak")) {
      continue;
    }
    const bool hasCanonical = sd.exists(canonical);
    const bool hasTmp = sd.exists(tmpPath);
    const bool hasBak = sd.exists(bakPath);

    if (hasCanonical && hasTmp && hasBak) {
      sd.remove(tmpPath);
      sd.remove(bakPath);
      if (Serial) Serial.printf("[%lu] [SD] recover: %s — cleanup completed\n",
                                 millis(), canonical);
    } else if (hasCanonical && hasTmp && !hasBak) {
      sd.remove(tmpPath);
      if (Serial) Serial.printf("[%lu] [SD] recover: %s — discarded stale .tmp\n",
                                 millis(), canonical);
    } else if (hasCanonical && !hasTmp && hasBak) {
      sd.remove(bakPath);
      if (Serial) Serial.printf("[%lu] [SD] recover: %s — removed leftover .bak\n",
                                 millis(), canonical);
    } else if (!hasCanonical && hasTmp && hasBak) {
      sd.remove(tmpPath);
      if (sd.rename(bakPath, canonical)) {
        if (Serial) Serial.printf("[%lu] [SD] recover: %s — promoted .bak\n",
                                   millis(), canonical);
      } else if (Serial) {
        Serial.printf("[%lu] [SD] recover: %s — promote of .bak FAILED\n",
                       millis(), canonical);
      }
    } else if (!hasCanonical && hasTmp && !hasBak) {
      sd.remove(tmpPath);
      if (Serial) Serial.printf("[%lu] [SD] recover: %s — discarded orphan .tmp\n",
                                 millis(), canonical);
    } else if (!hasCanonical && !hasTmp && hasBak) {
      if (sd.rename(bakPath, canonical)) {
        if (Serial) Serial.printf("[%lu] [SD] recover: %s — promoted .bak (post-rotation)\n",
                                   millis(), canonical);
      } else if (Serial) {
        Serial.printf("[%lu] [SD] recover: %s — promote of .bak FAILED\n",
                       millis(), canonical);
      }
    }
    // The remaining states are no-ops by construction (no candidate
    // would have been registered for them).
  }
}

// Collect immediate child directory names of `dirPath` into `out`.
// Returns count. Caller-supplied buffer; bounded to MAX_SUBDIRS so a
// pathologically deep `/.sumi/cache/` tree can't blow the stack.
int collectSubdirsOf(SdFat& sd, const char* dirPath, char out[][MAX_ATOMIC_PATH],
                     int maxOut) {
  if (maxOut <= 0 || !sd.exists(dirPath)) return 0;
  FsFile dir = sd.open(dirPath);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return 0; }
  int count = 0;
  char name[64];
  for (auto f = dir.openNextFile(); f && count < maxOut; f = dir.openNextFile()) {
    if (!f.isDirectory()) { f.close(); continue; }
    f.getName(name, sizeof(name));
    f.close();

    const size_t nlen = strlen(name);
    const size_t prefixLen = strlen(dirPath);
    if (prefixLen + 1 + nlen + 1 > MAX_ATOMIC_PATH) continue;
    memcpy(out[count], dirPath, prefixLen);
    out[count][prefixLen] = '/';
    memcpy(out[count] + prefixLen + 1, name, nlen);
    out[count][prefixLen + 1 + nlen] = '\0';
    count++;
  }
  dir.close();
  return count;
}
}  // namespace

void SDCardManager::recoverAtomicWrites() {
  SdLockGuard lock(sdMutex_);
  if (!initialized) return;

  // SUMI's persistent state lives under /.sumi/ (system) and a few
  // user-editable plugin directories (/notes for the Notes app —
  // see audit pass follow-up after Batch 9). Atomic-write
  // consumers live at multiple depths:
  //
  //   level 0: /.sumi             (settings.bin, library.bin, etc.)
  //            /notes              (Notes app: <name>.txt + .tmp/.bak)
  //   level 1: /.sumi/<sub>        (cache, transition, ...)
  //   level 2: /.sumi/<sub>/<sub>  (per-book cache contents)
  //
  // We deliberately stop at level 2 so a future deep tree can't make
  // boot expensive. /notes is a flat directory of files — level 0 is
  // sufficient.
  constexpr const char* SUMI_ROOT = "/.sumi";
  constexpr const char* NOTES_ROOT = "/notes";
  constexpr const char* DATA_ROOT = "/data";
  constexpr const char* CUSTOM_ROOT = "/custom";

  if (sd.exists(NOTES_ROOT)) {
    // Notes lives outside /.sumi but was migrated to atomic writes in
    // the post-Batch-9 audit pass. Just a level-0 sweep.
    recoverOrphansInOneDir(sd, NOTES_ROOT);
  }

  if (sd.exists(DATA_ROOT)) {
    // /data is a flat directory where the TodoList plugin stores
    // todo.txt. Migrated to atomic writes in the post-the emulator-sweep
    // pass — needs an orphan scan so a brownout between
    // canonical→.bak and .tmp→canonical doesn't leave the user's
    // todo list inaccessible.
    recoverOrphansInOneDir(sd, DATA_ROOT);
  }

  if (sd.exists(CUSTOM_ROOT)) {
    // /custom holds Lua plugin scripts at level 0 and per-plugin
    // <plugin>_data/ sandbox dirs at level 1. sumi.writeFile() in the
    // Lua bindings is atomic-write, so any orphans land inside the
    // _data dirs (e.g. /custom/snake_data/highscore.txt.tmp). Scan
    // both depths.
    recoverOrphansInOneDir(sd, CUSTOM_ROOT);
    constexpr int MAX_CUSTOM_LVL1 = 16;
    char customLvl1[MAX_CUSTOM_LVL1][MAX_ATOMIC_PATH];
    const int customLvl1Count =
        collectSubdirsOf(sd, CUSTOM_ROOT, customLvl1, MAX_CUSTOM_LVL1);
    for (int i = 0; i < customLvl1Count; ++i) {
      recoverOrphansInOneDir(sd, customLvl1[i]);
    }
  }

  constexpr const char* GAMES_ROOT = "/games";
  if (sd.exists(GAMES_ROOT)) {
    // /games holds GB/GBC ROMs at level 0 and the GB emulator's
    // saveState() / saveSRAM() / saveCheats() write orphan .tmp/.bak
    // pairs into /games/saves and /games/cheats (level 1) via the
    // safeRename helper in plugins/gb/. Pre-this-pass the comment in
    // gb_emulator_base.h noted that orphans there were "acceptable";
    // adding the scan is trivial and gives uniform recovery behavior
    // with the rest of the atomic-write protocol.
    recoverOrphansInOneDir(sd, GAMES_ROOT);
    constexpr int MAX_GAMES_LVL1 = 8;
    char gamesLvl1[MAX_GAMES_LVL1][MAX_ATOMIC_PATH];
    const int gamesLvl1Count =
        collectSubdirsOf(sd, GAMES_ROOT, gamesLvl1, MAX_GAMES_LVL1);
    for (int i = 0; i < gamesLvl1Count; ++i) {
      recoverOrphansInOneDir(sd, gamesLvl1[i]);
    }
  }

  if (!sd.exists(SUMI_ROOT)) return;  // First boot — directory not created yet.

  // Level 0: /.sumi
  recoverOrphansInOneDir(sd, SUMI_ROOT);

  // Level 1: subdirs of /.sumi
  constexpr int MAX_LVL1 = 8;   // /.sumi has very few subdirs (cache, ...)
  char lvl1[MAX_LVL1][MAX_ATOMIC_PATH];
  const int lvl1Count = collectSubdirsOf(sd, SUMI_ROOT, lvl1, MAX_LVL1);
  for (int i = 0; i < lvl1Count; ++i) {
    recoverOrphansInOneDir(sd, lvl1[i]);

    // Level 2: subdirs of /.sumi/<sub> (e.g. each per-book cache dir)
    constexpr int MAX_LVL2 = 32;  // ~32 books cached at once is plenty
    char lvl2[MAX_LVL2][MAX_ATOMIC_PATH];
    const int lvl2Count = collectSubdirsOf(sd, lvl1[i], lvl2, MAX_LVL2);
    for (int j = 0; j < lvl2Count; ++j) {
      recoverOrphansInOneDir(sd, lvl2[j]);
    }
  }
}

bool SDCardManager::removeDir(const char* path) {
  SdLockGuard lock(sdMutex_);  // recursive — re-entered by self for subdirs

  auto dir = sd.open(path);
  if (!dir) {
    return false;
  }
  if (!dir.isDirectory()) {
    dir.close();
    return false;
  }

  // Stack-allocated path buffer reused for every entry. Pre-Batch-9
  // followup this used `String filePath = path + "/" + name` per entry,
  // which heap-allocated 3 times per child — a deep tree thrashed the
  // 380 KB heap. The fixed buffer is single-allocated on the stack and
  // overwritten per entry. Audit #11.
  //
  // SdFat's max path is 255; 256 + NUL fits any legitimate child path.
  char childPath[MAX_ATOMIC_PATH];
  const size_t prefixLen = strlen(path);
  if (prefixLen + 2 > MAX_ATOMIC_PATH) {
    // Even the trailing "/" wouldn't fit — corrupted/oversize input.
    dir.close();
    return false;
  }
  memcpy(childPath, path, prefixLen);
  size_t baseLen = prefixLen;
  if (baseLen == 0 || childPath[baseLen - 1] != '/') {
    childPath[baseLen++] = '/';
  }
  childPath[baseLen] = '\0';

  char name[128];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    const bool isDir = file.isDirectory();
    // Close the iterator's handle on this entry BEFORE recursing or
    // calling sd.remove. SdFat rejects remove()/rmdir() against a path
    // that still has an open FsFile; leaving this open caused deep
    // cache directories to fail to delete and left dangling handles.
    file.close();

    const size_t nameLen = strlen(name);
    if (baseLen + nameLen + 1 > MAX_ATOMIC_PATH) {
      // Child path doesn't fit in the buffer — skip this entry rather
      // than truncate (which would target the wrong file).
      continue;
    }
    memcpy(childPath + baseLen, name, nameLen + 1);

    if (isDir) {
      if (!removeDir(childPath)) {
        dir.close();
        return false;
      }
    } else {
      if (!sd.remove(childPath)) {
        dir.close();
        return false;
      }
    }
  }

  // Close the directory handle before rmdir — SdFat likewise fails to
  // remove a directory that still has an open iterator.
  dir.close();
  return sd.rmdir(path);
}
