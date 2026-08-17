#include "TextEditorBuffer.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <new>

// --- Text buffer ---
// Heap-allocated on editorInit()/freed on editorShutdown() rather than static:
// this ~20KB was previously resident for the whole firmware lifetime even when
// the Writer was never opened, eating into the shared heap other features
// (e.g. the JPEG cover-thumbnail decoder) budget against.
static char* textBuffer = nullptr;
static size_t textLength = 0;
static int cursorPosition = 0;

// --- File metadata ---
static char currentFile[MAX_FILENAME_LEN] = "";
static char currentTitle[MAX_TITLE_LEN] = "Untitled";
static bool unsavedChanges = false;

// --- Line management ---
static int* linePositions = nullptr;  // Index into textBuffer for start of each line
static int lineCount = 0;
static int cursorLine = 0;
static int cursorCol = 0;
static int viewportStartLine = 0;
static int charsPerLine = 40;
static int storedVisibleLines = 20;  // Updated by renderer each frame
static bool lineBreaksDirty = true;  // Only recompute line breaks when buffer/charsPerLine changes

static void ensureCursorVisible(int visibleLines);

// Recalculate line breaks (word wrap) and cursor position.
// The O(textLength) line break loop only runs when the buffer or charsPerLine changed.
// Cursor line/col is always recomputed (cheap O(cursorLine) with early exit).
void editorRecalculateLines() {
  if (!textBuffer || !linePositions) return;
  if (lineBreaksDirty) {
    lineCount = 0;
    linePositions[0] = 0;
    lineCount = 1;

    int col = 0;
    int lastSpace = -1;

    for (int i = 0; i < (int)textLength && lineCount < MAX_LINES; i++) {
      if (textBuffer[i] == '\n') {
        // Hard line break
        if (lineCount < MAX_LINES) {
          linePositions[lineCount] = i + 1;
          lineCount++;
        }
        col = 0;
        lastSpace = -1;
        continue;
      }

      if (textBuffer[i] == ' ') {
        lastSpace = i;
      }

      col++;
      if (col >= charsPerLine) {
        // Word wrap
        int breakPos;
        if (lastSpace > linePositions[lineCount - 1]) {
          breakPos = lastSpace + 1;  // Break after space
        } else {
          breakPos = i + 1;  // Hard break mid-word
        }

        if (lineCount < MAX_LINES) {
          linePositions[lineCount] = breakPos;
          lineCount++;
        }
        col = i + 1 - breakPos;
        lastSpace = -1;
      }
    }
    lineBreaksDirty = false;
  }

  // Compute cursor line and column (always — cheap O(cursorLine) with early exit)
  cursorLine = 0;
  for (int i = 1; i < lineCount; i++) {
    if (cursorPosition >= linePositions[i]) {
      cursorLine = i;
    } else {
      break;
    }
  }
  cursorCol = cursorPosition - linePositions[cursorLine];
}

// Ensure cursor is visible by adjusting viewport
static void ensureCursorVisible(int visibleLines) {
  if (visibleLines <= 0) visibleLines = 20;  // fallback

  if (cursorLine < viewportStartLine) {
    viewportStartLine = cursorLine;
  } else if (cursorLine >= viewportStartLine + visibleLines) {
    viewportStartLine = cursorLine - visibleLines + 1;
  }

  if (viewportStartLine < 0) viewportStartLine = 0;
  if (viewportStartLine >= lineCount) viewportStartLine = std::max(0, lineCount - 1);
}

bool editorInit() {
  if (!textBuffer) textBuffer = new (std::nothrow) char[TEXT_BUFFER_SIZE];
  if (!linePositions) linePositions = new (std::nothrow) int[MAX_LINES];
  if (!textBuffer || !linePositions) {
    LOG_ERR("WRT", "OOM: text editor buffer (%u bytes)",
            (unsigned)(TEXT_BUFFER_SIZE + MAX_LINES * sizeof(int)));
    editorShutdown();
    return false;
  }

  memset(textBuffer, 0, TEXT_BUFFER_SIZE);
  textLength = 0;
  cursorPosition = 0;
  currentFile[0] = '\0';
  strncpy(currentTitle, "Untitled", MAX_TITLE_LEN - 1);
  unsavedChanges = false;
  viewportStartLine = 0;
  lineBreaksDirty = true;
  editorRecalculateLines();
  return true;
}

void editorShutdown() {
  delete[] textBuffer;
  delete[] linePositions;
  textBuffer = nullptr;
  linePositions = nullptr;
  textLength = 0;
  cursorPosition = 0;
  lineCount = 0;
  lineBreaksDirty = true;
}

void editorClear() {
  if (!textBuffer) return;
  memset(textBuffer, 0, TEXT_BUFFER_SIZE);
  textLength = 0;
  cursorPosition = 0;
  unsavedChanges = false;
  viewportStartLine = 0;
  lineBreaksDirty = true;
  editorRecalculateLines();
}

void editorLoadBuffer(size_t length) {
  if (!textBuffer) return;
  textLength = length;
  textBuffer[textLength] = '\0';
  cursorPosition = (int)textLength;  // Start at end
  viewportStartLine = 0;
  lineBreaksDirty = true;
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

char* editorGetBuffer() { return textBuffer; }
size_t editorGetLength() { return textLength; }
int editorGetCursorPosition() { return cursorPosition; }

int editorGetWordCount() {
  if (!textBuffer) return 0;
  int count = 0;
  bool inWord = false;
  for (size_t i = 0; i < textLength; i++) {
    char c = textBuffer[i];
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
      inWord = false;
    } else {
      if (!inWord) {
        count++;
        inWord = true;
      }
    }
  }
  return count;
}

void editorInsertChar(char c) {
  if (!textBuffer || !linePositions) return;
  if (textLength >= TEXT_BUFFER_SIZE - 1) return;

  for (int i = (int)textLength; i > cursorPosition; i--) {
    textBuffer[i] = textBuffer[i - 1];
  }
  textBuffer[cursorPosition] = c;
  cursorPosition++;
  textLength++;
  textBuffer[textLength] = '\0';
  unsavedChanges = true;
  lineBreaksDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

// Insert a UTF-8 encoded string (one or more bytes) at the cursor position.
void editorInsertUtf8(const char* utf8str) {
  if (!utf8str) return;
  for (const char* p = utf8str; *p != '\0'; p++) {
    editorInsertChar(*p);
  }
}

// Returns the number of bytes of the codepoint that *ends* at position `pos-1`.
static int utf8BackLen(int pos) {
  if (pos <= 0) return 0;
  int len = 1;
  while (len < 4 && (pos - len) > 0 && (static_cast<unsigned char>(textBuffer[pos - len]) & 0xC0) == 0x80) {
    len++;
  }
  return len;
}

// Returns the number of bytes of the codepoint that *starts* at position `pos`.
static int utf8FwdLen(int pos) {
  if (pos >= (int)textLength) return 0;
  unsigned char c = static_cast<unsigned char>(textBuffer[pos]);
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xE) return 3;
  if ((c >> 3) == 0x1E) return 4;
  return 1;  // fallback for stray continuation byte
}

void editorDeleteChar() {
  if (!textBuffer || !linePositions) return;
  if (cursorPosition <= 0 || textLength == 0) return;

  int len = utf8BackLen(cursorPosition);
  int newPos = cursorPosition - len;

  for (int i = newPos; i < (int)textLength - len; i++) {
    textBuffer[i] = textBuffer[i + len];
  }
  cursorPosition = newPos;
  textLength -= len;
  textBuffer[textLength] = '\0';
  unsavedChanges = true;
  lineBreaksDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorDeleteForward() {
  if (!textBuffer || !linePositions) return;
  if (cursorPosition >= (int)textLength) return;

  int len = utf8FwdLen(cursorPosition);

  for (int i = cursorPosition; i < (int)textLength - len; i++) {
    textBuffer[i] = textBuffer[i + len];
  }
  textLength -= len;
  textBuffer[textLength] = '\0';
  unsavedChanges = true;
  lineBreaksDirty = true;

  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorLeft() {
  if (!textBuffer || !linePositions) return;
  if (cursorPosition > 0) {
    int len = utf8BackLen(cursorPosition);
    cursorPosition -= len;
    editorRecalculateLines();
    ensureCursorVisible(storedVisibleLines);
  }
}

void editorMoveCursorRight() {
  if (!textBuffer || !linePositions) return;
  if (cursorPosition < (int)textLength) {
    int len = utf8FwdLen(cursorPosition);
    cursorPosition += len;
    editorRecalculateLines();
    ensureCursorVisible(storedVisibleLines);
  }
}

void editorMoveCursorUp() {
  if (!textBuffer || !linePositions) return;
  if (cursorLine <= 0) return;

  int targetLine = cursorLine - 1;
  int lineStart = linePositions[targetLine];
  int lineEnd = (targetLine + 1 < lineCount) ? linePositions[targetLine + 1] : (int)textLength;
  int lineLen = lineEnd - lineStart;
  if (lineLen > 0 && textBuffer[lineStart + lineLen - 1] == '\n') lineLen--;

  cursorPosition = lineStart + std::min(cursorCol, lineLen);
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorDown() {
  if (!textBuffer || !linePositions) return;
  if (cursorLine >= lineCount - 1) return;

  int targetLine = cursorLine + 1;
  int lineStart = linePositions[targetLine];
  int lineEnd = (targetLine + 1 < lineCount) ? linePositions[targetLine + 1] : (int)textLength;
  int lineLen = lineEnd - lineStart;
  if (lineLen > 0 && textBuffer[lineStart + lineLen - 1] == '\n') lineLen--;

  cursorPosition = lineStart + std::min(cursorCol, lineLen);
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorHome() {
  if (!textBuffer || !linePositions) return;
  cursorPosition = linePositions[cursorLine];
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorMoveCursorEnd() {
  if (!textBuffer || !linePositions) return;
  int lineEnd;
  if (cursorLine + 1 < lineCount) {
    lineEnd = linePositions[cursorLine + 1];
    if (lineEnd > 0 && textBuffer[lineEnd - 1] == '\n') lineEnd--;
  } else {
    lineEnd = (int)textLength;
  }
  cursorPosition = lineEnd;
  editorRecalculateLines();
  ensureCursorVisible(storedVisibleLines);
}

void editorSetCharsPerLine(int cpl) {
  if (cpl != charsPerLine) {
    charsPerLine = cpl;
    lineBreaksDirty = true;
  }
  editorRecalculateLines();
}

void editorSetVisibleLines(int n) {
  if (n > 0) storedVisibleLines = n;
}

int editorGetStoredVisibleLines() { return storedVisibleLines; }

int editorGetViewportStart() { return viewportStartLine; }
int editorGetCursorLine() { return cursorLine; }
int editorGetCursorCol() { return cursorCol; }
int editorGetLineCount() { return lineCount; }

int editorGetLinePosition(int lineIndex) {
  if (lineIndex < 0 || lineIndex >= lineCount) return 0;
  return linePositions[lineIndex];
}

void editorSetCurrentFile(const char* filename) {
  strncpy(currentFile, filename, MAX_FILENAME_LEN - 1);
  currentFile[MAX_FILENAME_LEN - 1] = '\0';
}

void editorSetCurrentTitle(const char* title) {
  strncpy(currentTitle, title, MAX_TITLE_LEN - 1);
  currentTitle[MAX_TITLE_LEN - 1] = '\0';
}

const char* editorGetCurrentFile() { return currentFile; }
const char* editorGetCurrentTitle() { return currentTitle; }
bool editorHasUnsavedChanges() { return unsavedChanges; }
void editorSetUnsavedChanges(bool v) { unsavedChanges = v; }
