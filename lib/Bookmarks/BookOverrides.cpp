#include "BookOverrides.h"

#include <SDCardManager.h>

#include <cstring>

namespace sumi {

// Binary format:
// [4] magic = "BOVR"
// [1] version = 1
// [5] int8_t fields: fontSize, textDarkness, showImages, lineSpacing, hyphenation

static constexpr uint8_t MAGIC[4] = {'B', 'O', 'V', 'R'};
static constexpr uint8_t VERSION = 1;
static constexpr size_t HEADER_SIZE = 4 + 1;  // magic + version
static constexpr size_t FIELD_COUNT = 5;

static std::string filePath(const std::string& cachePath) {
  return cachePath + "/overrides.bin";
}

BookOverrides BookOverrides::load(const std::string& cachePath) {
  BookOverrides ov;

  FsFile f;
  if (!SdMan.openFileForRead("BOV", filePath(cachePath).c_str(), f)) {
    return ov;
  }

  uint8_t header[HEADER_SIZE];
  const int n = f.read(header, HEADER_SIZE);
  if (n != static_cast<int>(HEADER_SIZE)) {
    f.close();
    return ov;
  }

  if (memcmp(header, MAGIC, 4) != 0 || header[4] != VERSION) {
    f.close();
    return ov;
  }

  uint8_t fields[FIELD_COUNT];
  const int bytesRead = f.read(fields, FIELD_COUNT);
  f.close();

  if (bytesRead != static_cast<int>(FIELD_COUNT)) {
    return ov;
  }

  ov.fontSize = static_cast<int8_t>(fields[0]);
  ov.textDarkness = static_cast<int8_t>(fields[1]);
  ov.showImages = static_cast<int8_t>(fields[2]);
  ov.lineSpacing = static_cast<int8_t>(fields[3]);
  ov.hyphenation = static_cast<int8_t>(fields[4]);

  return ov;
}

void BookOverrides::save(const std::string& cachePath, const BookOverrides& overrides) {
  // Atomic — pre-audit this used regular openFileForWrite. A
  // brownout between O_TRUNC and the 11-byte payload landed left a
  // zero-byte overrides file, which load() then read as "no override
  // present" and the per-book font-size / text-darkness / etc reset to
  // the global defaults silently. atomicOpenWrite + atomicCommit closes
  // the window for the same reason settings.bin / recent.bin do.
  const std::string fp = filePath(cachePath);
  FsFile f;
  if (!SdMan.atomicOpenWrite("BOV", fp.c_str(), f)) {
    return;
  }

  f.write(MAGIC, 4);
  f.write(VERSION);

  uint8_t fields[FIELD_COUNT];
  fields[0] = static_cast<uint8_t>(overrides.fontSize);
  fields[1] = static_cast<uint8_t>(overrides.textDarkness);
  fields[2] = static_cast<uint8_t>(overrides.showImages);
  fields[3] = static_cast<uint8_t>(overrides.lineSpacing);
  fields[4] = static_cast<uint8_t>(overrides.hyphenation);

  f.write(fields, FIELD_COUNT);
  if (!SdMan.atomicCommit(f, fp.c_str())) {
    SdMan.atomicAbort(f, fp.c_str());
  }
}

}  // namespace sumi
