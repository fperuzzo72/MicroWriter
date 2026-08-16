#pragma once

#include <SdFat.h>
#include <WString.h>

#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class SDCardManager {
 public:
  SDCardManager();
  ~SDCardManager();
  bool begin();
  bool ready() const;
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);
  // Low-memory helpers:
  // Stream the file contents to a `Print` (e.g. `Serial`, or any `Print`-derived object).
  // Returns true on success, false on failure.
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  // Read up to `bufferSize-1` bytes into `buffer`, null-terminating it. Returns bytes read.
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  // Write a string to `path` on the SD card. Overwrites existing file.
  // Returns true on success.
  bool writeFile(const char* path, const String& content);
  // Ensure a directory exists, creating it if necessary. Returns true on success.
  bool ensureDirectoryExists(const char* path);

  // Used to be inline forwarders to `sd.*`. De-inlined as part of Batch 4 so
  // they can take the recursive bus mutex like every other public method
  // (CONCURRENCY.md C7). The pass-through semantics are unchanged.
  FsFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rmdir(const char* path);
  bool rename(const char* path, const char* newPath);

  bool openFileForRead(const char* moduleName, const char* path, FsFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForRead(const char* moduleName, const String& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, FsFile& file);

  // ─── Atomic write protocol ──────────────────────────────────────────
  // See docs/ATOMIC_WRITE_DESIGN.md for the full state-transition model.
  //
  // The truncate-then-overwrite pattern (`openFileForWrite` + write +
  // `syncAndClose`) leaves a window where canonical files are zero-bytes
  // on disk: a power loss between O_TRUNC and the first byte written
  // produces a structurally valid but content-empty file, which loaders
  // then misread as "default state". User loses progress / bookmarks /
  // achievements / settings.
  //
  // The atomic helpers route writes through a 3-rename rotation that
  // guarantees at least one valid copy of the file exists on disk at every
  // point during the commit. Boot-time `recoverAtomicWrites()` finds and
  // promotes any orphans left behind by a crash mid-rotation.
  //
  // Usage:
  //   FsFile f;
  //   if (!SdMan.atomicOpenWrite("LIB", "/.sumi/library.bin", f)) return false;
  //   f.write(payload, payloadSize);
  //   if (!SdMan.atomicCommit(f, "/.sumi/library.bin")) return false;
  //
  // On failure, atomicAbort() cleans up the .tmp.

  /// Open `<path>.tmp` for writing. `moduleName` shows up in log lines.
  /// Returns false if the .tmp couldn't be created.
  bool atomicOpenWrite(const char* moduleName, const char* path, FsFile& file);
  bool atomicOpenWrite(const char* moduleName, const std::string& path, FsFile& file);
  bool atomicOpenWrite(const char* moduleName, const String& path, FsFile& file);

  /// Promote `<path>.tmp` to `<path>` via the 3-rename rotation:
  ///   1. file.sync(); file.close();           // .tmp durable on media
  ///   2. if exists(path): rename(path, path.bak)
  ///   3. rename(path.tmp, path)
  ///   4. remove(path.bak)                     // best effort
  /// Returns true on full success. On rename failure the previous canonical
  /// is restored; the caller is told the commit failed.
  bool atomicCommit(FsFile& file, const char* path);
  bool atomicCommit(FsFile& file, const std::string& path);
  bool atomicCommit(FsFile& file, const String& path);

  /// Discard `<path>.tmp` and leave `<path>` untouched.
  /// Always safe to call; intended for the failure path of writers.
  void atomicAbort(FsFile& file, const char* path);
  void atomicAbort(FsFile& file, const std::string& path);
  void atomicAbort(FsFile& file, const String& path);

  /// Boot-time recovery scan. Walks `/.sumi/` looking for `.tmp` and
  /// `.bak` orphans left behind by a crash mid-rotation. For each
  /// canonical path it might own, it inspects the {canonical, .tmp,
  /// .bak} tuple and applies the 8-state recovery rules from
  /// docs/ATOMIC_WRITE_DESIGN.md (most importantly: when canonical is
  /// missing AND .bak exists, the .bak is promoted).
  ///
  /// Called once from `Storage::init()`. Cheap: one directory scan.
  void recoverAtomicWrites();

  // Flush + close a file after writing. Always call this instead of raw
  // file.close() when the file was opened for writing. SdFat's close() alone
  // does NOT guarantee multi-sector writes have landed on disk — without an
  // explicit sync(), files larger than 512 bytes may read back as truncated
  // or zero-length on next open. Verified on the emulator's virtual SD with the
  // Dictionary feature (2025-04-15). Small single-sector files happen to
  // work, but the failure mode is silent and unpredictable, so every
  // write path should go through this helper.
  void syncAndClose(FsFile& file);

  bool removeDir(const char* path);

  static SDCardManager& getInstance() { return instance; }

  // Raw SdFat access for libraries that take an SdFat& directly (ported
  // emulator core, etc.). Prefer the openFileFor*()/syncAndClose helpers
  // in new SUMI code — they handle the file-lifecycle discipline that
  // SdFat requires for multi-sector writes. This accessor exists for
  // drop-in third-party code that expects the raw handle.
  //
  // WARNING: bypasses the recursive bus mutex added in Batch 4. Callers
  // that hold the SdFat& and use it directly are NOT serialized against
  // other tasks. Use the wrapped methods instead, or take responsibility
  // for the surrounding synchronization yourself.
  SdFat& raw() { return sd; }

 private:
  static SDCardManager instance;

  bool initialized = false;
  SdFat sd;

  // Recursive bus mutex. Created lazily in begin() because static-init
  // ordering with FreeRTOS scheduler readiness is fragile across the
  // espressif32 framework versions we ship with. Recursive variant
  // because public methods call other public methods (e.g. writeFile →
  // openFileForWrite → syncAndClose), all of which take the lock.
  // CONCURRENCY.md C7. Audit #H10.
  SemaphoreHandle_t sdMutex_ = nullptr;
};

#define SdMan SDCardManager::getInstance()
