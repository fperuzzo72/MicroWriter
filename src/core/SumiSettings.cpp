#include "SumiSettings.h"

#include <Crc32.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <Serialization.h>

#include <type_traits>

#include "../FontManager.h"
#include "../Theme.h"
#include "../config.h"
#include "../drivers/Storage.h"

namespace sumi {

namespace {
// Magic signature to identify SUMI settings files ("PPXS" in little-endian)
constexpr uint32_t SETTINGS_MAGIC = 0x53585050;
// Minimum version we can read (allows backward compatibility)
constexpr uint8_t MIN_SETTINGS_VERSION = 3;
// Version 13: Added reader font family override
// Version 14: Added dictionaryName (active StarDict dictionary under /dictionary/)
// Version 15: Added textDarkness
// Version 16: Added language (I18n UI language)
// Version 17: v16 layout + 4-byte CRC32 trailer (audit #26).
//             Migration: v16 files load fine without CRC verification;
//             on next save they're rewritten as v17 with the trailer.
// Version 18: Added timeZoneOffsetMinutes (int16_t, audit #47 follow-up).
//             Older files just don't have the field; the visitor's
//             exhausted() guard leaves the struct's `0` default in
//             place — same UTC display as pre-Batch-9. On next save
//             the file is rewritten as v18.
constexpr uint8_t SETTINGS_FILE_VERSION = 18;
// Version at which the CRC32 trailer was introduced. Loads of older
// versions skip the trailer check.
constexpr uint8_t SETTINGS_VERSION_WITH_CRC = 17;

// ─── Field schema (single source of truth for save + load) ────────────
//
// Two ~190-line functions used to be open-coded with the same 35 fields
// in identical order. Adding a field meant editing both, plus bumping
// SETTINGS_COUNT, plus the matching readPodValidated bound — four
// places. The audit's #8/#9 findings warned this was a foot-gun, and
// indeed it had already drifted once: pendingTransition's max-bound
// silently dropped the EMULATOR enum value because the bound was set
// to 3 instead of 4 when the enum gained a fourth case.
//
// `forEachSettingsField(s, v)` walks the schema once. Save and load
// supply visitors that take a different action on each row. Add or
// reorder a field by editing this function only — `SETTINGS_COUNT` is
// derived from a runtime CountVisitor walk so the count header in the
// file stays in sync automatically.
//
// The on-disk wire format is the order below. Reorder = bump
// SETTINGS_FILE_VERSION + write a migration. The bytes-out are
// byte-identical to the v0.6.0-ramfix format that existing devices
// have on disk; no migration is needed for this refactor.
template <typename Visitor>
void forEachSettingsField(Settings& s, Visitor&& v) {
  v("sleepScreen",            s.sleepScreen,            uint8_t(5));
  v("textLayout",             s.textLayout,             uint8_t(3));
  v("shortPwrBtn",            s.shortPwrBtn,            uint8_t(4));
  v("statusBar",              s.statusBar,              uint8_t(3));
  v("orientation",            s.orientation,            uint8_t(4));
  v("fontSize",               s.fontSize,               uint8_t(4));
  v("pagesPerRefresh",        s.pagesPerRefresh,        uint8_t(6));
  v("sideButtonLayout",       s.sideButtonLayout,       uint8_t(2));
  v("autoSleepMinutes",       s.autoSleepMinutes,       uint8_t(5));
  v("paragraphAlignment",     s.paragraphAlignment,     uint8_t(4));
  v("hyphenation",            s.hyphenation,            uint8_t(2));
  v("textAntiAliasing",       s.textAntiAliasing,       uint8_t(2));
  v("showImages",             s.showImages,             uint8_t(3));
  v("startupBehavior",        s.startupBehavior,        uint8_t(2));
  v("_reserved",              s._reserved,              uint8_t(2));
  v("lineSpacing",            s.lineSpacing,            uint8_t(4));
  v("themeName",              s.themeName);                          // char[32]
  v("lastBookPath",           s.lastBookPath);                       // char[256]
  // pendingTransition: 0=none, 1=UI, 2=Reader, 3=Emulator. Max bound
  // must be 4 to allow 3 to round-trip; setting it to 3 silently
  // dropped EMULATOR mode (audit's #9 drift incident).
  v("pendingTransition",      s.pendingTransition,      uint8_t(4));
  v("transitionReturnTo",     s.transitionReturnTo,     uint8_t(2));
  v("sunlightFadingFix",      s.sunlightFadingFix,      uint8_t(2));
  v("fileListDir",            s.fileListDir);                        // char[256]
  v("fileListSelectedName",   s.fileListSelectedName);               // char[128]
  v("fileListSelectedIndex",  s.fileListSelectedIndex);              // uint16, no bound
  v("frontButtonLayout",      s.frontButtonLayout,      uint8_t(2));
  v("bleKeyboard",            s.bleKeyboard);                        // char[18]
  v("blePageTurner",          s.blePageTurner);                      // char[18]
  v("homeArtTheme",           s.homeArtTheme);                       // char[32]
  v("showTables",             s.showTables,             uint8_t(2));
  v("hiddenPluginMask",       s.hiddenPluginMask);                   // uint8_t[3]
  v("bleTimeout",             s.bleTimeout,             uint8_t(5));
  v("readerFont",             s.readerFont);                         // char[32]
  v("dictionaryName",         s.dictionaryName);                     // char[32]
  v("textDarkness",           s.textDarkness,           uint8_t(4));
  v("language",               s.language,               uint8_t(13));
  // Timezone offset (audit #47): no max-bound here, SumiClock clamps
  // [-720, +840] when setTimeZoneOffsetMinutes is called from main.
  v("timeZoneOffsetMinutes",  s.timeZoneOffsetMinutes);
}

// ─── Visitors ─────────────────────────────────────────────────────────

struct WriteVisitor {
  FsFile& file;
  // Optional CRC tracker. Save paths that want to emit a CRC32 trailer
  // pass a non-null pointer; the visitor updates it for every byte
  // written. Audit #26.
  Crc32* crc;

  // Explicit ctor for the same GCC 8.4 reason ReadVisitor has one:
  // aggregate brace-init of a struct mixing a reference member with a
  // default-initialised pointer member is a parse error on the
  // riscv32-esp-elf 8.4 toolchain that ships with espressif32 6.12.0.
  explicit WriteVisitor(FsFile& f, Crc32* c = nullptr) : file(f), crc(c) {}

  // POD with validation max — write raw, max is unused on save.
  template <typename T>
  void operator()(const char*, T& field, T) {
    serialization::writePod(file, field);
    if (crc) crc->update(&field, sizeof(T));
  }
  // POD without validation (e.g. uint16_t fileListSelectedIndex).
  template <typename T>
  void operator()(const char*, T& field) {
    serialization::writePod(file, field);
    if (crc) crc->update(&field, sizeof(T));
  }
  // Fixed-size array (char[N] or uint8_t[N]).
  template <typename T, size_t N>
  void operator()(const char*, T (&field)[N]) {
    file.write(reinterpret_cast<const uint8_t*>(field), N * sizeof(T));
    if (crc) crc->update(field, N * sizeof(T));
  }
};

struct ReadVisitor {
  FsFile& file;
  uint8_t fileFieldCount;
  uint8_t fieldsRead;
  // Optional CRC tracker. Load paths that want to verify a CRC32
  // trailer pass a non-null pointer; the visitor updates it for every
  // byte read FROM DISK (before any in-memory clamping that
  // readPodValidated may apply). Audit #26.
  Crc32* crc = nullptr;

  // Explicit ctor — GCC 8.4 (riscv32-esp-elf shipped with espressif32
  // 6.12.0) doesn't accept aggregate brace-init of structs that mix a
  // default-member-initialiser with a reference member. The codebase is
  // built on `-std=c++2a`-equivalent for the .cpp, but the toolchain's
  // aggregate handling is conservative.
  ReadVisitor(FsFile& f, uint8_t count)
      : file(f), fileFieldCount(count), fieldsRead(0) {}

  // Past the file's reported field count, do nothing — preserves the
  // struct's defaults for fields the older firmware that wrote this
  // file didn't know about.
  bool exhausted() const { return fieldsRead >= fileFieldCount; }

  // POD with validation. Mirrors readPodValidated() semantics: read raw
  // bytes; if the read was truncated, leave `field` at its prior default;
  // otherwise commit ONLY when raw < maxValue (strict less-than — the
  // existing semantic the visitor inherited from the pre-Batch-3
  // open-coded loader). CRC is updated over the on-disk bytes regardless
  // of the validation outcome so the trailer match reflects the file.
  template <typename T>
  void operator()(const char*, T& field, T maxValue) {
    if (exhausted()) return;
    T raw{};
    const int n = file.read(reinterpret_cast<uint8_t*>(&raw), sizeof(T));
    if (crc) crc->update(&raw, sizeof(T));
    if (n == static_cast<int>(sizeof(T)) && raw < maxValue) {
      field = raw;
    }
    ++fieldsRead;
  }
  // POD without validation.
  template <typename T>
  void operator()(const char*, T& field) {
    if (exhausted()) return;
    serialization::readPod(file, field);
    if (crc) crc->update(&field, sizeof(T));
    ++fieldsRead;
  }
  // Fixed-size array.
  template <typename T, size_t N>
  void operator()(const char*, T (&field)[N]) {
    if (exhausted()) return;
    file.read(reinterpret_cast<uint8_t*>(field), N * sizeof(T));
    if (crc) crc->update(field, N * sizeof(T));
    // Force null termination on char arrays so a corrupt-or-truncated
    // file can't leave us with an unterminated string. (Using
    // ::value rather than _v because the TU defaults to C++14 under
    // the espressif32@6.12.0 toolchain even though the project's
    // platformio.ini doesn't pin a std level.)
    if (std::is_same<T, char>::value) {
      field[N - 1] = '\0';
    }
    ++fieldsRead;
  }
};

// Counts the rows `forEachSettingsField` produces. Used by save() to
// stamp a count header that stays in sync with the schema even when
// fields are added.
struct CountVisitor {
  uint8_t count = 0;
  template <typename T> void operator()(const char*, T&, T) { ++count; }
  template <typename T> void operator()(const char*, T&)    { ++count; }
  template <typename T, size_t N>
  void operator()(const char*, T (&)[N]) { ++count; }
};

uint8_t computeSettingsCount() {
  Settings dummy;
  CountVisitor v;
  forEachSettingsField(dummy, v);
  return v.count;
}

}  // namespace

Result<void> Settings::save(drivers::Storage& storage) const {
  // Make sure the directories exist
  storage.mkdir(SUMI_DIR);
  storage.mkdir(SUMI_CACHE_DIR);

  // Atomic write — see docs/ATOMIC_WRITE_DESIGN.md. pre-audit
  // settings.bin used regular openWrite + O_TRUNC, which had two failure
  // modes:
  //   1. Power loss between O_TRUNC and the first byte landed on disk
  //      left settings.bin empty → next boot read defaults → user lost
  //      every saved setting.
  //   2. On a fragmented heap (~25 KB free, observed in the emulator after
  //      running through several states), `sd.open(... O_RDWR | O_CREAT
  //      | O_TRUNC)` fails to allocate its FsFile internals → save
  //      silently does nothing → user's in-memory changes don't persist.
  // Both close with the atomic 3-rename rotation: a failed atomicOpenWrite
  // leaves the previous canonical settings.bin intact (no truncation),
  // and atomicCommit's rotation guarantees at least one valid file copy
  // exists at every point during a successful commit.
  FsFile outputFile;
  auto result = storage.atomicOpenWrite(SUMI_SETTINGS_FILE, outputFile);
  if (!result.ok()) {
    Serial.printf("[%lu] [SET] atomicOpenWrite failed for settings.bin\n",
                  millis());
    return result;
  }

  // CRC32 covers magic + version + count + every visitor-emitted byte.
  // The trailer goes outside the CRC. Audit #26.
  Crc32 c;

  // Header — magic + version + field-count. The count is derived from
  // the schema visitor walk so it stays correct as fields are added.
  const uint8_t fieldCount = computeSettingsCount();
  serialization::writePod(outputFile, SETTINGS_MAGIC);
  c.update(&SETTINGS_MAGIC, sizeof(SETTINGS_MAGIC));
  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  c.update(&SETTINGS_FILE_VERSION, sizeof(SETTINGS_FILE_VERSION));
  serialization::writePod(outputFile, fieldCount);
  c.update(&fieldCount, sizeof(fieldCount));

  // Body — walk the schema once, write each field, hashing as we go.
  WriteVisitor visitor{outputFile, &c};
  forEachSettingsField(const_cast<Settings&>(*this), visitor);

  // Trailer — 4-byte CRC32. Atomic-write protocol prevents
  // partial writes; CRC32 catches the rarer SD-bit-flip / cluster
  // corruption case on top.
  const uint32_t trailer = c.finalize();
  serialization::writePod(outputFile, trailer);

  if (!storage.atomicCommit(outputFile, SUMI_SETTINGS_FILE).ok()) {
    storage.atomicAbort(outputFile, SUMI_SETTINGS_FILE);
    Serial.printf("[%lu] [SET] atomicCommit failed for settings.bin\n",
                  millis());
    return ErrVoid(Error::IOError);
  }
  Serial.printf("[%lu] [SET] Settings saved to file (%u fields, crc=0x%08X)\n",
                millis(), static_cast<unsigned>(fieldCount),
                static_cast<unsigned>(trailer));
  return Ok();
}

Result<void> Settings::load(drivers::Storage& storage) {
  FsFile inputFile;
  auto result = storage.openRead(SUMI_SETTINGS_FILE, inputFile);
  if (!result.ok()) {
    return result;
  }

  // CRC tracking. Used only when version >= SETTINGS_VERSION_WITH_CRC.
  // Older files have no trailer; we still load them and the next save
  // will add the trailer.
  Crc32 c;

  // Check magic signature to detect incompatible settings files
  // (e.g. files written by Crosspoint firmware sharing the same SD).
  uint32_t magic;
  serialization::readPod(inputFile, magic);
  c.update(&magic, sizeof(magic));
  if (magic != SETTINGS_MAGIC) {
    Serial.printf("[%lu] [SET] Invalid settings file (wrong magic 0x%08X), deleting\n", millis(), magic);
    inputFile.close();
    storage.remove(SUMI_SETTINGS_FILE);
    return ErrVoid(Error::UnsupportedVersion);
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  c.update(&version, sizeof(version));
  if (version < MIN_SETTINGS_VERSION || version > SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [SET] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return ErrVoid(Error::UnsupportedVersion);
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  c.update(&fileSettingsCount, sizeof(fileSettingsCount));

  // Cap to the schema's current count so a corrupt file can't trick us
  // into draining bytes that don't follow the schema.
  const uint8_t schemaCount = computeSettingsCount();
  if (fileSettingsCount > schemaCount) {
    Serial.printf("[%lu] [SET] fileSettingsCount %u exceeds max %u, capping\n",
                  millis(), fileSettingsCount, schemaCount);
    fileSettingsCount = schemaCount;
  }

  // Body — walk the schema, ReadVisitor stops once it has consumed
  // `fileFieldCount` fields. Older files with fewer fields fall through
  // here with the struct's defaults intact for the unwritten tail; that
  // covers the v3..v15 → v16 migration path with no per-version code.
  // The CRC pointer is wired only for v17+ where the trailer exists.
  const bool hasTrailer = version >= SETTINGS_VERSION_WITH_CRC;
  ReadVisitor visitor{inputFile, fileSettingsCount};
  if (hasTrailer) visitor.crc = &c;
  forEachSettingsField(*this, visitor);

  // Verify CRC32 trailer if present. Tolerant per audit policy: a
  // mismatch logs and the data already loaded stays — we don't roll
  // back. The atomic-write protocol (Batch 1) already ensures we
  // never see a half-written settings.bin; CRC mismatch here means
  // the canonical file got bit-flipped after writing, which is rare
  // enough that "log + use the data anyway" is the right call.
  if (hasTrailer) {
    uint32_t fileCrc = 0;
    if (serialization::readPodChecked(inputFile, fileCrc)) {
      const uint32_t computed = c.finalize();
      if (fileCrc != computed) {
        Serial.printf("[%lu] [SET] WARN: settings.bin CRC32 mismatch "
                      "(file=0x%08X computed=0x%08X); accepting payload\n",
                      millis(), static_cast<unsigned>(fileCrc),
                      static_cast<unsigned>(computed));
      }
    } else {
      Serial.printf("[%lu] [SET] WARN: settings.bin v%u missing CRC32 trailer\n",
                    millis(), static_cast<unsigned>(version));
    }
  }

  // Migrate font size from version < 8 (enum values shifted +1 when
  // FontXSmall was added at index 0).
  // Old: FontSmall=0, FontMedium=1, FontLarge=2
  // New: FontXSmall=0, FontSmall=1, FontMedium=2, FontLarge=3
  // Bound-check after the bump so a corrupt v<8 file can't roll fontSize
  // off the end of the enum (audit #27). The visitor's < maxValue clamp
  // already enforces fontSize < 4 on read, but the +1 happens AFTER the
  // visitor, so it can push a valid 3 to an invalid 4. Clamp back to
  // FontLarge in that case.
  // TODO: Delete this migration when MIN_SETTINGS_VERSION >= 8.
  if (version < 8) {
    fontSize++;
    if (fontSize > FontLarge) fontSize = FontLarge;
  }

  inputFile.close();
  Serial.printf("[%lu] [SET] Settings loaded from file (%u of %u fields, v%u%s)\n",
                millis(), static_cast<unsigned>(fileSettingsCount),
                static_cast<unsigned>(schemaCount),
                static_cast<unsigned>(version),
                hasTrailer ? " +crc" : "");
  return Ok();
}

int Settings::getReaderFontId(const Theme& theme) const {
  // If user has selected a specific font, override theme's choice
  const char* family = nullptr;
  int builtinId = 0;
  switch (fontSize) {
    case FontXSmall:
      family = readerFont[0] ? readerFont : theme.readerFontFamilyXSmall;
      builtinId = theme.readerFontIdXSmall;
      break;
    case FontMedium:
      family = readerFont[0] ? readerFont : theme.readerFontFamilyMedium;
      builtinId = theme.readerFontIdMedium;
      break;
    case FontLarge:
      family = readerFont[0] ? readerFont : theme.readerFontFamilyLarge;
      builtinId = theme.readerFontIdLarge;
      break;
    default:  // FontSmall
      family = readerFont[0] ? readerFont : theme.readerFontFamilySmall;
      builtinId = theme.readerFontId;
      break;
  }
  return FONT_MANAGER.getReaderFontId(family, builtinId);
}

bool Settings::hasExternalReaderFont(const Theme& theme) const {
  if (readerFont[0]) return true;  // User-selected font always counts
  const char* family = nullptr;
  switch (fontSize) {
    case FontXSmall:  family = theme.readerFontFamilyXSmall; break;
    case FontMedium:  family = theme.readerFontFamilyMedium; break;
    case FontLarge:   family = theme.readerFontFamilyLarge;  break;
    default:          family = theme.readerFontFamilySmall;  break;
  }
  return family && *family;
}

RenderConfig Settings::getRenderConfig(const Theme& theme, uint16_t viewportWidth, uint16_t viewportHeight) const {
  return RenderConfig(getReaderFontId(theme), getLineCompression(), getIndentLevel(), getSpacingLevel(),
                      paragraphAlignment, static_cast<bool>(hyphenation), static_cast<bool>(showImages),
                      static_cast<bool>(showTables), viewportWidth, viewportHeight);
}

// Legacy methods that use SdMan directly (for early init before Core).
// They share the schema visitor with the Storage-wrapping methods above —
// they're just the version that doesn't have a Storage instance yet.
bool Settings::saveToFile() const {
  SdMan.mkdir(SUMI_DIR);
  SdMan.mkdir(SUMI_CACHE_DIR);

  // Atomic write — same rationale as Settings::save above. The the emulator
  // sweep observed [DRV] Failed save attempts when heap was fragmented
  // during state-back navigation; switching to atomicOpenWrite means
  // a save failure is well-defined (canonical settings.bin stays at
  // its previous content) and a partial write is impossible.
  FsFile outputFile;
  if (!SdMan.atomicOpenWrite("SET", SUMI_SETTINGS_FILE, outputFile)) {
    Serial.printf("[%lu] [SET] atomicOpenWrite failed for settings.bin\n",
                  millis());
    return false;
  }

  Crc32 c;
  const uint8_t fieldCount = computeSettingsCount();
  serialization::writePod(outputFile, SETTINGS_MAGIC);
  c.update(&SETTINGS_MAGIC, sizeof(SETTINGS_MAGIC));
  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  c.update(&SETTINGS_FILE_VERSION, sizeof(SETTINGS_FILE_VERSION));
  serialization::writePod(outputFile, fieldCount);
  c.update(&fieldCount, sizeof(fieldCount));

  WriteVisitor visitor{outputFile, &c};
  forEachSettingsField(const_cast<Settings&>(*this), visitor);

  const uint32_t trailer = c.finalize();
  serialization::writePod(outputFile, trailer);

  if (!SdMan.atomicCommit(outputFile, SUMI_SETTINGS_FILE)) {
    SdMan.atomicAbort(outputFile, SUMI_SETTINGS_FILE);
    Serial.printf("[%lu] [SET] atomicCommit failed for settings.bin\n",
                  millis());
    return false;
  }
  Serial.printf("[%lu] [SET] Settings saved to file (%u fields, crc=0x%08X)\n",
                millis(), static_cast<unsigned>(fieldCount),
                static_cast<unsigned>(trailer));
  return true;
}

bool Settings::loadFromFile() {
  FsFile inputFile;
  if (!SdMan.openFileForRead("SET", SUMI_SETTINGS_FILE, inputFile)) {
    return false;
  }

  Crc32 c;

  uint32_t magic;
  serialization::readPod(inputFile, magic);
  c.update(&magic, sizeof(magic));
  if (magic != SETTINGS_MAGIC) {
    Serial.printf("[%lu] [SET] Invalid settings file (wrong magic 0x%08X), deleting\n", millis(), magic);
    inputFile.close();
    SdMan.remove(SUMI_SETTINGS_FILE);
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  c.update(&version, sizeof(version));
  if (version < MIN_SETTINGS_VERSION || version > SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [SET] Deserialization failed: Unknown version %u\n", millis(), version);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  c.update(&fileSettingsCount, sizeof(fileSettingsCount));

  const uint8_t schemaCount = computeSettingsCount();
  if (fileSettingsCount > schemaCount) {
    Serial.printf("[%lu] [SET] fileSettingsCount %u exceeds max %u, capping\n",
                  millis(), fileSettingsCount, schemaCount);
    fileSettingsCount = schemaCount;
  }

  const bool hasTrailer = version >= SETTINGS_VERSION_WITH_CRC;
  ReadVisitor visitor{inputFile, fileSettingsCount};
  if (hasTrailer) visitor.crc = &c;
  forEachSettingsField(*this, visitor);

  // CRC32 trailer (v17+). Tolerant per audit policy: a mismatch logs
  // and the data already loaded stays — atomic-write protocol covers
  // partial-write corruption; CRC mismatch implies post-write bit-flip
  // which is rare enough that "log + use the data anyway" is correct.
  if (hasTrailer) {
    uint32_t fileCrc = 0;
    if (serialization::readPodChecked(inputFile, fileCrc)) {
      const uint32_t computed = c.finalize();
      if (fileCrc != computed) {
        Serial.printf("[%lu] [SET] WARN: settings.bin CRC32 mismatch "
                      "(file=0x%08X computed=0x%08X); accepting payload\n",
                      millis(), static_cast<unsigned>(fileCrc),
                      static_cast<unsigned>(computed));
      }
    } else {
      Serial.printf("[%lu] [SET] WARN: settings.bin v%u missing CRC32 trailer\n",
                    millis(), static_cast<unsigned>(version));
    }
  }

  // Migrate font size from version < 8 (enum values shifted +1 when
  // FontXSmall was added at index 0).
  if (version < 8) {
    fontSize++;
  }

  inputFile.close();
  Serial.printf("[%lu] [SET] Settings loaded from file (%u of %u fields, v%u%s)\n",
                millis(), static_cast<unsigned>(fileSettingsCount),
                static_cast<unsigned>(schemaCount),
                static_cast<unsigned>(version),
                hasTrailer ? " +crc" : "");
  return true;
}

}  // namespace sumi
