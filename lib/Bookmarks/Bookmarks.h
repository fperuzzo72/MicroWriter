#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sumi {

class Bookmarks {
 public:
  static constexpr int MAX_BOOKMARKS = 50;

  // Load bookmarks from <cachePath>/bookmarks.txt (one page number per line)
  static std::vector<uint32_t> load(const std::string& cachePath);

  // Toggle bookmark at given page -- adds if not present, removes if present.
  // Returns true if bookmark was ADDED, false if REMOVED.
  static bool toggle(const std::string& cachePath, uint32_t page);

  // Check if a specific page is bookmarked
  static bool isBookmarked(const std::string& cachePath, uint32_t page);

  // Get count without loading all data
  static int count(const std::string& cachePath);

 private:
  static std::string filePath(const std::string& cachePath);
};

}  // namespace sumi
