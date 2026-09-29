#pragma once

// ============================================================================================================
// Where the camera is and where it looks, in Oblivion's world coordinates (game thread only).
//
// The Remastered's camera is Unreal's (logic library 7552: table-driven, set per state), so it is READ from the player
// camera manager (GetCameraLocation / GetCameraRotation, called through ProcessEvent - never a hook). Unreal's world
// and Oblivion's differ by a scale and possibly by swapped or mirrored axes; neither is assumed. While the player
// walks, each step's movement is measured on both sides (the pawn's location, the player reference's location) and
// the one of the eight axis arrangements that explains every step is kept, with the median scale. Only relative
// vectors are converted (camera minus the character's feet), so Unreal's world origin never matters.
// ============================================================================================================

namespace aim
{
	struct View
	{
		bool         ok = false;
		RE::NiPoint3 eye;   // Oblivion world coordinates
		RE::NiPoint3 fwd;   // unit vector
	};

	View Read(RE::PlayerCharacter* a_player);   // once per frame, on the game thread

	json State();   // the last reading and the calibration, for the TestBench tool (any thread)
	UE::UObject* PlayerController();

	// an Oblivion world position in Unreal's world, from the last reading and the calibration (false until calibrated)
	bool ToUnreal(const RE::NiPoint3& a_oblivion, UE::FVector& a_out);   // game thread; nullptr until found (looked for at most every 2 s)
	bool Calibrated();
}
