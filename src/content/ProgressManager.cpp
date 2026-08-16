#include "ProgressManager.h"

#include <Arduino.h>
#include <Crc32.h>
#include <SdFat.h>

#include <cstdio>
#include <cstring>

#include "../content/ContentHandle.h"
#include "../core/Core.h"

namespace sumi {

// ─── Versioned binary format ───────────────────────────────────────────
//
// Older firmware wrote progress.bin as 4 raw bytes with no header. Two
// problems with that:
//   - Format change is silent: a per-type 4-byte layout meant the same
//     bytes meant different things depending on content type. Changing
//     the encoding silently misreads.
//   - spineIndex was stored unsigned-truncated to 16 bits, so a -1
//     "cover page" sentinel round-tripped to 0xFFFF which validate()
//     then clamped to last spine — user opens at END of book.
//
// New format:
//   offset  size  field
//   0       4     magic 'PROG' (0x474F5250 little-endian)
//   4       1     version (1 = no CRC, 2 = +CRC32 trailer)
//   5       1     reserved (must be 0)
//   6       4     spineIndex   (int32 LE, signed; -1 = cover sentinel)
//   10      4     sectionPage  (int32 LE, signed)
//   14      4     flatPage     (uint32 LE)
//   v2 only:
//   18      4     CRC32 trailer (over bytes 0..17)
//   total: 18 bytes (v1) or 22 bytes (v2)
//
// On read, four accepted shapes:
//   - 22 bytes with magic + v2: new format with CRC. Verify trailer
//     (tolerant — mismatch logs and we still load the data). Parse.
//   - 18 bytes with magic + v1: pre-Batch-8 new format, no trailer.
//     Parse, will be migrated to v2 on next save.
//   - 4 bytes (no magic): pre-magic legacy. Parse per content type.
//   - other: assume corrupt, use defaults.
//
// Writes always emit the latest version; the next save migrates the
// file in place.
constexpr uint32_t PROGRESS_MAGIC = 0x474F5250u;  // 'PROG' little-endian
constexpr uint8_t  PROGRESS_VERSION = 2;
constexpr uint8_t  PROGRESS_VERSION_WITH_CRC = 2;
constexpr size_t   PROGRESS_BODY_SIZE = 18;            // v1 + v2 share this
constexpr size_t   PROGRESS_V2_SIZE = PROGRESS_BODY_SIZE + 4;
constexpr size_t   PROGRESS_LEGACY_SIZE = 4;

namespace {
inline void packLE32(uint8_t* out, uint32_t v) {
  out[0] = static_cast<uint8_t>(v);
  out[1] = static_cast<uint8_t>(v >> 8);
  out[2] = static_cast<uint8_t>(v >> 16);
  out[3] = static_cast<uint8_t>(v >> 24);
}

inline uint32_t unpackLE32(const uint8_t* in) {
  // All shifts on uint32_t to avoid signed-int UB on bytes >= 0x80
  // (the audit's #39 finding — `(int)data[3] << 24` shifted 1 into the
  // sign bit, undefined per C++17).
  return  static_cast<uint32_t>(in[0])        |
         (static_cast<uint32_t>(in[1]) << 8)  |
         (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}
}  // namespace

bool ProgressManager::save(Core& core, const char* cacheDir, ContentType type, const Progress& progress) {
  if (!cacheDir || cacheDir[0] == '\0') {
    return false;
  }

  char progressPath[280];
  snprintf(progressPath, sizeof(progressPath), "%s/progress.bin", cacheDir);

  FsFile file;
  auto result = core.storage.atomicOpenWrite(progressPath, file);
  if (!result.ok()) {
    Serial.printf("[PROGRESS] atomicOpenWrite failed for %s\n", progressPath);
    return false;
  }

  uint8_t buf[PROGRESS_V2_SIZE];
  // Header
  packLE32(buf + 0, PROGRESS_MAGIC);
  buf[4] = PROGRESS_VERSION;
  buf[5] = 0;  // reserved
  // Body. All three fields written every save; the loader picks the
  // right ones by type.
  packLE32(buf + 6,  static_cast<uint32_t>(progress.spineIndex));   // signed bit-pattern
  packLE32(buf + 10, static_cast<uint32_t>(progress.sectionPage));
  packLE32(buf + 14, progress.flatPage);
  // CRC32 trailer over the 18-byte body. Audit #26 follow-up.
  packLE32(buf + 18, sumi::crc32(buf, PROGRESS_BODY_SIZE));

  if (file.write(buf, PROGRESS_V2_SIZE) != static_cast<int>(PROGRESS_V2_SIZE)) {
    Serial.printf("[PROGRESS] Write failed for %s\n", progressPath);
    core.storage.atomicAbort(file, progressPath);
    return false;
  }

  if (!core.storage.atomicCommit(file, progressPath).ok()) {
    Serial.printf("[PROGRESS] atomicCommit failed for %s\n", progressPath);
    core.storage.atomicAbort(file, progressPath);
    return false;
  }

  if (type == ContentType::Epub) {
    Serial.printf("[PROGRESS] Saved EPUB: spine=%d page=%d\n",
                  progress.spineIndex, progress.sectionPage);
  } else if (type == ContentType::Xtc || type == ContentType::Comic) {
    Serial.printf("[PROGRESS] Saved XTC: page %u\n", progress.flatPage);
  } else {
    Serial.printf("[PROGRESS] Saved text: page %d\n", progress.sectionPage);
  }
  return true;
}

ProgressManager::Progress ProgressManager::load(Core& core, const char* cacheDir, ContentType type) {
  Progress progress;
  progress.reset();

  if (!cacheDir || cacheDir[0] == '\0') {
    return progress;
  }

  char progressPath[280];
  snprintf(progressPath, sizeof(progressPath), "%s/progress.bin", cacheDir);

  FsFile file;
  auto result = core.storage.openRead(progressPath, file);
  if (!result.ok()) {
    Serial.println("[PROGRESS] No saved progress found");
    return progress;
  }

  const size_t fileSize = file.size();

  // ─── New versioned format (18 B v1 / 22 B v2, magic 'PROG') ──────
  if (fileSize >= PROGRESS_BODY_SIZE) {
    // Read up to PROGRESS_V2_SIZE — we tolerate the v1 short form by
    // checking version after parsing the header.
    uint8_t buf[PROGRESS_V2_SIZE];
    const size_t toRead = (fileSize >= PROGRESS_V2_SIZE) ? PROGRESS_V2_SIZE
                                                          : PROGRESS_BODY_SIZE;
    if (file.read(buf, toRead) != static_cast<int>(toRead)) {
      Serial.println("[PROGRESS] Read failed (new format), using defaults");
      file.close();
      return progress;
    }
    file.close();

    const uint32_t magic = unpackLE32(buf + 0);
    if (magic == PROGRESS_MAGIC) {
      const uint8_t version = buf[4];
      if (version < 1 || version > PROGRESS_VERSION) {
        Serial.printf("[PROGRESS] Unknown version %u, using defaults\n", version);
        return progress;
      }
      // Bit-pattern read into the signed fields. Two's complement
      // round-trips negative spineIndex (-1 cover sentinel) properly.
      progress.spineIndex  = static_cast<int>(static_cast<int32_t>(unpackLE32(buf + 6)));
      progress.sectionPage = static_cast<int>(static_cast<int32_t>(unpackLE32(buf + 10)));
      progress.flatPage    = unpackLE32(buf + 14);

      // CRC32 trailer (v2+) — tolerant per audit policy: a mismatch
      // logs and we still return the parsed progress. Atomic-write
      // protocol covers partial-write corruption; CRC mismatch implies
      // a post-write bit-flip, rare enough that "log + use the data"
      // is the right call.
      if (version >= PROGRESS_VERSION_WITH_CRC && toRead == PROGRESS_V2_SIZE) {
        const uint32_t fileCrc  = unpackLE32(buf + 18);
        const uint32_t computed = sumi::crc32(buf, PROGRESS_BODY_SIZE);
        if (fileCrc != computed) {
          Serial.printf("[PROGRESS] WARN: CRC32 mismatch "
                        "(file=0x%08X computed=0x%08X); accepting payload\n",
                        static_cast<unsigned>(fileCrc),
                        static_cast<unsigned>(computed));
        }
      }

      if (type == ContentType::Epub) {
        Serial.printf("[PROGRESS] Loaded EPUB: spine=%d page=%d (v%u%s)\n",
                      progress.spineIndex, progress.sectionPage,
                      static_cast<unsigned>(version),
                      version >= PROGRESS_VERSION_WITH_CRC ? " +crc" : "");
      } else if (type == ContentType::Xtc || type == ContentType::Comic) {
        Serial.printf("[PROGRESS] Loaded XTC: page %u (v%u%s)\n",
                      progress.flatPage,
                      static_cast<unsigned>(version),
                      version >= PROGRESS_VERSION_WITH_CRC ? " +crc" : "");
      } else {
        Serial.printf("[PROGRESS] Loaded text: page %d (v%u%s)\n",
                      progress.sectionPage,
                      static_cast<unsigned>(version),
                      version >= PROGRESS_VERSION_WITH_CRC ? " +crc" : "");
      }
      return progress;
    }
    // Body-size+ file but no magic — corrupt, fall through to defaults.
    Serial.println("[PROGRESS] Bad magic in new-format-sized file, using defaults");
    return progress;
  }

  // ─── Legacy 4-byte format (no header) ─────────────────────────────
  if (fileSize != PROGRESS_LEGACY_SIZE) {
    Serial.printf("[PROGRESS] Unsupported file size %u, using defaults\n",
                  static_cast<unsigned>(fileSize));
    file.close();
    return progress;
  }

  uint8_t data[PROGRESS_LEGACY_SIZE];
  if (file.read(data, PROGRESS_LEGACY_SIZE) != static_cast<int>(PROGRESS_LEGACY_SIZE)) {
    Serial.println("[PROGRESS] Read failed (legacy), using defaults");
    file.close();
    return progress;
  }
  file.close();

  if (type == ContentType::Epub) {
    progress.spineIndex  = static_cast<int16_t>(data[0] | (data[1] << 8));
    progress.sectionPage = static_cast<int16_t>(data[2] | (data[3] << 8));
    Serial.printf("[PROGRESS] Loaded EPUB (legacy): spine=%d page=%d\n",
                  progress.spineIndex, progress.sectionPage);
  } else if (type == ContentType::Xtc || type == ContentType::Comic) {
    progress.flatPage = unpackLE32(data);  // UB-safe via uint32 shifts
    Serial.printf("[PROGRESS] Loaded XTC (legacy): page %u\n", progress.flatPage);
  } else {
    progress.sectionPage = static_cast<int16_t>(data[0] | (data[1] << 8));
    Serial.printf("[PROGRESS] Loaded text (legacy): page %d\n", progress.sectionPage);
  }
  return progress;
}

ProgressManager::Progress ProgressManager::validate(Core& core, ContentType type, const Progress& progress) {
  Progress validated = progress;

  if (type == ContentType::Epub) {
    // Validate spine index
    auto* provider = core.content.asEpub();
    if (provider && provider->getEpub()) {
      uint32_t spineCount = provider->getEpub()->getSpineItemsCount();
      if (validated.spineIndex < 0) {
        validated.spineIndex = 0;
      }
      if (validated.spineIndex >= static_cast<int>(spineCount)) {
        validated.spineIndex = spineCount > 0 ? spineCount - 1 : 0;
        validated.sectionPage = 0;
      }
    }
  } else if (type == ContentType::Xtc || type == ContentType::Comic) {
    // Validate flat page
    uint32_t total = core.content.pageCount();
    if (validated.flatPage >= total) {
      validated.flatPage = total > 0 ? total - 1 : 0;
    }
  }
  // TXT/Markdown: page validation happens during cache creation

  return validated;
}

}  // namespace sumi
