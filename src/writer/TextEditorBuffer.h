#pragma once

// Text buffer + cursor + word-wrap engine, ported from MicroSlate
// (github.com/Josh-writes/microslate-firmware, src/text_editor.h/.cpp) —
// see NOTICE.md. Pure logic, no hardware dependency, ported near-verbatim.
// Module-level state, not a class: only one note is ever being edited at a
// time (same assumption the original makes), and WriterActivity instances
// come and go with the Activity stack.

#include <cstddef>

constexpr size_t TEXT_BUFFER_SIZE = 16384;
constexpr int MAX_LINES = 1024;
constexpr int MAX_TITLE_LEN = 40;
constexpr int MAX_FILENAME_LEN = 64;

// Allocates the text buffer + line-position table on the heap (~20KB total).
// Returns false on OOM — caller must not use the editor and should bail out.
bool editorInit();
// Frees the buffer allocated by editorInit(). Safe to call even if editorInit()
// was never called or already failed.
void editorShutdown();
void editorClear();
void editorLoadBuffer(size_t length);  // After filling buffer externally, set length + reset cursor

// Buffer access
char* editorGetBuffer();
size_t editorGetLength();
int editorGetCursorPosition();

// Editing operations
void editorInsertChar(char c);
void editorInsertUtf8(const char* utf8str);  // Insert a UTF-8 string byte by byte
void editorDeleteChar();     // Backspace
void editorDeleteForward();  // Delete key

// Cursor movement
void editorMoveCursorLeft();
void editorMoveCursorRight();
void editorMoveCursorUp();
void editorMoveCursorDown();
void editorMoveCursorHome();
void editorMoveCursorEnd();

// Line/viewport management
void editorSetCharsPerLine(int cpl);
void editorSetVisibleLines(int n);  // Tell editor how many lines are visible on screen
int editorGetStoredVisibleLines();  // Get the last set visible lines count
void editorRecalculateLines();
int editorGetViewportStart();
int editorGetCursorLine();
int editorGetCursorCol();
int editorGetLineCount();
int editorGetLinePosition(int lineIndex);

// File metadata
void editorSetCurrentFile(const char* filename);
void editorSetCurrentTitle(const char* title);
const char* editorGetCurrentFile();
const char* editorGetCurrentTitle();
bool editorHasUnsavedChanges();
void editorSetUnsavedChanges(bool v);
int editorGetWordCount();
