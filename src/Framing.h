#pragma once

// ============================================================================================================
// Framing and smoothing (plan 7.2 / 7.3; the settings surface is SmoothCam's, described in SMOOTHCAM-SETTINGS.md -
// no SmoothCam code or text). The owner, 2026-09-29: "the offsets for how far over the shoulder the camera is ... XY
// offsets for where the camera is located in relation to the character. And then you have the interpolation settings.
// for smoothing."
//
// Everything is written to the camera manager's CurrentCameraSettingData, never the spring arm (probe P6: the manager
// puts the arm back within 50 ms; P6b: its setting data reaches the arm and holds). Each field's base is the GAME'S
// value, re-based whenever the game writes one CCM did not (a state change, a zoom), so every offset is a delta and
// 0 / -1 is the unmodded camera:
//   * DesiredSocketOffset.Y += side, .Z += height (the whole Y mirrored by the shoulder swap), DesiredArmLength +=
//     distance - eased towards a new value over fOffsetSeconds on one of 22 easing curves, so a settings change, the
//     weapon being drawn or the shoulder swap slides the camera instead of cutting;
//   * PositionLagSpeed, CameraLagMaxDistance, RotationLagSpeedPitch / Yaw and the state TransitionDuration are
//     replaced while their setting is not -1.
// ============================================================================================================

namespace framing
{
	// game thread, every camera tick. a_enabled false eases everything back to the game's values.
	void Apply(UE::UObject* a_manager, bool a_enabled, bool a_shoulderLeft, bool a_weaponDrawn, double a_dt);

	constexpr int kEasingCount = 22;
	double Ease(int a_curve, double a_t);   // the standard easing formulas; t and the result in [0, 1]

	std::array<double, 3> SocketBase();   // the game's own socket offset (for the Status page)
	json State();                         // any thread
}
