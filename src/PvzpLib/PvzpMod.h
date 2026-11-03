/* PvZ-Portable startup-only JSON mod framework. SPDX-License-Identifier: LGPL-3.0-or-later
 * See DOCS_MOD.md for schema, precedence, limits, names, and units (game ticks).
 * Restart for mod rule changes: plant/zombie definitions are cached.
 * Player-level catalog publication is isolated and takes effect immediately.
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
		std::string name, tint, behavior = "nut", base = "WALL_NUT";
		int cost = 0, health = 6000, step = 1000, badChance = 0, refresh = 3000;
		// Zero disables timed pulses; otherwise 50..60000 game ticks.
		int interval = 0;
		// Validated event tokens only: sun:0..9990, heal:0..10000000,
		// chill, chill:row, freeze:row, damage:row:0..10000, push:row:0..160,
		// plant:random, destroy:plant, and supported zombie names.
		std::vector<std::string> events, badEvents, pulseEvents, foodEvents;
	};
	struct CustomZombie {
		std::string id, name, base = "ZOMBIE", head = "seed_packet", ability = "none";
		int health = 600, armor = 0, interval = 600, magnitude = 30;
		int speedPercent = 100, damagePercent = 100, slowResistancePercent = 0, sunDrop = 0;
		int tintR = 255, tintG = 255, tintB = 255;
	};
	struct CustomSpawn { std::string zombie; int row = -1, count = 1; };
	struct CustomWave { int delay = 800; std::vector<CustomSpawn> spawns; };
	struct CustomLevel {
		std::string id, name, description;
		int startingSun = 300;
		std::vector<CustomWave> waves;
	};
	const std::vector<CustomPlant>& CustomPlants();
	const std::vector<CustomZombie>& CustomZombies();
	const std::vector<CustomLevel>& CustomLevels();
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
	// Gameplay rules only: excludes bundled/editor level catalogs.
	uint64_t GameplayFingerprint();
	// Menu-only publication. Rejects duplicate/non-player IDs and bundled collisions.
	// Does not rebuild cached entity rules or alter either rules fingerprint.
	bool PublishPlayerLevels(const std::vector<CustomLevel>& levels);
	// Tooling snapshot generation, including resets/toggles with equal effective rules.
	// Consumers caching derived definitions must refresh when this changes.
	uint64_t SnapshotRevision();
	void WarnSavedRules(uint64_t theSavedFingerprint);
	bool HasMods();
	int PlantInt(const char* thePlantName, const char* theField, int theDefault);
	int ZombieInt(const char* theZombieName, const char* theField, int theDefault);
	int GlobalInt(const char* theField, int theDefault);
}
