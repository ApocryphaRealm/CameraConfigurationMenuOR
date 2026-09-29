// CCM - Camera Configuration Menu (Oblivion Remastered, OBSE64) - entry point.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Game.h"
#include "Page.h"
#include "Settings.h"
#include "Tool.h"

namespace
{
	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) return;
		if (a_msg->type == OBSE::MessagingInterface::kPostLoad) {
			page::Register();
			if (!tool::Register()) logger::debug("TestBench not loaded yet - the driving tools are tried again shortly");
			game::Install();
			// TestBench is a test-only plugin: try its interface a few more times in case it loaded late
			std::thread([] {
				for (int i = 0; i < 30 && !tool::Register(); ++i) std::this_thread::sleep_for(1s);
			}).detach();
		}
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	settings::Load();
	{
		const auto level = static_cast<spdlog::level::level_enum>(std::clamp(settings::Get().logLevel, 0, 6));
		logger::set_level(level, level);
	}
	logger::info("CCM - Camera Configuration Menu {} (Oblivion Remastered). Log level {} - set [Log] uLogLevel=1 in "
				 "CameraConfigurationMenu.ini for more detail when reporting a problem.", CCM_VERSION, settings::Get().logLevel);

	if (auto* messaging = OBSE::GetMessagingInterface()) {
		if (!messaging->RegisterListener(&OnMessage)) {
			logger::error("OBSE messaging refused the listener - CCM will not start");
		}
	} else {
		logger::error("OBSE messaging interface missing - CCM will not start");
	}
	return true;
}
