# Settings Visitor — load/save dedup sketch

**Status:** design (pre-implementation). Reviewed before Batch 3 begins.
**Drives:** [AUDIT_PLAN.md](AUDIT_PLAN.md) Batch 3.

## Why

`src/core/SumiSettings.cpp` has two ~200-line functions writing the same 35
fields in identical order:

- `Settings::save(file)` and `Settings::saveToFile(path)` — two save paths
- `Settings::load(file)` and `Settings::loadFromFile(path)` — two load paths

Adding a field today is a four-place edit. `SETTINGS_COUNT = 35` is a
hand-maintained constant with no `static_assert`. Every release ships a
settings change, and the audit documents one drift incident already
(`pendingTransition` enum `EMULATOR=3` was silently dropped on reload because
`readPodValidated` rejected `value >= maxValue` and the bound was hard-coded
to 3 instead of 4).

## Shape

The "table-of-pointers" approach is cast-hell on a struct of mixed types.
Pure type-erased visitors are unreadable. Middle path: a single
`forEachSettingsField(s, visitor)` template that **looks like a list of
field declarations** at the call site.

```cpp
// In SumiSettings.h:

template <typename Visitor>
constexpr void forEachSettingsField(Settings& s, Visitor&& v) {
  // Each row: name (for logs), reference to field, validation max
  // (or sentinel `kNoMax` for fields that don't validate).
  // The ORDER below is the on-disk wire order. It is also the only
  // place that order is declared. Add a field = one row.

  v("language",          s.language,           uint8_t(Settings::LangCount));
  v("fontSize",          s.fontSize,           uint8_t(Settings::FontSizeCount));
  v("paragraphAlign",    s.paragraphAlign,     uint8_t(Settings::AlignCount));
  v("hyphenation",       s.hyphenation,        uint8_t(Settings::HyphenCount));
  v("textDarkness",      s.textDarkness,       uint8_t(Settings::DarknessCount));
  v("sleepScreen",       s.sleepScreen,        uint8_t(Settings::SleepScreenCount));
  v("sleepTimer",        s.sleepTimer,         uint8_t(Settings::SleepTimerCount));
  v("autoSleep",         s.autoSleep,          uint8_t(Settings::AutoSleepCount));
  v("powerButton",       s.powerButton,        uint8_t(Settings::PowerButtonCount));
  v("pendingTransition", s.pendingTransition,  uint8_t(Settings::TransitionCount));
  v("orientation",       s.orientation,        uint8_t(Settings::OrientationCount));
  v("theme",             s.theme,              uint8_t(Settings::ThemeCount));
  // ... 23 more rows, one per field

  // Strings: the visitor knows how to handle char arrays vs PODs via
  // overload (see "Visitor signature" below).
  v("activeDictPath",    s.activeDictPath,     SETTINGS_STRING(MAX_DICT_PATH));
  v("lastBookPath",      s.lastBookPath,       SETTINGS_STRING(MAX_BOOK_PATH));
  // ... etc.
}
```

The function body **is** the schema. Reading the file is reading the wire
format.

## Visitor signature

Two `operator()` overloads — one for PODs/enums, one for strings:

```cpp
struct WriteVisitor {
  FsFile& file;
  bool ok = true;

  // POD / enum path.
  template <typename T>
  void operator()(const char* /*name*/, T& field, T /*max*/) {
    if (!ok) return;
    ok = serialization::writePod(file, field);
  }

  // Fixed-size char array path.
  template <size_t N>
  void operator()(const char* /*name*/, char (&field)[N], SettingsString) {
    if (!ok) return;
    ok = serialization::writeString(file, field, N);
  }
};
```

Read is symmetric:

```cpp
struct ReadVisitor {
  FsFile& file;
  size_t fieldsRead = 0;
  bool ok = true;

  template <typename T>
  void operator()(const char* name, T& field, T max) {
    if (!ok) return;
    if (!serialization::readPodValidated(file, field, max)) {
      Serial.printf("[SET] field '%s' rejected (out of range)\n", name);
      // tolerant: leave default, continue
    }
    ++fieldsRead;
  }

  template <size_t N>
  void operator()(const char* name, char (&field)[N], SettingsString) {
    if (!ok) return;
    if (!serialization::readString(file, field, N)) {
      Serial.printf("[SET] field '%s' read failed\n", name);
    }
    ++fieldsRead;
  }
};
```

Save and load each become ~10 lines:

```cpp
bool Settings::save(FsFile& f) const {
  // header
  serialization::writeMagic(f, SETTINGS_MAGIC);
  serialization::writePod(f, SETTINGS_VERSION);
  serialization::writePod(f, SETTINGS_COUNT);

  WriteVisitor v{f};
  forEachSettingsField(const_cast<Settings&>(*this), v);
  return v.ok;
}

bool Settings::load(FsFile& f) {
  if (!serialization::readMagic(f, SETTINGS_MAGIC)) return false;
  uint8_t fileVer = 0;
  if (!serialization::readPod(f, fileVer)) return false;
  uint8_t fileCount = 0;
  if (!serialization::readPod(f, fileCount)) return false;

  ReadVisitor v{f};
  forEachSettingsField(*this, v);

  // Tolerance: file may have FEWER fields than current (older firmware
  // wrote it). Defaults stand for the rest. File may NOT have more
  // unknown fields — fileCount > SETTINGS_COUNT means future firmware
  // wrote it; we should refuse and keep defaults.
  if (fileCount > SETTINGS_COUNT) {
    Serial.printf("[SET] file from future (count=%u > %u); using defaults\n",
                  fileCount, SETTINGS_COUNT);
    *this = Settings{};
    return false;
  }
  return v.ok && v.fieldsRead == fileCount;
}
```

## SETTINGS_COUNT becomes derived

```cpp
namespace {
struct CountVisitor {
  size_t count = 0;
  template <typename... Args> void operator()(Args&&...) { ++count; }
};
constexpr size_t computeSettingsCount() {
  Settings dummy;
  CountVisitor v;
  forEachSettingsField(dummy, v);
  return v.count;
}
}  // namespace

constexpr size_t SETTINGS_COUNT = computeSettingsCount();
static_assert(SETTINGS_COUNT == 35,
              "SETTINGS_COUNT changed — bump SETTINGS_VERSION and add a migration step");
```

If you add a row to `forEachSettingsField`, the `static_assert` fails with a
clear instruction to bump the version. **No more silent drift.** Adding a
field literally cannot get committed without addressing the migration story.

## Migration

Migrations stay where they are (lines 215-217 / 447-449 in the current
SumiSettings.cpp). They run **after** the per-field reads but before the
`*this = decoded` assignment, so the visitor sees raw on-disk values and the
migration sees the assembled struct. Pseudocode:

```cpp
bool Settings::load(FsFile& f) {
  Settings raw{};        // local — visitors mutate this
  // ... visitor walk into `raw` ...
  if (fileVer < SETTINGS_VERSION) {
    if (!migrate(raw, fileVer)) return false;   // see SumiSettings.cpp:migrate
  }
  *this = raw;
  return true;
}
```

`migrate(raw, fileVer)` is the existing function. It already knows about
`fileVer < 8 → fontSize++` and similar. Its signature stays the same; the
caller stays the same.

## Removing duplication

After this change:

- `Settings::saveToFile(path)` reduces to: open file → call `save(f)` → close.
- `Settings::loadFromFile(path)` reduces to: open file → call `load(f)` → close.
- The 200-line bodies become ~30 lines each (header serialization + visitor
  walk). The 35-field declaration lives in **one place**: `forEachSettingsField`.

## One thing to watch

The visitor signature uses `T&` not `const T&` for the field, so save can
share with load. The `WriteVisitor` doesn't mutate the field, so a clean
`const_cast` at the save call site (visible in the snippet above) is fine
and standard. The alternative — duplicate `forEachSettingsField` for const
and non-const — defeats the purpose.

If you find this objectionable, the C++ idiom is to make `forEachSettingsField`
an overload pair (one `Settings&`, one `const Settings&`) — but that's two
copies of the declaration list, exactly what we're avoiding. Keep the
const_cast. It's localized, justified, and the visitor concept makes the
intent obvious.

## Test plan

`test/unit/settings/SettingsVisitorTest.cpp`:
- Field count matches `static_assert`.
- Round-trip every field via the visitor; bytes-out == bytes-in.
- File-from-future (`fileCount > SETTINGS_COUNT`) is rejected.
- File-from-past (`fileCount < SETTINGS_COUNT`, older version) is accepted;
  unread fields stay at defaults; migration runs.
- A field with `value >= max` is rejected (not silently accepted).
- All existing settings tests keep passing — the visitor is an internal
  refactor with the same on-disk format.
