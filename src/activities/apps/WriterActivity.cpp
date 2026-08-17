#include "WriterActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "ble/BleKeyboard.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "writer/DeadKeys.h"
#include "writer/HidKeyCodes.h"
#include "writer/NotesStore.h"
#include "writer/TextEditorBuffer.h"

namespace {
constexpr int EDITOR_FONT_ID = NOTOSANS_14_FONT_ID;
constexpr unsigned long AUTO_SAVE_IDLE_MS = 10000;   // Save after 10s of no keystrokes
constexpr unsigned long AUTO_SAVE_MAX_MS = 120000;   // Hard cap: save every 2min during continuous typing
}  // namespace

void WriterActivity::onEnter() {
  Activity::onEnter();
  notesStoreSetup();
  mode = Mode::NotesList;
  selectedIndex = 0;
  requestUpdate();
}

void WriterActivity::onExit() {
  if (mode == Mode::Editing && editorHasUnsavedChanges()) {
    saveCurrentNote();
  }
  Activity::onExit();
}

void WriterActivity::computeEditorLayout() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(EDITOR_FONT_ID);
  textLeft = metrics.contentSidePadding;
  textTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int textAreaWidth = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  const int textAreaHeight =
      renderer.getScreenHeight() - textTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // charsPerLine is an estimate (avg char width) — editorRecalculateLines()
  // wraps on this many *characters*, not pixels, same tradeoff MicroSlate's
  // original renderer made. Good enough for a monospace-ish reading font;
  // revisit if proportional fonts make wrapping look ragged in practice.
  const int avgCharWidth = renderer.getTextWidth(EDITOR_FONT_ID, "n");
  charsPerLine = avgCharWidth > 0 ? std::max(10, textAreaWidth / avgCharWidth) : 40;
  visibleLines = lineHeight > 0 ? std::max(1, textAreaHeight / lineHeight) : 20;

  editorSetCharsPerLine(charsPerLine);
  editorSetVisibleLines(visibleLines);
}

void WriterActivity::enterEditor(int noteIndex) {
  computeEditorLayout();
  if (noteIndex < 0) {
    createNewNote();
  } else {
    NoteInfo* notes = getNoteList();
    loadNote(notes[noteIndex].filename);
  }
  mode = Mode::Editing;
  capsLockOn = false;
  deadKeyReset();
  lastKeystrokeMs = millis();
  lastAutoSaveMs = millis();
  requestUpdate();
}

void WriterActivity::exitEditorToList(bool saveFirst) {
  deadKeyReset();
  if (saveFirst && editorHasUnsavedChanges()) {
    // A brand-new, still-untitled note that was never typed into: don't
    // leave an empty "untitled.txt" behind.
    if (editorGetCurrentFile()[0] == '\0') {
      if (editorGetLength() > 0) {
        char filename[MAX_FILENAME_LEN];
        deriveUniqueFilename(editorGetCurrentTitle(), filename, MAX_FILENAME_LEN);
        editorSetCurrentFile(filename);
        saveCurrentNote();
      }
    } else {
      saveCurrentNote();
    }
  }
  refreshNoteList();
  mode = Mode::NotesList;
  if (selectedIndex >= getNoteCount()) selectedIndex = std::max(0, getNoteCount() - 1);
  requestUpdate();
}

void WriterActivity::checkAutoSave() {
  if (!editorHasUnsavedChanges() || editorGetCurrentFile()[0] == '\0') return;
  const unsigned long now = millis();
  if (now - lastKeystrokeMs >= AUTO_SAVE_IDLE_MS || now - lastAutoSaveMs >= AUTO_SAVE_MAX_MS) {
    saveCurrentNote(false);
    lastAutoSaveMs = now;
  }
}

void WriterActivity::insertPrintable(char c) {
  const char* composed = deadKeyProcess(c);
  if (composed == nullptr) {
    editorInsertChar(c);
  } else if (composed[0] != '\0') {
    editorInsertUtf8(composed);
    char requeued = deadKeyTakeRequeue();
    if (requeued != 0) editorInsertChar(requeued);
  }
  // else: composed[0] == '\0' -> dead key stored, nothing to insert yet
}

void WriterActivity::handleEditorHidKey(uint8_t keyCode, uint8_t modifiers) {
  lastKeystrokeMs = millis();

  if (hidIsCtrl(modifiers) && keyCode == HID_KEY_S) {
    if (editorGetCurrentFile()[0] == '\0' && editorGetLength() > 0) {
      char filename[MAX_FILENAME_LEN];
      deriveUniqueFilename(editorGetCurrentTitle(), filename, MAX_FILENAME_LEN);
      editorSetCurrentFile(filename);
    }
    saveCurrentNote();
    requestUpdate();
    return;
  }

  if (keyCode == HID_KEY_ESCAPE) {
    exitEditorToList(true);
    return;
  }

  switch (keyCode) {
    case HID_KEY_LEFT: editorMoveCursorLeft(); requestUpdate(); return;
    case HID_KEY_RIGHT: editorMoveCursorRight(); requestUpdate(); return;
    case HID_KEY_UP: editorMoveCursorUp(); requestUpdate(); return;
    case HID_KEY_DOWN: editorMoveCursorDown(); requestUpdate(); return;
    case HID_KEY_HOME: editorMoveCursorHome(); requestUpdate(); return;
    case HID_KEY_END: editorMoveCursorEnd(); requestUpdate(); return;
    case HID_KEY_BACKSPACE:
      deadKeyReset();
      editorDeleteChar();
      requestUpdate();
      return;
    case HID_KEY_DELETE: editorDeleteForward(); requestUpdate(); return;
    case HID_KEY_CAPSLOCK: capsLockOn = !capsLockOn; return;
    default: break;
  }

  char c = hidToAscii(keyCode, modifiers, capsLockOn);
  if (c != 0) {
    insertPrintable(c);
    requestUpdate();
  }
}

void WriterActivity::handleListNav(bool up) {
  const int count = getNoteCount();
  if (count == 0) return;
  selectedIndex = up ? (selectedIndex - 1 + count) % count : (selectedIndex + 1) % count;
  requestUpdate();
}

void WriterActivity::handleListConfirm() {
  if (getNoteCount() == 0) return;
  enterEditor(selectedIndex);
}

void WriterActivity::loop() {
  if (mode == Mode::NotesList) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      handleListNav(true);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      handleListNav(false);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      handleListConfirm();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      enterEditor(-1);  // new note
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
  } else {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      exitEditorToList(true);
    }
  }

  // popBleKeyEvent() is cheap to poll even when nothing is connected — the
  // queue is just empty. Polled unconditionally so a keyboard that connects
  // mid-session works immediately without extra wiring here.
  BleKeyEvent evt;
  while (popBleKeyEvent(evt)) {
    if (!evt.pressed) continue;  // key-up not needed for text entry
    if (mode == Mode::NotesList) {
      if (evt.keyCode == HID_KEY_UP) {
        handleListNav(true);
      } else if (evt.keyCode == HID_KEY_DOWN) {
        handleListNav(false);
      } else if (evt.keyCode == HID_KEY_ENTER) {
        handleListConfirm();
      } else if (hidIsCtrl(evt.modifiers) && evt.keyCode == HID_KEY_N) {
        enterEditor(-1);
      } else if (evt.keyCode == HID_KEY_ESCAPE) {
        finish();
        return;
      }
    } else {
      handleEditorHidKey(evt.keyCode, evt.modifiers);
    }
  }

  if (mode == Mode::Editing) {
    checkAutoSave();
  }
}

void WriterActivity::render(RenderLock&& lock) {
  if (mode == Mode::NotesList) {
    renderNotesList();
  } else {
    renderEditor();
  }
}

void WriterActivity::renderNotesList() {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_WRITE));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const int count = getNoteCount();
  if (count == 0) {
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_WRITE_NO_NOTES));
  } else {
    NoteInfo* notes = getNoteList();
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, count, selectedIndex,
                [notes](int index) { return std::string(notes[index].title); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", tr(STR_WRITE_NEW_NOTE));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void WriterActivity::renderEditor() {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const bool unsaved = editorHasUnsavedChanges();
  std::string title = editorGetCurrentTitle();
  if (unsaved) title += " *";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const char* buf = editorGetBuffer();
  const int viewportStart = editorGetViewportStart();
  const int lineCount = editorGetLineCount();
  const int cursorLine = editorGetCursorLine();
  const int cursorCol = editorGetCursorCol();

  int y = textTop;
  for (int i = viewportStart; i < lineCount && i < viewportStart + visibleLines; i++) {
    const int start = editorGetLinePosition(i);
    int end = (i + 1 < lineCount) ? editorGetLinePosition(i + 1) : (int)editorGetLength();
    if (end > start && buf[end - 1] == '\n') end--;
    const int len = end - start;

    if (len > 0) {
      char lineBuf[256];
      const int copyLen = std::min(len, (int)sizeof(lineBuf) - 1);
      memcpy(lineBuf, buf + start, copyLen);
      lineBuf[copyLen] = '\0';
      renderer.drawText(EDITOR_FONT_ID, textLeft, y, lineBuf);
    }

    // Cursor: a thin vertical bar at the cursor's position within its line.
    if (i == cursorLine) {
      char prefix[256];
      const int prefixLen = std::min(cursorCol, (int)sizeof(prefix) - 1);
      memcpy(prefix, buf + start, prefixLen);
      prefix[prefixLen] = '\0';
      const int cursorX = textLeft + renderer.getTextWidth(EDITOR_FONT_ID, prefix);
      renderer.fillRect(cursorX, y, 2, lineHeight);
    }

    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
