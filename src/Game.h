#pragma once

// The engine side of CCM. Everything that touches a UObject runs on the GAME thread, inside CCM's
// UObject::ProcessEvent detour:
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

	void Install();         // starts the hook's retry thread
	Snapshot Status();

	// Actions from keys, the page and ccm.drive - queued, applied on the next game tick.
	enum class Action { kShoulderSwap, kCycleStyle, kToggle, kUnstick };
	void Queue(Action a_action);
	void NotePageDrawn();   // the AMF page drew this frame: CCM's keys stay quiet while its menu is open
}
