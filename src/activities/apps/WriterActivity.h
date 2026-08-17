#pragma once

#include <cstdint>

#include "../Activity.h"

// Note list + text editor (scroll mode only for now — typewriter and
// pagination modes from MicroSlate are not ported yet). Reachable as the
// "Write" shortcut. See NOTICE.md for what this is ported from.
class WriterActivity final : public Activity {
 public:
  explicit WriterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Writer", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode { NotesList, Editing };
  Mode mode = Mode::NotesList;

  int selectedIndex = 0;
  bool capsLockOn = false;
  unsigned long lastKeystrokeMs = 0;
  unsigned long lastAutoSaveMs = 0;

  // Cached editor layout, recomputed in onEnter()/enterEditor() — screen
  // size doesn't change mid-session so this doesn't need to be per-frame.
  int charsPerLine = 40;
  int visibleLines = 20;
  int lineHeight = 18;
  int textLeft = 0;
  int textTop = 0;

  void enterEditor(int noteIndex);  // -1 = create a new note
  void exitEditorToList(bool saveFirst);
  void computeEditorLayout();

  void handleListNav(bool up);
  void handleListConfirm();
  void handleEditorHidKey(uint8_t keyCode, uint8_t modifiers);
  void insertPrintable(char c);
  void checkAutoSave();

  void renderNotesList();
  void renderEditor();
};
