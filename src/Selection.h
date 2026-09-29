#pragma once

// ============================================================================================================
// The selection (game thread, every frame): the references near the character that the player could use - items,
// containers, doors, activators, flora, furniture, people and creatures, each with a name - within RANGE of the
// character and within MAX ANGLE of where the camera looks. The best is the one closest to the aim, then the nearest;
// the game's own crosshair pick always wins when it has one, so this only widens the pick.
//
// The choice goes into InterfaceManager::activateRef (0x158) - the field the game's own prompt target lives in, read in
// game 2026-09-29 - while the game's own pick is empty, and the game's own activation uses it. Nothing in the game is
// activated or changed from here: the engine traps reference changes off its TES thread (the bench crash, 11:32).
// Better Third-Person Selection 1.0.0 as finalized, inside CCM (plan section 13.2); its settings are CCM's [Selection].
// ============================================================================================================

namespace selection
{
	void Tick();

	enum class Reason
	{
		kActive,        // selecting in this view
		kNotReady,      // no player or interface yet
		kOff,           // switched off
		kMenu,          // a menu is open
		kOffThird,      // off in third person
		kOffFirst,      // off in first person
		kNoCamera,      // calibrated, but the camera cannot be read now
		kCalibrating,   // the character has not walked far enough yet
	};

	struct Status
	{
		bool        active = false;   // selecting in this view (on, not in a menu, the view's switch on, calibrated)
		Reason      reason = Reason::kNotReady;
		std::string why;              // the reason in English, for the log and the tool
		std::string choice;           // this mod's pick, "" when none
		std::string game;             // the game's own pick, "" when none
		std::uint32_t candidates = 0;
		std::string applyTo;
	};
	Status GetStatus();   // any thread
	json   State();       // any thread: the TestBench tool's answer
}
