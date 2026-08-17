#include "NotesStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

static constexpr char NOTES_DIR[] = "/notes";

// --- Note list ---
static NoteInfo noteList[MAX_NOTES];
static int noteCount = 0;

// Convert filename to a readable display title. "my_note_2.txt" -> "My Note 2"
static void filenameToTitle(const char* filename, char* out, int maxLen) {
  int j = 0;
  bool capitalizeNext = true;
  for (int i = 0; filename[i] != '\0' && filename[i] != '.' && j < maxLen - 1; i++) {
    char c = filename[i];
    if (c == '_') {
      if (j > 0) out[j++] = ' ';
      capitalizeNext = true;
    } else {
      if (capitalizeNext && c >= 'a' && c <= 'z') c -= 32;
      capitalizeNext = false;
      out[j++] = c;
    }
  }
  out[j] = '\0';
  if (j == 0) strncpy(out, "Untitled", maxLen - 1);
}

// Convert a title to a valid FAT filename (lowercase, spaces->underscores,
// non-alphanumeric stripped, ".txt" appended).
static void titleToFilename(const char* title, char* out, int maxLen) {
  int maxBase = maxLen - 5;  // room for ".txt" + null
  int j = 0;
  for (int i = 0; title[i] != '\0' && j < maxBase; i++) {
    char c = title[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out[j++] = c;
    } else if (c == ' ' || c == '_' || c == '-') {
      if (j > 0 && out[j - 1] != '_') out[j++] = '_';
    }
  }
  while (j > 0 && out[j - 1] == '_') j--;
  if (j == 0) {
    strncpy(out, "note", maxLen - 1);
    j = 4;
  }
  strcpy(out + j, ".txt");
}

// Derive a unique /notes/ filename from a title, handling collisions with _2, _3 suffix.
void deriveUniqueFilename(const char* title, char* out, int maxLen) {
  titleToFilename(title, out, maxLen);

  char path[320];
  snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, out);
  if (!Storage.exists(path)) return;

  char base[MAX_FILENAME_LEN];
  strncpy(base, out, maxLen - 1);
  base[strlen(base) - 4] = '\0';

  int suffix = 2;
  while (Storage.exists(path) && suffix <= 99) {
    snprintf(out, maxLen, "%s_%d.txt", base, suffix++);
    snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, out);
  }
}

void notesStoreSetup() {
  Storage.ensureDirectoryExists(NOTES_DIR);
  refreshNoteList();
}

void refreshNoteList() {
  noteCount = 0;
  auto names = Storage.listFiles(NOTES_DIR, MAX_NOTES);
  for (const auto& name : names) {
    const int nameLen = name.length();
    if (nameLen > 4 && strcasecmp(name.c_str() + nameLen - 4, ".txt") == 0 && noteCount < MAX_NOTES) {
      strncpy(noteList[noteCount].filename, name.c_str(), MAX_FILENAME_LEN - 1);
      noteList[noteCount].filename[MAX_FILENAME_LEN - 1] = '\0';
      filenameToTitle(name.c_str(), noteList[noteCount].title, MAX_TITLE_LEN);
      noteCount++;
    }
  }
  LOG_DBG("WRITER", "Note listing: %d note(s) found", noteCount);
}

int getNoteCount() { return noteCount; }
NoteInfo* getNoteList() { return noteList; }

void loadNote(const char* filename) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, filename);

  HalFile file;
  if (!Storage.openFileForRead("WRITER", path, file)) {
    LOG_ERR("WRITER", "Could not open: %s", path);
    return;
  }

  char* buf = editorGetBuffer();
  int readResult = file.read(buf, TEXT_BUFFER_SIZE - 1);
  size_t bytesRead = (readResult > 0) ? (size_t)readResult : 0;
  buf[bytesRead] = '\0';
  file.close();

  editorSetCurrentFile(filename);
  editorLoadBuffer(bytesRead);

  // Title comes from the filename, not the file content
  char title[MAX_TITLE_LEN];
  filenameToTitle(filename, title, MAX_TITLE_LEN);
  editorSetCurrentTitle(title);
  editorSetUnsavedChanges(false);

  LOG_DBG("WRITER", "Loaded: %s (%d bytes)", filename, (int)bytesRead);
}

void saveCurrentNote(bool refreshList) {
  const char* filename = editorGetCurrentFile();
  if (filename[0] == '\0') return;

  char path[320], tmpPath[336], bakPath[336];
  snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, filename);
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);

  // Step 1: Write new content to .tmp
  HalFile file = Storage.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file.isOpen()) {
    LOG_ERR("WRITER", "saveCurrentNote: could not create tmp: %s", tmpPath);
    return;
  }

  size_t toWrite = editorGetLength();
  size_t written = file.write(editorGetBuffer(), toWrite);
  file.close();

  // Step 2: Verify bytes written match expected length
  if (written != toWrite) {
    LOG_ERR("WRITER", "saveCurrentNote: write mismatch (%d/%d) - aborting", (int)written, (int)toWrite);
    Storage.remove(tmpPath);
    return;
  }

  // Step 3: Rotate original -> .bak (original is now safe in .tmp, preserve previous .bak)
  if (Storage.exists(path)) {
    Storage.remove(bakPath);  // Remove old .bak (if any)
    Storage.rename(path, bakPath);
  }

  // Step 4: Promote .tmp -> original
  Storage.rename(tmpPath, path);

  editorSetUnsavedChanges(false);
  if (refreshList) refreshNoteList();
  LOG_DBG("WRITER", "Saved: %s", filename);
}

void createNewNote() {
  editorClear();
  editorSetCurrentFile("");  // filename derived from title when user confirms
  editorSetCurrentTitle("Untitled");
  editorSetUnsavedChanges(true);
}

// Rename a note on disk to match a new title, updating editor state if needed.
void updateNoteTitle(const char* filename, const char* newTitle) {
  char newFilename[MAX_FILENAME_LEN];
  deriveUniqueFilename(newTitle, newFilename, MAX_FILENAME_LEN);

  if (strcmp(newFilename, filename) != 0) {
    char oldPath[320], newPath[320];
    snprintf(oldPath, sizeof(oldPath), "%s/%s", NOTES_DIR, filename);
    snprintf(newPath, sizeof(newPath), "%s/%s", NOTES_DIR, newFilename);
    Storage.rename(oldPath, newPath);

    if (strcmp(editorGetCurrentFile(), filename) == 0) {
      editorSetCurrentFile(newFilename);
    }
  }

  refreshNoteList();
}

void deleteNote(const char* filename) {
  char path[320], bakPath[336];
  snprintf(path, sizeof(path), "%s/%s", NOTES_DIR, filename);
  snprintf(bakPath, sizeof(bakPath), "%s.bak", path);
  Storage.remove(path);
  Storage.remove(bakPath);
  refreshNoteList();
  LOG_DBG("WRITER", "Deleted: %s", filename);
}
