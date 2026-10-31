/*
 * PvZ-Portable 模组接口（PvzpMod）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * 设计目标：不改动枚举、不重编译游戏，就能通过 mods/<名字>/mod.json 覆盖
 * 原版植物 / 僵尸的数值。加载目录：<用户数据目录>/mods/
 *
 * mod.json 结构（字段全部可选）：
 * {
 *   "name": "显示名",
 *   "priority": 0,                       // 越大越晚加载，后者覆盖前者
 *   "plants":  { "PEASHOOTER": { "cost": 50, "refresh": 500, "launchRate": 120, "health": 400 } },
 *   "zombies": { "ZOMBIE_NORMAL": { "health": 250, "pickWeight": 40, "value": 1 } },
 *   "globals": { "startingSun": 100 }
 * }
 *
 * 单位说明：cost 阳光；refresh/launchRate 毫秒；health 点。
 * 植物/僵尸名用原版内部名（PlantDefinition::mPlantName / ZombieDefinition::mZombieName），
 * 大写、下划线，例如 PEASHOOTER、ZOMBIE_CONEHEAD。
 */
#pragma once

#include <cstddef>

namespace PvzpMod
{
	// 扫描并加载 mods/ 下的所有模组，只应在启动时调用一次
	void Init();

	bool HasMods();

	// 查询覆盖值，没有命中就返回 theDefault
	int PlantInt(const char* thePlantName, const char* theField, int theDefault);
	int ZombieInt(const char* theZombieName, const char* theField, int theDefault);
	int GlobalInt(const char* theField, int theDefault);
}
