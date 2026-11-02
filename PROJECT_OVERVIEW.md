# PvZ-Portable MOD 交接梗概

## 项目与当前实测状态

基于 `wszqkzqk/PvZ-Portable`，用户 fork 为 `bei-114/PvZ-Portable`。Android APK 与 Windows 为主要目标。资源版权与代码分离：不要提交或打包 `main.pak`、玩家原版图片/音乐/properties 素材。

最新一轮本地 Linux 构建、**411 项无素材检查、94 项真实棋盘检查全部通过**。这不是所有模式和平台的无 bug 保证。Android 构建须另看 Actions 的对应最终 run，不能把 dispatch/in_progress 写成已出包。

## 快速验证

```sh
cmake --build build -j4
PVZP_MOD_TEST=1 ./build/pvz-portable
# 需要合法游戏资源及 X11/xvfb-run：
bash tests/run-live-regression.sh ./build/pvz-portable /path/to/resources
# 成功后保留画面用于截图（手动结束测试进程）：
PVZP_CUSTOM_HOLD=1 bash tests/run-live-regression.sh ./build/pvz-portable /path/to/resources
```

脚本自动新建临时 HOME，导入 70/80/90/95 四包，自动跳过标题进入日间生存，保留日志，不删除/覆盖用户档案。不要裸设置 `PVZP_CUSTOM_TEST=1` 在真实档案上启动，它是破坏性的内部集成测试。测试在 Board::Update 末尾分阶段进行，不能递归调用 Update。

真实测试覆盖：原版/自定义混合选卡、自然飞行落地、快速翻页、跨页退卡及重新排序、实际开始按钮、包 ID 保持、种植后正常冷却、大脑暂停输入/手套拖动/喂豆、25 只安全魅惑僵尸向右行走、赏金与移速倍率、极光坚果第800帧周期和能量豆、完整棋盘 Save→Load 保持伤害/周期/冷却/掉落标记。

## 关键文件

| 路径 | 用途 |
|---|---|
| src/PvzpLib/PvzpMod.h/.cpp | 严格 JSON、优先级/诊断、快照版本、规则指纹 |
| src/PvzpLib/PvzpModTests.cpp | 无素材测试，含实际定义缓存及存档格式边界 |
| src/PvzpLib/PvzpCustomPlants.h/.cpp | 4 个自定义槽、受伤/周期/能量豆事件、大脑召唤、棋盘隔离的运行状态及序列化 |
| src/PvzpLib/PvzpCustomRegression.cpp | 可重复真实选卡/棋盘回归 |
| src/PvzpLib/PvzpPlantFood.h/.cpp | 原版50项技能 + 自定义植物能量豆分发 |
| src/PvzpLib/PvzpModTools.cpp | 手套拖动（保留植物身份/状态，不重建对象） |
| src/PvzpLib/PvzpModUI.cpp | DBG、动态过滤空槽、食品UI、重置 |
| src/Lawn/Widget/SeedChooserScreen.cpp/.h | 原版风格模组页、银行/命中/飞行生命周期 |
| src/Lawn/SeedPacket.cpp | 所有种子包共同绘图入口、仅开局免 CD |
| src/Lawn/Plant.cpp | 普通/自定义定义缓存、健康、颜色、绘制 |
| src/Lawn/Zombie.cpp | 数值、移速、染色、一次性赏金，原版魅惑过渡 |
| src/Lawn/System/SaveGame.cpp | portable v4，严格 TLV、种子布局/自定义状态 chunk |
| src/Lawn/System/PortableSaveValidation.h | 共享边界校验与固定ID编译断言 |
| DOCS_MOD.md | 当前完整模组格式、事件和兼容限制 |

APK workflow：`.github/workflows/apk2.yml`，artifact `pvz-mod-apk`。

## 数据与扩展边界

当前 schemaVersion2：plants/zombies/globals 整数覆盖 + customPlants 数组。具体格式以 DOCS_MOD.md 为准。slot0..3 为全局共享固定槽，不是按 mod.id 自动分配。相同槽由优先级胜者整体覆盖并诊断，不会把两个植物拼接。

通用字段：interval、pulseEvents、foodEvents 支持完全由 JSON 组合的新玩法；behavior 仅 nut/brain。事件严格白名单，不允许脚本，不通过 stoi 解释任意用户文本。每个列表最多16项。

SnapshotRevision() 在快照重建时变化，GetPlantDefinition/GetZombieDefinition 据此刷新缓存。实际运行仍要求改包后重启，不能保证已生成实体热更新。

规则指纹包含有效自定义配置全部字段和事件顺序。自定义运行状态以 Board* + 植物 DataArray ID 隔离，重置时清除；计时使用剩余 tick 而不是全局永久表。受伤累计包括致死一口，回血不重发旧档奖励。

## ID 与存档铁律

固定当前布局：植物0..53（电能机枪53）、NUM_SEED_TYPES54、伪种子55..75、自定义200..203、NUM_SEED_SLOTS204。

**不要在中间插枚举**。扩容不仅要加枚举，还要更新独立定义表、缓存/数组/每个遍历、解析范围、存档校验、编译断言和测试。当前渲染基于坚果，不代表任意204+类型已经可用。

新 portable 存档写 MOD03 种子布局标记（layout200）与 MOD04 自定义状态。读档前验证外层 TLV；已知字段精确消费、数量/分配大小有限，拒绝伪种子作为活动 Plant 类型。未知字段保留前向跳过行为。

旧无标记存档按现行计数54基线解释；历史过渡版曾把自定义插到54..57，旧数字歧义无法可靠自动恢复，**不能声称旧档全兼容或都已写坏**。必须告知用户备份/必要时重开。内部加载不是事务式，失败可能已局部改变棋盘。

## 当前内容包

- 70-box-nuts：slot0盲盒、slot1逆盲盒，受伤随机事件；现已补能量豆事件列表。
- 80-brain：slot2大脑，0阳光100血，普通点击50/50敌人/花100..500阳光魅惑召唤；大招第一列每行5只橄榄球/铁桶/路障。
- 90-aurora-nut：slot3极光坚果，125阳光1800血，800tick减速本行、受伤600再减速；大招全场减速+回血+100阳光。
- 95-bounty-zombies：橙色读报（身体400、150%行走、75阳光赏金）及冰蓝铁桶（身体350、125%行走、100赏金）。这是**替换既有类型的模组变体**，不是两个独立新 ZombieType。

电能机枪仍为 DBG-only，不混进原版紫卡/模组选择页。DBG 植物表从有效枚举/实际启用配置生成，不许再写54/55等“魔法数字”。

## 已修重要错误

- 自定义只被绘制但不参与 Update/SeedHitTest/银行查询；飞行不结束导致全部控件锁死。
- 跨页银行卡无法退回、未配置槽出现在DBG、选择不足时索引SEED_NONE。
- 初CD通过 mRefreshTime=0误实现为永久无CD；现只在 SetPacketType 初始就绪，种下后3000tick。
- 大脑抢走工具点击；现和电能机枪共用 Board 鼠标命中之后的安全优先级。
- 大脑坚果本体与包图叠图不一致；种子包统一入口直接画大脑，不改共享 Graphics 缩放。
- 魅惑撑杆/关联队伍不安全；友方白名单仅普通/路障/铁桶/橄榄球/铁门/读报，调用 StartMindControlled，不只写bool。
- 自定义事件数字异常、非法 tint、指纹漏字段、定义缓存永久不刷新。
- 加载器忽略部分截断/重复TLV与不受限数量，修复并加畸形输入测试。

## 仍需推进/不要过度承诺

1. 敌方随机池虽覆盖实际类型，僵王/水生/载具跨场景行为未穷尽；应再加跨地形和Boss专项测试。
2. 当前自定义外观仍限制于坚果骨架+现有贴图；尚无任意 base 注册和独立自定义僵尸注册。
3. 需要更广的 Android 触摸/不同屏幕尺寸回归及现有原版50技能语义验证。
4. 仍应改善冲突界面、独立模组浏览/导入体验、中文字体和本地化。
5. CI失败不能无证据归因“与本次无关”，要读具体日志。

交付手机文件放 `/sdcard/Download/DSH-Go/`；应用 mods 目录受 Android scoped storage 限制，复制入 Android/data 最后一步仍需用户文件管理器授权。任何凭证不要写进源码、梗概、ZIP或聊天回复。
