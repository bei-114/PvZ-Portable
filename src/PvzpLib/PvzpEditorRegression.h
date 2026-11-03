// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
class LawnApp;
class Board;
// Destructive opt-in desktop Linux integration harness. Use a disposable HOME
// with the nine v8 fixture packs. Never combine with CUSTOM_TEST/LEVEL_TEST.
// PVZP_EDITOR_UI_ONLY=1 stops before play; PVZP_EDITOR_HOLD=1 keeps the final
// native menu/dialog visible on success instead of shutting the application down.
bool PvzpEditorRegressionEnabled();
void PvzpRunEditorMenuRegression(LawnApp* app);
void PvzpRunEditorBoardRegression(Board* board);
