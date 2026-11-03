// SPDX-License-Identifier: LGPL-3.0-or-later
// Run with PVZP_MOD_TEST=1 before constructing LawnApp; needs no main.pak/assets.
#include "PvzpMod.h"
#include "PvzpCustomPlants.h"
#include "PvzpPlayerLevels.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"
#include "../Lawn/System/PortableSaveValidation.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Internal, deterministic storage failure hook: absent from the UI/public API.
namespace PvzpPlayerLevels::ModelTest {
void FailRename(int countdown);
bool CanEditState(bool hasBoard,bool hasChooser,int scene);
}

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
        {"number-underflow", R"({"id":"org.bad.underflow","globals":{"startingSun":1e-10000}})"},
        {"negative-underflow", R"({"id":"org.bad.negative","globals":{"startingSun":-1e-10000}})"},
        {"subnormal-integer", R"({"id":"org.bad.subnormal","globals":{"startingSun":1e-310}})"},
        {"vertical-tab", "\v{\"id\":\"org.bad.vertical\"}"},
        {"form-feed", "{\f\"id\":\"org.bad.form\"}"},
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
            plant.tint == "purple" && plant.cost == 125 && plant.health == 8000 && plant.step == 400 &&
            plant.interval == 400 && plant.events == std::vector<std::string>{"damage:row:600", "heal:1000", "freeze:row", "push:row:120"} &&
            plant.pulseEvents == std::vector<std::string>{"chill:row", "freeze:row", "damage:row:300", "push:row:30"} &&
            plant.foodEvents == std::vector<std::string>{"chill", "freeze:row", "damage:row:1800", "push:row:160", "heal:8000", "sun:250"};
    }
    t.Check(aurora, "Aurora Nut tank8000 step400 pulses every400 ticks with stronger damage push freeze and food");
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

void PrismAndElitePacks(Checks& t, const fs::path& tmp) {
    const fs::path root=tmp/"prism-elite";
    for(const char* name:{"70-box-nuts","80-brain","90-aurora-nut","95-bounty-zombies","100-prism-megaphone","105-elite-invasion"}) CopyFixture(root,name,name);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() && PvzpMod::List().size()==6 && PvzpMod::CustomPlants().size()==5,"all six shipped v6 packs coexist and use five distinct slots");
    bool prism=false;
    for(const auto& p:PvzpMod::CustomPlants()) if(p.slot==4) prism=p.behavior=="prism" && p.base=="MARIGOLD" && p.cost==400 && p.refresh==3000 && p.health==300;
    t.Check(prism,"Prism Megaphone Flower is slot4 marigold support cost400 refresh3000 health300");
    t.Check(GetPlantDefinition(SEED_CUSTOM_4).mReanimationType==gPlantDefs[SEED_MARIGOLD].mReanimationType && GetPlantDefinition(SEED_CUSTOM_4).mSeedCost==400 && GetPlantDefinition(SEED_CUSTOM_4).mRefreshTime==3000 && GetPlantDefinition(SEED_CUSTOM_4).mLaunchRate==0,"prism fixture reaches actual engine definition without marigold attacks or coin production");
    t.Check(PvzpMod::ZombieInt("FOOTBALL_ZOMBIE","helmetHealth",0)==4200 && PvzpMod::ZombieInt("FOOTBALL_ZOMBIE","damagePercent",0)==250 && PvzpMod::ZombieInt("FOOTBALL_ZOMBIE","slowResistancePercent",0)==70,"elite football has armor bite and slow resistance");
    t.Check(PvzpMod::ZombieInt("SCREEN_DOOR_ZOMBIE","shieldHealth",0)==6500 && PvzpMod::ZombieInt("GARGANTUAR","health",0)==12000,"elite shield and giant expose distinct defensive roles");
    t.Check(PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE","helmetHealth",0)==7000 && PvzpMod::ZombieInt("NEWSPAPER_ZOMBIE","damagePercent",0)==350,"elite bucket and newspaper override lower-priority bounty theme");
    const auto fingerprint=PvzpMod::RulesFingerprint();
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.elite-invasion",false) && PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE","health",0)==350 && PvzpMod::ZombieInt("BUCKETHEAD_ZOMBIE","helmetHealth",-1)==-1,"disabling hard pack restores bounty stats without residual armor");
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.elite-invasion",true) && PvzpMod::RulesFingerprint()==fingerprint,"elite toggles restore identical full fingerprint");
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.prism-megaphone",false) && PvzpMod::CustomPlants().size()==4 && !PvzpCustomPlants::IsSlotUsed(SEED_CUSTOM_4),"prism slot disappears without affecting the first four plants");
}

void CompositeCampaignPacks(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "composite-campaign-packs";
    for (const char* name : {"70-box-nuts", "80-brain", "90-aurora-nut", "95-bounty-zombies", "100-prism-megaphone",
        "110-composite-zombies", "120-custom-campaign"}) CopyFixture(root, name, name);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().size() == 7 &&
        PvzpMod::CustomPlants().size() == 5 && PvzpMod::CustomZombies().size() == 8 &&
        PvzpMod::CustomLevels().size() == 3 && PvzpMod::Diagnostics().empty(),
        "seven shipped base composite and campaign packs coexist with five plants eight variants three levels");
    auto unresolved = [] {
        size_t count = 0;
        for (const auto& level : PvzpMod::CustomLevels()) for (const auto& wave : level.waves) for (const auto& spawn : wave.spawns) {
            bool found = false;
            for (const auto& zombie : PvzpMod::CustomZombies()) found = found || zombie.id == spawn.zombie;
            for (const auto& zombie : gZombieDefs) found = found || (zombie.mZombieName && spawn.zombie == zombie.mZombieName);
            if (!found) ++count;
        }
        return count;
    };
    t.Check(unresolved() == 0, "all shipped campaign spawn references resolve to builtin or effective custom stable ids");
    const char* ids[] = {"pvzp.seed_packet_head", "pvzp.tombstone_head", "pvzp.snowpea_head", "pvzp.firepea_head",
        "pvzp.sunflower_head", "pvzp.chomper_head", "pvzp.melon_head", "pvzp.gatling_head"};
    const char* abilities[] = {"seed_jam", "raise", "shoot_ice", "shoot_fire", "heal", "bite", "shoot_melon", "shoot_pea"};
    bool variants = PvzpMod::CustomZombies().size() == 8;
    for (size_t i = 0; variants && i < 8; ++i) variants = PvzpMod::CustomZombies()[i].id == ids[i] &&
        PvzpMod::CustomZombies()[i].ability == abilities[i] && PvzpMod::CustomZombies()[i].base == "ZOMBIE";
    t.Check(variants, "eight composite variants retain fixture identities abilities order and stable builtin base enum");
    const auto& levels = PvzpMod::CustomLevels();
    t.Check(levels.size() == 3 && levels[0].id == "pvzp.seed_heist" && levels[0].startingSun == 900 && levels[0].waves.size() == 6 &&
        levels[1].id == "pvzp.grave_orchestra" && levels[1].startingSun == 1400 && levels[1].waves.size() == 7 &&
        levels[2].id == "pvzp.botanical_siege" && levels[2].startingSun == 2200 && levels[2].waves.size() == 8,
        "three authored campaign levels publish exact increasing resources wave counts and menu order");
    const auto original = PvzpMod::RulesFingerprint();
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.composite-zombies", false) && PvzpMod::CustomZombies().empty() &&
        PvzpMod::CustomLevels().size() == 3 && unresolved() > 0 && PvzpMod::RulesFingerprint() != original &&
        PvzpMod::Diagnostics().empty(), "disabling110 removes variants but leaves campaign for runtime unresolved-reference validation");
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.composite-zombies", true) && unresolved() == 0 &&
        PvzpMod::RulesFingerprint() == original, "reenabling110 resolves all campaign ids and restores exact effective fingerprint");
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.custom-campaign", false) && PvzpMod::CustomLevels().empty() &&
        PvzpMod::CustomZombies().size() == 8 && PvzpMod::CustomPlants().size() == 5,
        "disabling120 leaves all composite variants and prior plant packs intact");
    t.Check(PvzpMod::SetEnabled("org.pvzp.example.custom-campaign", true) && PvzpMod::RulesFingerprint() == original,
        "reenabling120 restores authored campaign fingerprint");
    Write(root / "override" / "mod.json", R"({"id":"org.test.composite-override","priority":111,"customZombies":[{"id":"pvzp.seed_packet_head","name":"Priority Saboteur","health":999,"ability":"none"}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomZombies().size() == 8 &&
        PvzpMod::CustomZombies()[0].name == "Priority Saboteur" && PvzpMod::CustomZombies()[0].health == 999 &&
        PvzpMod::CustomZombies()[0].armor == 0 && unresolved() == 0 && HasDiagnostic("override"),
        "later priority replaces composite whole entry in stable position while campaign id keeps resolving");
    t.Check(PvzpMod::SetEnabled("org.test.composite-override", false) && PvzpMod::CustomZombies()[0].health == 850 &&
        PvzpMod::CustomZombies()[0].armor == 1200 && PvzpMod::CustomZombies()[0].ability == "seed_jam" &&
        PvzpMod::Diagnostics().empty() && PvzpMod::RulesFingerprint() == original,
        "disabling priority override restores full shipped saboteur and removes collision diagnostic");
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
        "zombie:", "zombie:TYPO", "zombie:BOSS", "plant:PEASHOOTER", "destroy:all", "chill:all",
        "freeze", "freeze:all", "freeze:row:1", "damage:row:", "damage:row:-1", "damage:row:+1",
        "damage:row:10001", "damage:row:1.0", "damage:row:1e2", "damage:all:10", "damage:row:10x",
        "push:row:", "push:row:-1", "push:row:161", "push:row:1 ", "push:row:1\\u0000tail",
        "damage:row:99999999999999999999999", "push:row:\\u0661"
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
        R"(,"pulseEvents":"chill")", R"(,"foodEvents":[true])",
        R"(,"base":"PEASHOOTER")", R"(,"base":"wall_nut")", R"(,"base":false)",
        R"(,"refresh":0)", R"(,"refresh":60001)", R"(,"refresh":1.5)", R"(,"refresh":"3000")"
    };
    for (const char* fields : invalidFields) {
        Write(manifest, pack(fields));
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().size() == 1 &&
            PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate"),
            "invalid custom behavior tint interval or event type rejected");
    }
    // Row-effect extensions are numeric-only; all dispatch contexts share validation.
    for (const char* field : {"events", "badEvents", "pulseEvents", "foodEvents"}) {
        Write(manifest, pack(std::string(",\"") + field +
            "\":[\"freeze:row\",\"damage:row:0\",\"damage:row:10000\",\"push:row:0\",\"push:row:160\",\"push:row:00070\"]"));
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
            PvzpMod::CustomPlants().size() == 1, "bounded row effect grammar accepted in every event context");
    }
    // The original supported grammar, including endpoints and benign leading zeros.
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
        PvzpMod::CustomPlants()[0].behavior == "nut" && PvzpMod::CustomPlants()[0].base == "WALL_NUT" &&
        PvzpMod::CustomPlants()[0].refresh == 3000 && PvzpMod::CustomPlants()[0].tint.empty() &&
        PvzpMod::CustomPlants()[0].interval == 0 && PvzpMod::CustomPlants()[0].pulseEvents.empty() &&
        PvzpMod::CustomPlants()[0].foodEvents.empty(), "omitted custom fields retain legacy defaults");
    Write(manifest, R"({"customPlants":[{"slot":0,"name":"bad\u0000name"}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate"),
        "control characters in custom display names rejected");
    Write(manifest, R"({"customPlants":[{"slot":0,"name":"One"},{"slot":0,"name":"Two"}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().empty() && HasDiagnostic("candidate"),
        "duplicate custom slots in one manifest rejected atomically");
}

void ExpandedCustomSlots(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "expanded-custom";
    const fs::path manifest = root / "slots" / "mod.json";
    std::string all = R"({"id":"org.test.slots","customPlants":[)";
    for (int i = 0; i < int(NUM_CUSTOM_PLANT_SLOTS); ++i) {
        if (i) all += ',';
        all += "{\"slot\":" + std::to_string(i) + ",\"name\":\"Slot " + std::to_string(i) +
            "\",\"cost\":" + std::to_string(100 + i) + ",\"refresh\":" + std::to_string(2000 + i) + "}";
    }
    all += "]}";
    Write(manifest, all);
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::CustomPlants().size() == 16, "all sixteen custom slots load together");
    bool independent = true;
    for (int i = 0; i < int(NUM_CUSTOM_PLANT_SLOTS); ++i) {
        const auto seed = static_cast<SeedType>(int(SEED_CUSTOM_FIRST) + i);
        const auto& def = GetPlantDefinition(seed);
        independent = independent && def.mSeedType == seed && def.mSeedCost == 100 + i &&
            def.mRefreshTime == 2000 + i && def.mLaunchRate == 0 && def.mSubClass == SUBCLASS_NORMAL &&
            std::string(def.mPlantName) == "CUSTOM_PLANT_" + std::to_string(i);
    }
    t.Check(independent, "sixteen independent definitions retain custom identity cost and refresh");
    const char* stableName = GetPlantDefinition(SEED_CUSTOM_LAST).mPlantName;
    Write(manifest, R"({"id":"org.test.slots","customPlants":[{"slot":15,"name":"Prism","behavior":"prism","refresh":1}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().size() == 1 &&
        PvzpMod::CustomPlants()[0].base == "MARIGOLD" && PvzpMod::CustomPlants()[0].refresh == 1 &&
        GetPlantDefinition(SEED_CUSTOM_LAST).mReanimationType == REANIM_MARIGOLD &&
        GetPlantDefinition(SEED_CUSTOM_LAST).mLaunchRate == 0 &&
        GetPlantDefinition(SEED_CUSTOM_LAST).mPlantName == stableName &&
        Plant::GetRefreshTime(SEED_CUSTOM_LAST, SEED_NONE) == 1,
        "last slot prism defaults to marigold art without coin production and refreshes cached definition");
    const auto prismFingerprint = PvzpMod::RulesFingerprint();
    Write(manifest, R"({"id":"org.test.slots","customPlants":[{"slot":15,"name":"Prism","behavior":"prism","base":"MARIGOLD","refresh":1}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::RulesFingerprint() == prismFingerprint,
        "explicit and implicit prism art defaults share an effective fingerprint");
    Write(manifest, R"({"id":"org.test.slots","customPlants":[{"slot":15,"name":"Prism","behavior":"prism","base":"WALL_NUT","refresh":60000}]})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        GetPlantDefinition(SEED_CUSTOM_LAST).mReanimationType == REANIM_WALLNUT &&
        GetPlantDefinition(SEED_CUSTOM_LAST).mRefreshTime == 60000 && PvzpMod::RulesFingerprint() != prismFingerprint,
        "explicit prism art override and maximum refresh accepted");
    for (const char* slot : {"-1", "16", "4.5", "\"15\""}) {
        Write(manifest, std::string(R"({"customPlants":[{"slot":)") + slot + R"(,"name":"Bad"}]})");
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::CustomPlants().empty() && HasDiagnostic("slots"),
            "custom slot integer bounds reject negative sixteen fractional and string values");
    }
    t.Check(GetPlantDefinition(SEED_CUSTOM_LAST).mReanimationType == REANIM_WALLNUT &&
        GetPlantDefinition(SEED_CUSTOM_LAST).mSeedCost == 0 && GetPlantDefinition(SEED_CUSTOM_LAST).mRefreshTime == 3000,
        "missing last slot resets to harmless default definition");
}

void CustomPlantFingerprintsAndReset(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "custom-fingerprint";
    const fs::path manifest = root / "custom" / "mod.json";
    const std::string baseline = R"({"id":"org.test.custom","customPlants":[{"slot":0,"name":"Test","behavior":"nut","base":"WALL_NUT","refresh":3000,"tint":"gold","cost":200,"health":6000,"step":1000,"badChance":50,"interval":800,"events":["sun:150","heal:200"],"badEvents":["zombie:DIGGER"],"pulseEvents":["chill:row"],"foodEvents":["chill","sun:100"]}]})";
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
        {R"("base":"WALL_NUT")", R"("base":"MARIGOLD")"}, {R"("refresh":3000)", R"("refresh":2999)"},
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
    Write(manifest, R"({"id":"org.test.variant","zombies":{"ZOMBIE":{"speedPercent":25,"sunDrop":0,"tintR":0,"tintG":255,"tintB":128,"damagePercent":25,"helmetHealth":1,"shieldHealth":1,"slowResistancePercent":0}}})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::ZombieInt("ZOMBIE", "speedPercent", -1) == 25 &&
        PvzpMod::ZombieInt("ZOMBIE", "sunDrop", -1) == 0 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintR", -1) == 0 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintG", -1) == 255 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintB", -1) == 128 &&
        PvzpMod::ZombieInt("ZOMBIE", "damagePercent", -1) == 25 &&
        PvzpMod::ZombieInt("ZOMBIE", "helmetHealth", -1) == 1 &&
        PvzpMod::ZombieInt("ZOMBIE", "shieldHealth", -1) == 1 &&
        PvzpMod::ZombieInt("ZOMBIE", "slowResistancePercent", -1) == 0,
        "generic zombie variant fields accept lower and color bounds");
    const auto fingerprint = PvzpMod::RulesFingerprint();
    Write(manifest, R"({"id":"org.test.variant","zombies":{"ZOMBIE":{"speedPercent":300,"sunDrop":1000,"damagePercent":500,"helmetHealth":10000000,"shieldHealth":10000000,"slowResistancePercent":100}}})");
    t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::Diagnostics().empty() &&
        PvzpMod::ZombieInt("ZOMBIE", "speedPercent", -1) == 300 &&
        PvzpMod::ZombieInt("ZOMBIE", "sunDrop", -1) == 1000 &&
        PvzpMod::ZombieInt("ZOMBIE", "tintR", -1) == -1 &&
        PvzpMod::ZombieInt("ZOMBIE", "damagePercent", -1) == 500 &&
        PvzpMod::ZombieInt("ZOMBIE", "helmetHealth", -1) == 10000000 &&
        PvzpMod::ZombieInt("ZOMBIE", "shieldHealth", -1) == 10000000 &&
        PvzpMod::ZombieInt("ZOMBIE", "slowResistancePercent", -1) == 100 && PvzpMod::RulesFingerprint() != fingerprint,
        "generic zombie variant maximums accepted and missing colors reset");
    for (const char* fields : {R"("speedPercent":24)", R"("speedPercent":301)", R"("sunDrop":-1)",
        R"("sunDrop":1001)", R"("tintR":-1)", R"("tintG":256)", R"("tintB":1.5)", R"("tintR":"0")",
        R"("damagePercent":24)", R"("damagePercent":501)", R"("damagePercent":100.5)",
        R"("helmetHealth":0)", R"("helmetHealth":10000001)", R"("helmetHealth":"100")",
        R"("shieldHealth":0)", R"("shieldHealth":10000001)", R"("shieldHealth":false)",
        R"("slowResistancePercent":-1)", R"("slowResistancePercent":101)", R"("slowResistancePercent":0.5)"}) {
        Write(manifest, std::string(R"({"id":"org.test.variant","zombies":{"ZOMBIE":{)") + fields + "}}}");
        t.Check(PvzpMod::LoadFromDirectory(root) && PvzpMod::List().empty() && HasDiagnostic("variant"),
            "invalid zombie variant integer bounds and types rejected atomically");
    }
}

void CustomZombieAndLevelRegistries(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "custom-registries";
    const fs::path manifest = root / "registry" / "mod.json";
    auto write = [&](const std::string& fields) {
        Write(manifest, std::string(R"({"id":"org.test.registry",)") + fields + "}");
        return PvzpMod::LoadFromDirectory(root);
    };
    const std::string zombie = R"({"id":"test.synthetic-v1","name":"合成僵尸"})";
    const std::string level = R"({"id":"test.level-1","name":"合成关卡","waves":[{"spawns":[{"zombie":"test.synthetic-v1"}]}]})";
    t.Check(write("\"customZombies\":[" + zombie + "]") && PvzpMod::HasMods() &&
        PvzpMod::Diagnostics().empty() && PvzpMod::CustomZombies().size() == 1 &&
        PvzpMod::CustomLevels().empty(), "custom-zombie-only pack activates independent stable-id registry");
    const auto c = PvzpMod::CustomZombies().at(0);
    t.Check(c.id == "test.synthetic-v1" && c.name == "合成僵尸" && c.base == "ZOMBIE" &&
        c.head == "seed_packet" && c.ability == "none" && c.health == 600 && c.armor == 0 &&
        c.interval == 600 && c.magnitude == 30 && c.speedPercent == 100 && c.damagePercent == 100 &&
        c.slowResistancePercent == 0 && c.sunDrop == 0 && c.tintR == 255 && c.tintG == 255 && c.tintB == 255,
        "custom zombie defaults and UTF-8 display name are published exactly");
    t.Check(write("\"customLevels\":[" + level + "]") && PvzpMod::HasMods() &&
        PvzpMod::Diagnostics().empty() && PvzpMod::CustomLevels().size() == 1 &&
        PvzpMod::CustomZombies().empty(), "level-only pack accepts syntactic unresolved forward reference");
    const auto l = PvzpMod::CustomLevels().at(0);
    t.Check(l.startingSun == 300 && l.description.empty() && l.waves.size() == 1 &&
        l.waves[0].delay == 800 && l.waves[0].spawns[0].zombie == "test.synthetic-v1" &&
        l.waves[0].spawns[0].row == -1 && l.waves[0].spawns[0].count == 1,
        "custom level and spawn defaults preserve stable reference and random row sentinel");
    t.Check(write(R"("customZombies":[{"id":"bounds","name":"Bounds","health":1,"armor":0,"interval":50,"magnitude":1,"speedPercent":25,"damagePercent":25,"slowResistancePercent":0,"sunDrop":0,"tintR":0,"tintG":0,"tintB":0}])") &&
        PvzpMod::Diagnostics().empty() && PvzpMod::CustomZombies().at(0).health == 1,
        "custom zombie lower numeric bounds accepted");
    t.Check(write(R"("customZombies":[{"id":"bounds","name":"Bounds","health":10000000,"armor":10000000,"interval":60000,"magnitude":1000,"speedPercent":300,"damagePercent":500,"slowResistancePercent":100,"sunDrop":1000,"tintR":255,"tintG":255,"tintB":255}])") &&
        PvzpMod::Diagnostics().empty() && PvzpMod::CustomZombies().at(0).armor == 10000000,
        "custom zombie upper numeric bounds accepted");
    for (const char* base : {"ZOMBIE", "CONEHEAD_ZOMBIE", "BUCKETHEAD_ZOMBIE", "FOOTBALL_ZOMBIE", "SCREEN_DOOR_ZOMBIE", "NEWSPAPER_ZOMBIE"}) {
        t.Check(write(std::string(R"("customZombies":[{"id":"choice","name":"Choice","base":")") + base + "\"}]") &&
            PvzpMod::CustomZombies().size() == 1, "custom zombie permitted base accepted without inserting enums");
    }
    for (const char* head : {"seed_packet", "tombstone", "pea", "snowpea", "firepea", "sunflower", "chomper", "melon", "gatling", "pumpkin", "coffee", "garlic", "cherry", "magnet", "tallnut"}) {
        t.Check(write(std::string(R"("customZombies":[{"id":"choice","name":"Choice","head":")") + head + "\"}]") &&
            PvzpMod::CustomZombies().size() == 1, "custom zombie whitelisted composite head accepted");
    }
    for (const char* ability : {"none", "seed_jam", "raise", "shoot_ice", "shoot_fire", "heal", "bite", "shoot_melon", "shoot_pea", "shield_aura", "rally", "lane_dash", "cherry_charge", "magnet_steal", "rebound"}) {
        t.Check(write(std::string(R"("customZombies":[{"id":"choice","name":"Choice","ability":")") + ability + "\"}]") &&
            PvzpMod::CustomZombies().size() == 1, "custom zombie whitelisted ability accepted");
    }
    const std::vector<std::string> badZombies = {
        "null", "{}", "[null]", "[{}]", "[{\"id\":\"only-id\"}]",
        "[" + zombie + "," + zombie + "]",
        R"([{"id":"../escape/","name":"Bad"}])", R"([{"id":"bad id","name":"Bad"}])",
        R"([{"id":"bad\u0000id","name":"Bad"}])", R"([{"id":"bad","name":"\u0000"}])",
        R"([{"id":"bad","name":"Bad","base":"GARGANTUAR"}])",
        R"([{"id":"bad","name":"Bad","head":"arbitrary"}])",
        R"([{"id":"bad","name":"Bad","ability":"arbitrary"}])",
        "[{\"id\":\"" + std::string(97, 'a') + "\",\"name\":\"Bad\"}]",
        "[{\"id\":\"bad\",\"name\":\"" + std::string(97, 'a') + "\"}]",
    };
    for (const auto& invalid : badZombies) {
        t.Check(write("\"customZombies\":" + invalid) && PvzpMod::List().empty() && HasDiagnostic("registry"),
            "custom zombie invalid type identity name duplicate and whitelist rejected atomically");
    }
    for (const char* field : {R"("health":0)", R"("health":10000001)", R"("armor":-1)", R"("armor":10000001)",
        R"("interval":49)", R"("interval":60001)", R"("magnitude":0)", R"("magnitude":1001)",
        R"("speedPercent":24)", R"("speedPercent":301)", R"("damagePercent":24)", R"("damagePercent":501)",
        R"("slowResistancePercent":-1)", R"("slowResistancePercent":101)", R"("sunDrop":-1)", R"("sunDrop":1001)",
        R"("tintR":-1)", R"("tintG":256)", R"("tintB":false)", R"("health":1.5)", R"("health":"600")", R"("unknown":1)"}) {
        t.Check(write(std::string(R"("customZombies":[{"id":"bad","name":"Bad",)") + field + "}]") &&
            PvzpMod::List().empty() && HasDiagnostic("registry"), "custom zombie numeric range type fraction and unknown field rejected");
    }
    const std::vector<std::string> badLevels = {
        "null", "{}", "[null]", "[{}]", "[" + level + "," + level + "]",
        R"([{"id":"bad/level","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"([{"id":"bad","name":"Bad","description":"\u0000","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[]}])", R"([{"id":"bad","name":"Bad","waves":[{}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"delay":0,"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"delay":60001,"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"bad/ref"}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE","row":-2}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE","row":5}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE","count":0}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE","count":51}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE","count":26},{"zombie":"ZOMBIE","count":25}]}]}])",
        R"([{"id":"bad","name":"Bad","startingSun":9991,"waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"([{"id":"bad","name":"Bad","mystery":1,"waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"mystery":1,"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"([{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE","mystery":1}]}]}])",
    };
    for (const auto& invalid : badLevels) {
        t.Check(write("\"customLevels\":" + invalid) && PvzpMod::List().empty() && HasDiagnostic("registry"),
            "custom level invalid schema range total and nested unknown fields rejected atomically");
    }
    t.Check(write("\"customZombies\":[{\"id\":\"" + std::string(96, 'i') + "\",\"name\":\"" +
        std::string(96, 'n') + "\"}]") && PvzpMod::CustomZombies().size() == 1,
        "custom zombie identity and display name accept exact 96-byte limits");
    const std::string longLevel = "\"customLevels\":[{\"id\":\"" + std::string(128, 'i') +
        "\",\"name\":\"" + std::string(128, 'n') + "\",\"description\":\"" + std::string(1024, 'd') +
        R"(","startingSun":9990,"waves":[{"delay":60000,"spawns":[{"zombie":"ZOMBIE","row":0}]}]}])";
    t.Check(write(longLevel) && PvzpMod::CustomLevels().size() == 1 &&
        PvzpMod::CustomLevels()[0].description.size() == 1024,
        "custom level exact id name description and upper sun delay bounds accepted");
    for (const std::pair<const char*, int> field : {std::pair<const char*, int>{"id",129}, {"name",129}, {"description",1025}}) {
        std::string invalid = R"("customLevels":[{"id":"bad","name":"Bad","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}])";
        if (std::string(field.first) == "description") invalid += ",\"description\":\"" + std::string(field.second, 'x') + "\"";
        else {
            const std::string needle = std::string("\"") + field.first + "\":\"" + (std::string(field.first) == "id" ? "bad" : "Bad") + "\"";
            const auto offset = invalid.find(needle);
            invalid.replace(offset, needle.size(), std::string("\"") + field.first + "\":\"" + std::string(field.second, 'x') + "\"");
        }
        t.Check(write(invalid + "}]") && PvzpMod::List().empty(), "custom level oversized id name or description rejected");
    }
    auto many = [](int count, const std::string& item) {
        std::string array = "[";
        for (int i = 0; i < count; ++i) { if(i) array += ','; array += item; }
        return array + "]";
    };
    for (const char* section : {"customZombies", "customLevels"}) {
        std::string array = "[";
        for (int i = 0; i < 65; ++i) {
            if(i) array += ',';
            array += "{\"id\":\"id" + std::to_string(i) + "\",\"name\":\"Name\"";
            if (std::string(section) == "customLevels") array += R"(,"waves":[{"spawns":[{"zombie":"ZOMBIE"}]}])";
            array += '}';
            if (i == 63) t.Check(write("\"" + std::string(section) + "\":" + array + "]") &&
                PvzpMod::Diagnostics().empty(), "custom registry accepts exact 64-entry limit");
        }
        t.Check(write("\"" + std::string(section) + "\":" + array + "]") && PvzpMod::List().empty() &&
            HasDiagnostic("registry"), "custom registry rejects 65 entries");
    }
    const std::string wave = R"({"delay":1,"spawns":[{"zombie":"ZOMBIE","row":4,"count":50}]})";
    t.Check(write(R"("customLevels":[{"id":"bounds","name":"Bounds","startingSun":0,"waves":)" + many(100, wave) + "}]") &&
        PvzpMod::CustomLevels().size() == 1 && PvzpMod::CustomLevels()[0].waves.size() == 100,
        "custom level accepts 100 waves and 50 zombies per wave at valid row boundary");
    t.Check(write(R"("customLevels":[{"id":"bounds","name":"Bounds","waves":)" + many(101, wave) + "}]") &&
        PvzpMod::List().empty(), "custom level rejects 101 waves");
    t.Check(write(R"("customLevels":[{"id":"bounds","name":"Bounds","waves":[{"spawns":)" + many(50, R"({"zombie":"ZOMBIE"})") + "}]}]") &&
        PvzpMod::CustomLevels().size() == 1, "custom wave accepts 50 distinct spawn records");
    t.Check(write(R"("customLevels":[{"id":"bounds","name":"Bounds","waves":[{"spawns":)" + many(51, R"({"zombie":"ZOMBIE"})") + "}]}]") &&
        PvzpMod::List().empty(), "custom wave rejects 51 spawn records");

    // The early campaign references a custom id defined by a later-priority pack.
    const fs::path cross = tmp / "cross-registry";
    Write(cross / "a-levels" / "mod.json", "{\"id\":\"org.cross.levels\",\"customLevels\":[" + level + "]}");
    Write(cross / "z-zombies" / "mod.json", "{\"id\":\"org.cross.zombies\",\"priority\":10,\"customZombies\":[" + zombie + "]}");
    t.Check(PvzpMod::LoadFromDirectory(cross) && PvzpMod::List().size() == 2 &&
        PvzpMod::CustomLevels().size() == 1 && PvzpMod::CustomZombies().size() == 1 &&
        PvzpMod::Diagnostics().empty(), "cross-mod forward references survive complete snapshot rebuild");
    const auto crossHash = PvzpMod::RulesFingerprint();
    const auto revision = PvzpMod::SnapshotRevision();
    t.Check(PvzpMod::SetEnabled("org.cross.zombies", false) && PvzpMod::CustomZombies().empty() &&
        PvzpMod::CustomLevels().size() == 1 && PvzpMod::HasMods() && PvzpMod::SnapshotRevision() > revision,
        "disabling variant pack retains unresolved campaign for runtime CanStart validation");
    t.Check(PvzpMod::SetEnabled("org.cross.zombies", true) && PvzpMod::RulesFingerprint() == crossHash,
        "reenabling forward definition restores complete effective fingerprint");

    const fs::path collisions = tmp / "registry-collisions";
    Write(collisions / "a" / "mod.json", "{\"id\":\"org.registry.a\",\"customZombies\":[" + zombie + "],\"customLevels\":[" + level + "]}");
    Write(collisions / "z" / "mod.json", R"({"id":"org.registry.z","priority":5,"customZombies":[{"id":"test.synthetic-v1","name":"Winner","health":700}],"customLevels":[{"id":"test.level-1","name":"Winner","startingSun":10,"waves":[{"spawns":[{"zombie":"ZOMBIE","count":2}]}]}]})");
    t.Check(PvzpMod::LoadFromDirectory(collisions) && PvzpMod::CustomZombies().size() == 1 &&
        PvzpMod::CustomLevels().size() == 1 && PvzpMod::CustomZombies()[0].name == "Winner" &&
        PvzpMod::CustomLevels()[0].startingSun == 10 && PvzpMod::Diagnostics().size() == 2,
        "stable zombie and level id collisions replace whole entries with two diagnostics");
    const auto collisionHash = PvzpMod::RulesFingerprint();
    t.Check(PvzpMod::SetEnabled("org.registry.z", false) && PvzpMod::CustomZombies()[0].name == "合成僵尸" &&
        PvzpMod::CustomLevels()[0].startingSun == 300 && PvzpMod::Diagnostics().empty(),
        "disabling collision winner restores previous entries and removes transient warnings");
    t.Check(PvzpMod::SetEnabled("org.registry.z", true) && PvzpMod::Diagnostics().size() == 2 &&
        PvzpMod::RulesFingerprint() == collisionHash, "reenabling collisions regenerates stable diagnostics without accumulating");
    t.Check(PvzpMod::LoadFromDirectory(tmp / "no-custom-registry") && PvzpMod::CustomZombies().empty() &&
        PvzpMod::CustomLevels().empty() && !PvzpMod::HasMods(), "empty load resets both custom registries and HasMods");
}

void CustomRegistryFingerprints(Checks& t, const fs::path& tmp) {
    const fs::path root = tmp / "registry-hash";
    const fs::path manifest = root / "hash" / "mod.json";
    auto hash = [&](const std::string& content) {
        Write(manifest, "{\"id\":\"org.registry.hash\"," + content + "}");
        PvzpMod::LoadFromDirectory(root);
        return PvzpMod::RulesFingerprint();
    };
    const std::string zombie = R"("customZombies":[{"id":"synthetic","name":"Synthetic"})";
    const auto baseline = hash(zombie + "]");
    for (const char* field : {R"("health":601)", R"("armor":1)", R"("interval":601)", R"("magnitude":31)",
        R"("speedPercent":101)", R"("damagePercent":101)", R"("slowResistancePercent":1)", R"("sunDrop":1)",
        R"("tintR":254)", R"("tintG":254)", R"("tintB":254)", R"("base":"CONEHEAD_ZOMBIE")",
        R"("head":"pea")", R"("ability":"shoot_pea")"}) {
        std::string changed = zombie; changed.pop_back(); changed += ','; changed += field; changed += "}]";
        t.Check(hash(changed) != baseline && PvzpMod::CustomZombies().size() == 1,
            "every custom zombie optional field changes fingerprint");
    }
    t.Check(hash(R"("customZombies":[{"id":"different","name":"Synthetic"}])") != baseline,
        "custom zombie stable identity changes fingerprint");
    t.Check(hash(R"("customZombies":[{"id":"synthetic","name":"Different"}])") != baseline,
        "custom zombie display name changes fingerprint");
    const std::string a = R"({"id":"a","name":"A"})", b = R"({"id":"b","name":"B"})";
    const auto ordered = hash("\"customZombies\":[" + a + "," + b + "]");
    t.Check(hash("\"customZombies\":[" + b + "," + a + "]") != ordered,
        "custom zombie registry first-encounter order participates in fingerprint");
    const std::string prefix = R"("customLevels":[{"id":"level","name":"Level")";
    const std::string wave = R"({"spawns":[{"zombie":"ZOMBIE"}]})";
    const auto levelBaseline = hash(prefix + ",\"waves\":[" + wave + "]}]");
    for (const char* field : {R"("description":"Different")", R"("startingSun":301)"}) {
        t.Check(hash(prefix + "," + field + ",\"waves\":[" + wave + "]}]") != levelBaseline,
            "custom level descriptive and sun fields change fingerprint");
    }
    for (const char* changed : {
        R"("customLevels":[{"id":"different","name":"Level","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"("customLevels":[{"id":"level","name":"Different","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"("customLevels":[{"id":"level","name":"Level","waves":[{"delay":801,"spawns":[{"zombie":"ZOMBIE"}]}]}])",
        R"("customLevels":[{"id":"level","name":"Level","waves":[{"spawns":[{"zombie":"CONEHEAD_ZOMBIE"}]}]}])",
        R"("customLevels":[{"id":"level","name":"Level","waves":[{"spawns":[{"zombie":"ZOMBIE","row":0}]}]}])",
        R"("customLevels":[{"id":"level","name":"Level","waves":[{"spawns":[{"zombie":"ZOMBIE","count":2}]}]}])"}) {
        t.Check(hash(changed) != levelBaseline && PvzpMod::CustomLevels().size() == 1,
            "custom level identity wave and spawn fields all change fingerprint");
    }
    const std::string alternate = R"({"spawns":[{"zombie":"CONEHEAD_ZOMBIE"}]})";
    const auto waves = hash(prefix + ",\"waves\":[" + wave + "," + alternate + "]}]");
    t.Check(hash(prefix + ",\"waves\":[" + alternate + "," + wave + "]}]") != waves,
        "custom level wave order is gameplay-significant in fingerprint");
    const std::string one = R"({"zombie":"ZOMBIE"})", two = R"({"zombie":"CONEHEAD_ZOMBIE"})";
    const auto spawns = hash(prefix + ",\"waves\":[{\"spawns\":[" + one + "," + two + "]}]}]");
    t.Check(hash(prefix + ",\"waves\":[{\"spawns\":[" + two + "," + one + "]}]}]") != spawns,
        "custom level spawn sequence is fingerprinted without sorting");
    const std::string firstLevel = R"({"id":"a","name":"A","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]})";
    const std::string secondLevel = R"({"id":"b","name":"B","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]})";
    const auto campaignOrder = hash("\"customLevels\":[" + firstLevel + "," + secondLevel + "]");
    t.Check(hash("\"customLevels\":[" + secondLevel + "," + firstLevel + "]") != campaignOrder,
        "custom level menu and campaign registry order participates in fingerprint");
    t.Check(hash(R"("globals":{"startingSun":0e-10000})") && PvzpMod::List().size() == 1 &&
        PvzpMod::GlobalInt("startingSun", -1) == 0, "zero with tiny exponent is exact zero not nonzero underflow");
    t.Check(hash(R"("globals":{"startingSun":1e2})") && PvzpMod::GlobalInt("startingSun", -1) == 100,
        "locale-independent number parser retains valid integer exponent support");
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
    for (int32_t id = 200; id <= 215; ++id) valid = valid && PvzpSave::ValidSeedId(id);
    t.Check(valid, "all stable plants pseudo seeds custom slots and none sentinel accepted");
    for (int32_t id : {-2147483647 - 1, -2, 54, 76, 79, 80, 199, 216, 2147483647})
        t.Check(!PvzpSave::ValidSeedId(id), "invalid seed sentinel gap and intermediate-only values rejected");
    bool plantContexts = true, imitaterContexts = PvzpSave::ValidImitaterSeedId(-1);
    for (int32_t id = 0; id <= 53; ++id) plantContexts = plantContexts && PvzpSave::ValidPlantSeedId(id);
    for (int32_t id = 200; id <= 215; ++id) plantContexts = plantContexts && PvzpSave::ValidPlantSeedId(id);
    for (int32_t id = 0; id <= 48; ++id) imitaterContexts = imitaterContexts && PvzpSave::ValidImitaterSeedId(id);
    t.Check(plantContexts && imitaterContexts, "plant and imitater contexts accept supported plant ranges");
    bool pseudoRejected = true;
    for (int32_t id = 55; id <= 75; ++id)
        pseudoRejected = pseudoRejected && PvzpSave::ValidSeedId(id) &&
            !PvzpSave::ValidPlantSeedId(id) && !PvzpSave::ValidImitaterSeedId(id);
    t.Check(pseudoRejected, "IZombie pseudo seeds stay valid in packets but cannot become active plants or imitater targets");
    for (int32_t id : {-2, -1, 54, 76, 79, 199, 216, 2147483647})
        t.Check(!PvzpSave::ValidPlantSeedId(id), "active plant rejects none count gaps and intermediate-only ids");
    for (int32_t id : {-2, 49, 53, 54, 76, 200, 201, 202, 203, 204, 215, 216})
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

std::string PlayerFileText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void PlayerLevelModel(Checks& t, const fs::path& tmp) {
    namespace P = PvzpPlayerLevels;
    t.Check(PvzpMod::LoadFromDirectory(tmp / "player-model-mods"), "player model starts from isolated mod snapshot");
    t.Check(bool(P::Initialize(tmp / "player-model")) && P::Levels().empty() && P::Diagnostics().empty(),
        "explicit empty player root creates empty isolated catalog");
    auto draft = P::NewDraft();
    t.Check(bool(P::Validate(draft)) && P::IsPlayerId(draft.id) && draft.startingSun == 500 &&
        draft.waves.size() == 1 && draft.waves[0].spawns[0].zombie == "ZOMBIE",
        "new draft has generated player ID and valid one-wave 500-sun default");
    std::set<std::string> ids;
    for (int i = 0; i < 100; ++i) ids.insert(P::NewDraft().id);
    t.Check(ids.size() == 100 && !P::CanEdit(nullptr), "generated stable ASCII IDs are unique and null app cannot edit");
    for (int scene = -1; scene <= 8; ++scene) {
        t.Check(P::ModelTest::CanEditState(false, false, scene) == (scene == static_cast<int>(GameScenes::SCENE_MENU)) &&
            !P::ModelTest::CanEditState(true, false, scene) && !P::ModelTest::CanEditState(false, true, scene),
            "editor guard accepts only main-menu scene with no board or seed chooser");
    }
    for (const char* id : {"", "player.", "bundled.id", "player...x", "player../x", "player.a/b", "player.a\\b", "player.a:b", "player.\n"})
        t.Check(!P::IsPlayerId(id) && !P::Remove(id), "unsafe IDs cannot escape the player namespace");
    draft.name = "Quoted \"name\" \\ 中文 🌻";
    draft.description = std::string("line\n\t\r") + char(1) + "\\ \" 🌻";
    t.Check(bool(P::Save(draft)) && P::Find(draft.id) && P::Levels().size() == 1,
        "UTF-8 and JSON escaping model saves immediately");
    const std::string exported = P::ExportJson(draft);
    t.Check(exported.find("\\u0001") != std::string::npos && exported.find("\\\"") != std::string::npos &&
        exported.find("\\\\") != std::string::npos && exported.find("\"plants\"") == std::string::npos,
        "export escapes controls quotes and backslashes without executable/rule sections");
    t.Check(bool(P::Initialize(P::Directory())) && P::Find(draft.id) &&
        P::ExportJson(*P::Find(draft.id)) == exported, "strict parse/export roundtrip preserves every level field");
    auto copy = P::CopyDraft(draft);
    t.Check(copy.id != draft.id && copy.name.ends_with(" (copy)") && copy.waves[0].spawns[0].zombie == "ZOMBIE",
        "copy has independent stable ID and retains wave content");
    auto unicode = draft; unicode.name.clear();
    for (int i = 0; i < 42; ++i) unicode.name += "花";
    auto bounded = P::CopyDraft(unicode);
    t.Check(bounded.name.size() <= 128 && bounded.name.ends_with(" (copy)") && bool(P::Validate(bounded)),
        "copy suffix truncates only at UTF-8 boundary within byte budget");
    auto boundary = draft;
    boundary.id = "player." + std::string(121, 'x'); boundary.name = std::string(128, 'n');
    boundary.description = std::string(1024, 'd'); boundary.startingSun = 9990;
    boundary.waves.assign(30, {60000, std::vector<PvzpMod::CustomSpawn>(12, {"ZOMBIE", 4, 1})});
    t.Check(bool(P::Validate(boundary)), "all model ID text sun wave delay group row upper bounds accepted");
    boundary.waves = {{1, {{"ZOMBIE", -1, 50}}}}; boundary.startingSun = 0;
    t.Check(bool(P::Validate(boundary)), "zero sun delay1 random row count50 boundary accepted");
    const auto invalid = [&](auto mutate) {
        auto level = draft; mutate(level);
        const auto before = P::ExportJson(*P::Find(draft.id));
        const auto revision = PvzpMod::SnapshotRevision();
        t.Check(!P::Validate(level) && !P::Save(level) &&
            P::ExportJson(*P::Find(draft.id)) == before && PvzpMod::SnapshotRevision() == revision,
            "invalid model mutation fails validation/save without publishing partial changes");
    };
    invalid([](auto& c) { c.id = "player." + std::string(122, 'x'); });
    invalid([](auto& c) { c.name = ""; });
    invalid([](auto& c) { c.name.assign(129, 'n'); });
    invalid([](auto& c) { c.name = "bad\nname"; });
    invalid([](auto& c) { c.name = std::string("bad\0name", 8); });
    invalid([](auto& c) { c.name = std::string("\xc0\xaf", 2); });
    invalid([](auto& c) { c.description.assign(1025, 'd'); });
    invalid([](auto& c) { c.description = std::string("\xed\xa0\x80", 3); });
    invalid([](auto& c) { c.startingSun = -1; });
    invalid([](auto& c) { c.startingSun = 9991; });
    invalid([](auto& c) { c.waves.clear(); });
    invalid([](auto& c) { c.waves.assign(31, c.waves.front()); });
    invalid([](auto& c) { c.waves[0].delay = 0; });
    invalid([](auto& c) { c.waves[0].delay = 60001; });
    invalid([](auto& c) { c.waves[0].spawns.clear(); });
    invalid([](auto& c) { c.waves[0].spawns.assign(13, {"ZOMBIE", -1, 1}); });
    invalid([](auto& c) { c.waves[0].spawns[0].row = -2; });
    invalid([](auto& c) { c.waves[0].spawns[0].row = 5; });
    invalid([](auto& c) { c.waves[0].spawns[0].count = 0; });
    invalid([](auto& c) { c.waves[0].spawns[0].count = 51; });
    invalid([](auto& c) { c.waves[0].spawns = {{"ZOMBIE", 0, 30}, {"ZOMBIE", 1, 21}}; });
    invalid([](auto& c) { c.waves[0].spawns[0].zombie = "missing.reference"; });
    invalid([](auto& c) { c.waves[0].spawns[0].zombie = "BOSS"; });
}

void PlayerLevelStrictFiles(Checks& t, const fs::path& tmp) {
    namespace P = PvzpPlayerLevels;
    PvzpMod::LoadFromDirectory(tmp / "player-files-mods");
    const auto root = tmp / "player-files";
    auto good = P::NewDraft(); good.id = "player.good";
    const std::string json = P::ExportJson(good);
    const std::vector<std::string> bad = {
        "{}", "[]", json + " trailing", "{\"schemaVersion\":2,\"customLevels\":[]}",
        "{\"schemaVersion\":1,\"customLevels\":[]}",
        "{\"schemaVersion\":2,\"schemaVersion\":2,\"customLevels\":[]}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"scripts\":[]}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"plants\":{}}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"zombies\":{}}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"globals\":{}}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"customZombies\":[]}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"customPlants\":[]}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"enabled\":true}",
        "{\"schemaVersion\":2,\"customLevels\":[],\"priority\":1}",
        std::string(65537, ' '), std::string("\xff", 1)
    };
    for (size_t i = 0; i < bad.size(); ++i) {
        const auto caseRoot = root / std::to_string(i);
        Write(caseRoot / (good.id + ".json"), json);
        Write(caseRoot / "player.invalid.json", bad[i]);
        t.Check(bool(P::Initialize(caseRoot)) && P::Levels().size() == 1 && P::Find(good.id) &&
            !P::Diagnostics().empty() && fs::exists(caseRoot / "player.invalid.json"),
            "invalid player envelope/JSON/size retained with diagnostic without wiping valid sibling");
    }
    for (const auto& replacement : std::vector<std::pair<std::string, std::string>>{
        {"\"startingSun\":500", "\"startingSun\":1.5"},
        {"\"startingSun\":500", "\"startingSun\":1e-10000"},
        {"\"startingSun\":500", "\"startingSun\":\"500\""},
        {"\"startingSun\":500", "\"startingSun\":500,\"startingSun\":501"},
        {"\"delay\":800", "\"delay\":0"},
        {"\"row\":-1", "\"row\":5"},
        {"\"count\":1", "\"count\":51"},
        {"\"name\":\"New level\"", "\"name\":\"\\uD800\""},
        {"\"name\":\"New level\"", "\"name\":\"\\u0000\""},
        {"\"count\":1", "\"count\":1,\"script\":\"bad\""}
    }) {
        auto text = json; const auto at = text.find(replacement.first);
        if (at == std::string::npos) throw std::runtime_error("bad player parser test mutation");
        text.replace(at, replacement.first.size(), replacement.second);
        const auto caseRoot = root / "mutated";
        Write(caseRoot / (good.id + ".json"), text);
        t.Check(bool(P::Initialize(caseRoot)) && P::Levels().empty() && !P::Diagnostics().empty(),
            "strict player parser rejects invalid field types ranges duplicate keys escapes and unknown fields");
    }
    std::vector<PvzpMod::CustomLevel> invalidModels;
    auto oversized = good; oversized.waves.assign(31, good.waves.front()); invalidModels.push_back(oversized);
    oversized = good; oversized.waves[0].spawns.assign(13, {"ZOMBIE", -1, 1}); invalidModels.push_back(oversized);
    oversized = good; oversized.waves[0].spawns = {{"ZOMBIE", -1, 25}, {"ZOMBIE", 0, 26}}; invalidModels.push_back(oversized);
    oversized = good; oversized.id = "player.../escape"; invalidModels.push_back(oversized);
    for (const auto& model : invalidModels) {
        const auto caseRoot = root / "structural-limits";
        Write(caseRoot / "player.good.json", P::ExportJson(model));
        t.Check(bool(P::Initialize(caseRoot)) && P::Levels().empty() && !P::Diagnostics().empty(),
            "structural file parse enforces editor wave/group/total/path limits independently of Save");
    }
    auto doubled = json;
    const auto lastEntry = doubled.rfind("]}");
    const auto firstEntry = doubled.find("[{" );
    std::string entry = doubled.substr(firstEntry + 1, lastEntry - firstEntry - 1);
    entry.replace(entry.find(good.id), good.id.size(), "player.second");
    doubled.insert(lastEntry, "," + entry);
    Write(root / "two-levels" / "player.good.json", doubled);
    t.Check(bool(P::Initialize(root / "two-levels")) && P::Levels().empty() && !P::Diagnostics().empty(),
        "single player file cannot contain two customLevels even when individually valid");
    const auto exactRoot = root / "exact64";
    auto padded = json; padded.resize(65536, ' '); Write(exactRoot / (good.id + ".json"), padded);
    t.Check(bool(P::Initialize(exactRoot)) && P::Levels().size() == 1,
        "player file accepts exact 64 KiB boundary");
    const auto duplicateRoot = root / "duplicates";
    Write(duplicateRoot / (good.id + ".json"), json);
    Write(duplicateRoot / "player.alias.json", json);
    t.Check(bool(P::Initialize(duplicateRoot)) && P::Levels().size() == 1 && !P::Diagnostics().empty() &&
        fs::exists(duplicateRoot / "player.alias.json"), "filename mismatch/duplicate identity rejected and retained");
    auto missing = good; missing.id = "player.missing"; missing.waves[0].spawns[0].zombie = "org.test.absent";
    Write(duplicateRoot / (missing.id + ".json"), P::ExportJson(missing));
    t.Check(bool(P::Initialize(duplicateRoot)) && P::Find(missing.id) && bool(P::Validate(missing, false)) &&
        !P::Validate(missing, true) && !P::Save(missing), "missing dependency structurally loads for repair but cannot play/save");
    missing.waves[0].spawns[0].zombie = "ZOMBIE";
    t.Check(bool(P::Save(missing)) && bool(P::Validate(*P::Find(missing.id))), "repair resolves missing dependency and saves immediately");
    const auto capRoot = root / "cap";
    for (int i = 0; i < 33; ++i) {
        auto level = good; level.id = "player.cap" + std::to_string(100 + i);
        Write(capRoot / (level.id + ".json"), P::ExportJson(level));
    }
    t.Check(bool(P::Initialize(capRoot)) && P::Levels().size() == 32 && !P::Diagnostics().empty() &&
        fs::exists(capRoot / "player.cap132.json"), "33rd valid player file retained with explicit max32 diagnostic");
    auto extra = good; extra.id = "player.extra";
    const auto revision = PvzpMod::SnapshotRevision();
    t.Check(!P::Save(extra) && P::Levels().size() == 32 && !fs::exists(capRoot / "player.extra.json") &&
        PvzpMod::SnapshotRevision() == revision, "save at catalog capacity cannot write or publish partial content");
    t.Check(bool(P::Initialize(root / "empty")) && P::Levels().empty() && PvzpMod::CustomLevels().empty() &&
        P::Diagnostics().empty(), "reload empty player root clears old publication and stale diagnostics");
}

void PlayerLevelTransactionsAndPaths(Checks& t, const fs::path& tmp) {
    namespace P = PvzpPlayerLevels;
    PvzpMod::LoadFromDirectory(tmp / "player-atomic-mods");
    const auto root = tmp / "player-atomic";
    t.Check(bool(P::Initialize(root)), "atomic player tests initialize explicit scratch root");
    auto level = P::NewDraft(); level.id = "player.atomic";
    t.Check(bool(P::Save(level)), "atomic baseline saves");
    const auto target = root / (level.id + ".json");
    const auto temp = root / (level.id + ".json.tmp");
    const auto backup = root / (level.id + ".json.bak");
    const auto original = PlayerFileText(target);
    level.name = "Changed";
    for (const auto& obstacle : {temp, backup}) {
        fs::create_directory(obstacle);
        const auto revision = PvzpMod::SnapshotRevision();
        t.Check(!P::Save(level) && PlayerFileText(target) == original && P::Find(level.id)->name != level.name &&
            PvzpMod::SnapshotRevision() == revision, "temporary/backup obstruction leaves file model catalog and revision untouched");
        if (obstacle == backup)
            t.Check(!P::Remove(level.id) && fs::exists(target), "blocked removal leaves original target present");
        fs::remove(obstacle);
    }
    for (int failureAt : {1, 2}) {
        const auto revision = PvzpMod::SnapshotRevision();
        P::ModelTest::FailRename(failureAt);
        t.Check(!P::Save(level) && PlayerFileText(target) == original && !fs::exists(temp) && !fs::exists(backup) &&
            P::Find(level.id)->name != level.name && PvzpMod::SnapshotRevision() == revision,
            "failure before/after backup rename rolls back original file and leaves catalog revision untouched");
    }
    auto newLevel = level; newLevel.id = "player.newfailure";
    const auto newRevision = PvzpMod::SnapshotRevision();
    P::ModelTest::FailRename(1);
    t.Check(!P::Save(newLevel) && !P::Find(newLevel.id) && !fs::exists(root / "player.newfailure.json") &&
        !fs::exists(root / "player.newfailure.json.tmp") && PvzpMod::SnapshotRevision() == newRevision,
        "failed new-file commit leaves neither disk artifacts nor catalog publication");
    P::ModelTest::FailRename(1);
    t.Check(!P::Remove(level.id) && PlayerFileText(target) == original && P::Find(level.id),
        "injected remove rename failure preserves disk and published model");
    Write(temp, "foreign transaction");
    t.Check(!P::Save(level) && PlayerFileText(temp) == "foreign transaction" && PlayerFileText(target) == original,
        "preexisting transaction file is never truncated or removed");
    fs::remove(temp);
    t.Check(bool(P::Save(level)) && P::Find(level.id)->name == "Changed" && !fs::exists(temp) && !fs::exists(backup),
        "successful replacement publishes new model and cleans transaction artifacts");
    auto unowned = level; unowned.id = "player.unowned";
    Write(root / "player.unowned.json", "invalid file retained");
    t.Check(!P::Save(unowned) && PlayerFileText(root / "player.unowned.json") == "invalid file retained",
        "save cannot overwrite invalid unowned file by guessed player ID");
    const auto revision = PvzpMod::SnapshotRevision();
    t.Check(!P::Initialize(root / ".." / "escape") && P::Directory() == fs::absolute(root) &&
        PvzpMod::SnapshotRevision() == revision && !P::Remove("player.../escape"),
        "root traversal and ID traversal fail without switching published catalog");
    const auto outside = tmp / "player-outside";
    fs::create_directories(outside); Write(outside / "sentinel", "untouched");
    std::error_code ec;
    fs::create_directory_symlink(outside, tmp / "player-linked-root", ec);
    if (!ec) {
        t.Check(!P::Initialize(tmp / "player-linked-root") && !P::Initialize(tmp / "player-linked-root" / "nested") &&
            P::Directory() == fs::absolute(root), "direct and ancestor symlink player roots refused");
    }
    ec.clear(); fs::create_symlink(outside / "sentinel", root / "player.link.json", ec);
    if (!ec) {
        t.Check(bool(P::Initialize(root)) && !P::Find("player.link") && !P::Diagnostics().empty() &&
            PlayerFileText(outside / "sentinel") == "untouched", "symlink JSON diagnosed without following or modifying target");
        auto linked = level; linked.id = "player.link";
        t.Check(!P::Save(linked) && PlayerFileText(outside / "sentinel") == "untouched", "save refuses symlink target");
    }
    ec.clear(); fs::create_symlink(outside / "sentinel", temp, ec);
    if (!ec) {
        t.Check(!P::Save(level) && PlayerFileText(outside / "sentinel") == "untouched", "save refuses symlink temporary file");
        fs::remove(temp);
    }
    ec.clear(); fs::create_symlink(outside / "sentinel", backup, ec);
    if (!ec) {
        t.Check(!P::Save(level) && !P::Remove(level.id) && PlayerFileText(outside / "sentinel") == "untouched",
            "save/remove refuse symlink backup without touching external file");
        fs::remove(backup);
    }
    // Replace a previously loaded own target to exercise write-time, not only scan-time, validation.
    fs::remove(target); ec.clear(); fs::create_symlink(outside / "sentinel", target, ec);
    if (!ec) {
        t.Check(!P::Save(level) && !P::Remove(level.id) && PlayerFileText(outside / "sentinel") == "untouched",
            "post-load symlink substitution cannot redirect Save or Remove");
        fs::remove(target); Write(target, P::ExportJson(level));
    } else Write(target, P::ExportJson(level));
    t.Check(bool(P::Remove(level.id)) && !P::Find(level.id) && !fs::exists(target) && !fs::exists(backup) &&
        !P::Remove(level.id), "remove deletes only own ID file and immediately removes catalog entry");
    t.Check(fs::exists(root / "player.unowned.json") && PlayerFileText(outside / "sentinel") == "untouched",
        "invalid neighboring files and external sentinel survive all transactions");
}

void PlayerPublicationAndFingerprints(Checks& t, const fs::path& tmp) {
    namespace P = PvzpPlayerLevels;
    const auto mods = tmp / "player-fingerprints-mods";
    const auto manifest = mods / "pack" / "mod.json";
    const std::string baseline = R"({"schemaVersion":2,"id":"org.test.playerfp","priority":7,"plants":{"PEASHOOTER":{"cost":321}},"zombies":{"ZOMBIE":{"pickWeight":5432}},"globals":{"startingSun":150},"customPlants":[{"slot":0,"name":"Cache","cost":432}],"customZombies":[{"id":"org.test.actor","name":"Actor"}],"customLevels":[{"id":"org.test.bundled","name":"Bundled","waves":[{"spawns":[{"zombie":"ZOMBIE"}]}]}]})";
    Write(manifest, baseline);
    t.Check(PvzpMod::LoadFromDirectory(mods) && bool(P::Initialize(tmp / "player-fingerprints")),
        "player fingerprint tests isolate both content roots");
    const auto rules = PvzpMod::RulesFingerprint(); const auto gameplay = PvzpMod::GameplayFingerprint();
    const auto revision = PvzpMod::SnapshotRevision();
    const int cost = GetPlantDefinition(SEED_PEASHOOTER).mSeedCost;
    const int customCost = GetPlantDefinition(SEED_CUSTOM_0).mSeedCost;
    const int weight = GetZombieDefinition(ZOMBIE_NORMAL).mPickWeight;
    auto level = P::NewDraft(); level.waves[0].spawns[0].zombie = "org.test.actor";
    t.Check(bool(P::Save(level)) && PvzpMod::CustomLevels().size() == 2 &&
        PvzpMod::CustomLevels()[0].id == "org.test.bundled" && PvzpMod::SnapshotRevision() > revision &&
        PvzpMod::RulesFingerprint() == rules && PvzpMod::GameplayFingerprint() == gameplay,
        "player publication merges after bundled catalog changes only snapshot revision not either fingerprint");
    level.waves[0].delay += 1; level.name = "Unrelated editor change";
    t.Check(bool(P::Save(level)) && PvzpMod::RulesFingerprint() == rules && PvzpMod::GameplayFingerprint() == gameplay &&
        GetPlantDefinition(SEED_PEASHOOTER).mSeedCost == cost && GetPlantDefinition(SEED_CUSTOM_0).mSeedCost == customCost &&
        GetZombieDefinition(ZOMBIE_NORMAL).mPickWeight == weight && PvzpMod::GlobalInt("startingSun", 0) == 150,
        "player edits cannot alter effective entity/global rules or derived definition cache values");
    const auto failedRevision = PvzpMod::SnapshotRevision();
    auto foreign = level; foreign.id = "org.test.bundled";
    t.Check(!PvzpMod::PublishPlayerLevels({foreign}) && !PvzpMod::PublishPlayerLevels({level, level}) &&
        PvzpMod::SnapshotRevision() == failedRevision && PvzpMod::CustomLevels().size() == 2,
        "publication rejects nonplayer overrides and duplicate player IDs atomically");
    t.Check(PvzpMod::SetEnabled("org.test.playerfp", false) && PvzpMod::CustomLevels().size() == 1 &&
        PvzpMod::CustomLevels()[0].id == level.id && PvzpMod::SetEnabled("org.test.playerfp", true) &&
        PvzpMod::RulesFingerprint() == rules && PvzpMod::GameplayFingerprint() == gameplay,
        "bundled toggles rebuild rules without discarding independent player publication");
    t.Check(PvzpMod::LoadFromDirectory(mods) && PvzpMod::CustomLevels().size() == 1 &&
        PvzpMod::CustomLevels()[0].id == "org.test.bundled", "tool mod reload resets player publication without reading player filesystem");
    auto changed = baseline;
    auto at = changed.find("\"name\":\"Bundled\""); changed.replace(at, std::string("\"name\":\"Bundled\"").size(), "\"name\":\"Renamed\"");
    Write(manifest, changed);
    t.Check(PvzpMod::LoadFromDirectory(mods) && PvzpMod::RulesFingerprint() != rules && PvzpMod::GameplayFingerprint() == gameplay,
        "bundled level metadata remains in unchanged legacy v7 hash but excluded from gameplay hash");
    for (const auto& field : std::vector<std::pair<std::string, std::string>>{
        {"\"cost\":321", "\"cost\":322"}, {"\"pickWeight\":5432", "\"pickWeight\":5433"},
        {"\"startingSun\":150", "\"startingSun\":151"}, {"\"cost\":432", "\"cost\":433"},
        {"\"name\":\"Actor\"", "\"name\":\"Actor2\""}, {"\"priority\":7", "\"priority\":8"},
        {"org.test.playerfp", "org.test.playerfp2"}
    }) {
        changed = baseline; at = changed.find(field.first); changed.replace(at, field.first.size(), field.second);
        Write(manifest, changed);
        t.Check(PvzpMod::LoadFromDirectory(mods) && PvzpMod::GameplayFingerprint() != gameplay,
            "gameplay hash includes effective normal/custom plant zombie global definitions and enabled priority metadata");
    }
    auto collision = P::NewDraft(); collision.id = "player.bundledcollision";
    // Use the exact exported envelope as a bundled pack: manifest accepts schema2/customLevels.
    Write(manifest, P::ExportJson(collision));
    t.Check(PvzpMod::LoadFromDirectory(mods) && bool(P::Initialize(tmp / "player-collision")) && !P::Save(collision) &&
        !fs::exists(P::Directory() / (collision.id + ".json")), "even a bundled player-namespace ID cannot be overridden by editor storage");
    Write(P::Directory() / (collision.id + ".json"), P::ExportJson(collision));
    auto safe = collision; safe.id = "player.safe";
    Write(P::Directory() / (safe.id + ".json"), P::ExportJson(safe));
    t.Check(bool(P::Initialize(P::Directory())) && P::Levels().size() == 1 && P::Find(safe.id) &&
        !P::Diagnostics().empty() && fs::exists(P::Directory() / (collision.id + ".json")),
        "bundled-ID collision file retained with diagnostic without wiping valid player siblings");
}

void PlayerModelMutationSequence(Checks& t, const fs::path& tmp) {
    namespace P = PvzpPlayerLevels;
    PvzpMod::LoadFromDirectory(tmp / "player-sequence-mods");
    const auto root = tmp / "player-sequence";
    t.Check(bool(P::Initialize(root)), "model mutation sequence initializes isolated storage");
    std::map<std::string, std::string> expected;
    bool valid = true;
    for (int step = 0; step < 80 && valid; ++step) {
        if (step % 5 == 0 && !expected.empty()) {
            const auto id = expected.begin()->first;
            valid = bool(P::Remove(id)); expected.erase(id);
        } else {
            PvzpMod::CustomLevel level;
            if (step % 3 == 0 && !P::Levels().empty()) level = *P::Find(expected.rbegin()->first);
            else if (step % 3 == 1 && !P::Levels().empty()) level = P::CopyDraft(P::Levels().front());
            else level = P::NewDraft();
            level.name = "Mutation " + std::to_string(step);
            level.waves[0].delay = 1 + step; level.waves[0].spawns[0].count = 1 + step % 50;
            valid = bool(P::Save(level)); if (valid) expected[level.id] = P::ExportJson(level);
        }
        if (step % 4 == 0) valid = valid && bool(P::Initialize(root));
        valid = valid && P::Levels().size() == expected.size() && PvzpMod::CustomLevels().size() == expected.size();
        for (const auto& [id, json] : expected) valid = valid && P::Find(id) && P::ExportJson(*P::Find(id)) == json;
        t.Check(valid, "deterministic save copy edit remove reload model mutation sequence matches reference map");
    }
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
        PrismAndElitePacks(t, scratch.dir);
        CompositeCampaignPacks(t, scratch.dir);
        CustomPlantSecurity(t, scratch.dir);
        ExpandedCustomSlots(t, scratch.dir);
        CustomPlantFingerprintsAndReset(t, scratch.dir);
        CustomPlantCollisions(t, scratch.dir);
        DefinitionCacheReset(t, scratch.dir);
        ZombieVariantFields(t, scratch.dir);
        CustomZombieAndLevelRegistries(t, scratch.dir);
        CustomRegistryFingerprints(t, scratch.dir);
        PortableSaveValidation(t);
        PathsAndMissing(t, scratch.dir);
        PlayerLevelModel(t, scratch.dir);
        PlayerLevelStrictFiles(t, scratch.dir);
        PlayerLevelTransactionsAndPaths(t, scratch.dir);
        PlayerPublicationAndFingerprints(t, scratch.dir);
        PlayerModelMutationSequence(t, scratch.dir);
    } catch (const std::exception& error) {
        t.Check(false, error.what());
    }
    std::fprintf(stderr, "MODTEST RESULT %d checks, %d failures\n", t.checks, t.failures);
    return t.failures ? 1 : 0;
}
