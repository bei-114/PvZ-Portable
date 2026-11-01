// SPDX-License-Identifier: LGPL-3.0-or-later
// Run with PVZP_MOD_TEST=1 before constructing LawnApp; needs no main.pak/assets.
#include "PvzpMod.h"
#include "../Lawn/Plant.h"
#include "../Lawn/Zombie.h"

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
        PathsAndMissing(t, scratch.dir);
    } catch (const std::exception& error) {
        t.Check(false, error.what());
    }
    std::fprintf(stderr, "MODTEST RESULT %d checks, %d failures\n", t.checks, t.failures);
    return t.failures ? 1 : 0;
}
