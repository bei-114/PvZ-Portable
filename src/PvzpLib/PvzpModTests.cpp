// SPDX-License-Identifier: LGPL-3.0-or-later
// Run with PVZP_MOD_TEST=1 before constructing LawnApp; needs no main.pak/assets.
#include "PvzpMod.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/System/PortableSaveValidation.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct Checks {
    int checks = 0;
    int failures = 0;

    void Check(bool ok, const char* what) {
        ++checks;
        if (!ok) ++failures;
        std::fprintf(stderr, "MODTEST %s %s\n", ok ? "PASS" : "FAIL", what);
    }
};

struct Scratch {
    fs::path dir;
    explicit Scratch() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path base = fs::temp_directory_path();
        for (int i = 0; i < 100; ++i) {
            dir = base / ("pvzp-mod-tests-" + std::to_string(stamp) + "-" + std::to_string(i));
            std::error_code error;
            if (fs::create_directory(dir, error)) return;
            if (error) throw fs::filesystem_error("cannot create test directory", dir, error);
        }
        throw std::runtime_error("cannot reserve unique test directory");
    }
    ~Scratch() {
        std::error_code error;
        fs::remove_all(dir, error);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};

void Write(const fs::path& path, std::string_view json) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!file) throw std::runtime_error("cannot write test manifest: " + path.string());
}

fs::path Fixture(const char* name) {
    const fs::path relative = fs::path("tests") / "mods" / name / "mod.json";
    const fs::path source = fs::path(__FILE__).parent_path().parent_path().parent_path() / relative;
    if (fs::is_regular_file(source)) return source;
    if (fs::is_regular_file(relative)) return relative;
    throw std::runtime_error("missing source fixture: " + relative.string());
}

void CopyFixture(const fs::path& root, const char* name, const char* folder) {
    const fs::path dest = root / folder / "mod.json";
    fs::create_directories(dest.parent_path());
    fs::copy_file(Fixture(name), dest, fs::copy_options::overwrite_existing);
}

bool HasDiagnostic(const std::string& folder) {
    for (const auto& entry : PvzpMod::Diagnostics()) {
        if (fs::path(entry.folder).filename() == folder && !entry.message.empty()) return true;
    }
    return false;
}

void LegacyAndModern(Checks& t, const fs::path& tmp) {
    const fs::path legacy = tmp / "legacy";
    CopyFixture(legacy, "legacy", "old-folder");
    t.Check(PvzpMod::LoadFromDirectory(legacy), "legacy directory scans");
    t.Check(PvzpMod::HasMods(), "legacy mod is active");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 50, "v1 plant cost");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "refresh", -1) == 500, "v1 refresh ticks");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "launchRate", -1) == 120, "v1 launch ticks");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "health", -1) == 400, "v1 plant health");
    t.Check(PvzpMod::ZombieInt("ZOMBIE", "health", -1) == 250, "v1 zombie health uses definition name");
    t.Check(PvzpMod::ZombieInt("ZOMBIE", "pickWeight", -1) == 4000, "v1 zombie pick weight");
    t.Check(PvzpMod::ZombieInt("ZOMBIE", "value", -1) == 1, "v1 zombie value");
    t.Check(PvzpMod::ZombieInt("ZOMBIE", "startingLevel", -1) == 1, "v1 zombie starting level");
    t.Check(PvzpMod::GlobalInt("startingSun", -1) == 100, "v1 global sun");
    t.Check(PvzpMod::PlantInt("SUNFLOWER", "cost", 333) == 333, "unmodified field keeps default");
    const auto old = PvzpMod::List();
    t.Check(old.size() == 1 && old[0].id == "old-folder" && old[0].enabled,
        "v1 missing id defaults to folder name");

    const fs::path modern = tmp / "modern";
    CopyFixture(modern, "modern", "different-folder");
    t.Check(PvzpMod::LoadFromDirectory(modern), "v2 directory scans");
    const auto loaded = PvzpMod::List();
    t.Check(loaded.size() == 1 && loaded[0].id == "org.example.balance" &&
        loaded[0].name == "Example balance" && loaded[0].priority == 10,
        "v2 metadata is exposed");
    t.Check(PvzpMod::GlobalInt("startingSun", -1) == 100, "v2 global override");

    // This distributable sample is data only; no user-supplied images/pak are read.
    const fs::path garden = tmp / "sample-garden";
    CopyFixture(garden, "garden-balance", "garden-balance");
    t.Check(PvzpMod::LoadFromDirectory(garden) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::List().size() == 1 &&
        PvzpMod::List()[0].id == "org.example.garden-balance",
        "shipped garden-balance sample validates as a standalone mod");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 75 &&
        PvzpMod::PlantInt("SUNFLOWER", "launchRate", -1) == 2200 &&
        PvzpMod::PlantInt("WALL_NUT", "health", -1) == 4300 &&
        PvzpMod::ZombieInt("ZOMBIE", "health", -1) == 280 &&
        PvzpMod::ZombieInt("CONEHEAD_ZOMBIE", "pickWeight", -1) == 2700 &&
        PvzpMod::GlobalInt("startingSun", -1) == 150,
        "shipped sample applies all declared entities without game assets");
}

void PriorityAndToggles(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "priority";
    Write(root / "z-last" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.z","priority":4,"plants":{"PEASHOOTER":{"cost":90}}})");
    Write(root / "a-first" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.a","priority":4,"plants":{"PEASHOOTER":{"cost":80}}})");
    Write(root / "b-low" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.b","priority":-10,"plants":{"PEASHOOTER":{"cost":70}}})");
    t.Check(PvzpMod::LoadFromDirectory(root), "priority directory scans");
    const auto list = PvzpMod::List();
    t.Check(list.size() == 3 && list[0].id == "org.test.b" &&
        list[1].id == "org.test.a" && list[2].id == "org.test.z",
        "priority then folder name give stable ordering");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 90,
        "later equal-priority folder wins conflict");
    t.Check(PvzpMod::SetEnabled("org.test.z", false), "disable existing mod");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 80,
        "disabling reveals previous winner");
    t.Check(!PvzpMod::List()[2].enabled, "disabled state is observable");
    t.Check(PvzpMod::SetEnabled("org.test.a", false), "disable second mod");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 70,
        "fallback follows priority");
    t.Check(PvzpMod::SetEnabled("org.test.z", true), "re-enable mod");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 90,
        "re-enable recomputes merged overrides");
    t.Check(!PvzpMod::SetEnabled("nonexistent.id", false), "unknown id cannot be toggled");
    const auto first = PvzpMod::List();
    t.Check(PvzpMod::LoadFromDirectory(root), "repeated scan succeeds");
    const auto second = PvzpMod::List();
    bool stable = first.size() == second.size();
    for (size_t i = 0; stable && i < first.size(); ++i)
        stable = first[i].id == second[i].id;
    t.Check(stable && PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 90,
        "rescan has deterministic ordering and no stale overrides");
}

void InvalidJsonAndFields(Checks& t, const fs::path& tmp) {
    const std::vector<std::pair<const char*, const char*>> invalid = {
        {"syntax", R"({"schemaVersion":2,"id":"org.bad.syntax",})"},
        {"trailing", R"({"schemaVersion":2,"id":"org.bad.trailing"} unexpected)"},
        {"nonobject", R"([1,2,3])"},
        {"type", R"({"schemaVersion":2,"id":"org.bad.type","plants":{"PEASHOOTER":{"cost":"99"}}})"},
        {"fraction", R"({"schemaVersion":2,"id":"org.bad.fraction","plants":{"PEASHOOTER":{"cost":7.5}}})"},
        {"too-large", R"({"schemaVersion":2,"id":"org.bad.large","plants":{"PEASHOOTER":{"health":10000001}}})"},
        {"zero-ticks", R"({"schemaVersion":2,"id":"org.bad.ticks","plants":{"PEASHOOTER":{"refresh":0}}})"},
        {"bad-global", R"({"schemaVersion":2,"id":"org.bad.global","globals":{"startingSun":9991}})"},
        {"bad-zombie", R"({"schemaVersion":2,"id":"org.bad.zombie","zombies":{"ZOMBIE":{"startingLevel":51}}})"},
        {"bad-priority", R"({"schemaVersion":2,"id":"org.bad.priority","priority":1001})"},
        {"unknown-plant", R"({"schemaVersion":2,"id":"org.bad.plant","plants":{"NONEXISTENT":{"cost":1}}})"},
        {"unknown-zombie", R"({"schemaVersion":2,"id":"org.bad.entity","zombies":{"ZOMBIE_NORMAL":{"health":1}}})"},
        {"unknown-field", R"({"schemaVersion":2,"id":"org.bad.field","plants":{"PEASHOOTER":{"cost":9,"magic":2}}})"},
        {"unknown-global", R"({"schemaVersion":2,"id":"org.bad.key","globals":{"__newProperty":9}})"},
        {"unsupported-version", R"({"schemaVersion":3,"id":"org.bad.version","globals":{"startingSun":1}})"},
        {"wrong-enabled", R"({"schemaVersion":2,"id":"org.bad.enabled","enabled":"false","globals":{"startingSun":1}})"},
        {"path-id", R"({"schemaVersion":2,"id":"../../escape","globals":{"startingSun":1}})"},
        {"duplicate-root-key", R"({"schemaVersion":2,"id":"org.test.once","id":"org.test.twice"})"},
        {"duplicate-field-key", R"({"schemaVersion":2,"id":"org.bad.double","plants":{"PEASHOOTER":{"cost":12,"cost":13}}})"},
        {"invalid-hex-escape", R"({"schemaVersion":2,"id":"org.bad.hex","name":"\uZZZZ"})"},
        {"unpaired-high-surrogate", R"({"schemaVersion":2,"id":"org.bad.high","name":"\uD83C"})"},
        {"unpaired-low-surrogate", R"({"schemaVersion":2,"id":"org.bad.low","name":"\uDF3B"})"},
        {"mismatched-surrogates", R"({"schemaVersion":2,"id":"org.bad.pair","name":"\uD83C\u0041"})"},
        {"nan", R"({"schemaVersion":2,"id":"org.bad.nan","globals":{"startingSun":NaN}})"},
        {"infinity", R"({"schemaVersion":2,"id":"org.bad.infinity","globals":{"startingSun":Infinity}})"},
        {"number-overflow", R"({"schemaVersion":2,"id":"org.bad.exponent","globals":{"startingSun":1e999}})"},
        {"integer-overflow", R"({"schemaVersion":2,"id":"org.bad.integer","globals":{"startingSun":999999999999999999999999999999999999}})"},
    };
    for (const auto& [folder, text] : invalid) {
        const fs::path root = tmp / (std::string("invalid-") + folder);
        Write(root / "valid" / "mod.json",
            R"({"schemaVersion":2,"id":"org.test.good","plants":{"PEASHOOTER":{"cost":71}}})");
        Write(root / folder / "mod.json", text);
        t.Check(PvzpMod::LoadFromDirectory(root), "directory with invalid mod still scans");
        t.Check(PvzpMod::List().size() == 1 &&
            PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 71 &&
            PvzpMod::GlobalInt("startingSun", 543) == 543,
            "invalid mod rejected atomically without affecting valid mod");
        t.Check(HasDiagnostic(folder), "bad mod reports folder and nonempty diagnostic");
    }
}

void ParserSafety(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "parser-limits";
    const std::string prefix = R"({"schemaVersion":2,"id":"org.test.limit"})";
    std::string exact = prefix;
    exact.resize(65536, ' ');
    Write(root / "boundary" / "mod.json", exact);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().size() == 1 &&
        PvzpMod::Diagnostics().empty(), "64 KiB manifest accepted at exact size limit");
    exact.push_back(' ');
    Write(root / "boundary" / "mod.json", exact);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().empty() &&
        HasDiagnostic("boundary"), "oversized 64 KiB + 1 manifest rejected with diagnostic");

    Write(root / "boundary" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.limit","name":"\uD83C\uDF3B"})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().size() == 1 &&
        PvzpMod::List()[0].name == "\xF0\x9F\x8C\xBB" &&
        PvzpMod::Diagnostics().empty(), "surrogate pair decodes to valid UTF-8 flower");

    const std::string nested = R"({"schemaVersion":2,"id":"org.test.deep","extra":)" +
        std::string(33, '[') + "0" + std::string(33, ']') + "}";
    Write(root / "boundary" / "mod.json", nested);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().empty() &&
        HasDiagnostic("boundary"), "JSON nesting beyond 32 is rejected");

    const std::string invalidUtf8 = R"({"schemaVersion":2,"id":"org.test.utf8","name":")" +
        std::string("\xC3\x28", 2) + R"("})";
    Write(root / "boundary" / "mod.json", invalidUtf8);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().empty() &&
        HasDiagnostic("boundary"), "malformed raw UTF-8 sequence is rejected");
}

void Aliases(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "aliases";
    Write(root / "alias" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.alias","plants":{"REPEATER":{"cost":125}},"zombies":{"ZOMBIE":{"health":305}}})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty(),
        "definition-name alias manifest loads");
    t.Check(std::string(gPlantDefs[SeedType::SEED_REPEATER].mPlantName) ==
        gPlantDefs[SeedType::SEED_LEFTPEATER].mPlantName &&
        PvzpMod::PlantInt(gPlantDefs[SeedType::SEED_REPEATER].mPlantName, "cost", -1) == 125 &&
        PvzpMod::PlantInt(gPlantDefs[SeedType::SEED_LEFTPEATER].mPlantName, "cost", -1) == 125,
        "REPEATER name alias applies to normal and left-facing definitions");
    t.Check(std::string(gZombieDefs[ZombieType::ZOMBIE_NORMAL].mZombieName) ==
        gZombieDefs[ZombieType::ZOMBIE_PEA_HEAD].mZombieName &&
        PvzpMod::ZombieInt(gZombieDefs[ZombieType::ZOMBIE_NORMAL].mZombieName, "health", -1) == 305 &&
        PvzpMod::ZombieInt(gZombieDefs[ZombieType::ZOMBIE_PEA_HEAD].mZombieName, "health", -1) == 305,
        "ZOMBIE name alias also applies to zombotany variants");
}

void RangeBoundaries(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "boundaries";
    Write(root / "minimum" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.minimum","priority":-1000,"plants":{"PEASHOOTER":{"cost":0,"refresh":1,"launchRate":60000,"health":10000000}},"zombies":{"ZOMBIE":{"health":1,"pickWeight":0,"value":10000,"startingLevel":50}},"globals":{"startingSun":0}})");
    t.Check(PvzpMod::LoadFromDirectory(root), "minimum boundary directory scans");
    t.Check(PvzpMod::List().size() == 1 && PvzpMod::List()[0].priority == -1000 &&
        PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 0 &&
        PvzpMod::PlantInt("PEASHOOTER", "refresh", -1) == 1 &&
        PvzpMod::PlantInt("PEASHOOTER", "launchRate", -1) == 60000 &&
        PvzpMod::PlantInt("PEASHOOTER", "health", -1) == 10000000 &&
        PvzpMod::ZombieInt("ZOMBIE", "health", -1) == 1 &&
        PvzpMod::ZombieInt("ZOMBIE", "pickWeight", -1) == 0 &&
        PvzpMod::ZombieInt("ZOMBIE", "value", -1) == 10000 &&
        PvzpMod::ZombieInt("ZOMBIE", "startingLevel", -1) == 50 &&
        PvzpMod::GlobalInt("startingSun", -1) == 0,
        "all supported field boundary values accepted");
    Write(root / "maximum" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.maximum","priority":1000,"plants":{"PEASHOOTER":{"cost":9990,"refresh":60000,"launchRate":1,"health":1}},"zombies":{"ZOMBIE":{"health":10000000,"pickWeight":100000,"value":0,"startingLevel":1}},"globals":{"startingSun":9990}})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().size() == 2 &&
        PvzpMod::List()[1].priority == 1000 &&
        PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 9990 &&
        PvzpMod::GlobalInt("startingSun", -1) == 9990,
        "maximum boundaries accepted and high priority wins");
}

void DuplicateAndDisabled(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "duplicates";
    Write(root / "a" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.duplicate","plants":{"PEASHOOTER":{"cost":40}}})");
    Write(root / "z" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.duplicate","plants":{"PEASHOOTER":{"cost":999}}})");
    Write(root / "off" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.off","enabled":false,"globals":{"startingSun":300}})");
    t.Check(PvzpMod::LoadFromDirectory(root), "duplicate directory scans");
    t.Check(PvzpMod::List().size() == 2 && HasDiagnostic("z"),
        "duplicate id is diagnosed and rejected");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 40,
        "duplicate cannot override first mod");
    t.Check(PvzpMod::GlobalInt("startingSun", 333) == 333,
        "initially disabled mod has no overrides");
    t.Check(PvzpMod::SetEnabled("org.test.off", true) &&
        PvzpMod::GlobalInt("startingSun", -1) == 300,
        "disabled mod can be enabled before game startup");
}

void SamplePacks(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "samples";
    CopyFixture(root, "10-sunny-lab", "10-sunny-lab");
    CopyFixture(root, "20-zombie-march", "20-zombie-march");
    CopyFixture(root, "30-electric-lab", "30-electric-lab");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty(),
        "three distribute-ready samples load cleanly");
    const auto list = PvzpMod::List();
    t.Check(list.size() == 3 && list[0].priority == 10 && list[1].priority == 20 &&
        list[2].priority == 30 && list[2].id == "org.pvzp.example.electric-lab",
        "samples sort by priority, highest last");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 65 &&
        PvzpMod::PlantInt("PEASHOOTER", "health", -1) == 420 &&
        PvzpMod::PlantInt("SUNFLOWER", "refresh", -1) == 600,
        "higher-priority sample wins only the fields it sets");
    t.Check(PvzpMod::ZombieInt("ZOMBIE", "health", -1) == 340 &&
        PvzpMod::ZombieInt("CONEHEAD_ZOMBIE", "health", -1) == 600 &&
        PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE", "health", -1) == 1000 &&
        PvzpMod::GlobalInt("startingSun", -1) == 300,
        "zombie and global sample values merge across packs");
    t.Check(PvzpMod::PlantInt("ELECTRIC_GATLING_PEA", "cost", -1) == 275 &&
        PvzpMod::PlantInt("ELECTRIC_GATLING_PEA", "health", -1) == 600,
        "custom electric gatling definition is addressable by its real name");
    const uint64_t before = PvzpMod::RulesFingerprint();
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.electric-lab", false) &&
        PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 80 &&
        PvzpMod::RulesFingerprint() != before,
        "disabling a sample reveals the previous winner and changes fingerprint");
    const uint64_t withoutElectric = PvzpMod::RulesFingerprint();
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.electric-lab", true) &&
        PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 65,
        "re-enabling restores merged rules");
    PvzpMod::WarnSavedRules(withoutElectric);
    t.Check(HasDiagnostic("savegame"),
        "saved-rules mismatch is reported without mutating gameplay state");
    t.Check(PvzpMod::LoadFromDirectory(tmp / "no-samples") && !PvzpMod::HasMods(),
        "loading a fresh directory clears sample overrides");
}

void FunPacks(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "fun";
    CopyFixture(root, "35-sun-empire", "35-sun-empire");
    CopyFixture(root, "40-pea-party", "40-pea-party");
    CopyFixture(root, "50-zombie-rush", "50-zombie-rush");
    CopyFixture(root, "60-glass-cannon", "60-glass-cannon");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty(),
        "four themed packs load without diagnostics");
    t.Check(PvzpMod::List().size() == 4 && PvzpMod::List()[3].id == "org.pvzp.example.glass-cannon",
        "themed packs order by priority");
    t.Check(PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == 50 &&
        PvzpMod::PlantInt("GATLING_PEA", "cost", -1) == 150 &&
        PvzpMod::PlantInt("GATLING_PEA", "launchRate", -1) == 50,
        "glass cannon overrides pea party for shared fields");
    t.Check(PvzpMod::ZombieInt("ZOMBIE", "health", -1) == 120 &&
        PvzpMod::ZombieInt("ZOMBIE", "pickWeight", -1) == 8000 &&
        PvzpMod::GlobalInt("startingSun", -1) == 150,
        "zombie rush and sun empire values apply");
    t.Check(PvzpMod::PlantInt("SUNFLOWER", "launchRate", -1) == 1800 &&
        PvzpMod::PlantInt("SUN_SHROOM", "cost", -1) == 0,
        "sun producers use the faster cycle");
    t.Check(PvzpMod::LoadFromDirectory(tmp / "no-fun") && !PvzpMod::HasMods(),
        "leaving the themed directory clears their rules");
}

void CustomPlantPack(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "custom";
    CopyFixture(root, "70-box-nuts", "70-box-nuts");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty(),
        "custom plant pack loads without diagnostics");
    const auto& list = PvzpMod::CustomPlants();
    t.Check(list.size() == 2 && list[0].slot == 0 && list[0].cost == 200 &&
        list[0].health == 6000 && list[0].events.size() == 5,
        "mystery box fields parsed from mod json");
    t.Check(list[1].slot == 1 && list[1].cost == 0 && list[1].badChance == 50 &&
        list[1].badEvents.size() == 5,
        "reverse box fields parsed from mod json");
    t.Check(PvzpMod::LoadFromDirectory(tmp / "no-custom") && PvzpMod::CustomPlants().empty(),
        "leaving the custom directory clears definitions");
    CopyFixture(root, "80-brain", "80-brain");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::CustomPlants().size() == 3 && PvzpMod::CustomPlants()[2].slot == 2 &&
        PvzpMod::CustomPlants()[2].behavior == "brain" && PvzpMod::CustomPlants()[2].tint == "red",
        "existing box nuts and brain packs remain compatible with strict validation");
}

void NewContentPacks(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "new-content-packs";
    CopyFixture(root, "70-box-nuts", "70-box-nuts");
    CopyFixture(root, "80-brain", "80-brain");
    CopyFixture(root, "90-aurora-nut", "90-aurora-nut");
    CopyFixture(root, "95-bounty-zombies", "95-bounty-zombies");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::List().size() == 4 && PvzpMod::CustomPlants().size() == 4,
        "shipped box brain aurora and bounty packs coexist without diagnostics");
    bool slots[4] = {};
    for (const auto& plant : PvzpMod::CustomPlants()) {
        if (plant.slot >= 0 && plant.slot < 4) slots[plant.slot] = true;
    }
    t.Check(slots[0] && slots[1] && slots[2] && slots[3], "70 80 and90 packs occupy distinct custom slots zero through three");
    bool aurora = false, mysteryFood = false, reverseFood = false;
    for (const auto& plant : PvzpMod::CustomPlants()) {
        if (plant.slot == 0) mysteryFood = plant.foodEvents == std::vector<std::string>{"sun:150", "plant:random", "heal:2000"};
        if (plant.slot == 1) reverseFood = plant.foodEvents == std::vector<std::string>{"sun:100", "plant:random", "chill"};
        if (plant.slot == 3) aurora = plant.name == "Aurora Nut" && plant.behavior == "nut" &&
            plant.tint == "purple" && plant.cost == 125 && plant.health == 1800 && plant.step == 600 &&
            plant.interval == 800 && plant.events == std::vector<std::string>{"chill:row"} &&
            plant.pulseEvents == std::vector<std::string>{"chill:row"} &&
            plant.foodEvents == std::vector<std::string>{"chill", "heal:1800", "sun:100"};
    }
    t.Check(aurora, "Aurora Nut fixture declares row chill pulses every800 ticks and three food events");
    t.Check(mysteryFood && reverseFood, "both existing box nuts expose their three configured food events");
    t.Check(PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE", "health", -1) == 350 &&
        PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE", "speedPercent", -1) == 125 &&
        PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE", "sunDrop", -1) == 100 &&
        PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE", "tintR", -1) == 120 &&
        PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE", "tintG", -1) == 215 &&
        PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE", "tintB", -1) == 255,
        "bounty buckethead fixture applies speed125 sun100 and blue tint");
    t.Check(PvzpMod::ZombieInt("NEWSPAPER_ZOMBIE", "health", -1) == 400 &&
        PvzpMod::ZombieInt("NEWSPAPER_ZOMBIE", "speedPercent", -1) == 150 &&
        PvzpMod::ZombieInt("NEWSPAPER_ZOMBIE", "sunDrop", -1) == 75 &&
        PvzpMod::ZombieInt("NEWSPAPER_ZOMBIE", "tintR", -1) == 255 &&
        PvzpMod::ZombieInt("NEWSPAPER_ZOMBIE", "tintG", -1) == 195 &&
        PvzpMod::ZombieInt("NEWSPAPER_ZOMBIE", "tintB", -1) == 100,
        "bounty newspaper fixture applies speed150 sun75 and gold tint");
    const uint64_t fingerprint = PvzpMod::RulesFingerprint();
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.aurora-nut", false) &&
        PvzpMod::CustomPlants().size() == 3 && PvzpMod::RulesFingerprint() != fingerprint,
        "disabling Aurora Nut removes its effective definition");
    bool freed = true;
    for (const auto& plant : PvzpMod::CustomPlants()) if (plant.slot == 3) freed = false;
    t.Check(freed && PvzpMod::Diagnostics().empty(), "disabling90 frees slot three without disturbing other packs");
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.bounty-zombies", false), "bounty fixture can be disabled independently");
    bool cleared = true;
    for (const char* name : {"BUCKETHEAD_ZOMBIE", "NEWSPAPER_ZOMBIE"})
        for (const char* field : {"health", "speedPercent", "sunDrop", "tintR", "tintG", "tintB"})
            cleared = cleared && PvzpMod::ZombieInt(name, field, -123) == -123;
    t.Check(cleared && PvzpMod::CustomPlants().size() == 3 && PvzpMod::Diagnostics().empty(),
        "disabling95 clears every bounty override while preserving other custom plants");
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.aurora-nut", true) &&
        PvzpMod::SetEnabled("org.pvzp.example.bounty-zombies", true) &&
        PvzpMod::RulesFingerprint() == fingerprint && PvzpMod::Diagnostics().empty(),
        "re-enabling shipped content restores the identical effective rules");
}

void CustomPlantSecurity(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "custom-security";
    const fs::path manifest = root / "candidate" / "mod.json";
    auto pack = [](const std::string& fields) {
        return std::string(R"({"id":"org.test.custom","plants":{"PEASHOOTER":{"cost":99}},"customPlants":[{"slot":0,"name":"Test")") + fields + "}]}";
    };
    Write(root / "good" / "mod.json",
        R"({"id":"org.test.good","plants":{"SUNFLOWER":{"cost":71}}})");
    const std::vector<const char*> invalidTokens = {
        "", "sun:", "heal:", "sun:-1", "sun:+1", "sun: 1", "sun:1 ", "sun:1x",
        "sun:1.0", "sun:1e2", "sun:0x10", "sun:9991", "heal:10000001",
        "sun:2147483648", "heal:99999999999999999999999999999999999",
        "sun:\\u0661", "sun:1\\u0000tail", "sun:1\\n", "chill\\u0000", "CHILL",
        "zombie:", "zombie:TYPO", "zombie:BOSS", "plant:PEASHOOTER", "destroy:all", "chill:all"
    };
    for (const char* field : {"events", "badEvents", "pulseEvents", "foodEvents"}) {
        for (const char* token : invalidTokens) {
            Write(manifest, pack(std::string(",\"") + field + "\":[\"" + token + "\"]"));
            t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().size() == 1 &&
                PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate") &&
                PvzpMod::PlantInt("PEASHOOTER", "cost", -1) == -1 &&
                PvzpMod::PlantInt("SUNFLOWER", "cost", -1) == 71,
                "unsafe event rejected atomically in every event list");
        }
    }
    const std::vector<const char*> invalidFields = {
        R"(,"tint":"orange")", R"(,"tint":"Gold")", R"(,"tint":"")", R"(,"tint":1)",
        R"(,"behavior":"shooter")", R"(,"behavior":"Nut")", R"(,"behavior":false)",
        R"(,"interval":1)", R"(,"interval":49)", R"(,"interval":60001)", R"(,"interval":-1)",
        R"(,"interval":50.5)", R"(,"interval":"800")", R"(,"events":[null])", R"(,"badEvents":[1])",
        R"(,"pulseEvents":"chill")", R"(,"foodEvents":[true])"
    };
    for (const char* fields : invalidFields) {
        Write(manifest, pack(fields));
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().size() == 1 &&
            PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate"),
            "invalid custom behavior tint interval or event type rejected");
    }
    // The complete supported grammar, including endpoints and benign leading zeros.
    Write(manifest, pack(R"(,"tint":"purple","interval":50,"events":["sun:0","sun:9990","heal:0","heal:10000000","chill","chill:row","plant:random","destroy:plant","zombie:NORMAL","zombie:GARGANTUAR","zombie:BUNGEE","zombie:DIGGER","zombie:BALLOON","zombie:CONEHEAD","zombie:BUCKETHEAD","sun:00075"],"pulseEvents":["chill:row"],"foodEvents":["chill","sun:100"])"));
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::CustomPlants().size() == 1 && PvzpMod::CustomPlants()[0].events.size() == 16 &&
        PvzpMod::CustomPlants()[0].interval == 50 && PvzpMod::CustomPlants()[0].pulseEvents.size() == 1 &&
        PvzpMod::CustomPlants()[0].foodEvents.size() == 2,
        "supported event grammar boundaries and extended custom fields accepted");
    for (const char* field : {"events", "badEvents", "pulseEvents", "foodEvents"}) {
        std::string list;
        for (int i = 0; i < 17; ++i) list += (i ? ",\"chill\"" : "\"chill\"");
        Write(manifest, pack(std::string(",\"") + field + "\":[" + list + "]"));
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate"),
            "all event lists enforce sixteen token limit");
    }
    for (const char* tint : {"gold", "blue", "red", "green", "purple"}) {
        Write(manifest, pack(std::string(",\"tint\":\"") + tint + "\",\"interval\":60000"));
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().size() == 1 &&
            PvzpMod::Diagnostics().empty(), "every supported tint and maximum interval accepted");
    }
    Write(manifest, pack(""));
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().size() == 1 &&
        PvzpMod::CustomPlants()[0].behavior == "nut" && PvzpMod::CustomPlants()[0].tint.empty() &&
        PvzpMod::CustomPlants()[0].interval == 0 && PvzpMod::CustomPlants()[0].pulseEvents.empty() &&
        PvzpMod::CustomPlants()[0].foodEvents.empty(), "omitted custom fields retain legacy defaults");
    Write(manifest, R"({"customPlants":[{"slot":0,"name":"bad\u0000name"}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate"),
        "control characters in custom display names rejected");
    Write(manifest, R"({"customPlants":[{"slot":0,"name":"One"},{"slot":0,"name":"Two"}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate"),
        "duplicate custom slots in one manifest rejected atomically");
}

void CustomPlantFingerprintsAndReset(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "custom-fingerprint";
    const fs::path manifest = root / "custom" / "mod.json";
    const std::string baseline = R"({"id":"org.test.custom","customPlants":[{"slot":0,"name":"Test","behavior":"nut","tint":"gold","cost":200,"health":6000,"step":1000,"badChance":50,"interval":800,"events":["sun:150","heal:200"],"badEvents":["zombie:DIGGER"],"pulseEvents":["chill:row"],"foodEvents":["chill","sun:100"]}]})";
    Write(manifest, baseline);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::HasMods(), "custom-only pack counts as active mods");
    const uint64_t fingerprint = PvzpMod::RulesFingerprint();
    uint64_t revision = PvzpMod::SnapshotRevision();
    const std::vector<std::pair<const char*, const char*>> mutations = {
        {R"("slot":0)", R"("slot":1)"}, {R"("name":"Test")", R"("name":"Renamed")"},
        {R"("behavior":"nut")", R"("behavior":"brain")"}, {R"("tint":"gold")", R"("tint":"blue")"},
        {R"("cost":200)", R"("cost":201)"}, {R"("health":6000)", R"("health":6001)"},
        {R"("step":1000)", R"("step":1001)"}, {R"("badChance":50)", R"("badChance":51)"},
        {R"("interval":800)", R"("interval":801)"},
        {R"("events":["sun:150","heal:200"])", R"("events":["heal:200","sun:150"])"},
        {R"("badEvents":["zombie:DIGGER"])", R"("badEvents":["zombie:BUNGEE"])"},
        {R"("pulseEvents":["chill:row"])", R"("pulseEvents":["chill"])"},
        {R"("foodEvents":["chill","sun:100"])", R"("foodEvents":["chill"])"}
    };
    for (const auto& [before, after] : mutations) {
        std::string changed = baseline;
        changed.replace(changed.find(before), std::string(before).size(), after);
        Write(manifest, changed);
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().size() == 1 &&
            PvzpMod::Diagnostics().empty() && PvzpMod::RulesFingerprint() != fingerprint &&
            PvzpMod::SnapshotRevision() > revision,
            "every effective custom definition field is fingerprinted and invalidates snapshot cache");
        revision = PvzpMod::SnapshotRevision();
    }
    Write(manifest, baseline);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::RulesFingerprint() == fingerprint &&
        PvzpMod::SnapshotRevision() > revision, "identical rules retain fingerprint but refresh snapshot generation");
    PvzpMod::WarnSavedRules(fingerprint);
    t.Check(PvzpMod::Diagnostics().empty(), "matching custom fingerprint does not warn");
    revision = PvzpMod::SnapshotRevision();
    t.Check(PvzpMod::SetEnabled("org.test.custom", false) && PvzpMod::CustomPlants().empty() &&
        !PvzpMod::HasMods() && PvzpMod::RulesFingerprint() != fingerprint &&
        PvzpMod::SnapshotRevision() > revision, "disabling custom-only mod removes stale definitions and changes revision");
    PvzpMod::WarnSavedRules(fingerprint);
    t.Check(HasDiagnostic("savegame"), "custom definition mismatch warns on saved rules");
    const uint64_t disabledFingerprint = PvzpMod::RulesFingerprint();
    t.Check(PvzpMod::SetEnabled("org.test.custom", true) && PvzpMod::CustomPlants().size() == 1 &&
        PvzpMod::RulesFingerprint() == fingerprint, "re-enabling restores original custom definition");
    std::string disabled = baseline;
    disabled.insert(1, "\"enabled\":false,");
    Write(manifest, disabled);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().empty() &&
        !PvzpMod::HasMods() && PvzpMod::RulesFingerprint() == disabledFingerprint,
        "loading disabled custom pack clears prior enabled definitions");
    Write(manifest, baseline);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().size() == 1,
        "loading re-enabled custom manifest restores definitions");
    revision = PvzpMod::SnapshotRevision();
    t.Check(PvzpMod::LoadFromDirectory(tmp / "custom-reset-empty") && PvzpMod::CustomPlants().empty() &&
        !PvzpMod::HasMods() && PvzpMod::SnapshotRevision() > revision,
        "loading missing directory clears custom definitions and invalidates cache");
    Write(root / "not-directory", "not a directory");
    t.Check(PvzpMod::LoadFromDirectory(root) && !PvzpMod::LoadFromDirectory(root / "not-directory") &&
        PvzpMod::CustomPlants().empty() && !PvzpMod::HasMods(),
        "failed directory load cannot leave stale custom definitions");
    const std::string one = R"({"slot":0,"name":"One"})";
    const std::string two = R"({"slot":1,"name":"Two"})";
    Write(manifest, std::string(R"({"id":"org.test.custom","customPlants":[)") + one + "," + two + "]}");
    t.Check(PvzpMod::LoadFromDirectory(root), "ordered custom definitions load");
    const auto orderedFingerprint = PvzpMod::RulesFingerprint();
    Write(manifest, std::string(R"({"id":"org.test.custom","customPlants":[)") + two + "," + one + "]}");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::RulesFingerprint() == orderedFingerprint,
        "manifest custom definition ordering does not change effective fingerprint");
}

void CustomPlantCollisions(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "custom-collisions";
    Write(root / "low" / "mod.json",
        R"({"id":"org.test.low","priority":-1,"customPlants":[{"slot":0,"name":"Low","cost":100}]})");
    Write(root / "a" / "mod.json",
        R"({"id":"org.test.a","customPlants":[{"slot":0,"name":"A","cost":200}]})");
    Write(root / "z" / "mod.json",
        R"({"id":"org.test.z","customPlants":[{"slot":0,"name":"Z","cost":300}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().size() == 1 &&
        PvzpMod::CustomPlants()[0].cost == 300 && HasDiagnostic("a") && HasDiagnostic("z") &&
        PvzpMod::Diagnostics().size() == 2, "cross-mod slot collisions warn and preserve priority/folder last-wins");
    bool named = false;
    for (const auto& diagnostic : PvzpMod::Diagnostics()) {
        if (diagnostic.message.find("org.test.z") != std::string::npos &&
            diagnostic.message.find("org.test.a") != std::string::npos &&
            diagnostic.message.find("slot 0") != std::string::npos) named = true;
    }
    t.Check(named, "slot collision diagnostic identifies slot winner and previous owner");
    t.Check(PvzpMod::SetEnabled("org.test.z", false) && PvzpMod::CustomPlants()[0].cost == 200 &&
        PvzpMod::Diagnostics().size() == 1 && !HasDiagnostic("z"),
        "disabling slot winner reveals previous definition and removes stale conflict warnings");
    t.Check(PvzpMod::SetEnabled("org.test.z", true) && PvzpMod::Diagnostics().size() == 2 &&
        PvzpMod::CustomPlants()[0].cost == 300, "re-enabling restores slot winner without accumulating warnings");
    PvzpMod::WarnSavedRules(0);
    t.Check(PvzpMod::SetEnabled("org.test.z", false) && PvzpMod::SetEnabled("org.test.a", false) &&
        PvzpMod::Diagnostics().size() == 1 && HasDiagnostic("savegame") &&
        PvzpMod::CustomPlants()[0].cost == 100, "rebuild clears conflicts while retaining independent save diagnostics");
    t.Check(PvzpMod::SetEnabled("org.test.low", false) && PvzpMod::CustomPlants().empty() && !PvzpMod::HasMods(),
        "disabling all slot owners leaves no stale custom plant");
}

void DefinitionCacheReset(Checks& t, const fs::path& tmp) {
    const fs::path empty = tmp / "definition-cache-empty";
    const fs::path root = tmp / "definition-cache";
    const fs::path manifest = root / "cached" / "mod.json";
    t.Check(PvzpMod::LoadFromDirectory(empty), "cache regression starts with default snapshot");
    const int plantCost = GetPlantDefinition(SeedType::SEED_PEASHOOTER).mSeedCost;
    const int customCost = GetPlantDefinition(SeedType::SEED_CUSTOM_0).mSeedCost;
    const int zombieWeight = GetZombieDefinition(ZombieType::ZOMBIE_NORMAL).mPickWeight;
    const std::string json = R"({"id":"org.test.cached","plants":{"PEASHOOTER":{"cost":321}},"zombies":{"ZOMBIE":{"pickWeight":5432}},"customPlants":[{"slot":0,"name":"Cached","cost":432}]})";
    auto applied = [] {
        return GetPlantDefinition(SeedType::SEED_PEASHOOTER).mSeedCost == 321 &&
            GetPlantDefinition(SeedType::SEED_CUSTOM_0).mSeedCost == 432 &&
            GetZombieDefinition(ZombieType::ZOMBIE_NORMAL).mPickWeight == 5432;
    };
    auto defaults = [&] {
        return GetPlantDefinition(SeedType::SEED_PEASHOOTER).mSeedCost == plantCost &&
            GetPlantDefinition(SeedType::SEED_CUSTOM_0).mSeedCost == customCost &&
            GetZombieDefinition(ZombieType::ZOMBIE_NORMAL).mPickWeight == zombieWeight;
    };
    Write(manifest, json);
    t.Check(PvzpMod::LoadFromDirectory(root) && applied(), "loaded snapshot refreshes already-cached game definitions");
    t.Check(PvzpMod::SetEnabled("org.test.cached", false) && defaults(),
        "disabling restores default normal custom and zombie cached definitions");
    t.Check(PvzpMod::SetEnabled("org.test.cached", true) && applied(),
        "re-enabling refreshes all derived definition caches");
    std::string disabled = json;
    disabled.insert(1, "\"enabled\":false,");
    Write(manifest, disabled);
    t.Check(PvzpMod::LoadFromDirectory(root) && defaults(),
        "loading disabled manifest cannot retain cached custom costs");
    Write(manifest, json);
    t.Check(PvzpMod::LoadFromDirectory(root) && applied(), "reloaded enabled rules refresh definitions");
    t.Check(PvzpMod::LoadFromDirectory(empty) && defaults(), "loading empty snapshot resets all cached definitions");
}

void ZombieVariantFields(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "zombie-variant";
    const fs::path manifest = root / "variant" / "mod.json";
    Write(manifest, R"({"id":"org.test.variant","zombies":{"ZOMBIE":{"speedPercent":25,"sunDrop":0,"tintR":0,"tintG":255,"tintB":128}}})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::ZombieInt("ZOMBIE", "speedPercent", -1) == 25 &&
        PvzpMod::ZombieInt("ZOMBIE", "sunDrop", -1) == 0 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintR", -1) == 0 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintG", -1) == 255 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintB", -1) == 128, "generic zombie variant fields accept lower and color bounds");
    const auto fingerprint = PvzpMod::RulesFingerprint();
    Write(manifest, R"({"id":"org.test.variant","zombies":{"ZOMBIE":{"speedPercent":300,"sunDrop":1000}}})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::ZombieInt("ZOMBIE", "speedPercent", -1) == 300 &&
        PvzpMod::ZombieInt("ZOMBIE", "sunDrop", -1) == 1000 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintR", -1) == -1 && PvzpMod::RulesFingerprint() != fingerprint,
        "generic zombie variant maximums accepted and missing colors reset");
    for (const char* fields : {R"("speedPercent":24)", R"("speedPercent":301)", R"("sunDrop":-1)",
        R"("sunDrop":1001)", R"("tintR":-1)", R"("tintG":256)", R"("tintB":1.5)", R"("tintR":"0")"}) {
        Write(manifest, std::string(R"({"id":"org.test.variant","zombies":{"ZOMBIE":{)") + fields + "}}}");
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().empty() && HasDiagnostic("variant"),
            "invalid zombie variant integer bounds and types rejected atomically");
    }
}

void PortableSaveValidation(Checks& t) {
    using Bytes = std::vector<uint8_t>;
    auto u32 = [](Bytes& bytes, uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
    };
    auto field = [&](Bytes& bytes, uint32_t id, const Bytes& value) {
        u32(bytes, id); u32(bytes, static_cast<uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    };
    auto envelope = [&](const Bytes& value) {
        Bytes chunk; u32(chunk, 1); field(chunk, 1, value); return chunk;
    };
    auto number = [&](uint32_t value) { Bytes bytes; u32(bytes, value); return bytes; };
    Bytes baseline;
    field(baseline, 1, envelope(number(0))); // Envelope-only fixture; not an actual Board.
    auto preflight = [](const Bytes& bytes, bool expectedMarker) {
        std::vector<PvzpSave::ChunkView> chunks;
        bool marked = false;
        return PvzpSave::PreflightPayload({bytes.data(), bytes.size()}, chunks, marked) && marked == expectedMarker;
    };
    t.Check(preflight(baseline, false), "unmarked portable envelope retains baseline compatibility");
    Bytes current = baseline;
    field(current, PvzpSave::SeedLayoutChunk, envelope(number(PvzpSave::StableSeedLayout)));
    t.Check(preflight(current, true), "explicit stable custom200 seed layout marker accepted");
    Bytes first;
    field(first, PvzpSave::SeedLayoutChunk, envelope(number(PvzpSave::StableSeedLayout)));
    first.insert(first.end(), baseline.begin(), baseline.end());
    t.Check(preflight(first, true), "layout marker accepted independent of chunk ordering");
    Bytes duplicate = current;
    field(duplicate, PvzpSave::SeedLayoutChunk, envelope(number(PvzpSave::StableSeedLayout)));
    t.Check(!preflight(duplicate, true), "duplicate layout marker rejected before import");
    for (uint32_t layout : {0U, 54U, 58U, 201U, 0xFFFFFFFFU}) {
        Bytes bad = baseline;
        field(bad, PvzpSave::SeedLayoutChunk, envelope(number(layout)));
        t.Check(!preflight(bad, false), "unknown and mismatched layout markers rejected without enum migration");
    }
    Bytes conflict = current;
    field(conflict, PvzpSave::SeedLayoutChunk, envelope(number(58)));
    t.Check(!preflight(conflict, true), "conflicting layout markers rejected");
    Bytes repeated = baseline;
    repeated.insert(repeated.end(), baseline.begin(), baseline.end());
    t.Check(!preflight(repeated, false), "duplicate known board chunk rejected");
    Bytes unknown = current;
    field(unknown, 0x12345678U, Bytes{1, 2, 3});
    field(unknown, 0x12345678U, Bytes{4});
    t.Check(preflight(unknown, true), "unknown chunks remain forward-compatible including repeated unknown ids");
    Bytes custom = current;
    field(custom, 0x4D4F4404U, envelope(Bytes{1, 2, 3}));
    t.Check(preflight(custom, true), "custom runtime state chunk has a validated envelope");
    field(custom, 0x4D4F4404U, envelope(Bytes{1}));
    t.Check(!preflight(custom, true), "duplicate custom state chunk rejected");
    for (size_t remove = 1; remove <= 8; ++remove) {
        Bytes truncated = current;
        truncated.resize(truncated.size() - remove);
        t.Check(!preflight(truncated, true), "truncated top-level or known chunk envelope rejected");
    }
    Bytes suffix = current; suffix.push_back(0);
    t.Check(!preflight(suffix, true), "valid chunks followed by malformed suffix rejected before import");
    Bytes oversized = baseline; u32(oversized, 999); u32(oversized, 0xFFFFFFFFU);
    t.Check(!preflight(oversized, false), "oversized TLV declaration rejected without arithmetic overflow");
    Bytes badChunk = envelope(number(0)); badChunk[0] = 2;
    Bytes badVersion; field(badVersion, 1, badChunk);
    t.Check(!preflight(badVersion, false), "unsupported known chunk version rejected");
    Bytes duplicateField = envelope(number(0)); field(duplicateField, 1, number(1));
    Bytes badFields; field(badFields, 1, duplicateField);
    t.Check(!preflight(badFields, false), "duplicate primary chunk field rejected");
    Bytes extended = envelope(number(0)); field(extended, 77, Bytes{1}); field(extended, 77, Bytes{2});
    Bytes extendedPayload; field(extendedPayload, 1, extended);
    t.Check(preflight(extendedPayload, false), "unknown chunk fields remain forward-compatible");
    Bytes markerExtra = number(PvzpSave::StableSeedLayout); markerExtra.push_back(0);
    Bytes extra = baseline; field(extra, PvzpSave::SeedLayoutChunk, envelope(markerExtra));
    t.Check(!preflight(extra, false), "layout marker requires exactly one uint32 value");
    Bytes nested; field(nested, 1, number(1)); field(nested, 100, number(2));
    t.Check(PvzpSave::ValidFields({nested.data(), nested.size()}, 1, true), "known nested object fields validate");
    field(nested, 100, number(3));
    t.Check(!PvzpSave::ValidFields({nested.data(), nested.size()}, 1, true), "duplicate nested tail rejected");
    Bytes future; field(future, 99, Bytes{1}); field(future, 99, Bytes{2});
    t.Check(PvzpSave::ValidFields({future.data(), future.size()}, 1, true), "unknown nested fields may repeat safely");
    future.push_back(0);
    t.Check(!PvzpSave::ValidFields({future.data(), future.size()}, 1, true), "malformed nested field suffix rejected");
    bool valid = PvzpSave::ValidSeedId(-1);
    for (int32_t id = 0; id <= 53; ++id) valid = valid && PvzpSave::ValidSeedId(id);
    for (int32_t id = 55; id <= 75; ++id) valid = valid && PvzpSave::ValidSeedId(id) && PvzpSave::PseudoSeedId(id);
    for (int32_t id = 200; id <= 203; ++id) valid = valid && PvzpSave::ValidSeedId(id);
    t.Check(valid, "all stable plants pseudo seeds custom slots and none sentinel accepted");
    for (int32_t id : {-2147483647 - 1, -2, 54, 76, 79, 80, 199, 204, 2147483647})
        t.Check(!PvzpSave::ValidSeedId(id), "invalid seed sentinel gap and intermediate-only values rejected");
    bool plantContexts = true, imitaterContexts = PvzpSave::ValidImitaterSeedId(-1);
    for (int32_t id = 0; id <= 53; ++id) plantContexts = plantContexts && PvzpSave::ValidPlantSeedId(id);
    for (int32_t id = 200; id <= 203; ++id) plantContexts = plantContexts && PvzpSave::ValidPlantSeedId(id);
    for (int32_t id = 0; id <= 48; ++id) imitaterContexts = imitaterContexts && PvzpSave::ValidImitaterSeedId(id);
    t.Check(plantContexts && imitaterContexts, "plant and imitater contexts accept supported plant ranges");
    bool pseudoRejected = true;
    for (int32_t id = 55; id <= 75; ++id)
        pseudoRejected = pseudoRejected && PvzpSave::ValidSeedId(id) &&
            !PvzpSave::ValidPlantSeedId(id) && !PvzpSave::ValidImitaterSeedId(id);
    t.Check(pseudoRejected, "IZombie pseudo seeds stay valid in packets but cannot become active plants or imitater targets");
    for (int32_t id : {-2, -1, 54, 76, 79, 199, 204, 2147483647})
        t.Check(!PvzpSave::ValidPlantSeedId(id), "active plant rejects none count gaps and intermediate-only ids");
    for (int32_t id : {-2, 49, 53, 54, 76, 200, 201, 202, 203, 204})
        t.Check(!PvzpSave::ValidImitaterSeedId(id), "imitater target rejects non-imitatable and custom plants");
    t.Check(PvzpSave::ValidPacketCount(0, 10) && PvzpSave::ValidPacketCount(10, 10) &&
        !PvzpSave::ValidPacketCount(-1, 10) && !PvzpSave::ValidPacketCount(11, 10),
        "seed packet counts enforce nonnegative capacity bounds");
    t.Check(PvzpSave::ValidBlobSize(0, 0) && PvzpSave::ValidBlobSize(PvzpSave::MaxBlobBytes, PvzpSave::MaxBlobBytes) &&
        !PvzpSave::ValidBlobSize(PvzpSave::MaxBlobBytes + 1, PvzpSave::MaxBlobBytes + 1) &&
        !PvzpSave::ValidBlobSize(5, 4), "nested blob cap and remaining-byte bounds enforced before allocation");
    PvzpSave::TLVReader reader(nullptr, 0);
    const uint8_t* ptr = nullptr;
    t.Check(!reader.ReadBytes(ptr, static_cast<size_t>(-1)) && !reader.mOk,
        "TLV length arithmetic rejects size_t overflow");
}

void PathsAndMissing(Checks& t, const fs::path& tmp) {
    const fs::path absent = tmp / "no-such-mods";
    (void)PvzpMod::LoadFromDirectory(absent); // Missing directory is allowed, regardless of scan status.
    t.Check(PvzpMod::List().empty() && !PvzpMod::HasMods() &&
        PvzpMod::GlobalInt("startingSun", 321) == 321,
        "missing mod directory is harmless and clears previous data");

    const fs::path root = tmp / "paths";
    Write(root / "mod.json", R"({"schemaVersion":2,"id":"org.bad.root","globals":{"startingSun":1}})");
    Write(root / "nested" / "deeper" / "mod.json",
        R"({"schemaVersion":2,"id":"org.bad.nested","globals":{"startingSun":2}})");
    const fs::path outside = tmp / "outside";
    Write(outside / "mod.json",
        R"({"schemaVersion":2,"id":"org.bad.outside","globals":{"startingSun":3}})");
    Write(root / "safe" / "mod.json",
        R"({"schemaVersion":2,"id":"org.test.safe","globals":{"startingSun":100}})");
    std::error_code error;
    fs::create_directory_symlink(outside, root / "linked-dir", error);
    const bool dirLinkCreated = !error;
    error.clear();
    fs::create_directories(root / "linked-file");
    fs::create_symlink(outside / "mod.json", root / "linked-file" / "mod.json", error);
    const bool fileLinkCreated = !error;
    t.Check(PvzpMod::LoadFromDirectory(root), "directory traversal scan succeeds");
    t.Check(PvzpMod::List().size() == 1 &&
        PvzpMod::GlobalInt("startingSun", -1) == 100,
        "only direct, real, in-tree mod.json can override");
    if (dirLinkCreated) t.Check(HasDiagnostic("linked-dir"), "symlink folder diagnosed");
    if (fileLinkCreated) t.Check(HasDiagnostic("linked-file"), "symlink manifest diagnosed");
    if (!dirLinkCreated || !fileLinkCreated)
        std::fprintf(stderr, "MODTEST SKIP symlink creation denied by filesystem; other traversal tests ran\n");
}
} // namespace

int PvzpModRunTests() {
    Checks t;
    try {
        Scratch scratch;
        LegacyAndModern(t, scratch.dir);
        PriorityAndToggles(t, scratch.dir);
        InvalidJsonAndFields(t, scratch.dir);
        ParserSafety(t, scratch.dir);
        Aliases(t, scratch.dir);
        RangeBoundaries(t, scratch.dir);
        DuplicateAndDisabled(t, scratch.dir);
        SamplePacks(t, scratch.dir);
        FunPacks(t, scratch.dir);
        CustomPlantPack(t, scratch.dir);
        NewContentPacks(t, scratch.dir);
        CustomPlantSecurity(t, scratch.dir);
        CustomPlantFingerprintsAndReset(t, scratch.dir);
        CustomPlantCollisions(t, scratch.dir);
        DefinitionCacheReset(t, scratch.dir);
        ZombieVariantFields(t, scratch.dir);
        PortableSaveValidation(t);
        PathsAndMissing(t, scratch.dir);
    } catch (const std::exception& error) {
        t.Check(false, error.what());
    }
    std::fprintf(stderr, "MODTEST RESULT %d checks, %d failures\n", t.checks, t.failures);
    return t.failures ? 1 : 0;
}
