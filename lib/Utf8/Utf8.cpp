#include "Utf8.h"

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
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

size_t utf8RemoveLastChar(std::string& str) {
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

void utf8TruncateChars(std::string& str, size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

size_t utf8SafeCopy(char* dest, const char* src, size_t destCap) {
  if (dest == nullptr || destCap == 0) return 0;
  if (src == nullptr) {
    dest[0] = '\0';
    return 0;
  }

  // Walk up to destCap-1 bytes, but only accept complete UTF-8 sequences.
  const size_t maxBytes = destCap - 1;
  size_t pos = 0;
  while (src[pos] != '\0') {
    const auto lead = static_cast<unsigned char>(src[pos]);
    // How many bytes does THIS codepoint occupy?
    size_t codepointLen;
    if (lead < 0x80)          codepointLen = 1;
    else if ((lead >> 5) == 0x6) codepointLen = 2;
    else if ((lead >> 4) == 0xE) codepointLen = 3;
    else if ((lead >> 3) == 0x1E) codepointLen = 4;
    else {
      // Stray continuation or invalid lead byte. Treat it as a single
      // byte so we make progress and don't infinite-loop on bad input.
      codepointLen = 1;
    }
    if (pos + codepointLen > maxBytes) break;
    for (size_t i = 0; i < codepointLen; ++i) {
      // Bail out cleanly if the source ends inside a multi-byte sequence.
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

int utf8UnicodeWhitespaceBytes(const char* s, int remaining) {
  if (s == nullptr || remaining < 2) return 0;
  const auto b0 = static_cast<uint8_t>(s[0]);
  const auto b1 = static_cast<uint8_t>(s[1]);
  // U+00A0 NO-BREAK SPACE  => 0xC2 0xA0
  if (b0 == 0xC2 && b1 == 0xA0) return 2;
  if (remaining < 3) return 0;
  const auto b2 = static_cast<uint8_t>(s[2]);
  // U+2000..U+200A (EN QUAD .. HAIR SPACE)  => 0xE2 0x80 0x80..0x8A
  if (b0 == 0xE2 && b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) return 3;
  // U+202F NARROW NO-BREAK SPACE            => 0xE2 0x80 0xAF
  if (b0 == 0xE2 && b1 == 0x80 && b2 == 0xAF) return 3;
  // U+205F MEDIUM MATHEMATICAL SPACE        => 0xE2 0x81 0x9F
  if (b0 == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3;
  // U+3000 IDEOGRAPHIC SPACE                => 0xE3 0x80 0x80
  if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3;
  return 0;
}
