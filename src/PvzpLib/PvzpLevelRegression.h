// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
class Board;
class LawnApp;
// Destructive opt-in integration harness: Linux only, PVZP_LEVEL_TEST=1,
// disposable HOME + installed level/variant fixtures. Never combine with CTEST.
bool PvzpLevelRegressionEnabled();
void PvzpRunLevelRegression(Board* board);
