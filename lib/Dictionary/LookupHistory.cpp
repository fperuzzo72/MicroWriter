#include "LookupHistory.h"

#include <SDCardManager.h>

#include <algorithm>

namespace sumi {

std::string LookupHistory::filePath(const std::string& cachePath) { return cachePath + "/lookups.txt"; }

bool LookupHistory::hasHistory(const std::string& cachePath) {
  FsFile f;
  if (!SdMan.openFileForRead("LKH", filePath(cachePath).c_str(), f)) {
    return false;
  }
  const bool nonEmpty = f.available() > 0;
  f.close();
  return nonEmpty;
}

std::vector<std::string> LookupHistory::load(const std::string& cachePath) {
  std::vector<std::string> words;
  FsFile f;
  if (!SdMan.openFileForRead("LKH", filePath(cachePath).c_str(), f)) {
    return words;
  }

  // Per-line cap: no real dictionary word is > 128 bytes. A corrupt file
  // without any newlines would otherwise grow `line` until heap exhaustion
  // on the ESP32-C3 (380 KB total). Once a line hits the cap we discard
  // remaining characters until the next newline resumes normal parsing.
  constexpr size_t kMaxWordBytes = 128;
  std::string line;
  bool lineOverflowed = false;

  while (f.available() && static_cast<int>(words.size()) < MAX_ENTRIES) {
    char c;
    const int n = f.read(reinterpret_cast<uint8_t*>(&c), 1);
    if (n != 1) break;
    if (c == '\n') {
      if (!line.empty() && !lineOverflowed) {
        words.push_back(line);
      }
      line.clear();
      lineOverflowed = false;
    } else if (c != '\r') {
      if (lineOverflowed) continue;
      if (line.size() >= kMaxWordBytes) {
        lineOverflowed = true;
        line.clear();
        continue;
      }
      line += c;
    }
  }
  if (!line.empty() && !lineOverflowed && static_cast<int>(words.size()) < MAX_ENTRIES) {
    words.push_back(line);
  }
  f.close();
  return words;
}

void LookupHistory::removeWord(const std::string& cachePath, const std::string& word) {
  if (word.empty()) return;

  const auto existing = load(cachePath);

  // Atomic write — see docs/ATOMIC_WRITE_DESIGN.md. The earlier
  // O_APPEND fix (commit 3d45f69) only covered addWord; removeWord
  // still rewrote the file from scratch via openFileForWrite, leaving
  // a power-loss window where the canonical lookups.txt could be
  // empty and the user's entire history was lost.
  const std::string fp = filePath(cachePath);
  FsFile f;
  if (!SdMan.atomicOpenWrite("LKH", fp.c_str(), f)) {
    return;
  }

  for (const auto& w : existing) {
    if (w != word) {
      f.write(reinterpret_cast<const uint8_t*>(w.c_str()), w.size());
      f.write(reinterpret_cast<const uint8_t*>("\n"), 1);
    }
  }
  if (!SdMan.atomicCommit(f, fp.c_str())) {
    SdMan.atomicAbort(f, fp.c_str());
    // Caller has no return value to inspect; on commit failure the
    // existing history is preserved (previous behavior would have
    // silently lost it).
  }
}

void LookupHistory::addWord(const std::string& cachePath, const std::string& word) {
  if (word.empty()) return;

  // Dedupe + capacity check up front. load() is the only read pass we need.
  auto existing = load(cachePath);
  if (std::any_of(existing.begin(), existing.end(), [&word](const std::string& w) { return w == word; })) return;
  if (static_cast<int>(existing.size()) >= MAX_ENTRIES) return;

  // Append-only write. The previous implementation called
  // openFileForWrite() (which truncates), then re-streamed every existing
  // entry plus the new one. That meant a power loss between truncate and
  // the first re-write erased the entire history. O_APPEND keeps prior
  // entries intact even if we crash mid-write — at worst we lose the
  // single new word, never the whole file.
  FsFile f = SdMan.open(filePath(cachePath).c_str(), O_RDWR | O_CREAT | O_APPEND);
  if (!f) return;
  f.write(reinterpret_cast<const uint8_t*>(word.c_str()), word.size());
  f.write(reinterpret_cast<const uint8_t*>("\n"), 1);
  SdMan.syncAndClose(f);
}

}  // namespace sumi
