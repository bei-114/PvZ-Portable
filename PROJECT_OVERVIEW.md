# PvZ-Portable MOD 项目梗概（交接给 Codex）

本仓库是在 `wszqkzqk/PvZ-Portable`（PvZ1 原生 C++ 重制）基础上做的可扩展 MOD 工程。
目标：**数据驱动加植物/僵尸/规则**，同时保留原版美术与手感；Android（APK）+ Windows 双端。

## 1. 快速上手

```bash
# 桌面构建
cmake --build build -j6
./build/pvz-portable

# 模组加载器回归（无需开游戏窗口）
PVZP_MOD_TEST=1 ./build/pvz-portable     # 期望 "MODTEST RESULT 152 checks, 0 failures"

# 本地测试用开发开关（不设则完全不生效）
PVZP_DEV_UNLOCK=1    # 启动即全解档案（跳过教程链）
PVZP_DEV_CHOOSER=1   # 任意关卡强制弹出选卡界面
```

APK 由 GitHub Actions 构建：`.github/workflows/apk2.yml`，产物 artifact 名 `pvz-mod-apk`。
fork：`bei-114/PvZ-Portable`。推送走 GitHub Data API（blobs → tree → commit → ref PATCH），再 `workflows/.../dispatches`。

## 2. 目录与关键文件

| 路径 | 作用 |
| --- | --- |
| `src/PvzpLib/PvzpMod.{h,cpp}` | 模组加载器（严格 JSON、白名单、优先级合并、诊断） |
| `src/PvzpLib/PvzpCustomPlants.{h,cpp}` | **数据驱动自定义植物**：4 个通用槽位、事件、大脑行为 |
| `src/PvzpLib/PvzpPlantFood.{h,cpp}` | 能量豆技能注册表（原版 50 项技能） |
| `src/PvzpLib/PvzpModUI.cpp` | 游戏内 DBG 面板（4 页） |
| `src/PvzpLib/PvzpModTests.cpp` | 加载器回归 152 项 |
| `src/Lawn/Widget/SeedChooserScreen.cpp` | 选卡界面 + 第二页（MOD PLANTS） |
| `src/Lawn/Plant.cpp` | 植物定义表 `gPlantDefs`、自定义表 `gCustomPlantDefs`、颜色/绘制 |
| `src/Lawn/System/SaveGame.cpp` | 存档 v4（TLV chunk） |
| `tests/mods/*/mod.json` | 示例模组包 |
| `DOCS_MOD.md` | 模组格式文档（需与代码同步维护） |

## 3. 模组格式（schemaVersion 2）

```json
{
  "schemaVersion": 2,
  "id": "org.pvzp.example.brain",
  "name": "Brain",
  "priority": 80,
  "enabled": true,
  "plants":   { "PEASHOOTER":  { "cost": 25, "health": 300, "launchRate": 100 } },
  "zombies":  { "ZOMBIE":      { "health": 120 } },
  "globals":  { "startingSun": 150 },
  "customPlants": [
    {
      "slot": 2,                 // 0..3
      "name": "Brain",
      "behavior": "brain",       // "nut"(默认) | "brain"
      "tint": "red",             // gold|blue|red|green|purple
      "cost": 0,
      "health": 100,
      "step": 1000,              // 每掉多少血触发一次事件（nut 行为）
      "events":    ["sun:150", "plant:random", "heal:1200", "chill"],
      "badEvents": ["zombie:GARGANTUAR", "destroy:plant"],
      "badChance": 50
    }
  ]
}
```

- 事件词汇：`sun:N` `heal:N` `chill` `plant:random` `zombie:<类型>` `destroy:plant`
- 优先级小 → 大合并，后者覆盖前者；同 id 去重
- 严格解析：拒绝未知字段、重复键、越界值、尾随字节

## 4. 枚举布局（重要）

```cpp
SEED_IMITATER = 48, ... SEED_ELECTRIC_GATLINGPEA = 53,
NUM_SEED_TYPES = 54,          // 原版植物到此结束
... 伪种子（僵尸卡/老虎机/水族馆）...
SEED_CUSTOM_0 = 200,          // 模组植物固定从这里开始
SEED_CUSTOM_1, SEED_CUSTOM_2, SEED_CUSTOM_3,
NUM_SEED_SLOTS = 204
```

**规则：任何新内容只能往后追加（≥204），绝对不能插在原版 ID 中间。**
存档按数字存种子类型，插队会让旧存档错位（曾导致“我是僵尸”卡槽变成奖杯/灰团）。

数组按 204 建：`SeedChooserScreen::mChosenSeeds[NUM_SEED_SLOTS]`。
植物定义分两张表：原版 `gPlantDefs[54]`、模组 `gCustomPlantDefs[4]`（`GetPlantDefinition` 先判断自定义段）。
植物图缓存 `ReanimatorCache` 会把 200+ 映射回基础植物（坚果），防止越界。

## 5. 已实现的玩法

### 自定义植物（模组数据）
- **盲盒坚果**（slot 0）：200 阳光 / 6000 血，每掉 1000 血触发正面事件；礼盒贴图叠加 `IMAGE_PRESENT`
- **逆盲盒坚果**（slot 1）：0 阳光 / 6000 血，50% 走负面事件（巨人/蹦极/矿工/毁植物/气球）
- **大脑**（slot 2，`behavior: "brain"`）：0 阳光 / 100 血，不攻击，只画 `IMAGE_BRAIN`
  - 点击：50% 招来随机敌方僵尸（含僵王，走 `AddZombie(ZOMBIE_BOSS)`）；50% 消耗 100~500 阳光，在自己格子召唤随机**魅惑僵尸**
  - 能量豆大招：第一列每格召唤 5 只魅惑僵尸（仅橄榄球/铁桶/路障）
  - 点击有 0.5 秒冷却；不抢能量豆/手套手势

### 选卡第二页
底部按钮 `MOD PLANTS >`；只显示被模组声明过的槽位；卡片复用原版 `DrawSeedPacket`；
自定义植物初始 `mRefreshTime = 0`（开局就绪）。

### 其他
- 电能机枪豌豆（`SEED_ELECTRIC_GATLINGPEA`）：青绿色着色，DBG 可种
- DBG 4 页：阳光/植物/僵尸/手套/能量豆/完成关卡/全解/模组诊断

## 6. 已知问题 / 坑

1. **PvZ1 是“动画大于一切”**：加植物要在十多个 `switch (mSeedType)` 里补分支（射击、枪口、头部动画、缓存图、伤害贴图、升级判定），漏一个就变小苗/停火。
2. 大脑曾被坚果骨骼包裹 → 已改为 `Plant::Draw` 直接画大脑并 `return`；不要再用“给骨骼调 alpha”的思路（`Reanimation` 没有 alpha/scale 字段）。
3. 魅惑撑杆僵尸在前两列起跳会跳进房子导致判负 → 已从魅惑召唤池移除撑杆。
4. 手套与大脑点击的优先级：手套活跃时（`PvzpModTools::IsActive`）必须跳过大脑点击。
5. 旧存档兼容：不要动 0~199 的枚举；伪种子段（54~81）也不能动。
6. iOS CI 目前失败（与本工程改动无关），Android/Windows/macOS/WebAssembly 均通过。

## 7. 开发约定

- 文本用中文注释；提交信息用 `feat:` / `fix:` / `refactor:` 前缀
- 每次改动后跑 `PVZP_MOD_TEST=1`，并尽量用 `PVZP_DEV_UNLOCK/PVZP_DEV_CHOOSER` 实机截图验证
- 手机测试：`/sdcard/Download/DSH-Go/` 下放 mod zip；`mods/` 位于
  `Android/data/io.github.wszqkzqk.pvzportable/files/mods/`
- 不要把版权素材（`main.pak` / `properties`）提交进仓库或 artifact

## 8. 建议的下一步

- 大脑：召唤动画/音效、按阳光档位区分强度、把“僵王”改为概率更低
- 自定义植物：支持 `base` 字段（不止坚果）、支持 `attack` 行为
- 选卡第二页：自定义卡面（现在是借坚果卡 + 叠图）
- 存档：给自定义植物加独立 chunk，支持按 id 恢复
- 文档：`DOCS_MOD.md` 增补 `customPlants` 与事件词汇表
