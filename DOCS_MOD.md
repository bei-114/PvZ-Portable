# PvZ-Portable 数据模组（schemaVersion 2）

支持已有植物/僵尸的数值与外观覆盖，以及 **4 个自定义植物槽位**。植物逻辑由引擎提供受限行为与事件词汇，不执行任意脚本。不是任意第三方 PvZ MOD 的兼容层。

## 安装

将每个包整个文件夹解压到 `mods/<包文件夹>/mod.json`，然后完全退出并重启游戏。不直接读取 ZIP，不支持运行中热重载。

- Android：`/sdcard/Android/data/io.github.wszqkzqk.pvzportable/files/mods/`。Android 11+ 的目录授权需通过用户允许的文件管理器完成。
- 桌面：用户数据目录中的 `io.github.wszqkzqk/PvZPortable/mods/`，可用 `-savedir` 指定用户数据目录；`-resdir` 仅指定素材目录。
- iOS：应用 Documents 的 `mods/`。

只导入想玩的包。平衡包会相互覆盖，不推荐无差别全部启用。游戏仍需玩家自行提供合法获得的素材；本仓库与模组 ZIP 不分发 `main.pak`、图片或音乐。

## 格式和合并

UTF-8 严格 JSON，无注释、重复键、尾逗号或尾随字符；文件最多 64 KiB，嵌套最多 32 层。未知字段、实体名、类型或越界值拒绝整个包，不影响其他有效包。

```json
{
  "schemaVersion": 2,
  "id": "org.example.balance",
  "name": "Balance",
  "priority": 10,
  "enabled": true,
  "plants": {"PEASHOOTER": {"cost": 50, "refresh": 750}},
  "zombies": {"ZOMBIE": {"health": 250}},
  "globals": {"startingSun": 100}
}
```

`priority` 范围 -1000..1000，默认 0。按优先级、文件夹名从小到大稳定加载，后加载的同字段取胜。`id` 全局唯一；重复 ID 诊断并跳过。`enabled` 默认 true。旧版没有 schemaVersion/id 的纯数值包仍兼容，建议补齐稳定 ID。内部 `SetEnabled` 和快照版本 API 用于离线工具/测试；不保证运行中现有对象会随之重建。

数值单位为游戏 tick（正常速度约 100 tick/秒），不是毫秒。

| 类别 | 字段 | 范围 |
|---|---|---|
| plants | cost | 0..9990 |
| plants | refresh、launchRate | 1..60000 tick |
| plants | health | 1..10000000 |
| zombies | health | 1..10000000（身体血量，不是头盔/盾牌耐久） |
| zombies | pickWeight | 0..100000（仍受模式/波次限制） |
| zombies | value | 0..10000 |
| zombies | startingLevel | 1..50 |
| zombies | speedPercent | 25..300，默认 100；普通行走实际位移倍率，不改变特殊跳跃轨迹 |
| zombies | sunDrop | 0..1000；正常敌人掉落时追加阳光赏金，沿用一次性掉落标记 |
| zombies | tintR、tintG、tintB | 0..255，默认 255；受冻结、魅惑等状态颜色覆盖 |
| globals | startingSun | 0..9990 |

实体键用真实内部名称，例如 `PEASHOOTER`、`GATLING_PEA`、`NEWSPAPER_ZOMBIE`、`BUCKETHEAD_ZOMBIE`，不是 C++ 枚举名。有同名内部别名的实体会一起改变（如部分僵尸头变体共用 `ZOMBIE`）。赏金不适用于己方魅惑僵尸和“我是僵尸”，且只在原版允许掉落时发生。

## 自定义植物

自定义植物出现在原版风格的选卡第二页。电能机枪仍保留 DBG 入口，不放入这页。未声明的槽位不可选，DBG 也不轮换空槽。

```json
{
  "schemaVersion": 2,
  "id": "org.example.aurora",
  "name": "Aurora Nut",
  "customPlants": [{
    "slot": 3,
    "name": "Aurora Nut",
    "behavior": "nut",
    "tint": "purple",
    "cost": 125,
    "health": 1800,
    "step": 600,
    "events": ["chill:row"],
    "interval": 800,
    "pulseEvents": ["chill:row"],
    "foodEvents": ["chill", "heal:1800", "sun:100"]
  }]
}
```

- slot：0..3，必填。是包共享的固定槽位，不是自动分配的全局植物 ID。
- name：必填，1..40 字节可显示名称。
- behavior：`nut`（默认，不主动攻击）或 `brain`（点击随机事件及大脑大招）。当前不支持任意攻击代码或自选骨骼。
- tint：省略或 gold/blue/red/green/purple。gold/blue 的 nut 还叠加原版礼盒；brain 用原版大脑图替代坚果本体及种子包图。
- cost：0..9990，默认 0；health：1..10000000，默认 6000。
- step：100..10000，默认 1000。累计实际受伤满此数值，从 events 随机选一项；回血不抹掉进度。致死一口也结算最后一档，但 heal 不复活致死植物。
- badChance：0..100，默认 0；有 badEvents 时按百分比从该列表随机选一项，其余走 events。
- interval：0（禁用，默认）或 50..60000 tick。到期顺序执行 pulseEvents；暂停/时间停止/睡眠不推进。
- foodEvents：喂能量豆后依次执行；空列表的 nut 不消费能量豆。brain 使用固定召唤大招。
- events、badEvents、pulseEvents、foodEvents：每个列表最多 16 个严格验证的事件 token。
- 自定义植物开局就绪，种植后仍正常冷却 3000 tick；不是永远无冷却。

不同包使用同一个 slot 时整份植物定义由后加载者覆盖，DBG/日志会指出双方和胜者。不会静默拼接事件列表。当前只有 4 槽；以后扩容必须同时修改数组/缓存/遍历/存档校验和回归测试，不能只加一个枚举。

### 事件词汇

| token | 效果 |
|---|---|
| sun:N | 加阳光，N 为纯十进制数字 0..9990 |
| heal:N | 给当前植物回复 N，0..10000000，上限为最大生命 |
| chill | 减速所有敌方僵尸 |
| chill:row | 仅减速本行敌方僵尸 |
| plant:random | 从限定原版植物池随机选植物，在合法空位生成；不保证包含所有原版植物 |
| zombie:NORMAL/GARGANTUAR/BUNGEE/DIGGER/BALLOON/CONEHEAD/BUCKETHEAD | 在模式允许的行召唤对应敌人（斜杠表示可选值，不是 token 字符） |
| destroy:plant | 随机消灭另一株在场植物 |

例如 `sun:abc`、`heal:-1`、`zombie:TYPO` 都会拒绝包，不再让非法字符串进入游戏后抛异常。

### 大脑行为边界

0 阳光、100 血由包定义。普通空光标点击且未用工具时触发，50 tick 冷却按棋盘隔离并随存档保存。50% 吸引一个实际僵尸类型（包含僵王，已有活僵王时避免重复）；50% 随机花费 100..500 阳光，在本格召唤安全白名单中的魅惑僵尸。阳光不足或容量不足不扣费。魅惑池不含撑杆、跳跳、矿工、飞贼、载具、巨人和关联队伍。能量豆在第一列每行召唤 5 只魅惑橄榄球/铁桶/路障。

僵王及水生/特殊僵尸在非原版场景下仍有行为限制；“所有类型可抽到”不等于每种特殊僵尸跨场景都已穷尽验证。严禁宣传无 bug。

## 推荐体验包

| 包 | 内容 |
|---|---|
| 70-box-nuts | 盲盒与逆盲盒坚果，均有能量豆事件 |
| 80-brain | 大脑，0 阳光/100 血，点击召唤与全场己方大招 |
| 90-aurora-nut | 极光坚果，125 阳光/1800 血，每 800 tick 减速本行；受伤 600 再触发本行减速；大招全场减速、补血、100 阳光 |
| 95-bounty-zombies | 赏金僵尸主题：橙色读报（150% 移速、75 阳光赏金）和冰蓝铁桶（125% 移速、100 阳光赏金）。是原版类型的模组变体，不是独立新 ZombieType |

前四个植物共占 slot0..3，可一起启用。旧的 10/20/30/35/40/50/60 是平衡实验包，按优先级覆盖相应实体，尤其僵尸身体血量可能与赏金主题互相覆盖。

## 存档与兼容

- 固定现行布局：标准植物 0..53、计数值54、伪种子55..75、自定义200..203。编译期断言保护这些值。
- 新存档有独立种子布局标记与自定义状态 chunk，保存受伤进度、周期计时和点击冷却；重复加载不会重新派发已结算事件。
- 旧的无标记存档按现行54计数基线解释。曾把自定义植物插到54..57的过渡版本存在不可判别的歧义，**不能可靠自动修复所有旧档**。日志会提示；建议备份后重新开该局。不要把“兼容旧档”理解成能恢复已经写错的 ID。
- 加载前检查外层 TLV 边界、CRC、布局标记；内部限制数量/分配长度，拒绝非法活动植物 ID。内部加载仍非事务式，失败时不能假设棋盘未改变。
- 模组变化会触发规则指纹提示；缺包/禁用的自定义植物会退回无事件的基础表现，不应在当前局运行中切换定义。

## 开发测试

无素材测试（仓库根目录）：

```sh
cmake --build build -j4
PVZP_MOD_TEST=1 ./build/pvz-portable
```

包含严格解析、恶意 token、优先级、冲突、启停、规则指纹、实际定义缓存刷新、存档布局和 TLV 边界测试。

真实游戏测试需合法素材与 X11，见 `tests/run-live-regression.sh`。`PVZP_CUSTOM_TEST=1` 仅桌面 Linux：自动建测试档案、跳过标题、直接启动日间生存关；脚本自动使用新 HOME，勿手工在真实档案上运行此破坏性测试。真实测试通过实际点击路径验证选卡、飞行、跨页退卡、卡槽 ID、种植后 CD、手套、能量豆、魅惑行走、周期、赏金以及完整存读档。`PVZP_CUSTOM_HOLD=1` 成功后保留画面用于截图。

本地测试通过不代表 Android/Windows 运行时已验证，也不代表僵王/所有场景/所有第三方模组都已兼容。构建结果须以对应 Actions run 的最终结论为准。
