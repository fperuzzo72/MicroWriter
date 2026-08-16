#pragma once

#include <cstdint>
#include <string>

uint32_t utf8NextCodepoint(const unsigned char** string);

/**
 * UTF-8 safe string truncation - removes one character from the end.
 * Returns the new size after removing one UTF-8 character.
 */
size_t utf8RemoveLastChar(std::string& str);

/**
 * UTF-8 safe truncation - removes N characters from the end.
 */
void utf8TruncateChars(std::string& str, size_t numChars);

/**
 * UTF-8 safe copy: copy up to `destCap - 1` bytes from `src` to `dest`,
 * guaranteeing that the destination is null-terminated and that the
 * last byte written is not inside a partial UTF-8 multi-byte sequence.
 *
 * Returns the number of bytes written (excluding the terminator).
 *
 * Background: callers like the flashcards CSV loader did
 *   strncpy(card.front, src, MAX_TEXT - 1);
 * which happily cut a 3-byte CJK lead + continuation sequence in the
 * middle, leaving a corrupt "half character" at the end. The downstream
 * utf8NextCodepoint() then reads a lead byte followed by a null and
 * produces a garbage codepoint that falls back to '?'. Use this helper
 * instead to walk back to the last complete codepoint boundary.
 *
 * If `src` is nullptr or `destCap == 0`, does nothing (returns 0) and
 * — when possible — writes a null terminator at dest[0].
 */
size_t utf8SafeCopy(char* dest, const char* src, size_t destCap);

/**
 * If `s` begins with a multi-byte UTF-8 encoding of a Unicode whitespace
 * codepoint (beyond ASCII ' ', '\r', '\n', '\t'), return the number of
 * bytes that make up the sequence. Otherwise return 0.
 *
 * Recognised codepoints:
 *   U+00A0                 NO-BREAK SPACE                 (2 bytes)
 *   U+2000..U+200A         EN/EM/THIN/HAIR spaces         (3 bytes)
 *   U+202F                 NARROW NO-BREAK SPACE          (3 bytes)
 *   U+205F                 MEDIUM MATHEMATICAL SPACE      (3 bytes)
 *   U+3000                 IDEOGRAPHIC SPACE              (3 bytes)
 *
 * These act as word boundaries in natural-language tokenisation. French and
 * Spanish typography commonly inserts U+00A0 / U+202F between words, and
 * the EPUB parser needs to split on them to avoid emitting single atomic
 * "words" wider than the viewport.
 *
 * @param s          Start of byte stream (typically raw UTF-8 text).
 * @param remaining  Number of bytes remaining from `s` to end-of-buffer.
 *                   Used to avoid reading past the end when the sequence
 *                   would need more bytes than are available.
 */
int utf8UnicodeWhitespaceBytes(const char* s, int remaining);
