/* PvZ-Portable startup-only JSON mod framework. SPDX-License-Identifier: LGPL-3.0-or-later
 * See DOCS_MOD.md for schema, precedence, limits, names, and units (game ticks).
 * Restart the game to apply changes: plant/zombie definitions are cached.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace PvzpMod
{
	struct ModInfo { std::string id, name, folder; int priority=0; bool enabled=true; };
	struct Diagnostic { std::string folder, message; };
	struct CustomPlant
	{
		int slot = -1;
		std::string name, tint, behavior = "nut";
		int cost = 0, health = 6000, step = 1000, badChance = 0;
		std::vector<std::string> events, badEvents;
	};
	const std::vector<CustomPlant>& CustomPlants();
	void Init();
	// Test tool: resets the mod snapshot. Not a gameplay hot-reload API.
	bool LoadFromDirectory(const std::filesystem::path& theDirectory);
	// Changes the snapshot for tooling; already-cached game definitions need restart.
	bool SetEnabled(std::string_view theId, bool theEnabled);
	const std::vector<ModInfo>& List();
	const std::vector<Diagnostic>& Diagnostics();
	// Stable fingerprint of effective rules, for warning when loading a save
	// created with a different mod set. It is not an integrity/security hash.
	uint64_t RulesFingerprint();
	void WarnSavedRules(uint64_t theSavedFingerprint);
	bool HasMods();
	int PlantInt(const char* thePlantName, const char* theField, int theDefault);
	int ZombieInt(const char* theZombieName, const char* theField, int theDefault);
	int GlobalInt(const char* theField, int theDefault);
}
