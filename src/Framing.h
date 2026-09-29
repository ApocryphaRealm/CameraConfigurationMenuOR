#pragma once

// ============================================================================================================
// Framing and smoothing (plan 7.2 / 7.3; the settings surface is SmoothCam's, described in SMOOTHCAM-SETTINGS.md -
// no SmoothCam code or text). The owner, 2026-09-29: "the offsets for how far over the shoulder the camera is ... XY
// offsets for where the camera is located in relation to the character. And then you have the interpolation settings.
// for smoothing" - and then: "different offsets for different contexts, like if you're sneaking versus if you're
// standing versus if you're sprinting versus if you're in combat with weapons drawn versus if you're with a bow and
// arrow specifically."
//
// Everything is written to the camera manager's CurrentCameraSettingData, never the spring arm (probe P6: the manager
// puts the arm back within 50 ms; P6b: its setting data reaches the arm and holds). Each field's base is the GAME'S
// value, re-based whenever the game writes one CCM did not (a state change, a zoom), so every offset is a delta and
// 0 / -1 is the unmodded camera:
//   * DesiredSocketOffset.Y += side, .Z += height (the whole Y mirrored by the shoulder swap), DesiredArmLength +=
//     distance - taken from the CONTEXT the player is in (standing, moving, sprinting, sneaking, weapon drawn, bow
//     aiming, swimming, horseback; a context without its own position uses Standing's) and eased towards a new value
//     over fOffsetSeconds on one of 22 easing curves, so a context change or the shoulder swap slides the camera;
//   * PositionLagSpeed, CameraLagMaxDistance, RotationLagSpeedPitch / Yaw and the state TransitionDuration are
//     replaced while their setting is not -1.
// ============================================================================================================

namespace framing
{
	// The contexts, highest priority first when several hold at once (Context() picks the first that holds). Standing is
	// also the fallback for any context whose "own position" switch is off.
	enum class Group : std::int32_t
	{
		kStanding = 0,
		kMoving,        // walking or running (horizontal speed above ~20 cm/s)
		kSprinting,     // the movement component's IsSprinting, or the camera state tag State.Camera.Sprinting
		kSneaking,      // OblivionActorStatePairingComponent.bIsSneaking (UCR's signal)
		kWeaponDrawn,   // bInCombatStance
		kBowAiming,     // the camera state tag State.Camera.*Aiming (the game's own bow-aim camera)
		kSwimming,      // the movement component's IsSwimming
		kHorseback,     // a camera state tag naming a horse or a mount (NOT YET SEEN in game - to be confirmed)
		kCount
	};
	const char* GroupKey(Group a_g);    // "Moving" ... - the INI section is "Framing.<key>"

	struct Inputs
	{
		UE::UObject* manager = nullptr;
		UE::UObject* pawn = nullptr;
		UE::UObject* movement = nullptr;   // the pawn's CharacterMovement
		std::string  cameraTag;            // the manager's CameraTags.TagName this tick
		bool         weaponDrawn = false;
		bool         enabled = false;      // false eases everything back to the game's values
		bool         shoulderLeft = false;
		double       dt = 0.0;
	};

	// game thread, every camera tick
	void Apply(const Inputs& a_in);

	constexpr int kEasingCount = 22;
	double Ease(int a_curve, double a_t);   // the standard easing formulas; t and the result in [0, 1]

	Group Current();                      // the context applied last tick (any thread)
	std::array<double, 3> SocketBase();   // the game's own socket offset (for the Status page)
	json State();                         // any thread
}
