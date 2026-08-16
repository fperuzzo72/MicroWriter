#include "Dictionary.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace sumi {

namespace {
constexpr const char* ROOT_DIR = "/dictionary";
constexpr uint32_t CACHE_MAGIC = 0x44494354;  // "DICT"

// Build "/dictionary/<name>/<name>.<ext>" with no heap fragmentation beyond
// the single std::string return. Caller must hold name stable.
std::string buildPath(const std::string& name, const char* ext) {
  std::string out;
  out.reserve(strlen(ROOT_DIR) + 2 + name.size() * 2 + strlen(ext) + 1);
  out.append(ROOT_DIR);
  out.push_back('/');
  out.append(name);
  out.push_back('/');
  out.append(name);
  out.push_back('.');
  out.append(ext);
  return out;
}

// Case-insensitive suffix match on a filename. Used when scanning the
// dictionary directory to find .idx and .dict pairs regardless of how the
// user capitalised them (macOS tends to preserve case, Windows is CI).
bool hasExtensionCI(const char* name, const char* ext) {
  const size_t nameLen = strlen(name);
  const size_t extLen = strlen(ext);
  if (nameLen < extLen) return false;
  const char* suffix = name + (nameLen - extLen);
  for (size_t i = 0; i < extLen; i++) {
    const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
    const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
    if (a != b) return false;
  }
  return true;
}

// Strip an extension (".idx", ".dict") off a filename. Returns the base name
// or empty string if the extension doesn't match.
std::string stripExtension(const char* name, const char* ext) {
  const size_t nameLen = strlen(name);
  const size_t extLen = strlen(ext);
  if (nameLen <= extLen) return "";
  if (!hasExtensionCI(name, ext)) return "";
  return std::string(name, nameLen - extLen);
}
}  // namespace

std::vector<uint32_t> Dictionary::sparseOffsets_;
uint32_t Dictionary::totalWords_ = 0;
bool Dictionary::indexLoaded_ = false;
std::string Dictionary::activeName_;
std::string Dictionary::activeDisplayName_;
char Dictionary::activeSameTypeSeq_ = '\0';

std::string Dictionary::idxPath() {
  return activeName_.empty() ? std::string() : buildPath(activeName_, "idx");
}

std::string Dictionary::dictPath() {
  return activeName_.empty() ? std::string() : buildPath(activeName_, "dict");
}

std::string Dictionary::cachePath() {
  return activeName_.empty() ? std::string() : buildPath(activeName_, "cache");
}

std::string Dictionary::ifoPath() {
  return activeName_.empty() ? std::string() : buildPath(activeName_, "ifo");
}

const std::string& Dictionary::activeName() { return activeName_; }
std::string Dictionary::activeDisplayName() { return activeDisplayName_; }

// StarDict .ifo format (text, key=value per line). Only the keys SUMI cares
// about are extracted. See https://github.com/huzheng001/stardict-3/blob/master/dict/doc/StarDictFileFormat
bool Dictionary::parseIfo(const std::string& path, Info& out) {
  FsFile f;
  if (!SdMan.openFileForRead("DICT", path.c_str(), f)) return false;

  bool any = false;
  std::string line;
  while (f.available()) {
    const int ch = f.read();
    if (ch < 0) break;
    if (ch == '\n' || ch == '\r') {
      if (!line.empty()) {
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
          const std::string key = line.substr(0, eq);
          const std::string value = line.substr(eq + 1);
          if (key == "bookname" && !value.empty()) {
            out.displayName = value;
            any = true;
          } else if (key == "wordcount") {
            out.wordCount = static_cast<uint32_t>(std::atol(value.c_str()));
            any = true;
          } else if (key == "sametypesequence" && !value.empty()) {
            // Only the first character matters — the spec allows a
            // per-type sequence (e.g. "mh") for heterogeneous entries
            // but every dictionary we ship uses a single type.
            out.sameTypeSeq = value[0];
            any = true;
          }
        }
        line.clear();
      }
    } else if (line.size() < 256) {
      line.push_back(static_cast<char>(ch));
    }
  }
  f.close();
  return any;
}

std::vector<Dictionary::Info> Dictionary::listAvailable() {
  std::vector<Info> out;

  FsFile dir;
  if (!SdMan.open(ROOT_DIR).isDirectory()) return out;  // no /dictionary folder
  if (!dir.open(ROOT_DIR, O_RDONLY)) return out;
  if (!dir.isDirectory()) {
    dir.close();
    return out;
  }

  FsFile entry;
  char nameBuf[64];
  while (entry.openNext(&dir, O_RDONLY)) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }
    entry.getName(nameBuf, sizeof(nameBuf));
    // Skip hidden directories (.DS_Store, ._foo etc — macOS resource forks).
    if (nameBuf[0] == '.') {
      entry.close();
      continue;
    }
    entry.close();

    const std::string dirName = nameBuf;
    const std::string idx = buildPath(dirName, "idx");
    const std::string dict = buildPath(dirName, "dict");
    if (!SdMan.exists(idx.c_str()) || !SdMan.exists(dict.c_str())) continue;

    Info info;
    info.name = dirName;
    info.displayName = dirName;  // fallback — overwritten by .ifo if present
    const std::string ifo = buildPath(dirName, "ifo");
    if (SdMan.exists(ifo.c_str())) parseIfo(ifo, info);
    out.push_back(std::move(info));
  }
  dir.close();

  std::sort(out.begin(), out.end(),
            [](const Info& a, const Info& b) { return a.displayName < b.displayName; });
  return out;
}

bool Dictionary::anyAvailable() {
  // Cheap variant: check if the /dictionary folder exists and has at least
  // one child with an .idx file. Avoids the full listAvailable scan used by
  // the settings selector — this is hit on Home boot and in the reader menu.
  FsFile dir;
  if (!dir.open(ROOT_DIR, O_RDONLY)) return false;
  if (!dir.isDirectory()) {
    dir.close();
    return false;
  }

  FsFile entry;
  char nameBuf[64];
  while (entry.openNext(&dir, O_RDONLY)) {
    if (entry.isDirectory() && nameBuf[0] != '.') {
      entry.getName(nameBuf, sizeof(nameBuf));
      entry.close();
      const std::string idx = buildPath(nameBuf, "idx");
      if (SdMan.exists(idx.c_str())) {
        dir.close();
        return true;
      }
    } else {
      entry.close();
    }
  }
  dir.close();
  return false;
}

bool Dictionary::setActive(const std::string& name) {
  if (name == activeName_) return !activeName_.empty();

  sparseOffsets_.clear();
  totalWords_ = 0;
  indexLoaded_ = false;

  if (name.empty()) {
    activeName_.clear();
    activeDisplayName_.clear();
    return false;
  }

  const std::string idx = buildPath(name, "idx");
  const std::string dict = buildPath(name, "dict");
  if (!SdMan.exists(idx.c_str()) || !SdMan.exists(dict.c_str())) {
    activeName_.clear();
    activeDisplayName_.clear();
    return false;
  }

  activeName_ = name;
  activeDisplayName_ = name;  // default, refined by .ifo parse if present
  activeSameTypeSeq_ = '\0';  // reset before parse
  const std::string ifo = buildPath(name, "ifo");
  if (SdMan.exists(ifo.c_str())) {
    Info info;
    info.name = name;
    info.displayName = name;
    if (parseIfo(ifo, info)) {
      activeDisplayName_ = info.displayName;
      activeSameTypeSeq_ = info.sameTypeSeq;
    }
  }

  return true;
}

void Dictionary::unload() {
  sparseOffsets_.clear();
  sparseOffsets_.shrink_to_fit();
  totalWords_ = 0;
  indexLoaded_ = false;
}

std::string Dictionary::cleanWord(const std::string& word) {
  if (word.empty()) return "";

  // Trim leading/trailing non-alphanumerics. Multi-byte UTF-8 bytes are in
  // the 0x80..0xFF range, which std::isalnum rejects on most locales, so
  // CJK/accented characters get stripped too. That's intentional — these
  // English stemmer + StarDict lookups only make sense for ASCII-ish words.
  size_t start = 0;
  while (start < word.size() && !std::isalnum(static_cast<unsigned char>(word[start]))) {
    start++;
  }

  size_t end = word.size();
  while (end > start && !std::isalnum(static_cast<unsigned char>(word[end - 1]))) {
    end--;
  }

  if (start >= end) return "";

  std::string result = word.substr(start, end - start);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

bool Dictionary::loadCachedIndex() {
  if (activeName_.empty()) return false;

  const std::string cachePathStr = cachePath();
  FsFile cache;
  if (!SdMan.openFileForRead("DICT", cachePathStr.c_str(), cache)) return false;

  uint8_t header[16];
  if (cache.read(header, 16) != 16) {
    cache.close();
    return false;
  }

  // Header is network-byte-order to match what Crosspoint writes, so a dict
  // cache built on one device reads cleanly on another regardless of host
  // endianness. ESP32-C3 is little-endian so we un-swap the four uint32s.
  const uint32_t magic = (static_cast<uint32_t>(header[0]) << 24) | (static_cast<uint32_t>(header[1]) << 16) |
                         (static_cast<uint32_t>(header[2]) << 8) | static_cast<uint32_t>(header[3]);
  const uint32_t expectedIdxSize = (static_cast<uint32_t>(header[4]) << 24) | (static_cast<uint32_t>(header[5]) << 16) |
                                   (static_cast<uint32_t>(header[6]) << 8) | static_cast<uint32_t>(header[7]);
  const uint32_t cachedTotalWords = (static_cast<uint32_t>(header[8]) << 24) |
                                    (static_cast<uint32_t>(header[9]) << 16) |
                                    (static_cast<uint32_t>(header[10]) << 8) | static_cast<uint32_t>(header[11]);
  const uint32_t offsetCount = (static_cast<uint32_t>(header[12]) << 24) | (static_cast<uint32_t>(header[13]) << 16) |
                               (static_cast<uint32_t>(header[14]) << 8) | static_cast<uint32_t>(header[15]);

  if (magic != CACHE_MAGIC) {
    cache.close();
    return false;
  }

  // Sanity cap on offsetCount so a corrupt cache header doesn't reserve
  // hundreds of MB. Real dictionaries have at most a few thousand sparse
  // offsets (1 per 512 words × millions of words = tens of thousands max).
  constexpr uint32_t kMaxSparseOffsets = 262144;  // 1 GB of words worth of offsets
  if (offsetCount == 0 || offsetCount > kMaxSparseOffsets) {
    cache.close();
    return false;
  }

  // Cross-check: the .idx file size in the cache header must match the
  // current .idx, otherwise the user swapped dictionaries without clearing
  // the cache and the sparse offsets would point at garbage.
  FsFile idx;
  if (!SdMan.openFileForRead("DICT", idxPath().c_str(), idx)) {
    cache.close();
    return false;
  }
  const uint32_t actualIdxSize = static_cast<uint32_t>(idx.fileSize());
  idx.close();

  if (expectedIdxSize != actualIdxSize) {
    cache.close();
    return false;
  }

  sparseOffsets_.resize(offsetCount);
  const int bytesNeeded = static_cast<int>(offsetCount * 4);
  uint8_t scratch[1024];
  int readPos = 0;
  while (readPos < bytesNeeded) {
    const int chunk = std::min<int>(sizeof(scratch), bytesNeeded - readPos);
    const int got = cache.read(scratch, chunk);
    if (got <= 0) {
      cache.close();
      sparseOffsets_.clear();
      return false;
    }
    // Little-endian-ish unpack from the cache file (written as uint32 arr
    // by saveCachedIndex below so endianness matches on the same device
    // but is deterministic across devices if both are little-endian).
    for (int i = 0; i + 3 < got; i += 4) {
      const uint32_t v = static_cast<uint32_t>(scratch[i]) |
                         (static_cast<uint32_t>(scratch[i + 1]) << 8) |
                         (static_cast<uint32_t>(scratch[i + 2]) << 16) |
                         (static_cast<uint32_t>(scratch[i + 3]) << 24);
      sparseOffsets_[(readPos + i) / 4] = v;
    }
    readPos += got;
  }
  cache.close();

  totalWords_ = cachedTotalWords;
  indexLoaded_ = true;
  return true;
}

void Dictionary::saveCachedIndex() {
  if (activeName_.empty() || sparseOffsets_.empty()) return;

  FsFile idx;
  if (!SdMan.openFileForRead("DICT", idxPath().c_str(), idx)) return;
  const uint32_t idxSize = static_cast<uint32_t>(idx.fileSize());
  idx.close();

  FsFile cache;
  if (!SdMan.openFileForWrite("DICT", cachePath().c_str(), cache)) return;

  const uint32_t offsetCount = static_cast<uint32_t>(sparseOffsets_.size());

  uint8_t header[16];
  header[0] = (CACHE_MAGIC >> 24) & 0xFF;
  header[1] = (CACHE_MAGIC >> 16) & 0xFF;
  header[2] = (CACHE_MAGIC >> 8) & 0xFF;
  header[3] = CACHE_MAGIC & 0xFF;
  header[4] = (idxSize >> 24) & 0xFF;
  header[5] = (idxSize >> 16) & 0xFF;
  header[6] = (idxSize >> 8) & 0xFF;
  header[7] = idxSize & 0xFF;
  header[8] = (totalWords_ >> 24) & 0xFF;
  header[9] = (totalWords_ >> 16) & 0xFF;
  header[10] = (totalWords_ >> 8) & 0xFF;
  header[11] = totalWords_ & 0xFF;
  header[12] = (offsetCount >> 24) & 0xFF;
  header[13] = (offsetCount >> 16) & 0xFF;
  header[14] = (offsetCount >> 8) & 0xFF;
  header[15] = offsetCount & 0xFF;
  cache.write(header, 16);

  // Write offsets as little-endian uint32 to match loadCachedIndex unpack.
  uint8_t buf[1024];
  size_t bufPos = 0;
  for (uint32_t i = 0; i < offsetCount; i++) {
    const uint32_t v = sparseOffsets_[i];
    buf[bufPos++] = v & 0xFF;
    buf[bufPos++] = (v >> 8) & 0xFF;
    buf[bufPos++] = (v >> 16) & 0xFF;
    buf[bufPos++] = (v >> 24) & 0xFF;
    if (bufPos + 4 > sizeof(buf)) {
      cache.write(buf, bufPos);
      bufPos = 0;
    }
  }
  if (bufPos > 0) cache.write(buf, bufPos);
  // Sync before close to guarantee the full cache (header + offsets) lands
  // on disk before the next lookup re-opens the file. Observed on the emulator's
  // virtual SD: writes larger than one sector silently truncate to 0 bytes
  // if you rely on close() alone to flush. Real SD cards probably don't
  // exhibit this, but sync() is cheap and makes the failure mode impossible.
  cache.sync();
  cache.close();
}

bool Dictionary::loadIndex(const std::function<void(int percent)>& onProgress,
                           const std::function<bool()>& shouldCancel) {
  if (activeName_.empty()) return false;

  FsFile idx;
  if (!SdMan.openFileForRead("DICT", idxPath().c_str(), idx)) return false;

  const uint32_t fileSize = static_cast<uint32_t>(idx.fileSize());

  sparseOffsets_.clear();
  totalWords_ = 0;

  uint32_t pos = 0;
  int lastReportedPercent = -1;

  // Walk the StarDict .idx file: each entry is a null-terminated UTF-8 word
  // followed by 4 bytes offset + 4 bytes size (both big-endian). Record
  // every SPARSE_INTERVAL-th entry's starting file position so lookup can
  // binary-search.
  while (pos < fileSize) {
    // Check abort every 100 entries. Cheap because walking the .idx is I/O
    // bound, not CPU bound — the cancel callback usually just polls a flag.
    if (shouldCancel && (totalWords_ % 100 == 0) && shouldCancel()) {
      idx.close();
      sparseOffsets_.clear();
      totalWords_ = 0;
      return false;
    }

    if (totalWords_ % SPARSE_INTERVAL == 0) {
      sparseOffsets_.push_back(pos);
    }

    // Scan to the null terminator.
    int ch;
    do {
      ch = idx.read();
      if (ch < 0) {
        pos = fileSize;
        break;
      }
      pos++;
    } while (ch != 0);

    if (pos >= fileSize) break;

    // Skip the 8 bytes of (offset, size) metadata.
    uint8_t skip[8];
    if (idx.read(skip, 8) != 8) break;
    pos += 8;

    totalWords_++;

    if (onProgress && fileSize > 0) {
      // Cap progress at 90% during the walk; the final 10% is the cache
      // write so the progress bar doesn't jump to 100% before saveCachedIndex.
      const int percent = static_cast<int>(static_cast<uint64_t>(pos) * 90 / fileSize);
      if (percent > lastReportedPercent + 4) {
        lastReportedPercent = percent;
        onProgress(percent);
      }
    }
  }

  idx.close();
  indexLoaded_ = true;
  if (totalWords_ > 0) {
    saveCachedIndex();
    if (onProgress) onProgress(95);
  }
  return totalWords_ > 0;
}

std::string Dictionary::readWord(FsFile& file) {
  // StarDict spec: word_str is "a UTF-8 encoded string, with terminating
  // '\0', and total length <= 256". A corrupt or malicious .idx file
  // missing the terminator would otherwise grow `word` until it hit EOF
  // — on a 10 MB index that's ~10 MB of std::string heap allocations
  // on a 380 KB device, OOMing instantly.
  //
  // Cap at 1024 bytes (well above the spec's 256 to tolerate slightly-
  // out-of-spec dictionaries that ship in the wild). On overflow we
  // stop reading; the caller treats an empty / mismatched key as
  // "no match" the same way it treats a normal lookup miss. Audit-
  // adjacent — audit pass after Batch 9.
  constexpr size_t kMaxWordBytes = 1024;
  std::string word;
  word.reserve(64);  // typical word length
  while (word.size() < kMaxWordBytes) {
    const int ch = file.read();
    if (ch <= 0) break;
    word += static_cast<char>(ch);
  }
  if (word.size() == kMaxWordBytes) {
    Serial.printf("[%lu] [DICT] WARN: word exceeded %zu bytes — corrupt index?\n",
                  millis(), kMaxWordBytes);
    return std::string();  // signal "lookup miss" via empty
  }
  return word;
}

// Strip HTML tags and decode common entities from a StarDict definition
// whose sametypesequence is 'h' (HTML). Preserves block-level breaks so the
// reader sees `<br>`/`<p>`/`<dt>`/`<dd>`/`<blockquote>` as real newlines,
// and collapses tags that are purely presentational (`<i>`, `<b>`, `<span>`,
// `<a>`) down to their text content. Based on real Etymology Online and
// GCIDE output; no full HTML parser needed since the input is well-formed.
static std::string stripHtml(const std::string& s) {
  std::string out;
  out.reserve(s.size());

  auto endsWithNewline = [&out]() {
    return !out.empty() && out.back() == '\n';
  };

  size_t i = 0;
  while (i < s.size()) {
    const char c = s[i];

    if (c == '<') {
      // Find the closing '>'. A malformed file with unmatched '<' stops
      // the scan at EOF; keep the literal so the user sees raw output
      // rather than a silently truncated entry.
      size_t end = s.find('>', i + 1);
      if (end == std::string::npos) {
        out.push_back(c);
        i++;
        continue;
      }

      // Extract the tag name (minus any '/' and attributes). Lowercase
      // manually so we don't drag in <algorithm> + std::tolower.
      size_t start = i + 1;
      if (start < s.size() && s[start] == '/') start++;
      std::string tag;
      while (start < end && s[start] != ' ' && s[start] != '\t' && s[start] != '/') {
        char tc = s[start];
        if (tc >= 'A' && tc <= 'Z') tc = static_cast<char>(tc + 32);
        tag.push_back(tc);
        start++;
      }

      // Block-level tags that should render as a paragraph break. Writing
      // two newlines looks ugly; the outer page renderer already wraps
      // lines, so a single '\n' is enough to start a new visual line.
      const bool isBlockBreak =
          tag == "br" || tag == "p" || tag == "div" || tag == "tr" ||
          tag == "li" || tag == "dt" || tag == "dd" ||
          tag == "blockquote" || tag == "h1" || tag == "h2" ||
          tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6";

      if (isBlockBreak && !endsWithNewline()) {
        out.push_back('\n');
      }

      i = end + 1;
      continue;
    }

    if (c == '&') {
      // Decode a few common HTML entities. Unknown entities pass through
      // so a broken input still shows readable text.
      size_t semi = s.find(';', i + 1);
      if (semi != std::string::npos && semi - i <= 8) {
        const std::string ent = s.substr(i + 1, semi - i - 1);
        const char* rep = nullptr;
        if (ent == "amp") rep = "&";
        else if (ent == "lt") rep = "<";
        else if (ent == "gt") rep = ">";
        else if (ent == "quot") rep = "\"";
        else if (ent == "apos" || ent == "#39") rep = "'";
        else if (ent == "nbsp" || ent == "#160") rep = " ";
        else if (ent == "mdash" || ent == "#8212") rep = "—";
        else if (ent == "ndash" || ent == "#8211") rep = "–";
        else if (ent == "hellip" || ent == "#8230") rep = "…";
        else if (ent == "lsquo" || ent == "#8216") rep = "‘";
        else if (ent == "rsquo" || ent == "#8217") rep = "’";
        else if (ent == "ldquo" || ent == "#8220") rep = "“";
        else if (ent == "rdquo" || ent == "#8221") rep = "”";
        if (rep) {
          out.append(rep);
          i = semi + 1;
          continue;
        }
        // Numeric entity not handled above — fall through to raw byte.
      }
      out.push_back(c);
      i++;
      continue;
    }

    // Collapse runs of whitespace to a single space, but preserve newlines
    // we inserted above. This matches how a browser renders HTML: multiple
    // spaces collapse, explicit <br>/<p> are real line breaks.
    if (c == ' ' || c == '\t' || c == '\r') {
      if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
        out.push_back(' ');
      }
      i++;
      continue;
    }
    if (c == '\n') {
      if (!endsWithNewline()) out.push_back('\n');
      i++;
      continue;
    }

    out.push_back(c);
    i++;
  }

  // Trim leading/trailing whitespace + newlines.
  size_t begin = 0;
  while (begin < out.size() && (out[begin] == ' ' || out[begin] == '\n')) begin++;
  size_t back = out.size();
  while (back > begin && (out[back - 1] == ' ' || out[back - 1] == '\n')) back--;
  return out.substr(begin, back - begin);
}

std::string Dictionary::readDefinition(uint32_t offset, uint32_t size) {
  if (activeName_.empty()) return "";

  // Hard cap on definition size to prevent OOM from a corrupt .idx pointing
  // at a nonsense size. Real StarDict definitions are usually under 4KB;
  // 256KB is way beyond any legitimate entry.
  constexpr uint32_t kMaxDefinitionSize = 256 * 1024;
  if (size == 0 || size > kMaxDefinitionSize) return "";

  FsFile dict;
  if (!SdMan.openFileForRead("DICT", dictPath().c_str(), dict)) return "";

  dict.seekSet(offset);

  std::string def(size, '\0');
  const int bytesRead = dict.read(reinterpret_cast<uint8_t*>(&def[0]), size);
  dict.close();

  if (bytesRead <= 0) return "";
  if (static_cast<uint32_t>(bytesRead) < size) def.resize(bytesRead);

  // If the .ifo marks this dictionary as HTML (sametypesequence=h), strip
  // tags and decode entities before returning. Both of the dictionaries
  // SUMI ships (Etymology Online, GCIDE) use 'h'; without this the reader
  // sees raw `<dt>word</dt><dd><i>etymology</i>...` markup on screen.
  // 'x' (XDXF) is also tag-heavy; fall back to the same stripper — the
  // tag names differ but the structural treatment (drop presentational,
  // break on block) still cleans up the worst of it.
  if (activeSameTypeSeq_ == 'h' || activeSameTypeSeq_ == 'H' ||
      activeSameTypeSeq_ == 'x' || activeSameTypeSeq_ == 'X') {
    def = stripHtml(def);
  }

  return def;
}

std::string Dictionary::lookup(const std::string& word,
                               const std::function<void(int percent)>& onProgress,
                               const std::function<bool()>& shouldCancel) {
  if (activeName_.empty() || word.empty()) return "";

  if (!indexLoaded_) {
    if (!loadCachedIndex()) {
      if (!loadIndex(onProgress, shouldCancel)) return "";
    }
  }

  if (sparseOffsets_.empty()) return "";

  FsFile idx;
  if (!SdMan.openFileForRead("DICT", idxPath().c_str(), idx)) return "";

  // Binary search across the sparse offsets to pick which 512-entry segment
  // `word` would live in. Each step seeks to the segment start and reads
  // just the first (null-terminated) word there for comparison.
  int lo = 0;
  int hi = static_cast<int>(sparseOffsets_.size()) - 1;

  while (lo < hi) {
    if (shouldCancel && shouldCancel()) {
      idx.close();
      return "";
    }

    const int mid = lo + (hi - lo + 1) / 2;
    idx.seekSet(sparseOffsets_[mid]);
    const std::string key = readWord(idx);

    if (key <= word) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }

  if (onProgress) onProgress(95);

  // Linear scan up to SPARSE_INTERVAL entries in the chosen segment.
  idx.seekSet(sparseOffsets_[lo]);

  int maxEntries = SPARSE_INTERVAL;
  if (lo == static_cast<int>(sparseOffsets_.size()) - 1) {
    maxEntries = static_cast<int>(totalWords_ - static_cast<uint32_t>(lo) * SPARSE_INTERVAL);
  }

  for (int i = 0; i < maxEntries; i++) {
    if (shouldCancel && shouldCancel()) {
      idx.close();
      return "";
    }

    const std::string key = readWord(idx);
    if (key.empty()) break;

    uint8_t buf[8];
    if (idx.read(buf, 8) != 8) break;

    // Offsets/sizes are big-endian in StarDict .idx (spec confirms).
    const uint32_t dictOffset = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
                                (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
    const uint32_t dictSize = (static_cast<uint32_t>(buf[4]) << 24) | (static_cast<uint32_t>(buf[5]) << 16) |
                              (static_cast<uint32_t>(buf[6]) << 8) | static_cast<uint32_t>(buf[7]);

    if (key == word) {
      idx.close();
      if (onProgress) onProgress(100);
      return readDefinition(dictOffset, dictSize);
    }

    // .idx is sorted, so a key past the search term means we'll never find it.
    if (key > word) break;
  }

  idx.close();
  if (onProgress) onProgress(100);
  return "";
}

std::vector<Dictionary::LookupResult> Dictionary::lookupAll(
    const std::string& word,
    const std::function<void(int percent)>& onProgress,
    const std::function<bool()>& shouldCancel) {
  std::vector<LookupResult> results;
  if (word.empty()) return results;

  const auto dicts = listAvailable();
  if (dicts.empty()) return results;

  // Walk every dict for a DIRECT hit on `word` first. setActive() per dict
  // swaps the static cache so only one dict's sparseOffsets_ is resident.
  // Stem fallback runs in a second pass below — direct hits are higher
  // confidence and we want them to appear before stem-derived hits.
  for (size_t i = 0; i < dicts.size(); i++) {
    if (shouldCancel && shouldCancel()) return results;

    const auto& info = dicts[i];
    if (!setActive(info.name)) continue;

    // Per-dict sub-progress: total walk goes 0→90% spread over dicts;
    // the final 10% is reserved for the post-walk render path.
    auto subProgress = onProgress ? [&onProgress, i, n = dicts.size()](int pct) {
      const int base = static_cast<int>((i * 90) / n);
      const int span = static_cast<int>(90 / n);
      onProgress(base + (pct * span) / 100);
    } : std::function<void(int)>{};

    std::string def = lookup(word, subProgress, shouldCancel);
    if (!def.empty()) {
      LookupResult r;
      r.dictName = info.name;
      r.dictDisplayName = info.displayName;
      r.headword = word;
      r.definition = std::move(def);
      r.viaStemmer = false;
      results.push_back(std::move(r));
    }
  }

  if (!results.empty()) {
    if (onProgress) onProgress(100);
    return results;
  }

  // Second pass: stem fallback. Per-dict, try each stem variant in order,
  // record the FIRST stem that matches in that dict (don't pile up multiple
  // stem hits from one dict — that's confusing UX). Different dicts may
  // match different stems, which is fine.
  const auto stems = getStemVariants(word);
  if (stems.empty()) {
    if (onProgress) onProgress(100);
    return results;
  }

  for (const auto& info : dicts) {
    if (shouldCancel && shouldCancel()) return results;
    if (!setActive(info.name)) continue;

    for (const auto& stem : stems) {
      if (shouldCancel && shouldCancel()) return results;
      std::string def = lookup(stem);
      if (!def.empty()) {
        LookupResult r;
        r.dictName = info.name;
        r.dictDisplayName = info.displayName;
        r.headword = stem;
        r.definition = std::move(def);
        r.viaStemmer = true;
        results.push_back(std::move(r));
        break;  // one stem hit per dict is enough
      }
    }
  }

  if (onProgress) onProgress(100);
  return results;
}

std::vector<std::string> Dictionary::getStemVariants(const std::string& word) {
  std::vector<std::string> variants;
  const size_t len = word.size();
  if (len < 3) return variants;

  // Helpers captured for the readability of the mass of suffix rules below.
  // The rules are heuristic English morphology — they don't need to be
  // exhaustive, they just need to enumerate plausible stems for the caller
  // to try against the dictionary. Caller aborts on first hit.
  auto endsWith = [&word, len](const char* suffix) {
    const size_t slen = strlen(suffix);
    return len >= slen && word.compare(len - slen, slen, suffix) == 0;
  };

  auto add = [&variants](const std::string& s) {
    if (s.size() >= 2) variants.push_back(s);
  };

  // ── Plurals ─────────────────────────────────────────────────
  if (endsWith("sses")) add(word.substr(0, len - 2));
  if (endsWith("ses")) add(word.substr(0, len - 2) + "is");
  if (endsWith("ies")) {
    add(word.substr(0, len - 3) + "y");
    add(word.substr(0, len - 2));
  }
  if (endsWith("ves")) {
    add(word.substr(0, len - 3) + "f");
    add(word.substr(0, len - 3) + "fe");
    add(word.substr(0, len - 1));
  }
  if (endsWith("men")) add(word.substr(0, len - 3) + "man");
  if (endsWith("es") && !endsWith("sses") && !endsWith("ies") && !endsWith("ves")) {
    add(word.substr(0, len - 2));
    add(word.substr(0, len - 1));
  }
  if (endsWith("s") && !endsWith("ss") && !endsWith("us") && !endsWith("es")) {
    add(word.substr(0, len - 1));
  }

  // ── Past tense ──────────────────────────────────────────────
  if (endsWith("ied")) {
    add(word.substr(0, len - 3) + "y");
    add(word.substr(0, len - 1));
  }
  if (endsWith("ed") && !endsWith("ied")) {
    add(word.substr(0, len - 2));
    add(word.substr(0, len - 1));
    // doubled consonant, e.g. "stopped" → "stop"
    if (len > 4 && word[len - 3] == word[len - 4]) {
      add(word.substr(0, len - 3));
    }
  }

  // ── Progressive ─────────────────────────────────────────────
  if (endsWith("ying")) {
    add(word.substr(0, len - 4) + "ie");
  }
  if (endsWith("ing") && !endsWith("ying")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
    if (len > 5 && word[len - 4] == word[len - 5]) {
      add(word.substr(0, len - 4));
    }
  }

  // ── Adverbs ─────────────────────────────────────────────────
  if (endsWith("ically")) {
    add(word.substr(0, len - 6) + "ic");
    add(word.substr(0, len - 4));
  }
  if (endsWith("ally") && !endsWith("ically")) {
    add(word.substr(0, len - 4) + "al");
    add(word.substr(0, len - 2));
  }
  if (endsWith("ily") && !endsWith("ally")) {
    add(word.substr(0, len - 3) + "y");
  }
  if (endsWith("ly") && !endsWith("ily") && !endsWith("ally")) {
    add(word.substr(0, len - 2));
  }

  // ── Comparatives / superlatives ─────────────────────────────
  if (endsWith("ier")) {
    add(word.substr(0, len - 3) + "y");
  }
  if (endsWith("er") && !endsWith("ier")) {
    add(word.substr(0, len - 2));
    add(word.substr(0, len - 1));
    if (len > 4 && word[len - 3] == word[len - 4]) {
      add(word.substr(0, len - 3));
    }
  }
  if (endsWith("iest")) {
    add(word.substr(0, len - 4) + "y");
  }
  if (endsWith("est") && !endsWith("iest")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 2));
    if (len > 5 && word[len - 4] == word[len - 5]) {
      add(word.substr(0, len - 4));
    }
  }

  // ── Derivational suffixes ──────────────────────────────────
  if (endsWith("ness")) add(word.substr(0, len - 4));
  if (endsWith("ment")) add(word.substr(0, len - 4));
  if (endsWith("ful")) add(word.substr(0, len - 3));
  if (endsWith("less")) add(word.substr(0, len - 4));
  if (endsWith("able")) {
    add(word.substr(0, len - 4));
    add(word.substr(0, len - 4) + "e");
  }
  if (endsWith("ible")) {
    add(word.substr(0, len - 4));
    add(word.substr(0, len - 4) + "e");
  }
  if (endsWith("ation")) {
    add(word.substr(0, len - 5));
    add(word.substr(0, len - 5) + "e");
    add(word.substr(0, len - 5) + "ate");
  }
  if (endsWith("tion") && !endsWith("ation")) {
    add(word.substr(0, len - 4) + "te");
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
  }
  if (endsWith("ion") && !endsWith("tion")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
  }
  if (endsWith("al") && !endsWith("ial")) {
    add(word.substr(0, len - 2));
    add(word.substr(0, len - 2) + "e");
  }
  if (endsWith("ial")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
  }
  if (endsWith("ous")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
  }
  if (endsWith("ive")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
  }
  if (endsWith("ize")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
  }
  if (endsWith("ise")) {
    add(word.substr(0, len - 3));
    add(word.substr(0, len - 3) + "e");
  }
  if (endsWith("en")) {
    add(word.substr(0, len - 2));
    add(word.substr(0, len - 2) + "e");
  }

  // ── Common prefixes ────────────────────────────────────────
  if (len > 5 && word.compare(0, 2, "un") == 0) add(word.substr(2));
  if (len > 6 && word.compare(0, 3, "dis") == 0) add(word.substr(3));
  if (len > 6 && word.compare(0, 3, "mis") == 0) add(word.substr(3));
  if (len > 6 && word.compare(0, 3, "pre") == 0) add(word.substr(3));
  if (len > 7 && word.compare(0, 4, "over") == 0) add(word.substr(4));
  if (len > 5 && word.compare(0, 2, "re") == 0) add(word.substr(2));

  // Dedupe preserving order — caller probably wants to try "simpler" stems
  // first which means the order the suffix rules fired.
  std::vector<std::string> deduped;
  for (const auto& v : variants) {
    bool seen = false;
    for (const auto& existing : deduped) {
      if (existing == v) {
        seen = true;
        break;
      }
    }
    if (!seen) deduped.push_back(v);
  }
  return deduped;
}

int Dictionary::editDistance(const std::string& a, const std::string& b, int maxDist) {
  const int m = static_cast<int>(a.size());
  const int n = static_cast<int>(b.size());
  if (std::abs(m - n) > maxDist) return maxDist + 1;

  // Wagner–Fischer with one-row rolling storage. We early-exit when the row
  // minimum exceeds maxDist so "obviously too different" pairs don't cost us
  // the full O(m*n).
  std::vector<int> dp(n + 1);
  for (int j = 0; j <= n; j++) dp[j] = j;

  for (int i = 1; i <= m; i++) {
    int prev = dp[0];
    dp[0] = i;
    int rowMin = dp[0];
    for (int j = 1; j <= n; j++) {
      const int temp = dp[j];
      if (a[i - 1] == b[j - 1]) {
        dp[j] = prev;
      } else {
        dp[j] = 1 + std::min({prev, dp[j], dp[j - 1]});
      }
      prev = temp;
      if (dp[j] < rowMin) rowMin = dp[j];
    }
    if (rowMin > maxDist) return maxDist + 1;
  }
  return dp[n];
}

std::vector<std::string> Dictionary::findSimilar(const std::string& word, int maxResults) {
  if (activeName_.empty()) return {};
  if (!indexLoaded_ && !loadCachedIndex()) return {};
  if (sparseOffsets_.empty()) return {};

  FsFile idx;
  if (!SdMan.openFileForRead("DICT", idxPath().c_str(), idx)) return {};

  int lo = 0;
  int hi = static_cast<int>(sparseOffsets_.size()) - 1;
  while (lo < hi) {
    const int mid = lo + (hi - lo + 1) / 2;
    idx.seekSet(sparseOffsets_[mid]);
    const std::string key = readWord(idx);
    if (key <= word) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }

  // Widen the search window by one segment on either side. Similar words
  // can sit just outside the binary-searched segment (e.g. "colors" vs
  // "colour" falls on different segments).
  const int startSeg = std::max(0, lo - 1);
  const int endSeg = std::min(static_cast<int>(sparseOffsets_.size()) - 1, lo + 1);
  idx.seekSet(sparseOffsets_[startSeg]);

  int totalToScan = (endSeg - startSeg + 1) * SPARSE_INTERVAL;
  const int remaining = static_cast<int>(totalWords_) - startSeg * SPARSE_INTERVAL;
  if (totalToScan > remaining) totalToScan = remaining;

  const int maxDist = std::max(2, static_cast<int>(word.size()) / 3 + 1);

  struct Candidate {
    std::string text;
    int distance;
  };
  std::vector<Candidate> candidates;

  for (int i = 0; i < totalToScan; i++) {
    const std::string key = readWord(idx);
    if (key.empty()) break;

    uint8_t skip[8];
    if (idx.read(skip, 8) != 8) break;

    if (key == word) continue;
    const int dist = editDistance(key, word, maxDist);
    if (dist <= maxDist) {
      candidates.push_back({key, dist});
    }
  }

  idx.close();

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });

  std::vector<std::string> results;
  for (size_t i = 0; i < candidates.size() && static_cast<int>(results.size()) < maxResults; i++) {
    results.push_back(candidates[i].text);
  }
  return results;
}

std::vector<std::string> Dictionary::findSimilarAll(const std::string& word, int maxResults) {
  std::vector<std::string> aggregated;
  if (word.empty() || maxResults <= 0) return aggregated;

  const auto dicts = listAvailable();
  if (dicts.empty()) return aggregated;

  // Pull up to `maxResults` from each dict, then dedupe while preserving
  // order. Different dicts often surface different near-misses (one knows
  // British spellings, another knows technical jargon), so showing the
  // union keeps the user's "did you mean" screen useful even when their
  // typo is borderline.
  for (const auto& info : dicts) {
    if (!setActive(info.name)) continue;
    auto perDict = findSimilar(word, maxResults);
    for (auto& s : perDict) {
      if (std::find(aggregated.begin(), aggregated.end(), s) == aggregated.end()) {
        aggregated.push_back(std::move(s));
        if (static_cast<int>(aggregated.size()) >= maxResults) return aggregated;
      }
    }
  }
  return aggregated;
}

}  // namespace sumi
