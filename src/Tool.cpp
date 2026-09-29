// The TestBench driving tools (rule 64): ccm.status (read-only) and ccm.drive. They run on TestBench's listener
// thread, so they read game::Status() (a copy made on the game thread) and queue actions - never a UObject.
#include "Tool.h"

#include "Activate.h"
#include "Aim.h"
#include "Compass.h"
#include "Crosshair.h"
#include "Framing.h"
#include "Game.h"
#include "Marker.h"
#include "Page.h"
#include "Selection.h"
#include "Settings.h"
#include "AMF.h"
#include "TestBenchAPI.h"

namespace tool
{
	namespace
	{
		TestBenchAPI::ITestBenchInterface001* g_tb = nullptr;

		json StatusJson()
		{
			const auto s = game::Status();
			return json{
				{ "ok", true }, { "version", CCM_VERSION }, { "hook", s.hookInstalled }, { "process_event", s.processEvent },
				{ "ticks", s.ticks }, { "ticks_per_s", s.ticksPerSecond }, { "tick_us", s.tickMicros },
				{ "pawn", s.pawn }, { "controller", s.controller }, { "arm", s.arm }, { "fp_arm", s.fpArm }, { "movement", s.movement },
				{ "camera_manager", s.cameraManager }, { "camera_tag", s.cameraTag }, { "tags_seen", s.tagsSeen },
				{ "first_person", s.firstPerson }, { "combat_stance", s.combatStance }, { "attacking", s.attacking },
				{ "active", s.active }, { "locked", s.locked }, { "lock_remaining", s.lockRemaining }, { "mode", s.mode },
				{ "switches", s.switches }, { "vanilla", s.vanilla }, { "vanilla_recorded", s.vanillaRecorded },
				{ "shoulder_left", s.shoulderLeft }, { "socket_base", s.socketBase }, { "socket_now", s.socketNow },
				{ "arm_length", s.armLengthNow }, { "desired_arm_length", s.desiredArmLength }, { "fov", s.fov },
				{ "events", { { "block", s.blockEvents }, { "attack", s.attackEvents }, { "cast", s.castEvents } } },
				{ "problem", s.problem }, { "settings", settings::GetAll() },
				{ "selection", selection::State() }, { "aim", aim::State() }, { "activate", activate::State() }, { "marker", marker::State() },
				{ "crosshair", crosshair::State() }, { "compass", compass::State() }, { "framing", framing::State() } };
		}

		void Write(void* a_sink, TestBenchAPI::WriteFn a_write, const json& a_j) { a_write(a_sink, a_j.dump().c_str()); }

		void StatusTool(void*, const char*, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			Write(a_sink, a_write, StatusJson());
		}

		void DriveTool(void*, const char* a_args, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			json args = json::parse(a_args ? a_args : "{}", nullptr, false);
			if (args.is_discarded()) { Write(a_sink, a_write, { { "ok", false }, { "error", "args are not JSON" } }); return; }
			const std::string op = args.value("op", "");
			if (op == "get") {
				Write(a_sink, a_write, { { "ok", true }, { "settings", settings::GetAll() } });
			} else if (op == "set") {
				std::string why;
				const bool ok = args.contains("key") && args.contains("value") && settings::SetByName(args["key"].get<std::string>(), args["value"], why);
				Write(a_sink, a_write, { { "ok", ok }, { "error", ok ? "" : (why.empty() ? "needs key and value" : why) }, { "settings", settings::GetAll() } });
			} else if (op == "action") {
				const std::string n = args.value("name", "");
				const std::unordered_map<std::string, game::Action> m = { { "shoulderSwap", game::Action::kShoulderSwap },
					{ "cycleStyle", game::Action::kCycleStyle }, { "toggle", game::Action::kToggle }, { "unstick", game::Action::kUnstick } };
				const auto it = m.find(n);
				if (it == m.end()) { Write(a_sink, a_write, { { "ok", false }, { "error", "name: shoulderSwap|cycleStyle|toggle|unstick" } }); return; }
				game::Queue(it->second);
				Write(a_sink, a_write, { { "ok", true }, { "queued", n }, { "note", "applied on the next game tick; read ccm.status" } });
			} else if (op == "page") {
				Write(a_sink, a_write, { { "ok", AMF::OpenMenu(page::kModName) } });
			} else {
				Write(a_sink, a_write, { { "ok", false }, { "error", "op: get|set{key,value}|action{name}|page" } });
			}
		}
	}

	bool Register()
	{
		if (g_tb) return true;
		HMODULE tb = ::GetModuleHandleW(L"TestBench.dll");
		auto get = tb ? reinterpret_cast<void* (*)(unsigned)>(::GetProcAddress(tb, "TestBench_GetInterface")) : nullptr;
		g_tb = get ? static_cast<TestBenchAPI::ITestBenchInterface001*>(get(1)) : nullptr;
		if (!g_tb) return false;
		g_tb->RegisterTool("ccm.status",
			R"({"description":"CCM - Camera Configuration Menu: the live state (class watches, resolved objects, camera state tag, the four switches read back, facing lock, shoulder, offsets, event counts, settings; selection, aim calibration, activate press, marker, crosshair).","inputSchema":{"type":"object","properties":{}},"readOnly":true})",
			&StatusTool, nullptr);
		g_tb->RegisterTool("ccm.drive",
			R"({"description":"CCM - drive it for testing. op: get | set {key:'Section.Key' or 'Key', value} (same path as the page, saved to the INI) | action {name: shoulderSwap|cycleStyle|toggle|unstick} | page (opens AMF on CCM).","inputSchema":{"type":"object","properties":{"op":{"type":"string"},"key":{"type":"string"},"value":{},"name":{"type":"string"}}},"readOnly":false})",
			&DriveTool, nullptr);
		logger::info("TestBench tools registered: ccm.status, ccm.drive");
		return true;
	}
}
