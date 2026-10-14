#pragma once

#include "app.h"

// Draft autosave (crash recovery): dirty edit buffers are mirrored to
// %APPDATA%\Tinta\drafts every sweep; a graceful close removes them, so
// anything left behind at startup is a crash leftover offered back via
// the recovery chip.
#define TIMER_DRAFT_SAVE 11
#define DRAFT_SAVE_INTERVAL_MS 30000

// Write a draft for every dirty edit buffer of this window and remove
// drafts of buffers that went clean. Called from the sweep timer.
void draftsSweep(App& app);

// Remove one tab's draft immediately (successful save, tab close)
void draftsDeleteForTab(int tabId);

// Remove every draft belonging to this window (graceful close)
void draftsDeleteAll(const App& app);

// Startup: collect crash leftovers into app.recoveredDrafts, skipping
// drafts owned by this or any other live Tinta process
void draftsScanForRecovery(App& app);

// Whether `pid` is a running process of the same executable as this one -- a sibling window,
// whose drafts are live and must not be offered for recovery. Declared here rather than kept
// static so tests/draft_owner_tests.cpp can ask it directly.
bool draftOwnerIsLiveSibling(unsigned long pid);

// Chip click: reopen each recovered draft as a dirty edit tab, then
// delete the files
void draftsRecoverAll(App& app);

// Chip cross: delete the files without opening them
void draftsDiscardAll(App& app);
