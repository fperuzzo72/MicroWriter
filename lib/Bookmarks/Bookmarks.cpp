#include "Bookmarks.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cstdlib>

namespace sumi {

std::string Bookmarks::filePath(const std::string& cachePath) { return cachePath + "/bookmarks.txt"; }

std::vector<uint32_t> Bookmarks::load(const std::string& cachePath) {
  std::vector<uint32_t> pages;
  FsFile f;
  if (!SdMan.openFileForRead("BMK", filePath(cachePath).c_str(), f)) {
    return pages;
  }

  // Per-line cap: a bookmark entry is a decimal page number ("123\n"), so
  // 16 bytes is generous. A corrupt file with no newlines would otherwise
  // grow `line` unbounded — on a 380 KB heap device that's a real DoS.
  constexpr size_t kMaxLineBytes = 16;
  // Read in 256-byte chunks rather than byte-by-byte. Pre-Batch-9 the
  // hot path called f.read(&c, 1) per byte; SdFat goes through its full
  // cluster/sector cache check + position update + possible sector load
  // for every call. A 100-bookmark file = 500+ SdFat calls × ~5-20 µs
  // each = 2.5-10 ms per Bookmarks::load, called 3+ times per page
  // render (toggle, isBookmarked, count). Buffered = a handful of SdFat
  // calls instead. Audit #46.
  constexpr size_t kReadChunk = 256;
  uint8_t buf[kReadChunk];

  std::string line;
  bool lineOverflowed = false;
  auto commitLine = [&]() {
    if (!line.empty() && !lineOverflowed &&
        static_cast<int>(pages.size()) < MAX_BOOKMARKS) {
      char* end = nullptr;
      unsigned long val = strtoul(line.c_str(), &end, 10);
      if (end != line.c_str() && val <= 999999) {
        pages.push_back(static_cast<uint32_t>(val));
      }
    }
    line.clear();
    lineOverflowed = false;
  };

  while (f.available() && static_cast<int>(pages.size()) < MAX_BOOKMARKS) {
    const int got = f.read(buf, sizeof(buf));
    if (got <= 0) break;
    for (int i = 0; i < got && static_cast<int>(pages.size()) < MAX_BOOKMARKS; ++i) {
      const char c = static_cast<char>(buf[i]);
      if (c == '\n') {
        commitLine();
      } else if (c != '\r') {
        if (lineOverflowed) continue;
        if (line.size() >= kMaxLineBytes) {
          lineOverflowed = true;
          line.clear();
          continue;
        }
        line += c;
      }
    }
  }
  // Trailing line without final newline.
  commitLine();
  f.close();

  std::sort(pages.begin(), pages.end());
  return pages;
}

bool Bookmarks::toggle(const std::string& cachePath, uint32_t page) {
  auto pages = load(cachePath);

  auto it = std::lower_bound(pages.begin(), pages.end(), page);
  bool exists = (it != pages.end() && *it == page);

  if (exists) {
    // Remove
    pages.erase(it);
  } else {
    // Add -- silently refuse if at cap
    if (static_cast<int>(pages.size()) >= MAX_BOOKMARKS) {
      return false;
    }
    pages.insert(it, page);
  }

  // Rewrite entire file via the atomic-write protocol — see
  // docs/ATOMIC_WRITE_DESIGN.md. Truncate-then-rewrite previously left
  // bookmarks.txt empty if power was lost between truncate and the
  // first re-stream of the prior entries: user loses every bookmark
  // for the book.
  const std::string fp = filePath(cachePath);
  FsFile f;
  if (!SdMan.atomicOpenWrite("BMK", fp.c_str(), f)) {
    return !exists;
  }

  char buf[16];
  for (uint32_t p : pages) {
    int len = snprintf(buf, sizeof(buf), "%u\n", p);
    if (len > 0) {
      f.write(reinterpret_cast<const uint8_t*>(buf), len);
    }
  }
  if (!SdMan.atomicCommit(f, fp.c_str())) {
    SdMan.atomicAbort(f, fp.c_str());
    return !exists;
  }

  return !exists;  // true = added, false = removed
}

bool Bookmarks::isBookmarked(const std::string& cachePath, uint32_t page) {
  auto pages = load(cachePath);
  return std::binary_search(pages.begin(), pages.end(), page);
}

int Bookmarks::count(const std::string& cachePath) {
  return static_cast<int>(load(cachePath).size());
}

}  // namespace sumi
