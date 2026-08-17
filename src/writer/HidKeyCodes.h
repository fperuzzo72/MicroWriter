#pragma once

// USB HID keyboard usage codes + modifier masks, ported from MicroSlate
// (github.com/Josh-writes/microslate-firmware, src/config.h) — see NOTICE.md.

#include <cstdint>

static constexpr uint8_t HID_KEY_D = 0x07;
static constexpr uint8_t HID_KEY_N = 0x11;
static constexpr uint8_t HID_KEY_S = 0x16;
static constexpr uint8_t HID_KEY_ENTER = 0x28;
static constexpr uint8_t HID_KEY_ESCAPE = 0x29;
static constexpr uint8_t HID_KEY_BACKSPACE = 0x2A;
static constexpr uint8_t HID_KEY_TAB = 0x2B;
static constexpr uint8_t HID_KEY_SPACE = 0x2C;
static constexpr uint8_t HID_KEY_DELETE = 0x4C;
static constexpr uint8_t HID_KEY_RIGHT = 0x4F;
static constexpr uint8_t HID_KEY_LEFT = 0x50;
static constexpr uint8_t HID_KEY_DOWN = 0x51;
static constexpr uint8_t HID_KEY_UP = 0x52;
static constexpr uint8_t HID_KEY_HOME = 0x4A;
static constexpr uint8_t HID_KEY_END = 0x4D;
static constexpr uint8_t HID_KEY_CAPSLOCK = 0x39;

static constexpr uint8_t MOD_CTRL_LEFT = 0x01;
static constexpr uint8_t MOD_SHIFT_LEFT = 0x02;
static constexpr uint8_t MOD_ALT_LEFT = 0x04;
static constexpr uint8_t MOD_CTRL_RIGHT = 0x10;
static constexpr uint8_t MOD_SHIFT_RIGHT = 0x20;
static constexpr uint8_t MOD_ALT_RIGHT = 0x40;

inline bool hidIsCtrl(uint8_t mod) { return (mod & MOD_CTRL_LEFT) || (mod & MOD_CTRL_RIGHT); }
inline bool hidIsShift(uint8_t mod) { return (mod & MOD_SHIFT_LEFT) || (mod & MOD_SHIFT_RIGHT); }

// Decode a HID keycode + modifiers + capsLock state into an ASCII character,
// or 0 if the code isn't printable (arrows, function keys, etc. — those are
// handled separately as navigation, not text).
inline char hidToAscii(uint8_t hid, uint8_t modifiers, bool capsLockOn) {
  bool shifted = hidIsShift(modifiers) ^ capsLockOn;

  if (hid >= 0x04 && hid <= 0x1D) {  // a-z
    char base = 'a' + (hid - 0x04);
    return shifted ? (char)(base - 32) : base;
  }

  if (hid >= 0x1E && hid <= 0x27) {  // number row
    static const char unshifted[] = "1234567890";
    static const char shiftedNum[] = "!@#$%^&*()";
    int idx = hid - 0x1E;
    return hidIsShift(modifiers) ? shiftedNum[idx] : unshifted[idx];
  }

  switch (hid) {
    case HID_KEY_ENTER: return '\n';
    case HID_KEY_TAB: return '\t';
    case HID_KEY_SPACE: return ' ';
    case 0x2D: return hidIsShift(modifiers) ? '_' : '-';
    case 0x2E: return hidIsShift(modifiers) ? '+' : '=';
    case 0x2F: return hidIsShift(modifiers) ? '{' : '[';
    case 0x30: return hidIsShift(modifiers) ? '}' : ']';
    case 0x31: return hidIsShift(modifiers) ? '|' : '\\';
    case 0x33: return hidIsShift(modifiers) ? ':' : ';';
    case 0x34: return hidIsShift(modifiers) ? '"' : '\'';
    case 0x35: return hidIsShift(modifiers) ? '~' : '`';
    case 0x36: return hidIsShift(modifiers) ? '<' : ',';
    case 0x37: return hidIsShift(modifiers) ? '>' : '.';
    case 0x38: return hidIsShift(modifiers) ? '?' : '/';
    default: return 0;
  }
}
