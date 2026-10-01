#pragma once

// ============================================================================================================
// CCM's own target lock-on (1.0.1; the owner, 2026-09-30: "include our own lock-on in the camera mod" - our own design,
// nothing taken from Ultimate Combat's LockOn.lua; 2026-10-01: build it and release it before UCR 1.0.1 ships with its
// own lock-on off).
//
// The lock key (LeftAlt / R3 by default, rebindable on the Camera page) takes the best target in a cone ahead of the
// camera - a hostile fighting you before anyone else, then the one nearest the middle of the view, then the nearest -
// and the same key lets go. While locked, every player tick (game thread, after the Blueprint's own tick) turns the
// controller's ControlRotation toward the target: the yaw along the line from your character to it, the pitch from the
// camera to its chest, tilted down by "Look down" so both stay in view, eased exponentially over "Turn time" (0 =
// at once). The right stick's own turning is overwritten; a flick of it left or right moves the lock to the nearest
// target on that side. The lock lets go by itself when the target dies, is gone, or is past the range plus a quarter,
// in first person, in a conversation, when CCM or the lock-on is switched off, and when Ultimate Combat locks on.
//
// Ultimate Combat Redux keeps a lock-on of its own, off by default. When a player switches it on (its INI's
// LockOnEnabled), CCM's lock-on stands down and says so rather than both driving the camera.
// ============================================================================================================

namespace lockon
{
	struct In
	{
		UE::UObject*       pawn = nullptr;   // the player's pawn
		UE::UObject*       ctrl = nullptr;   // its controller
		UE::UObject*       mgr = nullptr;    // the player camera manager
		bool               firstPerson = false;
		const std::string* cameraTag = nullptr;
		double             dt = 0.0;         // seconds since the last tick
		bool               ucrLocked = false; // Ultimate Combat is locked on right now (its .lockon state file)
	};

	void Toggle();               // the lock key (game thread, from game::RunActions)
	void MoveAimPoint(int a_dir); // -1 up a body part, +1 down (the aim-point keys; game thread)
	void MoveTarget(int a_dir);   // -1 the previous target, +1 the next (the target keys; game thread)
	void Tick(const In& a_in);   // every player tick (game thread)
	bool Active();               // a target is locked: the body faces the camera
	bool UltimateCombatOwnsIt(); // Ultimate Combat's own lock-on is switched on in its INI: CCM's stands down
	std::string Status();        // one line for the page (any thread)
	json State();                // for ccm.status (any thread)
}
