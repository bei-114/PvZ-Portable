# 自定关卡与复合僵尸（v7）

## 体验入口

主菜单新增原版石质按钮「自定义关卡 / Custom Levels」。关卡需要启用 `120-custom-campaign`，其中引用的八种复合僵尸来自 `110-composite-zombies`，两包一起安装。

- **种子包劫案**：900初始阳光、6波。种子包头轮流干扰种子卡，寒冰头冻结植物；优先解决干扰者。
- **墓园交响曲**：1400初始阳光、7波。墓碑召唤师扩大敌群，向日葵医师群体回血；优先击杀医师与召唤师。
- **植物围城**：2200初始阳光、8波。机枪、瓜弹、大嘴花和支援单位混合进攻，建议使用晶能扩音花与加强后的极光坚果。

这些是独立有限波次模式，不是覆盖原版冒险/生存关。关卡选择器可浏览说明、开始新关卡、继续上次自定关卡。新开只替换**上一个自定关卡检查点**；继续按存档中的稳定关卡ID恢复，不按当前选择器页码恢复。胜负、重试、返回菜单使用专属流程，不发放原版挑战奖杯、不改冒险等级或原版挑战记录。

当前三关均为日间五行草坪。场景切换、脚本、自动种植与任意胜利条件不在此版格式内。

## 不是全局换皮

`customZombies` 使用独立字符串ID和实体侧表。普通僵尸和自定义僵尸可同时出现；八种类型不会把全局 `ZOMBIE` 定义替换成同一种外观，也不会往原版 `ZombieType` 中间插枚举。

身体复用原版行走骨架，头部用现有种子包、墓碑和植物缓存图组合，位置跟随实际头部骨骼。所需原版素材仍由玩家合法游戏资源提供，模组包只含JSON。

| 稳定ID | 名称 | 特殊能力 |
|---|---|---|
| pvzp.seed_packet_head | 种子包干扰者 | 每650tick轮流延迟一张卡200tick；魅惑后反向帮助冷却中的卡恢复进度 |
| pvzp.tombstone_head | 墓碑召唤师 | 每900tick召唤两只普通随从，一生最多三次/六只，避免无限铺场；魅惑后随从同阵营 |
| pvzp.snowpea_head | 寒冰豌豆僵尸 | 每240tick发射30伤害敌方寒冰弹，命中植物后冻结150tick；冻结期间攻击、周期技能和晶能增幅暂停 |
| pvzp.firepea_head | 余烬豌豆僵尸 | 每220tick发射45伤害火色豌豆弹；不是额外宣称存在持续灼烧脚本 |
| pvzp.sunflower_head | 向日葵医师僵尸 | 每300tick给附近180像素内、本行及邻行同阵营单位恢复100身体血，不超过上限 |
| pvzp.chomper_head | 大嘴花吞噬者 | 每400tick对近距离植物咬击500并回复125身体血；魅惑后攻击近距离敌方僵尸 |
| pvzp.melon_head | 西瓜散射僵尸 | 每420tick发射120伤害瓜弹，并对同一行相邻格植物造成半额溅射；本版不是原版投石车那种真实抛物线 |
| pvzp.gatling_head | 机枪豌豆突击僵尸 | 每75tick发射30伤害敌方豌豆弹 |

冻结、黄油和失去头部会停止僵尸主动能力。受伤、装甲、减速抗性、一次性赏金仍走正常引擎机制。原版敌豌豆和复合弹命中植物也会结算盲盒受伤事件；致死伤害与恰好0血及时处理。

## 数据格式

一个包可以同时包含植物、复合僵尸、关卡和原版属性覆盖。仍使用schemaVersion2严格JSON；未知字段、重复键、坏编码或越界拒绝整个包。

```json
{
  "schemaVersion": 2,
  "id": "org.example.campaign",
  "name": "Example campaign",
  "customZombies": [{
    "id": "example.packet",
    "name": "Packet Saboteur",
    "base": "ZOMBIE",
    "head": "seed_packet",
    "ability": "seed_jam",
    "health": 850,
    "armor": 1200,
    "interval": 650,
    "magnitude": 200,
    "speedPercent": 105
  }],
  "customLevels": [{
    "id": "example.first_level",
    "name": "First level",
    "description": "Protect the house and defeat both waves.",
    "startingSun": 500,
    "waves": [
      {"delay": 2500, "spawns": [{"zombie":"ZOMBIE", "count":3}]},
      {"delay": 2000, "spawns": [{"zombie":"example.packet", "row":2, "count":1}]}
    ]
  }]
}
```

### 复合僵尸字段

每包最多64种。`id` 是全局稳定ASCII标识，仅字母/数字/点/下划线/短横线，1..96字节；`name` 为1..96字节UTF-8名称。

- `base`：`ZOMBIE`、`CONEHEAD_ZOMBIE`、`BUCKETHEAD_ZOMBIE`、`FOOTBALL_ZOMBIE`、`SCREEN_DOOR_ZOMBIE`、`NEWSPAPER_ZOMBIE`。默认普通僵尸。
- `head`：`seed_packet`、`tombstone`、`pea`、`snowpea`、`firepea`、`sunflower`、`chomper`、`melon`、`gatling`。
- `ability`：`none`、`seed_jam`、`raise`、`shoot_ice`、`shoot_fire`、`heal`、`bite`、`shoot_melon`、`shoot_pea`。外观和能力可以独立组合。
- `health`：1..10000000，默认600；`armor`：0..10000000，默认0，正值提供头部护甲。
- `interval`：50..60000tick，默认600；`magnitude`：1..1000，默认30，按能力表示伤害/治疗/卡片延迟，不表示任意脚本。
- `speedPercent`：25..300；`damagePercent`：25..500；`slowResistancePercent`：0..100；`sunDrop`：0..1000；`tintR/G/B`：0..255。

### 关卡字段

每包最多64关。`id` 规则同上但上限128字节，`name` 上限128字节，`description` 上限1024字节。

- `startingSun`：0..9990。
- `waves`：1..100波，按数组顺序；`delay` 为该波前等待的1..60000tick，不是绝对时间戳。
- `spawns`：每波1..50组，总数量最多50只；每组 `count` 为1..50，默认1。
- `row`：-1（默认，按累计出怪数轮换五行）或0..4。
- `zombie`：原版内部名称或复合僵尸稳定ID。解析允许跨包前向引用，但开始关卡前必须找到全部定义并验证当前地形可生成；缺包不能悄悄当普通僵尸继续。

同ID按优先级/文件夹排序整体后加载者胜出，并显示双方和胜者诊断。不会拼接两个关卡波次。不要在游戏进行中修改快照。

## 存档与故障边界

- MOD05保存复合实体ID、能力倒计时/召唤次数、冻结植物和在途敌方子弹；MOD06保存稳定关卡ID、规则指纹、波次和计时器。
- 恢复顺序与文件里chunk顺序无关。缺失自定关卡检查点、缺少实体定义、规则改变或畸形引用会拒绝续关，不重置成另一关、不覆盖原版档。
- 修复了原portable导入里旧粒子系统析构误释放新粒子的问题：导入前销毁旧所有权图，再按发射器ID→粒子ID→系统/列表恢复，检查owner、定义索引和交叉淡出引用。
- 内部实体导入仍不是事务式。嵌套校验失败后必须丢弃该棋盘，不能假设已局部导入的对象完全未改变。

## 可重复测试

```sh
# 加载器、事件语法与存档边界
PVZP_MOD_TEST=1 ./build/pvz-portable
# 导入器（只使用合成文本，不分发汉化资源）
python3 -m unittest discover -s tools -p 'test_import_zh_locale.py' -v
# 普通真实棋盘：含复合能力、极光加强、存档后继续更新/绘制
bash tests/run-live-regression.sh ./build/pvz-portable ./build custom
# 独立关卡模式
bash tests/run-live-regression.sh ./build/pvz-portable ./build levels
```

脚本始终使用临时HOME和独立存档。关卡回归分别验证三关的真实选卡与首波自然计时；其余波次用明确标注的DIRECT导演循环测试有限结束逻辑，不冒充三关完整人工战斗平衡实测。平台构建结果与本地测试结果应分开记录。
