#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sumi {

// Cross-book bookmark index stored at /.sumi/global_bookmarks.bin
// Each entry records: book path hash, page number, short snippet
class GlobalBookmarkIndex {
 public:
  static constexpr int MAX_ENTRIES = 200;
  static constexpr int SNIPPET_LEN = 60;
  static constexpr const char* INDEX_PATH = "/.sumi/global_bookmarks.bin";

  struct Entry {
    uint32_t bookPathHash;     // FNV-1a of book path
    uint32_t page;
    char bookTitle[48];        // truncated book title for display
    char snippet[SNIPPET_LEN]; // first ~60 chars of bookmarked page
  };

  // Add a bookmark to the global index
  static void addBookmark(const char* bookPath, const char* bookTitle,
                          uint32_t page, const char* snippet = "");

  // Remove a bookmark from the global index
  static void removeBookmark(const char* bookPath, uint32_t page);

  // Load all entries
  static std::vector<Entry> loadAll();

  // Get count
  static int count();

  // Hash helper
  static uint32_t hashPath(const char* path);
};

}  // namespace sumi
