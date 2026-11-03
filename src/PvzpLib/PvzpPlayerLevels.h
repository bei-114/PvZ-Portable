// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "PvzpMod.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
class LawnApp;
namespace PvzpPlayerLevels {
constexpr int MaxLevels=32, MaxWaves=30, MaxGroups=12;
struct Result { bool ok=false; std::string message; explicit operator bool() const { return ok; } };
// Shared player-created content, not profile progression. Explicit root for tests.
Result Initialize(const std::filesystem::path& root={});
const std::vector<PvzpMod::CustomLevel>& Levels();
const PvzpMod::CustomLevel* Find(std::string_view id);
bool IsPlayerId(std::string_view id);
PvzpMod::CustomLevel NewDraft();
PvzpMod::CustomLevel CopyDraft(const PvzpMod::CustomLevel& source);
Result Validate(const PvzpMod::CustomLevel& level,bool requireDependencies=true);
Result Save(const PvzpMod::CustomLevel& level);
Result Remove(std::string_view id);
std::filesystem::path Directory();
std::string ExportJson(const PvzpMod::CustomLevel& level);
const std::vector<std::string>& Diagnostics();
// Runtime guard for all mutating editor entry points.
bool CanEdit(const LawnApp* app);
}
