#pragma once

// The engine side of CCM. Everything that touches a UObject runs on the GAME thread: the class watches are made from
// the PeekMessageW frame tick, and events arrive through pe::Watch's per-class ProcessEvent vtable slots (never a hook
// on the shared ProcessEvent body - that broke UE4SS, logic library 7567):
//   * BP_OblivionPlayerCharacter_C::ReceiveTick is the per-frame point (M0 probe P1: once per game frame, always on
//     the game thread);
//   * the four movement/camera switches are written EVERY tick (P2: the game re-applies bUseControllerDesiredRotation
//     per stance, so a one-off write does not last);
//   * framing is written to the camera manager's CurrentCameraSettingData, never the spring arm (P6: the manager puts
//     the arm back within 50 ms; P6b: its setting data drives the arm and holds). The game's value is the base; CCM
//     re-bases whenever the game writes a new one (a state change, a zoom).
// The page and the TestBench tool only read Snapshot() and queue actions - never a UObject.

namespace game
{
	struct Snapshot
	{
		bool          hookInstalled = false;
		std::string   processEvent;
		std::uint64_t ticks = 0;
		double        tickMicros = 0.0;     // cost of CCM's work in the last tick
		double        ticksPerSecond = 0.0;
		std::string   pawn, controller, arm, fpArm, movement, cameraManager;   // "0x..." or "" (waiting)
		std::string   cameraTag;
		std::vector<std::string> tagsSeen;
		bool          firstPerson = false;
		bool          combatStance = false;
		bool          attacking = false;
		bool          active = false;       // CCM is writing the free-camera switches this tick
		bool          locked = false;       // the body is turned to face the camera
		double        lockRemaining = 0.0;
		const char*   mode = "waiting";
		std::array<bool, 4> switches{};     // read BACK from the objects after the write
		std::array<bool, 4> vanilla{};
		bool          vanillaRecorded = false;
		bool          shoulderLeft = false;
		std::array<double, 3> socketBase{};
		std::array<double, 3> socketNow{};
		float         armLengthNow = 0.0f;
		float         desiredArmLength = 0.0f;
		float         fov = 0.0f;
		std::uint32_t blockEvents = 0, attackEvents = 0, castEvents = 0;
		std::string   problem;
	};

	void FrameTick();       // every frame from the PeekMessageW tick (game thread): makes the class watches, retried until each exists
	Snapshot Status();

	// Actions from keys, the page and ccm.drive - queued, applied on the next game tick.
	enum class Action { kShoulderSwap, kCycleStyle, kToggle, kUnstick, kNextPreset, kLockOn };
	void Queue(Action a_action);
	void NotePageDrawn();   // the AMF page drew this frame: CCM's keys stay quiet while its menu is open

	// The controller (1.0.1): read from the XInput function the game itself loaded - never LoadLibrary (a plugin that
	// loads an XInput DLL kills the controller, logic library 7178) and never through the game's import slot (another
	// plugin's controller rules would run twice). Nothing is taken from the game: a bound button also does what the game
	// does with it. 0 when no pad is connected or no reader exists; a_ok false then.
	WORD        PadButtons(bool* a_ok = nullptr);
	bool        PadRightX(float& a_x);   // the right stick's left-right, -1..1 (right +); false with no pad
	std::string PadName(std::int32_t a_mask);   // "A", "LB", "D-pad up", "Left stick click (LS)"... or "none"
	std::string KeyName(std::int32_t a_scan);   // the keyboard's own name for a DirectInput scan code, or "none"
}
