#pragma once

#include <string>
#include <vector>

namespace sumi {

/**
 * Per-book lookup history: remembers which dictionary words the user has
 * looked up while reading a given book. Used by the "Looked up words" list
 * in the reader menu so a student can revisit vocabulary they've marked.
 *
 * Stored as a newline-delimited plain-text file at
 *   <book cache path>/lookups.txt
 *
 * One line per unique word. Duplicates are skipped on add. Capped at
 * MAX_ENTRIES to prevent the file from growing unbounded over a long read.
 *
 * Ported straight from the Crosspoint fork's src/util/LookupHistory with
 * only the namespace and SUMI storage API swapped in.
 */
class LookupHistory {
 public:
  static std::vector<std::string> load(const std::string& cachePath);
  static void addWord(const std::string& cachePath, const std::string& word);
  static void removeWord(const std::string& cachePath, const std::string& word);
  static bool hasHistory(const std::string& cachePath);

 private:
  static std::string filePath(const std::string& cachePath);
  static constexpr int MAX_ENTRIES = 500;
};

}  // namespace sumi
