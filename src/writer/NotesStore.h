#pragma once

// Note file listing/load/save, ported from MicroSlate
// (github.com/Josh-writes/microslate-firmware, src/file_manager.h/.cpp) —
// see NOTICE.md. Adapted from raw SdMan/SDCardManager calls to this
// codebase's HalStorage (Storage) wrapper.

#include "TextEditorBuffer.h"

constexpr int MAX_NOTES = 50;

struct NoteInfo {
  char filename[MAX_FILENAME_LEN];
  char title[MAX_TITLE_LEN];
};

void notesStoreSetup();  // Ensures /notes exists, loads the initial list
void refreshNoteList();
int getNoteCount();
NoteInfo* getNoteList();

void loadNote(const char* filename);
void saveCurrentNote(bool refreshList = true);
void createNewNote();
void deriveUniqueFilename(const char* title, char* out, int maxLen);
void updateNoteTitle(const char* filename, const char* newTitle);
void deleteNote(const char* filename);
