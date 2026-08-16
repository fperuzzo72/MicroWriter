#include "Storage.h"

#include <SDCardManager.h>

namespace sumi {
namespace drivers {

Result<void> Storage::init() {
  if (mounted_) {
    return Ok();
  }

  if (!SdMan.begin()) {
    return ErrVoid(Error::SdCardNotFound);
  }

  mounted_ = true;

  // Boot-time recovery for atomic-write rotations interrupted by a
  // previous crash. See docs/ATOMIC_WRITE_DESIGN.md. Cheap — one /.sumi/
  // directory scan.
  SdMan.recoverAtomicWrites();

  return Ok();
}

void Storage::shutdown() {
  // SdFat doesn't have an explicit shutdown
  mounted_ = false;
}

Result<void> Storage::openRead(const char* path, FsFile& out) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.openFileForRead("DRV", path, out)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::openWrite(const char* path, FsFile& out) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.openFileForWrite("DRV", path, out)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::atomicOpenWrite(const char* path, FsFile& out) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.atomicOpenWrite("DRV", path, out)) {
    return ErrVoid(Error::IOError);
  }

  return Ok();
}

Result<void> Storage::atomicCommit(FsFile& file, const char* path) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.atomicCommit(file, path)) {
    return ErrVoid(Error::IOError);
  }

  return Ok();
}

void Storage::atomicAbort(FsFile& file, const char* path) {
  SdMan.atomicAbort(file, path);
}

Result<bool> Storage::exists(const char* path) {
  if (!mounted_) {
    return Err<bool>(Error::SdCardNotFound);
  }

  return Ok(SdMan.exists(path));
}

Result<void> Storage::remove(const char* path) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.remove(path)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::mkdir(const char* path) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.mkdir(path)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::rmdir(const char* path) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  if (!SdMan.removeDir(path)) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<void> Storage::openDir(const char* path, FsFile& out) {
  if (!mounted_) {
    return ErrVoid(Error::SdCardNotFound);
  }

  out = SdMan.open(path);
  if (!out) {
    return ErrVoid(Error::FileNotFound);
  }

  return Ok();
}

Result<size_t> Storage::readToBuffer(const char* path, char* buffer, size_t bufferSize) {
  if (!mounted_) {
    return Err<size_t>(Error::SdCardNotFound);
  }

  size_t bytesRead = SdMan.readFileToBuffer(path, buffer, bufferSize);
  if (bytesRead == 0) {
    return Err<size_t>(Error::FileNotFound);
  }

  return Ok(bytesRead);
}

}  // namespace drivers
}  // namespace sumi
