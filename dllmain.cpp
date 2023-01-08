#include <fstream>
#include <queue>
#include <functional>

#include <random>
#include <iostream>
#include <cmath>

#include <windows.h>

#include "minhook/include/MinHook.h"
#include "json/json.hpp"
#include "sol/sol.hpp"
#include "loader.h"
#include "ghidra_export.h"
#include "util.h"
#include <thread>

#include "game_utils.h"
#include "lua_core.h"

#include "hook_monster.h"
#include "hook_animals.h"

#include "uihook.h"

using namespace loader;

#pragma region Basic data processing program
static void MassageCommand() {
	void* MassagePlot = *(undefined**)MH::World::Message;
	void* MessageOffset = *offsetPtr<undefined**>((undefined(*)())MassagePlot, 0x38);
	if (MessageOffset != nullptr) {
		string Massage = offsetPtr<char>(MessageOffset, 0x80);
		string::size_type idx;
		//执行实时命令
		idx = Massage.find("luac:");
		if (idx != string::npos) {
			string command = Massage.substr(Massage.find("luac:") + 5);
			int err = 0;
			err = luaL_dostring(LuaHandle::Lc, command.c_str());
			if (err != 0)
			{
				int type = lua_type(LuaHandle::Lc, -1);
				if (type == 4) {
					string error = lua_tostring(LuaHandle::Lc, -1);
					LuaCore::LuaErrorRecord(error);
				}
			}
			*offsetPtr<char>(MessageOffset, 0x80) = *"";
		}
		//重载虚拟机
		idx = Massage.find("reload ");
		if (idx != string::npos) {
			string luae = Massage.substr(Massage.find("reload ") + 7);
			std::map<string, LuaHandle::LuaScriptData>::iterator it;
			it = LuaHandle::LuaScript.find(luae);
			if (it != LuaHandle::LuaScript.end()) {
				lua_close(LuaHandle::LuaScript[luae].L);
				LuaHandle::LuaScript[luae].L = luaL_newstate();
				luaL_openlibs(LuaHandle::LuaScript[luae].L);
				registerFunc(LuaHandle::LuaScript[luae].L);
				if (LuaCore::Lua_Run(LuaHandle::LuaScript[luae].L, luae) != 1) {
					LuaHandle::LuaScript[luae].start = false;
				}
				else {
					LuaHandle::LuaScript[luae].start = true;
				}
			}
			*offsetPtr<char>(MessageOffset, 0x80) = *"";
		}
	}
}
//数据更新程序
static void updata() {
	//地图更新时清理数据
	void* TimePlot = utils::GetPlot(*(undefined**)MH::Player::PlayerBasePlot, { 0x50, 0x7D20 });
	if (Chronoscope::NowTime > *offsetPtr<float>(TimePlot, 0xC24)) {
		//清除计时器数据
		Chronoscope::ChronoscopeList.clear();
		//清除按键数据
		Keyboard::TempData::t_KeyCount.clear();
		Keyboard::TempData::t_KeyDown.clear();
		//清除Xbox手柄数据
		XboxPad::TempData::t_KeyCount.clear();
		XboxPad::TempData::t_KeyDown.clear();
		LuaCore::run("on_switch_scenes");
	}
	//更新计时器
	Chronoscope::chronoscope();
	//更新手柄数据
	XboxPad::Updata();
	//执行玩家消息指令
	MassageCommand();
}
#pragma endregion


DWORD WINAPI LuaThread(LPVOID lpReserved)
{
	//脚本实时环境
	LuaHandle::Lc = luaL_newstate();
	luaL_openlibs(LuaHandle::Lc);
	registerFunc(LuaHandle::Lc);
	//加载并运行脚本
	for (string file_name : LuaHandle::LuaFiles) {
		if (LuaHandle::LuaScript[file_name].start && LuaHandle::LuaScript[file_name].L != nullptr) {
			lua_State* L = LuaHandle::LuaScript[file_name].L;
			luaL_openlibs(L);
			registerFunc(L);
			if (LuaCore::Lua_Run(L, file_name) != 1) {
				LuaHandle::LuaScript[file_name].start = false;
			}
		}
	}
	//其他绑定操作
	hook_monster::Hook();
	hook_animals::Hook();
	uihook::LuaRegister();
	//运行lua脚本中初始化代码
	LuaCore::run("on_init");
	return TRUE;
}

__declspec(dllexport) extern bool Load()
{
	//加载lua文件
	LuaCore::Lua_Load("Lua\\", LuaHandle::LuaFiles);
	//创建lua环境
	CreateThread(nullptr, 0, LuaThread, nullptr, 0, nullptr);
	//初始化钩子
	MH_Initialize();
	HookLambda(MH::World::MapClockLocal,
		[](auto clock, auto clock2) {
			auto ret = original(clock, clock2);
			//更新基础数据
			updata();
			//运行lua虚拟机
			LuaCore::run("on_time");
			uihook::init();
			//启动绘制更新
			if(!LuaCore::luaframe)
				LuaCore::luaframe = true;
			return ret;
		});
	MH_ApplyQueued();
	return true;
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
		DisableThreadLibraryCalls(hModule);
		uihook::hMod = hModule;
		return Load();
	return TRUE;
}

