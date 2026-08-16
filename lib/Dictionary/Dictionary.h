#pragma once

#include <SdFat.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sumi {

/**
 * StarDict-compatible dictionary lookup for SUMI.
 *
 * Layout on SD card:
 *   /dictionary/
 *     english/
 *       english.ifo      (optional metadata — provides the display name)
 *       english.idx      (sorted word → (offset, size) pairs)
 *       english.dict     (raw definition bytes)
 *       english.cache    (sparse offset table, auto-generated on first load)
 *     japanese/
 *       japanese.idx
 *       japanese.dict
 *
 * Every subdirectory of /dictionary/ that contains a matching .idx + .dict
 * pair is considered a dictionary. The subdirectory name is the key used
 * in Settings::dictionaryName. The .ifo's `bookname` key supplies a
 * human-friendly display name; if absent we fall back to the directory name.
 *
 * Only one dictionary is "active" at a time. Callers use setActive() to
 * switch; the sparse offset table is rebuilt lazily on next lookup.
 *
 * Ported from mcrosson/crosspoint-reader fork of Crosspoint.
 * Extended with multi-dictionary support and an .ifo parser — SUMI's model
 * is "drop multiple dictionaries in a folder and pick one," whereas the
 * Crosspoint fork ships a single hardcoded /dictionary.idx at SD root.
 */
class Dictionary {
 public:
  struct Info {
    std::string name;         // subdirectory name (e.g. "english") — the key
    std::string displayName;  // from .ifo bookname, or name if missing
    uint32_t wordCount = 0;   // from .ifo, or 0 if unknown
    // StarDict sametypesequence: 'm' = plain UTF-8 text, 'h' = HTML,
    // 'x' = XDXF, 'k' = KingSoft, etc. Empty = unknown (assume plain text).
    // We read this so readDefinition() can strip markup when appropriate.
    char sameTypeSeq = '\0';
  };

  // True if at least one usable dictionary exists under /dictionary/.
  static bool anyAvailable();

  // List every dictionary found under /dictionary/, sorted by displayName.
  // Returns an empty vector if none exist or the folder is missing.
  static std::vector<Info> listAvailable();

  // Set the active dictionary by its directory-name key. Returns true if
  // the dictionary exists and was selected. Clears cached state.
  // Pass an empty string to deactivate (lookup() will return "").
  static bool setActive(const std::string& name);

  // Currently active dictionary name (directory key), or empty if none.
  static const std::string& activeName();

  // Human-readable name of the active dictionary, or empty if none.
  static std::string activeDisplayName();

  // Look up the active dictionary for `word`. Returns the definition body
  // or empty string if not found / no active dictionary / cancelled.
  // `onProgress` is called occasionally with 0-100 during the first-load
  // index build (subsequent lookups skip it). `shouldCancel` is polled
  // during long operations so the UI can abort the lookup via a back press.
  static std::string lookup(const std::string& word,
                            const std::function<void(int percent)>& onProgress = nullptr,
                            const std::function<bool()>& shouldCancel = nullptr);

  // One hit from a multi-dictionary lookup — produced by lookupAll() below.
  // `viaStemmer == true` means the original word didn't match this dict but
  // `headword` (a stem variant) did. Callers may want to label these hits
  // differently in UI to make the synthesis transparent.
  struct LookupResult {
    std::string dictName;          // directory key, e.g. "english"
    std::string dictDisplayName;   // .ifo bookname or directory name
    std::string headword;          // exact key that matched (== word, or a stem)
    std::string definition;        // body, already stripped of HTML if applicable
    bool viaStemmer = false;
  };

  // Walk every dictionary under /dictionary/, looking up `word` in each.
  // Returns one entry per dictionary that had a direct hit (sorted by
  // dictionary displayName). If no dictionary had a direct hit, falls back
  // to the morphology stemmer and tries each stem in each dictionary,
  // returning one entry per dictionary that had a stem hit (first stem
  // wins per dict). Empty vector if nothing matched anywhere.
  //
  // Memory model: walks dicts sequentially, calling setActive()/unload()
  // around each so only one dict's sparse offset cache is resident at a
  // time. After return, the LAST dict walked stays "active" — subsequent
  // single-dict calls land there.
  static std::vector<LookupResult> lookupAll(
      const std::string& word,
      const std::function<void(int percent)>& onProgress = nullptr,
      const std::function<bool()>& shouldCancel = nullptr);

  // Trim punctuation and lowercase. Returns "" if nothing alphanumeric
  // remains. Used by callers to normalise user-selected / user-typed words
  // before invoking lookup().
  static std::string cleanWord(const std::string& word);

  // English morphology stemmer. Returns a list of candidate stems to try
  // when a direct lookup of `word` fails (e.g. "running" → "run"). Caller
  // should iterate and lookup each in order.
  static std::vector<std::string> getStemVariants(const std::string& word);

  // Find dictionary entries within edit-distance of `word`, for the
  // "Did you mean?" suggestions screen. Returns at most `maxResults`
  // entries ordered closest first. Empty if no active dictionary.
  static std::vector<std::string> findSimilar(const std::string& word, int maxResults);

  // Aggregate "Did you mean?" — walks every dictionary, gathers suggestions
  // from each, dedupes, returns up to `maxResults`. Used when lookupAll()
  // returned empty so the user still gets useful suggestions even without
  // a configured "active" dictionary.
  static std::vector<std::string> findSimilarAll(const std::string& word, int maxResults);

  // Release cached sparse offsets (called when switching away from the
  // dictionary feature or when RAM pressure demands it). The active
  // selection is preserved.
  static void unload();

 private:
  static constexpr int SPARSE_INTERVAL = 512;

  static std::vector<uint32_t> sparseOffsets_;
  static uint32_t totalWords_;
  static bool indexLoaded_;
  static std::string activeName_;
  static std::string activeDisplayName_;
  // sametypesequence from the active .ifo (see Info::sameTypeSeq).
  // Controls whether readDefinition() strips HTML markup before returning.
  static char activeSameTypeSeq_;

  // Paths derived from the active dictionary name.
  static std::string idxPath();
  static std::string dictPath();
  static std::string cachePath();
  static std::string ifoPath();

  static bool loadIndex(const std::function<void(int percent)>& onProgress,
                        const std::function<bool()>& shouldCancel);
  static bool loadCachedIndex();
  static void saveCachedIndex();
  static std::string readWord(FsFile& file);
  static std::string readDefinition(uint32_t offset, uint32_t size);
  static int editDistance(const std::string& a, const std::string& b, int maxDist);

  // Parse the StarDict .ifo file to extract bookname/wordcount. Returns
  // true if the file exists and at least one key was parsed.
  static bool parseIfo(const std::string& path, Info& out);
};

}  // namespace sumi
