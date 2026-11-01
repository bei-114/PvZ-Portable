# PvZ-Portable 数据模组（mod.json，v0.2）

v0.2 **仅支持数值覆盖**：修改**已存在**的植物、僵尸及关卡初始阳光数值，无须重编译。每个模组占一个独立文件夹，文件名固定为 `mod.json`；**不支持 ZIP 模组包**，请先解压成 `mods/<模组文件夹>/mod.json`。直接放在 `mods/` 下的 JSON、嵌套两级的文件夹及外部链接均不是模组。启动时扫描，修改文件或启停后请**重启游戏**；游戏过程中不会热重载，已有植物/僵尸定义也可能已缓存。

## 放置位置

- **Android：** `Android/data/io.github.wszqkzqk.pvzportable/files/mods/<模组文件夹>/mod.json`，即应用专属外部文件目录下的 `mods/`；**不是** `/sdcard/mods/` 或 APK 内 `assets/`。Android 11+ 对 `Android/data` 的访问受系统限制，应使用允许访问该目录的文件管理方式；导入正版游戏资源仍按主项目 README 操作。
- **Linux / Windows / macOS：** 用户数据目录 `io.github.wszqkzqk/PvZPortable/mods/<模组文件夹>/mod.json`，可用 `-savedir <路径>` 指定用户数据目录。资源目录 `-resdir` 只影响游戏素材位置，不改变模组的用户数据目录。
- **iOS：** 应用 Documents 下的 `mods/<模组文件夹>/mod.json`。

模组与 `main.pak`、`properties/` 分开存放；**玩游戏仍需自行提供合法获得的游戏资源**。本公开代码仓库不含 PopCap/EA 的图片、音乐、字体或 `main.pak`。测试命令只检验 JSON 加载逻辑，可在没有这些玩家素材的源码构建环境中运行。

## JSON 格式（v2）

文件必须是 UTF-8 严格 JSON：字段名与实体名区分大小写；不能写注释、尾随逗号、重复键或 JSON 文本后的额外字符。仅列出要修改的字段；未指定字段使用游戏默认值。示例见 `tests/mods/modern/mod.json`：

```json
{
  "schemaVersion": 2,
  "id": "org.example.balance",
  "name": "Example balance",
  "priority": 10,
  "enabled": true,
  "plants": {
    "PEASHOOTER": { "cost": 50, "refresh": 500, "launchRate": 120, "health": 400 }
  },
  "zombies": {
    "ZOMBIE": { "health": 250, "pickWeight": 4000, "value": 1, "startingLevel": 1 }
  },
  "globals": { "startingSun": 100 }
}
```

`id` 是模组的稳定标识，不是路径；跨文件夹不可重复，不允许 `/`、`..` 等目录遍历。`name` 仅供显示。`priority` 默认为 `0`，范围 `-1000`～`1000`；加载按**优先级从小到大，优先级相同按文件夹名稳定排序**，后加载的模组对同一字段取胜。`enabled` 默认 `true`；设为 `false` 后保留模组清单但不应用任何覆盖，改回 `true` 重启即可。内部 `SetEnabled` 仅供启动前工具/离线测试重算快照，**不是游戏运行时的启停或热更新接口**；实际游戏中请修改 JSON 并重启。不要依赖操作系统的目录遍历顺序。

| 位置 | 字段 | 允许范围 | 单位 / 作用 |
| --- | --- | --- | --- |
| `plants.<名称>` | `cost` | 0～9990 | 阳光 |
|  | `refresh` | 1～60000 | **游戏 tick**，种子包冷却 |
|  | `launchRate` | 1～60000 | **游戏 tick**，攻击/产出周期；并非所有植物都使用 |
|  | `health` | 1～10000000 | 植物基础生命值 |
| `zombies.<名称>` | `health` | 1～10000000 | 僵尸基础生命值 |
|  | `pickWeight` | 0～100000 | 出现权重；仍受关卡规则限制 |
|  | `value` | 0～10000 | 波次预算权重 |
|  | `startingLevel` | 1～50 | 开始出现的关卡 |
| `globals` | `startingSun` | 0～9990 | 开局阳光；可能影响多种模式 |

游戏逻辑通常每秒运行约 100 tick（正常速度下 500 tick 约 5 秒），**不要把 `refresh` / `launchRate` 当成毫秒**。植物/僵尸名称是游戏定义的内部 `mPlantName` / `mZombieName`：例如 `PEASHOOTER`、`SUNFLOWER`、`ZOMBIE`、`CONEHEAD_ZOMBIE`；`ZOMBIE_NORMAL` 是枚举名，**不是**这里的 JSON 名称。若两个现有实体共用同一个内部名称，则数值覆盖会同时影响它们，如 `REPEATER` 也影响左向射手、`ZOMBIE` 也影响部分僵尸变种；当前版本不支持按枚举分别修改这些别名。未知实体名或字段、错误类型、非整数及超出范围的数值会拒绝**整个模组**，并记录带文件夹路径及原因的诊断；其他有效模组仍可加载。`mod.json` 最大 **64 KiB**，JSON 嵌套深度最大 **32 层**。缺失 `mods/` 目录是正常的空模组状态。可直接复制不含游戏素材的完整平衡示例 `tests/mods/garden-balance/mod.json` 到自己的 `mods/garden-balance/mod.json`。

## 附带样例模组

`tests/mods/` 下有三个可直接复制到 `mods/` 的玩法样例，均使用真实内部名称、无版权素材：

| 文件夹 | 优先级 | 效果 |
| --- | --- | --- |
| `10-sunny-lab` | 10 | 初始阳光 300；豌豆射手 80 阳光、血量 420；向日葵冷却 600 tick |
| `20-zombie-march` | 20 | 普通/路障/铁桶僵尸血量 340/600/1000；普通僵尸权重 3000 |
| `30-electric-lab` | 30 | 电能机枪豌豆 275 阳光、冷却 4000、射速 45、血量 600；豌豆射手 65 阳光 |

三个同时启用时，按优先级 10 → 20 → 30 合并，`PEASHOOTER.cost` 最终为 65（30 号包覆盖了 10 号包的 80）。把它们各自整个文件夹复制到 `mods/` 下重启游戏即可。开局阳光类改动会影响多种模式，测试前建议备份存档。

## 兼容性与限制

早期格式（v1）没有 `schemaVersion` / `id`：仍可写 `name`、`priority`、`plants`、`zombies`、`globals`，未指定 `id` 时使用模组文件夹名。迁移到 v2 时建议显式添加稳定、唯一的 `id`，并按上述真实内部名称检查实体键；数值格式与 `ticks` 单位保持一致。模组间冲突按优先级解决，而不是把数值叠加；不修改存档格式，但极端平衡数值可能影响战局，建议备份存档。查看程序日志中的 `[mod]` 诊断，修复不合法模组后重启。

此阶段仅支持**已有实体的白名单整数属性**；不能通过 JSON 动态注册新植物、新僵尸、重映射资源、注入代码或运行脚本。请勿在模组中捆绑、上传或传播原版 `main.pak`、`properties/`、美术、音频等受版权保护素材。各模组作者对自己的原创内容自行选择并声明许可证；本项目源代码以 LGPL-3.0-or-later 授权，不代表获得 PopCap Games / EA 游戏素材或商标的许可。第三方模组不受本项目官方支持。

## 无游戏素材的加载回归测试

从源码构建后，在**源码仓库根目录**运行 `PVZP_MOD_TEST=1 <构建目录>/pvz-portable`。该入口在创建 `LawnApp`、加载 `main.pak` 前运行：仅创建临时 JSON 目录、验证加载/冲突/启停/诊断并返回退出码，**不会**请求玩家资源。正常游戏启动不设置该环境变量。Windows 可在命令提示符执行 `set PVZP_MOD_TEST=1` 后启动程序；测试使用并清理自己的临时目录，不改用户已有模组或存档。
