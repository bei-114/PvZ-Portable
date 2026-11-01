// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>
#include "misc/Rect.h"
class Board;
class LawnApp;
namespace PvzpModUI {
Sexy::Rect ActionRect(Board*, int index);
void UnlockProfile(LawnApp*);
std::vector<uint8_t> Save(Board*);
bool Load(Board*, const std::vector<uint8_t>&);
}
