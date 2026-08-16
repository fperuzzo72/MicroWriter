#include "test_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Inline copies of the three pure-logic helpers from lib/Dictionary/Dictionary.cpp.
// Keeping them byte-for-byte identical to the production implementation lets us
// unit-test the word-normalisation / stemmer / edit-distance logic without
// linking against the full Dictionary translation unit — which would drag in
// SdMan, SDCardManager, and a full host stub chain. This mirrors the pattern
// established by test/unit/utf8/Utf8Test.cpp.

// ──────────────────────────────────────────────────────────────
// cleanWord: trim non-alphanumerics, lowercase. ASCII-only by design.
// ──────────────────────────────────────────────────────────────
static std::string cleanWord(const std::string& word) {
  if (word.empty()) return "";

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

// ──────────────────────────────────────────────────────────────
// getStemVariants: enumerate plausible morphological stems for
// a given English word. Heuristic — caller tries each in order.
// ──────────────────────────────────────────────────────────────
static std::vector<std::string> getStemVariants(const std::string& word) {
  std::vector<std::string> variants;
  const size_t len = word.size();
  if (len < 3) return variants;

  auto endsWith = [&word, len](const char* suffix) {
    const size_t slen = std::strlen(suffix);
    return len >= slen && word.compare(len - slen, slen, suffix) == 0;
  };

  auto add = [&variants](const std::string& s) {
    if (s.size() >= 2) variants.push_back(s);
  };

  // Plurals
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

  // Past tense
  if (endsWith("ied")) {
    add(word.substr(0, len - 3) + "y");
    add(word.substr(0, len - 1));
  }
  if (endsWith("ed") && !endsWith("ied")) {
    add(word.substr(0, len - 2));
    add(word.substr(0, len - 1));
    if (len > 4 && word[len - 3] == word[len - 4]) {
      add(word.substr(0, len - 3));
    }
  }

  // Progressive
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

  // Adverbs
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

  // Comparatives / superlatives
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

  // Derivational suffixes
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

  // Common prefixes
  if (len > 5 && word.compare(0, 2, "un") == 0) add(word.substr(2));
  if (len > 6 && word.compare(0, 3, "dis") == 0) add(word.substr(3));
  if (len > 6 && word.compare(0, 3, "mis") == 0) add(word.substr(3));
  if (len > 6 && word.compare(0, 3, "pre") == 0) add(word.substr(3));
  if (len > 7 && word.compare(0, 4, "over") == 0) add(word.substr(4));
  if (len > 5 && word.compare(0, 2, "re") == 0) add(word.substr(2));

  // Dedupe preserving order
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

// ──────────────────────────────────────────────────────────────
// editDistance: Wagner–Fischer with maxDist early exit.
// ──────────────────────────────────────────────────────────────
static int editDistance(const std::string& a, const std::string& b, int maxDist) {
  const int m = static_cast<int>(a.size());
  const int n = static_cast<int>(b.size());
  if (std::abs(m - n) > maxDist) return maxDist + 1;

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

// Helper for tests that need "does this stem list contain X?"
static bool containsVariant(const std::vector<std::string>& variants, const std::string& target) {
  for (const auto& v : variants) {
    if (v == target) return true;
  }
  return false;
}

int main() {
  TestUtils::TestRunner runner("Dictionary Functions");

  // ============================================
  // cleanWord() tests
  // ============================================

  // Empty / whitespace-only / punctuation-only
  runner.expectEqual("", cleanWord(""), "cleanWord: empty string");
  runner.expectEqual("", cleanWord("   "), "cleanWord: whitespace-only");
  runner.expectEqual("", cleanWord("!!!"), "cleanWord: punctuation-only");
  runner.expectEqual("", cleanWord("---"), "cleanWord: dash-only");
  runner.expectEqual("", cleanWord(".,;:!?"), "cleanWord: mixed punctuation");

  // Basic cases
  runner.expectEqual("hello", cleanWord("hello"), "cleanWord: already clean lowercase");
  runner.expectEqual("hello", cleanWord("Hello"), "cleanWord: Title Case → lowercase");
  runner.expectEqual("hello", cleanWord("HELLO"), "cleanWord: ALLCAPS → lowercase");
  runner.expectEqual("hello", cleanWord("HeLLo"), "cleanWord: MixedCase → lowercase");

  // Leading / trailing punctuation stripping
  runner.expectEqual("hello", cleanWord("hello!"), "cleanWord: trailing bang");
  runner.expectEqual("hello", cleanWord("\"hello\""), "cleanWord: double-quotes");
  runner.expectEqual("hello", cleanWord("'hello'"), "cleanWord: single-quotes");
  runner.expectEqual("hello", cleanWord("(hello)"), "cleanWord: parens");
  runner.expectEqual("hello", cleanWord("[hello]"), "cleanWord: brackets");
  runner.expectEqual("hello", cleanWord("{hello}"), "cleanWord: braces");
  runner.expectEqual("hello", cleanWord("...hello..."), "cleanWord: leading+trailing dots");
  runner.expectEqual("word", cleanWord("-word-"), "cleanWord: dashes stripped");

  // Word with internal punctuation preserved (only leading/trailing is trimmed)
  runner.expectEqual("don't", cleanWord("don't"), "cleanWord: internal apostrophe kept");
  runner.expectEqual("don't", cleanWord("Don't."), "cleanWord: apostrophe + trailing dot");
  runner.expectEqual("co-op", cleanWord("co-op"), "cleanWord: internal hyphen kept");
  runner.expectEqual("co-op", cleanWord("\"co-op\""), "cleanWord: hyphenated + quotes");

  // Numbers and alphanumerics (cleanWord is alnum-based, not alpha)
  runner.expectEqual("abc123", cleanWord("abc123"), "cleanWord: alphanumeric");
  runner.expectEqual("123", cleanWord("123"), "cleanWord: pure digits are alnum");

  // Single character (boundary case — start and end point at the same char)
  runner.expectEqual("a", cleanWord("a"), "cleanWord: single letter");
  runner.expectEqual("a", cleanWord("\"a\""), "cleanWord: single letter quoted");

  // Unicode / accented stripped (function is ASCII-only by design)
  runner.expectEqual("caf", cleanWord("caf\xC3\xA9"), "cleanWord: trailing multi-byte dropped");
  runner.expectEqual("hello", cleanWord("\xC2\xABhello\xC2\xBB"), "cleanWord: guillemets stripped");

  // ============================================
  // getStemVariants() tests
  // ============================================

  // Too-short inputs return empty
  runner.expectTrue(getStemVariants("").empty(), "getStemVariants: empty returns empty");
  runner.expectTrue(getStemVariants("a").empty(), "getStemVariants: 1-char returns empty");
  runner.expectTrue(getStemVariants("ab").empty(), "getStemVariants: 2-char returns empty");

  // Plurals
  runner.expectTrue(containsVariant(getStemVariants("cats"), "cat"),
                    "getStemVariants: 'cats' → 'cat'");
  runner.expectTrue(containsVariant(getStemVariants("boxes"), "box"),
                    "getStemVariants: 'boxes' → 'box'");
  runner.expectTrue(containsVariant(getStemVariants("berries"), "berry"),
                    "getStemVariants: 'berries' → 'berry'");
  runner.expectTrue(containsVariant(getStemVariants("classes"), "class"),
                    "getStemVariants: 'classes' → 'class'");
  runner.expectTrue(containsVariant(getStemVariants("wolves"), "wolf"),
                    "getStemVariants: 'wolves' → 'wolf'");
  runner.expectTrue(containsVariant(getStemVariants("knives"), "knife"),
                    "getStemVariants: 'knives' → 'knife'");
  runner.expectTrue(containsVariant(getStemVariants("men"), "man"),
                    "getStemVariants: 'men' → 'man'");
  runner.expectTrue(containsVariant(getStemVariants("women"), "woman"),
                    "getStemVariants: 'women' → 'woman'");

  // Past tense
  runner.expectTrue(containsVariant(getStemVariants("walked"), "walk"),
                    "getStemVariants: 'walked' → 'walk'");
  runner.expectTrue(containsVariant(getStemVariants("stopped"), "stop"),
                    "getStemVariants: 'stopped' → 'stop' (doubled consonant)");
  runner.expectTrue(containsVariant(getStemVariants("cried"), "cry"),
                    "getStemVariants: 'cried' → 'cry'");
  runner.expectTrue(containsVariant(getStemVariants("liked"), "like"),
                    "getStemVariants: 'liked' → 'like' (trailing -e form)");

  // Progressive
  runner.expectTrue(containsVariant(getStemVariants("running"), "run"),
                    "getStemVariants: 'running' → 'run' (doubled consonant)");
  runner.expectTrue(containsVariant(getStemVariants("walking"), "walk"),
                    "getStemVariants: 'walking' → 'walk'");
  runner.expectTrue(containsVariant(getStemVariants("making"), "make"),
                    "getStemVariants: 'making' → 'make' (trailing -e form)");
  runner.expectTrue(containsVariant(getStemVariants("dying"), "die"),
                    "getStemVariants: 'dying' → 'die'");

  // Adverbs
  runner.expectTrue(containsVariant(getStemVariants("quickly"), "quick"),
                    "getStemVariants: 'quickly' → 'quick'");
  runner.expectTrue(containsVariant(getStemVariants("basically"), "basic"),
                    "getStemVariants: 'basically' → 'basic'");
  runner.expectTrue(containsVariant(getStemVariants("happily"), "happy"),
                    "getStemVariants: 'happily' → 'happy'");

  // Comparatives
  runner.expectTrue(containsVariant(getStemVariants("bigger"), "big"),
                    "getStemVariants: 'bigger' → 'big' (doubled consonant)");
  runner.expectTrue(containsVariant(getStemVariants("happier"), "happy"),
                    "getStemVariants: 'happier' → 'happy'");
  runner.expectTrue(containsVariant(getStemVariants("biggest"), "big"),
                    "getStemVariants: 'biggest' → 'big' (doubled consonant)");
  runner.expectTrue(containsVariant(getStemVariants("happiest"), "happy"),
                    "getStemVariants: 'happiest' → 'happy'");

  // Derivational suffixes
  runner.expectTrue(containsVariant(getStemVariants("kindness"), "kind"),
                    "getStemVariants: 'kindness' → 'kind'");
  runner.expectTrue(containsVariant(getStemVariants("agreement"), "agree"),
                    "getStemVariants: 'agreement' → 'agree'");
  runner.expectTrue(containsVariant(getStemVariants("hopeful"), "hope"),
                    "getStemVariants: 'hopeful' → 'hope'");
  runner.expectTrue(containsVariant(getStemVariants("hopeless"), "hope"),
                    "getStemVariants: 'hopeless' → 'hope'");
  runner.expectTrue(containsVariant(getStemVariants("readable"), "read"),
                    "getStemVariants: 'readable' → 'read'");
  runner.expectTrue(containsVariant(getStemVariants("creation"), "create"),
                    "getStemVariants: 'creation' → 'create'");
  runner.expectTrue(containsVariant(getStemVariants("creative"), "create"),
                    "getStemVariants: 'creative' → 'create'");
  runner.expectTrue(containsVariant(getStemVariants("realize"), "real"),
                    "getStemVariants: 'realize' → 'real'");

  // Prefixes
  runner.expectTrue(containsVariant(getStemVariants("unhappy"), "happy"),
                    "getStemVariants: 'unhappy' → 'happy'");
  runner.expectTrue(containsVariant(getStemVariants("disagree"), "agree"),
                    "getStemVariants: 'disagree' → 'agree'");
  runner.expectTrue(containsVariant(getStemVariants("misplace"), "place"),
                    "getStemVariants: 'misplace' → 'place'");
  runner.expectTrue(containsVariant(getStemVariants("preview"), "view"),
                    "getStemVariants: 'preview' → 'view'");
  runner.expectTrue(containsVariant(getStemVariants("overreact"), "react"),
                    "getStemVariants: 'overreact' → 'react'");
  runner.expectTrue(containsVariant(getStemVariants("rewrite"), "write"),
                    "getStemVariants: 'rewrite' → 'write'");

  // Dedup: running should not have duplicate entries in the result
  {
    const auto variants = getStemVariants("running");
    std::vector<std::string> seen;
    for (const auto& v : variants) {
      runner.expectTrue(std::find(seen.begin(), seen.end(), v) == seen.end(),
                        "getStemVariants: 'running' result has no duplicates");
      seen.push_back(v);
    }
  }

  // No over-stemming of short words: "ss" word like "ass" (len=3) has no
  // meaningful suffix match, should produce no variants or only minimal ones.
  // The function filters out variants shorter than 2 chars, so short words
  // stay stable.
  {
    const auto variants = getStemVariants("cat");
    // "cat" matches "s" end check... no wait, "cat" doesn't end in "s".
    // Expected: empty (the len>=3 guard lets "cat" through, but no suffix matches).
    runner.expectTrue(variants.empty(), "getStemVariants: 'cat' has no matching suffixes");
  }

  // Words ending in "ss" should not be treated as plural
  {
    const auto variants = getStemVariants("class");
    runner.expectTrue(!containsVariant(variants, "clas"),
                      "getStemVariants: 'class' does NOT yield 'clas' (ss guard)");
  }

  // Words ending in "us" should not be treated as plural
  {
    const auto variants = getStemVariants("bonus");
    runner.expectTrue(!containsVariant(variants, "bonu"),
                      "getStemVariants: 'bonus' does NOT yield 'bonu' (us guard)");
  }

  // ============================================
  // editDistance() tests
  // ============================================

  // Identical strings → 0
  runner.expectEq(0, editDistance("", "", 5), "editDistance: both empty → 0");
  runner.expectEq(0, editDistance("hello", "hello", 5), "editDistance: identical → 0");
  runner.expectEq(0, editDistance("a", "a", 5), "editDistance: single char identical → 0");

  // Empty vs non-empty → length of non-empty
  runner.expectEq(5, editDistance("", "hello", 10), "editDistance: '' vs 'hello' → 5");
  runner.expectEq(5, editDistance("hello", "", 10), "editDistance: 'hello' vs '' → 5");

  // Single substitution
  runner.expectEq(1, editDistance("cat", "bat", 5), "editDistance: 'cat' vs 'bat' → 1");
  runner.expectEq(1, editDistance("cat", "cut", 5), "editDistance: 'cat' vs 'cut' → 1");
  runner.expectEq(1, editDistance("cat", "car", 5), "editDistance: 'cat' vs 'car' → 1");

  // Single insertion / deletion
  runner.expectEq(1, editDistance("cat", "cats", 5), "editDistance: insert last → 1");
  runner.expectEq(1, editDistance("cats", "cat", 5), "editDistance: delete last → 1");
  runner.expectEq(1, editDistance("at", "cat", 5), "editDistance: insert first → 1");

  // Multiple edits
  runner.expectEq(3, editDistance("kitten", "sitting", 5),
                  "editDistance: classic 'kitten' vs 'sitting' → 3");
  runner.expectEq(2, editDistance("flaw", "lawn", 5), "editDistance: 'flaw' vs 'lawn' → 2");

  // Early exit: if abs(len diff) > maxDist, returns maxDist+1
  runner.expectEq(3, editDistance("a", "abcd", 2), "editDistance: length gap > maxDist → maxDist+1");
  runner.expectEq(3, editDistance("abcdefghij", "xy", 2),
                  "editDistance: huge length gap → maxDist+1 fast");

  // Row-minimum early exit
  runner.expectEq(3, editDistance("abcdef", "xyz123", 2),
                  "editDistance: totally different, early exits at maxDist+1");

  // maxDist=0: only identical strings
  runner.expectEq(0, editDistance("hello", "hello", 0), "editDistance: maxDist=0 identical → 0");
  runner.expectEq(1, editDistance("hello", "hallo", 0),
                  "editDistance: maxDist=0 one-sub → 1 (maxDist+1)");

  // Case sensitivity (cleanWord is supposed to lowercase first; the
  // distance function itself is case-sensitive)
  runner.expectEq(1, editDistance("Hello", "hello", 5),
                  "editDistance: 'H' vs 'h' → 1 (case-sensitive)");

  // Real-world "did you mean" scenarios that findSimilar() would use
  runner.expectEq(1, editDistance("definately", "definatly", 3),
                  "editDistance: common typo variants");
  runner.expectEq(2, editDistance("recieve", "receive", 3),
                  "editDistance: transposition typo → 2 (not a single swap)");

  // Longer strings stay correct — classic Wagner–Fischer example.
  // "intention" → "execution" has a canonical Levenshtein distance of 5.
  runner.expectEq(5, editDistance("intention", "execution", 10),
                  "editDistance: 'intention' vs 'execution' → 5");
  // And with a tight cap, the row-min early exit should fire.
  runner.expectEq(4, editDistance("intention", "execution", 3),
                  "editDistance: 'intention' vs 'execution' with maxDist=3 → maxDist+1");

  return runner.allPassed() ? 0 : 1;
}
