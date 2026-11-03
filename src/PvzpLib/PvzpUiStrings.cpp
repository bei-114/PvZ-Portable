// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PvzpUiStrings.h"
#include "PvzpLocale.h"
#include "PvzpPlantFood.h"
#include "SexyAppBase.h"
#include <iterator>

namespace {
struct Pair { const char* en; const char* zh; };
// Original provisional wording for this port's tools, not an attributed pack.
constexpr Pair kUi[] = {
 {"DBG","调试"},{"DBG CLOSE","关闭调试"},{"MOD LOAD STATUS","模组加载状态"},
 {"SUN +1000","阳光 +1000"},{"SUN LOCK: ","锁定阳光："},{"ON","开"},{"OFF","关"},
 {"FOOD x3","能量豆 ×3"},{"RECHARGE: ","瞬间冷却："},{"PLANT <","植物 <"},{"PLANT >","植物 >"},
 {"PLACE ANYWHERE","任意放置"},{"HEAL PLANTS","治疗植物"},{"PAGE ","页 "},
 {"PAGE 1/4  >>","第 1/4 页 >>"},{"PAGE 2/4  >>","第 2/4 页 >>"},
 {"PAGE 3/4  >>","第 3/4 页 >>"},{"PAGE 4/4  >>","第 4/4 页 >>"},
 {"ZOMBIE <","僵尸 <"},{"ZOMBIE >","僵尸 >"},{"SPAWN MIDDLE","中路生成"},
 {"SPAWN ALL LANES","各路生成"},{"CLEAR ZOMBIES","清除僵尸"},{"CHILL ALL","全部减速"},
 {"NEXT WAVE","下一波"},{"CLEAR BULLETS","清除子弹"},{"FINISH LEVEL","完成关卡"},
 {"UNLOCK PROFILE","解锁存档"},{"CANCEL TOOLS","取消工具"},{"FILL SEED BANK","刷新卡槽"},
 {"PLANT: E-GATL","植物：电能机枪"},{"PLANT: SUNFLOWER","植物：向日葵"},
 {"PLANT: WALLNUT","植物：坚果"},{"RESET FOOD TIMER","重置能量豆计时"},
 {"LOADED: ","已加载："},{"  MOD ERRORS: ","  模组错误："},{"ERR: ","错误："},
 {"OLD SAVE RULES (SAFE)","旧存档规则（安全）"},{"NO LOAD ERRORS","无加载错误"},
 {"EDIT JSON / RESTART GAME","修改 JSON 后重启游戏"},
 {"FOOD: TAP PLANT / TAP BEAN TO CANCEL","能量豆：点植物使用／点豆取消"},
 {"DEBUG PLACE: ANY CELL / DBG CANCEL","调试放置：点任意格／点调试取消"},
 {"MOVE","移动"},{"GLOVE: HOLD PLANT, DRAG, RELEASE","手套：按住植物、拖动、松开"},
 {"PLANT NOT ON GROUND","植物未在地面上"},{"BUNGEE: WAIT","蹦极抓取中：请稍候"},
 {"BOWLING: UNAVAILABLE","保龄球模式不可用"},{"SPECIAL ANIMATION: WAIT","特殊动作中：请稍候"},
 {"GRAVE-BOUND PLANT","该植物绑定墓碑"},{"SQUASH ATTACK: WAIT","窝瓜攻击中：请稍候"},
 {"CHOMPER EATING: WAIT","大嘴花进食中：请稍候"},{"ATTACK IN PROGRESS: WAIT","攻击中：请稍候"},
 {"SUPPORT SETTLING: WAIT","底座安放中：请稍候"},{"SUPPORT OCCUPIED","底座上有其他植物"},
 {"OUTSIDE BOARD: CANCELLED","超出草坪：已取消"},{"PLANT MOVED: CANCELLED","植物已移动：已取消"},
 {"INVALID DESTINATION: CANCELLED","目标位置无效：已取消"},{"s","秒"},
 {"Mystery Nut","盲盒坚果"},{"Reverse Mystery Nut","逆盲盒坚果"},{"Brain","大脑"},
 {"Aurora Nut","极光坚果"},{"Prism Megaphone Flower","晶能扩音花"},
 {"Seed Packet Saboteur","种子包干扰者"},{"Tombstone Necromancer","墓碑召唤师"},
 {"Frost Pea Zombie","寒冰豌豆僵尸"},{"Ember Pea Zombie","余烬豌豆僵尸"},
 {"Sunflower Medic Zombie","向日葵医师僵尸"},{"Chomper Ravager","大嘴花吞噬者"},
 {"Melon Mortar Zombie","西瓜散射僵尸"},{"Gatling Pea Commando","机枪豌豆突击僵尸"},
 {"Seed Packet Heist","种子包劫案"},{"Graveyard Orchestra","墓园交响曲"},{"Botanical Siege","植物围城"},
 {"Pumpkin Bodyguard","南瓜保镖"},{"Espresso Commander","浓缩咖啡指挥官"},
 {"Garlic Side-Stepper","蒜头侧跳者"},{"Cherry Courier","樱桃快递员"},
 {"Sunshine Pickpocket","阳光扒手"},{"Rebound Tall-nut","反弹高坚果"},
 {"Overtime at the Coffee Shop","加班咖啡馆"},{"Special Garlic Delivery","蒜香快递"},
 {"The Sunshine Bank Job","阳光保卫战"},{"Funhouse Grand Finale","奇妙防线总动员"},
 {"New level","新建关卡"},{" (copy)","（副本）"},
 {"Coffee commanders grant a short, non-stacking speed rally. Pumpkin guards absorb nearby allied damage while their armor survives. Freeze the guard, break its armor, and burst down the commander between rallies.","咖啡指挥官提供短暂且不叠加的加速；南瓜保镖有甲时替附近同伴承伤。冻结或破甲可关闭保镖，在号令间隙集火指挥官。"},
 {"Garlic hoppers show an arrow before changing to an adjacent lane and cannot hop through a close blocker. Cherry couriers arm a visible one-second fuse near plants. Freeze or butter pauses both wind-ups; defeat a courier to disarm it.","蒜头侧跳前会显示箭头，不能穿过贴身阻挡物；樱桃快递员靠近后启动一秒引信。冰冻或黄油能暂停预备动作，提前击杀樱桃即可拆弹。"},
 {"Pickpockets steal only uncollected sun near them, never your banked balance. Each holds at most 300 sun and returns it as bounty when defeated. Collect promptly, protect your production, and recover the stolen sunshine.","扒手只偷附近尚未收集的阳光，不会扣除银行余额；每只最多携带300阳光，击败后返还。及时拾取、保护生产，再追回失去的阳光。"},
 {"A hard mixed puzzle. Tall-nuts reflect ordinary peas only while armored and ready; lobbed shots, fire, electric shots and prism-refraction bypass reflection. Freeze disables active guards and wind-ups. Mix defenses rather than relying on one shooter.","高难混合解法关：高坚果仅在有甲且就绪时反弹普通豌豆，抛射、火焰、电能和晶能折射可以突破。冰冻能关闭主动防护与引信，别只依赖一种射手。"},
 {"Protect your seed bank. Packet-head saboteurs delay one card at a time; Frost Peas briefly freeze plants. Start with sun producers and prioritize saboteurs.","保护种子库：种子包头会轮流延迟卡片，寒冰豌豆头会短暂冻结植物。先建立阳光生产，优先消灭干扰者。"},
 {"Tombstone heads raise at most six minions each. Sunflower medics heal nearby allies. Eliminate medics and summoners before the crowd grows.","每只墓碑头一生最多召唤六只随从，向日葵医师会治疗附近同阵营僵尸。优先解决医师与召唤师，避免敌群越积越多。"},
 {"A hard mixed assault. Gatling heads fire rapidly; melon heads hit a target and adjacent plants; Chompers punish weak front lines. Prism fields and Aurora tanks are recommended.","高难度混合攻势：机枪头高速射击，西瓜头溅射邻格，大嘴花突破薄弱前线。建议搭配晶能扩音花与极光坚果。"},
 {"Electric Gatling Pea","电能机枪豌豆"},{"Electric Gatling Peashooter","电能机枪豌豆"},
 {"ELECTRIC_GATLING_PEA","电能机枪豌豆"},
 {"cannot open mod.json","无法打开 mod.json"},{"invalid file size or UTF-8","文件大小或 UTF-8 编码无效"},
 {"invalid JSON, duplicate key, or trailing content","JSON 无效、键重复或存在尾随内容"},
 {"unsupported schemaVersion","不支持的 schemaVersion"},{"invalid priority","优先级无效"},
 {"enabled must be boolean","enabled 必须为布尔值"},{"section must be an object","配置分区必须为对象"},
 {"customPlants must be an array","customPlants 必须为数组"},
 {"customPlants entry must be an object","customPlants 项必须为对象"},
 {"customPlants slot/name/duplicate","自定义植物槽位、名称无效或重复"},
};
struct Name { const char* key; const char* en; const char* zh; };
constexpr Name kNames[] = {
 {"PEASHOOTER","Peashooter","豌豆射手"},{"SUNFLOWER","Sunflower","向日葵"},
 {"CHERRY_BOMB","Cherry Bomb","樱桃炸弹"},{"WALL_NUT","Wall-nut","坚果"},
 {"POTATO_MINE","Potato Mine","土豆地雷"},{"SNOW_PEA","Snow Pea","寒冰射手"},
 {"CHOMPER","Chomper","大嘴花"},{"REPEATER","Repeater","双发射手"},
 {"PUFF_SHROOM","Puff-shroom","小喷菇"},{"SUN_SHROOM","Sun-shroom","阳光菇"},
 {"FUME_SHROOM","Fume-shroom","大喷菇"},{"GRAVE_BUSTER","Grave Buster","墓碑吞噬者"},
 {"HYPNO_SHROOM","Hypno-shroom","魅惑菇"},{"SCAREDY_SHROOM","Scaredy-shroom","胆小菇"},
 {"ICE_SHROOM","Ice-shroom","寒冰菇"},{"DOOM_SHROOM","Doom-shroom","毁灭菇"},
 {"LILY_PAD","Lily Pad","睡莲"},{"SQUASH","Squash","窝瓜"},
 {"THREEPEATER","Threepeater","三线射手"},{"TANGLE_KELP","Tangle Kelp","缠绕海草"},
 {"JALAPENO","Jalapeno","火爆辣椒"},{"SPIKEWEED","Spikeweed","地刺"},
 {"TORCHWOOD","Torchwood","火炬树桩"},{"TALL_NUT","Tall-nut","高坚果"},
 {"SEA_SHROOM","Sea-shroom","海蘑菇"},{"PLANTERN","Plantern","路灯花"},
 {"CACTUS","Cactus","仙人掌"},{"BLOVER","Blover","三叶草"},
 {"SPLIT_PEA","Split Pea","裂荚射手"},{"STARFRUIT","Starfruit","杨桃"},
 {"PUMPKIN","Pumpkin","南瓜头"},{"MAGNET_SHROOM","Magnet-shroom","磁力菇"},
 {"CABBAGE_PULT","Cabbage-pult","卷心菜投手"},{"FLOWER_POT","Flower Pot","花盆"},
 {"KERNEL_PULT","Kernel-pult","玉米投手"},{"COFFEE_BEAN","Coffee Bean","咖啡豆"},
 {"GARLIC","Garlic","大蒜"},{"UMBRELLA_LEAF","Umbrella Leaf","叶子保护伞"},
 {"MARIGOLD","Marigold","金盏花"},{"MELON_PULT","Melon-pult","西瓜投手"},
 {"GATLING_PEA","Gatling Pea","机枪豌豆"},{"TWIN_SUNFLOWER","Twin Sunflower","双子向日葵"},
 {"GLOOM_SHROOM","Gloom-shroom","忧郁菇"},{"CATTAIL","Cattail","香蒲"},
 {"WINTER_MELON","Winter Melon","冰西瓜"},{"GOLD_MAGNET","Gold Magnet","吸金磁"},
 {"SPIKEROCK","Spikerock","地刺王"},{"COB_CANNON","Cob Cannon","玉米加农炮"},
 {"IMITATER","Imitater","模仿者"},{"ELECTRIC_GATLING_PEA","Electric Gatling Pea","电能机枪豌豆"},
 {"ZOMBIE","Zombie","普通僵尸"},{"CONEHEAD_ZOMBIE","Conehead Zombie","路障僵尸"},
 {"BUCKETHEAD_ZOMBIE","Buckethead Zombie","铁桶僵尸"},{"NEWSPAPER_ZOMBIE","Newspaper Zombie","读报僵尸"},
 {"FOOTBALL_ZOMBIE","Football Zombie","橄榄球僵尸"},{"BALLOON_ZOMBIE","Balloon Zombie","气球僵尸"},
 {"SNORKEL_ZOMBIE","Snorkel Zombie","潜水僵尸"},{"GARGANTUAR","Gargantuar","巨人僵尸"},
 {"FLAG_ZOMBIE","Flag Zombie","旗帜僵尸"},{"POLE_VAULTING_ZOMBIE","Pole Vaulting Zombie","撑杆僵尸"},
 {"SCREEN_DOOR_ZOMBIE","Screen Door Zombie","铁门僵尸"},{"DANCING_ZOMBIE","Dancing Zombie","舞王僵尸"},
 {"BACKUP_DANCER","Backup Dancer","伴舞僵尸"},{"JACK_IN_THE_BOX_ZOMBIE","Jack-in-the-Box Zombie","小丑僵尸"},
 {"DIGGER_ZOMBIE","Digger Zombie","矿工僵尸"},{"POGO_ZOMBIE","Pogo Zombie","跳跳僵尸"},
 {"ZOMBIE_YETI","Zombie Yeti","雪人僵尸"},{"LADDER_ZOMBIE","Ladder Zombie","扶梯僵尸"},
 {"IMP","Imp","小鬼僵尸"},{"REDEYED_GARGANTUAR","Giga-gargantuar","红眼巨人僵尸"},
 {"PVZP_SELECTOR_ADVENTURE","Adventure","冒险模式"},{"PVZP_SELECTOR_START","Start Adventure","开始冒险"},
 {"PVZP_SELECTOR_MINIGAMES","Mini-games","小游戏"},{"PVZP_SELECTOR_PUZZLE","Puzzle","解谜模式"},
 {"PVZP_SELECTOR_SURVIVAL","Survival","生存模式"},{"PVZP_SELECTOR_OPTIONS","Options","设置"},
 {"PVZP_SELECTOR_HELP","Help","帮助"},{"PVZP_SELECTOR_QUIT","Quit","退出"},
 {"PVZP_SELECTOR_STORE","Shop","商店"},{"ALMANAC_BUTTON","Almanac","图鉴"},
 {"ZEN_GARDEN","Zen Garden","禅境花园"},{"PVZP_SELECTOR_CHANGEUSER","Change User","切换用户"},
 {"PVZP_SELECTOR_ZOMBATAR","Zombatar","僵尸头像"},{"PVZP_SELECTOR_ACHIEVEMENTS","Achievements","成就"},
};
constexpr Pair kSkillChinese[] = {
 {"豌豆游行","持续 6 秒，每 0.4 秒向四周发射一圈豌豆。"},
 {"口袋日出","分四波产生阳光，每波两个，可正常收集。"},
 {"樱桃庆典","扩大爆炸至五行范围，造成 1800 伤害；使用后消失。"},
 {"硬糖外壳","完全修复，并获得最多 2000 点临时护盾，持续 9 秒。"},
 {"速成土豆","立刻就绪，并在附近引爆可见的 900 伤害预警爆炸。"},
 {"雪球大战","额外发射寒冰豌豆，每 0.45 秒施放大范围减速。"},
 {"再来一口","结束消化并立即吞咬，随后 6 秒内加快消化。"},
 {"豌豆鼓点","持续 6 秒进行双管连射。"},
 {"孢子派对","发射成对短程孢子，并持续 4 秒追加孢子齐射。"},
 {"蘑菇黎明","立即长大，分五波释放可收集的阳光。"},
 {"蒸汽快车","使用原版同行攻击，额外喷射十次穿透烟雾。"},
 {"墓碑返利","清除附近墓碑和弹坑；每清除一格掉落阳光，最多五次。"},
 {"舞会伙伴","魅惑附近最多三个普通地面僵尸；使用后消失。"},
 {"勇敢孢子","持续 4 秒发射瞄准孢子，即使受到惊吓也会攻击。"},
 {"严冬降临","反复冻结，每次造成 250 冰冻伤害，足以消灭普通僵尸。"},
 {"友好蘑菇云","对全场造成 3600 伤害，代价是在周围 3×3 区域留下弹坑。"},
 {"水上急救","修复同格和相邻水上底座，并提供有上限的护盾。"},
 {"双重践踏","在最近目标周围发动两次可见的 900 伤害践踏；使用后消失。"},
 {"五路嘉年华","持续 6 秒向五行齐射，覆盖自身上下各两行。"},
 {"水池漩涡","拖走附近最多三个普通水中僵尸；使用后消失。"},
 {"三路辣焰","使用原版火焰效果和伤害烧灼三行；使用后消失。"},
 {"地刺踢踏","在 4 秒内发动八次可见的近距离地刺攻击。"},
 {"篝火集结","持续 6 秒协助附近射手装填，并立即发动一次近距离火焰爆发。"},
 {"糖果堡垒","完全修复，并获得最多 4000 点临时护盾，持续 9 秒。"},
 {"潮汐孢子","发射成对短程孢子，并在水上持续 4 秒追加齐射。"},
 {"复苏灯塔","在周围九格发动六次可见修复脉冲，没有被动伤害光环。"},
 {"针刺风暴","持续 6 秒向三行发射尖刺。"},
 {"强风来袭","吹走全场飞行僵尸，并使地面敌人减速；使用后消失。"},
 {"后台返场","持续 6 秒向前后成对射击。"},
 {"流星雨","在 4 秒内额外发动八次五向杨桃齐射。"},
 {"南瓜马车","修复外壳，并给同格所有植物提供有上限的护盾。"},
 {"金属探测","使用原版磁力接口，移除最多五件符合条件的金属装备。"},
 {"沙拉投射","持续 6 秒投掷成对卷心菜，完整保留原版抛物线。"},
 {"陶艺工坊","修复周围九格植物并提供护盾，尤其照顾屋顶底座。"},
 {"黄油盛宴","持续 6 秒追加黄油投掷，保留真实弹道和原版黄油定身。"},
 {"咖啡小憩","唤醒周围蘑菇、修复它们并提前下一次攻击；使用后消失。"},
 {"换行舞曲","修复大蒜，并将附近最多五个步行僵尸引导至可用相邻旱地行。"},
 {"野餐遮阳","触发叶伞防御，并给周围九格提供持续 6 秒的护盾。"},
 {"金色花束","分四波产生可收集金币，而非隐形增加账户金额。"},
 {"西瓜野餐","持续 6 秒投掷成对溅射西瓜，保留原版抛物线。"},
 {"豌豆焰火","在前方召唤三个火炬树桩，并向三行发射火豌豆。"},
 {"双子日出","分五波产生阳光，每波三个，可正常收集。"},
 {"紫色迪斯科","释放旋转孢子风暴，并在周围召唤一圈蘑菇。"},
 {"九命齐射","持续 6 秒每次发射三根追踪尖刺，各自保留目标标识。"},
 {"寒冬野餐","持续 6 秒投掷成对冰冻溅射西瓜，并立即施放一次附近减速。"},
 {"寻宝扫荡","快速执行四次原版金币吸引，并产生两个可收集金币。"},
 {"摇滚演出","修复地刺王，并在 5 秒内发动十次重型近距离地刺攻击。"},
 {"弹药补给","立即装填，并在下一次玉米发射后再装填一次；没有隐藏范围伤害。"},
 {"全场喝彩","立即变身，并施放被模仿植物自己的技能。"},
 {"电扇庆典","启动持续约 5 秒的真实电扇弹幕；普通四连发电链保持不变。"},
};
static_assert(std::size(kSkillChinese) == 50);
}

void PvzpRegisterUiStrings()
{
    for (const auto& p : kUi)
        PvzpLocaleRegisterLiteral(p.en, p.zh);
    for (const auto& n : kNames)
        PvzpLocaleRegisterLiteral(n.en, n.zh);
    for (std::size_t i = 0; i < std::size(kSkillChinese); ++i)
        if (auto skill = PvzpPlantFood::SkillAt(i))
        {
            PvzpLocaleRegisterLiteral(skill->name, kSkillChinese[i].en);
            PvzpLocaleRegisterLiteral(skill->description, kSkillChinese[i].zh);
        }
}

std::string PvzpUiText(std::string_view text)
{
    return std::string(PvzpLocaleTranslateLiteral(text));
}

std::string PvzpUiResourceName(std::string_view key, std::string_view fallback)
{
    std::string value;
    if (Sexy::gSexyAppBase)
    {
        auto found = Sexy::gSexyAppBase->mStringProperties.find(key);
        if (found != Sexy::gSexyAppBase->mStringProperties.end())
            value = found->second;
    }
    for (const auto& name : kNames)
        if (key == name.key)
        {
            if (value.empty())
                value = PvzpLocaleIsChinese() ? name.zh : name.en;
            else if (PvzpLocaleIsChinese())
            {
                auto translated = PvzpLocaleTranslateLiteral(value);
                // Respect supplied CJK/resource translations. Only supply our
                // provisional fallback when the resource still contains ASCII.
                bool ascii = true;
                for (unsigned char ch : value) if (ch >= 0x80) { ascii = false; break; }
                value = translated != value ? std::string(translated) : ascii ? name.zh : value;
            }
            return value;
        }
    return value.empty() ? PvzpUiText(fallback.empty() ? key : fallback) : PvzpUiText(value);
}

std::string PvzpUiClip(std::string_view text, std::size_t maxChars)
{
    std::size_t offset = 0, count = 0;
    while (offset < text.size() && count < maxChars)
    {
        ++offset;
        while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80)
            ++offset;
        ++count;
    }
    return std::string(text.substr(0, offset));
}
