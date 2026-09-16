#ifndef TINTA_FILE_UTILS_H
#define TINTA_FILE_UTILS_H

#include "app.h"
#include <string>

#define TIMER_FILE_WATCH 1

void updateFileWriteTime(App& app);
bool isRootPath(const std::wstring& path);
std::wstring getParentPath(const std::wstring& path);
std::wstring getDirectoryFromFile(const std::string& filePath);
void populateFolderItems(App& app);

// Read a markdown file and put the parse result in App. Returns false and leaves App
// untouched if the file cannot be opened or does not parse -- so a caller that ignores
// the result gets the start page rather than a half-loaded document.
//
// Declared here rather than in document.h because the FILE READ is the part that needed
// writing; parseDocument already existed and is what this calls. See the definition for
// why it is not the two lambdas inside WinMain, and for the std::filesystem::path step
// that makes the ifstream portable.
bool loadDocumentFromPath(App& app, const std::string& path);

#endif // TINTA_FILE_UTILS_H
