#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>

// Inline the UTF-8 functions directly for testing (avoiding linker issues).
// Keep these byte-for-byte identical to lib/Utf8/Utf8.cpp so the tests
// guard against regressions in the production implementation.

static int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

static uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const int bytes = utf8CodepointLen(**string);
  const uint8_t* chr = *string;
  *string += bytes;

  if (bytes == 1) {
    return chr[0];
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  return cp;
}

static size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  // Walk back to find the start of the last UTF-8 character
  // UTF-8 continuation bytes start with 10xxxxxx (0x80-0xBF)
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

static void utf8TruncateChars(std::string& str, size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

static int utf8UnicodeWhitespaceBytes(const char* s, int remaining) {
  if (s == nullptr || remaining < 2) return 0;
  const auto b0 = static_cast<unsigned char>(s[0]);
  const auto b1 = static_cast<unsigned char>(s[1]);
  if (b0 == 0xC2 && b1 == 0xA0) return 2;
  if (remaining < 3) return 0;
  const auto b2 = static_cast<unsigned char>(s[2]);
  if (b0 == 0xE2 && b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) return 3;
  if (b0 == 0xE2 && b1 == 0x80 && b2 == 0xAF) return 3;
  if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3;
  if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3;
  return 0;
}

static size_t utf8SafeCopy(char* dest, const char* src, size_t destCap) {
  if (dest == nullptr || destCap == 0) return 0;
  if (src == nullptr) {
    dest[0] = '\0';
    return 0;
  }
  const size_t maxBytes = destCap - 1;
  size_t pos = 0;
  while (src[pos] != '\0') {
    const auto lead = static_cast<unsigned char>(src[pos]);
    size_t codepointLen;
    if (lead < 0x80)               codepointLen = 1;
    else if ((lead >> 5) == 0x6)   codepointLen = 2;
    else if ((lead >> 4) == 0xE)   codepointLen = 3;
    else if ((lead >> 3) == 0x1E)  codepointLen = 4;
    else                           codepointLen = 1;
    if (pos + codepointLen > maxBytes) break;
    for (size_t i = 0; i < codepointLen; ++i) {
      if (src[pos + i] == '\0') {
        dest[pos] = '\0';
        return pos;
      }
      dest[pos + i] = src[pos + i];
    }
    pos += codepointLen;
  }
  dest[pos] = '\0';
  return pos;
}

int main() {
  TestUtils::TestRunner runner("Utf8 Functions");

  // ============================================
  // utf8NextCodepoint() tests
  // ============================================

  // Test 1: ASCII (1-byte)
  {
    const unsigned char* str = reinterpret_cast<const unsigned char*>("ABC");
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>('A'), cp, "utf8NextCodepoint: ASCII 'A'");
    runner.expectEq(static_cast<size_t>(1), static_cast<size_t>(ptr - str), "utf8NextCodepoint: ASCII advances 1 byte");

    cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>('B'), cp, "utf8NextCodepoint: ASCII 'B'");
  }

  // Test 2: Latin Extended (2-byte) - é = U+00E9 = 0xC3 0xA9
  {
    const unsigned char str[] = {0xC3, 0xA9, 0x00};  // é
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0x00E9), cp, "utf8NextCodepoint: 2-byte 'e-acute' (U+00E9)");
    runner.expectEq(static_cast<size_t>(2), static_cast<size_t>(ptr - str), "utf8NextCodepoint: 2-byte advances 2 bytes");
  }

  // Test 3: CJK (3-byte) - 中 = U+4E2D = 0xE4 0xB8 0xAD
  {
    const unsigned char str[] = {0xE4, 0xB8, 0xAD, 0x00};  // 中
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0x4E2D), cp, "utf8NextCodepoint: 3-byte CJK (U+4E2D)");
    runner.expectEq(static_cast<size_t>(3), static_cast<size_t>(ptr - str), "utf8NextCodepoint: 3-byte advances 3 bytes");
  }

  // Test 4: Emoji (4-byte) - grinning face = U+1F600 = 0xF0 0x9F 0x98 0x80
  {
    const unsigned char str[] = {0xF0, 0x9F, 0x98, 0x80, 0x00};
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0x1F600), cp, "utf8NextCodepoint: 4-byte emoji (U+1F600)");
    runner.expectEq(static_cast<size_t>(4), static_cast<size_t>(ptr - str), "utf8NextCodepoint: 4-byte advances 4 bytes");
  }

  // Test 5: Null terminator
  {
    const unsigned char str[] = {0x00};
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0), cp, "utf8NextCodepoint: null terminator returns 0");
    runner.expectEq(static_cast<size_t>(0), static_cast<size_t>(ptr - str), "utf8NextCodepoint: null doesn't advance");
  }

  // Test 6: Invalid start byte (continuation byte at start) - 0x80-0xBF
  {
    const unsigned char str[] = {0x80, 'A', 0x00};  // Invalid start byte
    const unsigned char* ptr = str;
    utf8NextCodepoint(&ptr);
    // Falls back to 1-byte handling
    runner.expectEq(static_cast<size_t>(1), static_cast<size_t>(ptr - str),
                    "utf8NextCodepoint: invalid start byte advances 1 byte (fallback)");
  }

  // ============================================
  // utf8RemoveLastChar() tests
  // ============================================

  // Test 7: Empty string
  {
    std::string str = "";
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(0), newSize, "utf8RemoveLastChar: empty string returns 0");
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: empty string stays empty");
  }

  // Test 8: Single ASCII char
  {
    std::string str = "A";
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(0), newSize, "utf8RemoveLastChar: single ASCII returns 0");
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: single ASCII becomes empty");
  }

  // Test 9: Multiple ASCII chars
  {
    std::string str = "ABC";
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(2), newSize, "utf8RemoveLastChar: 'ABC' -> 'AB' (size 2)");
    runner.expectEqual("AB", str, "utf8RemoveLastChar: 'ABC' -> 'AB'");
  }

  // Test 10: Remove 2-byte char (e-acute)
  {
    std::string str = "caf\xC3\xA9";  // "cafe" with accent
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(3), newSize, "utf8RemoveLastChar: accented cafe -> 'caf' (size 3)");
    runner.expectEqual("caf", str, "utf8RemoveLastChar: accented cafe -> 'caf'");
  }

  // Test 11: Remove 3-byte char (CJK)
  {
    std::string str = "A\xE4\xB8\xAD";  // "A" + CJK char
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(1), newSize, "utf8RemoveLastChar: 'A+CJK' -> 'A' (size 1)");
    runner.expectEqual("A", str, "utf8RemoveLastChar: 'A+CJK' -> 'A'");
  }

  // Test 12: Remove 4-byte char (emoji)
  {
    std::string str = "Hi\xF0\x9F\x98\x80";  // "Hi" + emoji
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(2), newSize, "utf8RemoveLastChar: 'Hi+emoji' -> 'Hi' (size 2)");
    runner.expectEqual("Hi", str, "utf8RemoveLastChar: 'Hi+emoji' -> 'Hi'");
  }

  // Test 13: Mixed content - remove emoji first, then ASCII
  {
    std::string str = "A\xF0\x9F\x98\x80";  // "A" + emoji
    utf8RemoveLastChar(str);
    runner.expectEqual("A", str, "utf8RemoveLastChar: 'A+emoji' -> 'A'");
    utf8RemoveLastChar(str);
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: 'A' -> empty");
  }

  // Test 14: Only multi-byte characters
  {
    std::string str = "\xE4\xB8\xAD\xE6\x96\x87";  // Two CJK chars
    utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(3), str.size(), "utf8RemoveLastChar: 2 CJK -> 1 CJK (size 3)");
    utf8RemoveLastChar(str);
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: 1 CJK -> empty");
  }

  // ============================================
  // utf8TruncateChars() tests
  // ============================================

  // Test 15: Truncate 0 chars (no change)
  {
    std::string str = "Hello";
    utf8TruncateChars(str, 0);
    runner.expectEqual("Hello", str, "utf8TruncateChars: truncate 0 chars is no-op");
  }

  // Test 16: Truncate 1 char
  {
    std::string str = "Hello";
    utf8TruncateChars(str, 1);
    runner.expectEqual("Hell", str, "utf8TruncateChars: 'Hello' - 1 = 'Hell'");
  }

  // Test 17: Truncate N chars
  {
    std::string str = "Hello";
    utf8TruncateChars(str, 3);
    runner.expectEqual("He", str, "utf8TruncateChars: 'Hello' - 3 = 'He'");
  }

  // Test 18: Truncate more chars than exist
  {
    std::string str = "Hi";
    utf8TruncateChars(str, 10);
    runner.expectTrue(str.empty(), "utf8TruncateChars: truncate more than exist makes empty");
  }

  // Test 19: Truncate from empty string
  {
    std::string str = "";
    utf8TruncateChars(str, 5);
    runner.expectTrue(str.empty(), "utf8TruncateChars: empty string stays empty");
  }

  // Test 20: Mixed ASCII and multi-byte truncation
  {
    std::string str = "AB\xC3\xA9\xE4\xB8\xAD";  // "AB" + accent + CJK (4 chars)
    utf8TruncateChars(str, 2);                   // Remove CJK and accent
    runner.expectEqual("AB", str, "utf8TruncateChars: 'AB+accent+CJK' - 2 = 'AB'");
  }

  // Test 21: Truncate all chars from multi-byte string
  {
    std::string str = "\xF0\x9F\x98\x80\xF0\x9F\x98\x81";  // Two emojis
    utf8TruncateChars(str, 2);
    runner.expectTrue(str.empty(), "utf8TruncateChars: 2 emojis - 2 = empty");
  }

  // ============================================
  // Corner cases for invalid UTF-8
  // ============================================

  // Test 22: Incomplete 2-byte sequence at end
  {
    std::string str = "A\xC3";  // Incomplete 2-byte char
    utf8RemoveLastChar(str);
    // Should still handle gracefully - removes the orphan byte
    runner.expectEq(static_cast<size_t>(1), str.size(), "utf8RemoveLastChar: incomplete 2-byte handled");
  }

  // Test 23: Incomplete 3-byte sequence at end
  {
    std::string str = "A\xE4\xB8";  // Incomplete 3-byte char
    utf8RemoveLastChar(str);
    // The implementation walks back past continuation bytes
    runner.expectTrue(str.size() <= 2, "utf8RemoveLastChar: incomplete 3-byte handled");
  }

  // Test 24: Overlong encoding detection (should be handled as fallback)
  // 0xC0 0x80 is an overlong encoding of NUL - technically invalid
  {
    const unsigned char str[] = {0xC0, 0x80, 0x00};
    const unsigned char* ptr = str;
    utf8NextCodepoint(&ptr);
    // Should at least not crash - advances some bytes
    runner.expectTrue(ptr > str, "utf8NextCodepoint: overlong encoding advances");
  }

  // Test 25: String with only continuation bytes
  {
    std::string str = "\x80\x80\x80";  // Invalid: only continuation bytes
    utf8RemoveLastChar(str);
    // Implementation walks back until it finds a non-continuation byte or beginning
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: all continuation bytes removed");
  }

  // ============================================
  // utf8UnicodeWhitespaceBytes() — word-boundary detection for
  // French/Spanish NBSP + NARROW NBSP, CJK ideographic space, etc.
  // ============================================

  // Recognised codepoints should each report the correct byte count:
  {
    const char nbsp[]       = "\xC2\xA0";      // U+00A0 NO-BREAK SPACE
    const char enQuad[]     = "\xE2\x80\x80";  // U+2000 EN QUAD
    const char enSpace[]    = "\xE2\x80\x82";  // U+2002 EN SPACE
    const char emSpace[]    = "\xE2\x80\x83";  // U+2003 EM SPACE
    const char thinSpace[]  = "\xE2\x80\x89";  // U+2009 THIN SPACE
    const char hairSpace[]  = "\xE2\x80\x8A";  // U+200A HAIR SPACE
    const char narrowNbsp[] = "\xE2\x80\xAF";  // U+202F NARROW NO-BREAK SPACE
    const char medMathSp[]  = "\xE2\x81\x9F";  // U+205F MEDIUM MATHEMATICAL SPACE
    const char ideoSpace[]  = "\xE3\x80\x80";  // U+3000 IDEOGRAPHIC SPACE

    runner.expectEq(2, utf8UnicodeWhitespaceBytes(nbsp, 2),       "utf8UnicodeWhitespaceBytes: NBSP");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(enQuad, 3),     "utf8UnicodeWhitespaceBytes: EN QUAD");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(enSpace, 3),    "utf8UnicodeWhitespaceBytes: EN SPACE");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(emSpace, 3),    "utf8UnicodeWhitespaceBytes: EM SPACE");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(thinSpace, 3),  "utf8UnicodeWhitespaceBytes: THIN SPACE");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(hairSpace, 3),  "utf8UnicodeWhitespaceBytes: HAIR SPACE");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(narrowNbsp, 3), "utf8UnicodeWhitespaceBytes: NARROW NBSP");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(medMathSp, 3),  "utf8UnicodeWhitespaceBytes: MEDIUM MATH SPACE");
    runner.expectEq(3, utf8UnicodeWhitespaceBytes(ideoSpace, 3),  "utf8UnicodeWhitespaceBytes: IDEOGRAPHIC SPACE");
  }

  // Non-whitespace multi-byte sequences must return 0.
  {
    const char acuteE[]     = "\xC3\xA9";      // U+00E9 é
    const char cjkA[]       = "\xE3\x81\x82";  // U+3042 hiragana 'a'
    const char emdash[]     = "\xE2\x80\x94";  // U+2014 EM DASH
    const char leftGuill[]  = "\xC2\xAB";      // U+00AB «
    const char rightGuill[] = "\xC2\xBB";      // U+00BB »
    const char ascii[]      = "hello";

    runner.expectEq(0, utf8UnicodeWhitespaceBytes(acuteE, 2),     "utf8UnicodeWhitespaceBytes: é is not whitespace");
    runner.expectEq(0, utf8UnicodeWhitespaceBytes(cjkA, 3),       "utf8UnicodeWhitespaceBytes: hiragana a is not whitespace");
    runner.expectEq(0, utf8UnicodeWhitespaceBytes(emdash, 3),     "utf8UnicodeWhitespaceBytes: em-dash is not whitespace");
    runner.expectEq(0, utf8UnicodeWhitespaceBytes(leftGuill, 2),  "utf8UnicodeWhitespaceBytes: « is not whitespace");
    runner.expectEq(0, utf8UnicodeWhitespaceBytes(rightGuill, 2), "utf8UnicodeWhitespaceBytes: » is not whitespace");
    runner.expectEq(0, utf8UnicodeWhitespaceBytes(ascii, 5),      "utf8UnicodeWhitespaceBytes: ASCII letter");
  }

  // Truncation / bounds safety — should never read past `remaining`.
  {
    // 3-byte sequence but only 2 bytes remaining → not enough to verify → 0.
    runner.expectEq(0, utf8UnicodeWhitespaceBytes("\xE2\x80", 2), "utf8UnicodeWhitespaceBytes: truncated 3-byte returns 0");
    // 2-byte NBSP but only 1 byte remaining → 0.
    runner.expectEq(0, utf8UnicodeWhitespaceBytes("\xC2", 1),     "utf8UnicodeWhitespaceBytes: truncated NBSP returns 0");
    // remaining == 0 → 0, even with a valid buffer.
    runner.expectEq(0, utf8UnicodeWhitespaceBytes("\xC2\xA0", 0), "utf8UnicodeWhitespaceBytes: remaining=0 returns 0");
    // nullptr safety.
    runner.expectEq(0, utf8UnicodeWhitespaceBytes(nullptr, 5),    "utf8UnicodeWhitespaceBytes: nullptr returns 0");
  }

  // French typography sanity: every space in "«\u00A0mot\u00A0:\u00A0»" must
  // be recognised so the phrase splits into four word-tokens instead of one.
  {
    const char phrase[] = "\xC2\xAB\xC2\xA0mot\xC2\xA0:\xC2\xA0\xC2\xBB";  // «\u00A0mot\u00A0:\u00A0»
    int splits = 0;
    int i = 0;
    const int len = static_cast<int>(sizeof(phrase) - 1);
    while (i < len) {
      const int n = utf8UnicodeWhitespaceBytes(phrase + i, len - i);
      if (n > 0) {
        ++splits;
        i += n;
      } else {
        ++i;
      }
    }
    runner.expectEq(3, splits, "utf8UnicodeWhitespaceBytes: French phrase has 3 NBSPs");
  }

  // ============================================
  // utf8SafeCopy() tests
  // ============================================

  // ASCII fits entirely — standard copy, no truncation.
  {
    char dest[16] = {};
    const size_t n = utf8SafeCopy(dest, "hello", sizeof(dest));
    runner.expectEq(static_cast<size_t>(5), n, "utf8SafeCopy: ASCII full fit returns length");
    runner.expectTrue(std::strcmp(dest, "hello") == 0, "utf8SafeCopy: ASCII content preserved");
  }

  // ASCII truncated at capacity boundary.
  {
    char dest[4] = {};  // room for 3 chars + NUL
    const size_t n = utf8SafeCopy(dest, "hello", sizeof(dest));
    runner.expectEq(static_cast<size_t>(3), n, "utf8SafeCopy: ASCII truncated to destCap-1");
    runner.expectTrue(std::strcmp(dest, "hel") == 0, "utf8SafeCopy: ASCII truncation is clean");
  }

  // CJK string fits entirely: "中文" = 6 bytes (3+3), destCap must be >= 7.
  {
    char dest[16] = {};
    const size_t n = utf8SafeCopy(dest, "\xE4\xB8\xAD\xE6\x96\x87", sizeof(dest));
    runner.expectEq(static_cast<size_t>(6), n, "utf8SafeCopy: CJK full fit returns 6 bytes");
    runner.expectEq(static_cast<unsigned char>(0xE4), static_cast<unsigned char>(dest[0]), "utf8SafeCopy: CJK byte 0");
    runner.expectEq(static_cast<unsigned char>(0x87), static_cast<unsigned char>(dest[5]), "utf8SafeCopy: CJK byte 5");
    runner.expectEq(static_cast<char>(0), dest[6], "utf8SafeCopy: CJK null-terminated");
  }

  // CJK truncation: "中文" (6 bytes) into destCap=5 → only "中" fits (3 bytes + NUL),
  // the second codepoint would need 3 more bytes but only 1 is available. This is
  // the regression the helper was written for — strncpy would have emitted the
  // lead byte of "文" plus a stray continuation and corrupted the final char.
  {
    char dest[5] = {};
    const size_t n = utf8SafeCopy(dest, "\xE4\xB8\xAD\xE6\x96\x87", sizeof(dest));
    runner.expectEq(static_cast<size_t>(3), n, "utf8SafeCopy: CJK truncation stops at codepoint boundary");
    runner.expectEq(static_cast<unsigned char>(0xE4), static_cast<unsigned char>(dest[0]), "utf8SafeCopy: CJK trunc byte 0");
    runner.expectEq(static_cast<unsigned char>(0xB8), static_cast<unsigned char>(dest[1]), "utf8SafeCopy: CJK trunc byte 1");
    runner.expectEq(static_cast<unsigned char>(0xAD), static_cast<unsigned char>(dest[2]), "utf8SafeCopy: CJK trunc byte 2");
    runner.expectEq(static_cast<char>(0), dest[3], "utf8SafeCopy: CJK trunc null at byte 3");
  }

  // Exact-fit CJK: "中" (3 bytes) into destCap=4 → perfect fit.
  {
    char dest[4] = {};
    const size_t n = utf8SafeCopy(dest, "\xE4\xB8\xAD", sizeof(dest));
    runner.expectEq(static_cast<size_t>(3), n, "utf8SafeCopy: CJK exact fit");
    runner.expectEq(static_cast<char>(0), dest[3], "utf8SafeCopy: CJK exact fit null-terminated");
  }

  // Mixed ASCII + CJK truncation: "AB中文" into destCap=5 → "AB" fits,
  // then "中" (3 bytes) would overflow (2+3=5 > maxBytes=4) so stop at "AB".
  {
    char dest[5] = {};
    const size_t n = utf8SafeCopy(dest, "AB\xE4\xB8\xAD\xE6\x96\x87", sizeof(dest));
    runner.expectEq(static_cast<size_t>(2), n, "utf8SafeCopy: mixed trunc stops before CJK");
    runner.expectTrue(std::strcmp(dest, "AB") == 0, "utf8SafeCopy: mixed trunc preserves ASCII prefix");
  }

  // Mixed ASCII + CJK where CJK fits: "AB中" into destCap=6 → full copy.
  {
    char dest[6] = {};
    const size_t n = utf8SafeCopy(dest, "AB\xE4\xB8\xAD", sizeof(dest));
    runner.expectEq(static_cast<size_t>(5), n, "utf8SafeCopy: mixed full fit returns 5 bytes");
    runner.expectEq(static_cast<char>(0), dest[5], "utf8SafeCopy: mixed full fit null-terminated");
  }

  // 4-byte codepoint (emoji): U+1F600 😀 = 0xF0 0x9F 0x98 0x80
  {
    char dest[8] = {};
    const size_t n = utf8SafeCopy(dest, "\xF0\x9F\x98\x80", sizeof(dest));
    runner.expectEq(static_cast<size_t>(4), n, "utf8SafeCopy: 4-byte emoji fits");
  }

  // 4-byte codepoint doesn't fit: destCap=4 means maxBytes=3, emoji needs 4.
  {
    char dest[4] = {};
    const size_t n = utf8SafeCopy(dest, "\xF0\x9F\x98\x80", sizeof(dest));
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: 4-byte emoji rejected when won't fit");
    runner.expectEq(static_cast<char>(0), dest[0], "utf8SafeCopy: empty result null-terminated");
  }

  // nullptr src safety — still null-terminates dest.
  {
    char dest[8] = {'x', 'y', 'z', 0, 0, 0, 0, 0};
    const size_t n = utf8SafeCopy(dest, nullptr, sizeof(dest));
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: nullptr src returns 0");
    runner.expectEq(static_cast<char>(0), dest[0], "utf8SafeCopy: nullptr src writes NUL");
  }

  // destCap=0 safety — writes nothing, returns 0. Using a single-byte dest
  // so that if it ever did write, AddressSanitizer would catch the overflow.
  {
    char dest[1] = {'Z'};
    const size_t n = utf8SafeCopy(dest, "hello", 0);
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: destCap=0 returns 0");
    runner.expectEq(static_cast<char>('Z'), dest[0], "utf8SafeCopy: destCap=0 leaves dest untouched");
  }

  // destCap=1 — only room for the terminator.
  {
    char dest[1] = {'Z'};
    const size_t n = utf8SafeCopy(dest, "hello", 1);
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: destCap=1 returns 0");
    runner.expectEq(static_cast<char>(0), dest[0], "utf8SafeCopy: destCap=1 writes NUL");
  }

  // Empty source string.
  {
    char dest[8] = {'x'};
    const size_t n = utf8SafeCopy(dest, "", sizeof(dest));
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: empty src returns 0");
    runner.expectEq(static_cast<char>(0), dest[0], "utf8SafeCopy: empty src writes NUL");
  }

  // Regression case matching the Flashcards CSV loader bug: a Japanese phrase
  // that would have been cut mid-sequence by strncpy(dest, src, MAX_TEXT-1).
  // "日本語テスト" is 6 CJK codepoints × 3 bytes = 18 bytes.
  // With destCap=10 (maxBytes=9), 3 full codepoints fit (9 bytes exactly).
  {
    const char* src = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88";
    char dest[10] = {};
    const size_t n = utf8SafeCopy(dest, src, sizeof(dest));
    runner.expectEq(static_cast<size_t>(9), n, "utf8SafeCopy: Flashcards regression — 3 CJK codepoints fit in 10-byte buffer");
    runner.expectEq(static_cast<char>(0), dest[9], "utf8SafeCopy: Flashcards regression — null-terminated at boundary");
    // Verify the last byte written is a valid codepoint trailer, not a lead byte.
    // dest[6..8] should be 0xE8 0xAA 0x9E (語), a complete sequence.
    runner.expectEq(static_cast<unsigned char>(0xE8), static_cast<unsigned char>(dest[6]), "utf8SafeCopy: Flashcards regression — codepoint 3 lead");
    runner.expectEq(static_cast<unsigned char>(0x9E), static_cast<unsigned char>(dest[8]), "utf8SafeCopy: Flashcards regression — codepoint 3 trailer (not a lead byte)");
  }

  return runner.allPassed() ? 0 : 1;
}
