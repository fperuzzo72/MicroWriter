#pragma once

#include "config.h"

void fileManagerSetup();
void refreshFileList();
int getFileCount();
FileInfo* getFileList();

void loadFile(const char* filename);
void saveCurrentFile(bool refreshList = true);
void createNewFile();
// `except` names a file that doesn't count as a collision -- the one being
// renamed, so confirming a rename unchanged doesn't bump it to _2.
void deriveUniqueFilename(const char* title, char* out, int maxLen, const char* except = nullptr);
void updateFileTitle(const char* filename, const char* newTitle);
void deleteFile(const char* filename);
