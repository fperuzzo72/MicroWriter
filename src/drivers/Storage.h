#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/Result.h"

// Forward declare SdFat types
class FsFile;

namespace sumi {
namespace drivers {

class Storage {
 public:
  Result<void> init();
  void shutdown();

  bool isMounted() const { return mounted_; }

  // File operations
  Result<void> openRead(const char* path, FsFile& out);
  Result<void> openWrite(const char* path, FsFile& out);
  Result<bool> exists(const char* path);
  Result<void> remove(const char* path);
  Result<void> mkdir(const char* path);
  Result<void> rmdir(const char* path);

  // Atomic write protocol — see docs/ATOMIC_WRITE_DESIGN.md.
  // Thin wrappers around SDCardManager's atomicOpenWrite/Commit/Abort
  // so consumers using the Storage API stay on it.
  Result<void> atomicOpenWrite(const char* path, FsFile& out);
  Result<void> atomicCommit(FsFile& file, const char* path);
  void         atomicAbort(FsFile& file, const char* path);

  // Directory operations
  Result<void> openDir(const char* path, FsFile& out);

  // Utility
  Result<size_t> readToBuffer(const char* path, char* buffer, size_t bufferSize);

 private:
  bool mounted_ = false;
};

}  // namespace drivers
}  // namespace sumi
