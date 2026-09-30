#pragma once

// ============================================================================================================
// The game's camera, edited where it lives (the owner, 2026-09-30: the Aiming a bow position "doesn't seem to do
// anything", then "only firing when I release the arrow"). Each state of the camera manager's state machine keeps its
// camera in an ASP_CameraSettings_C object (CameraSettings: an FVCameraSettings - CameraTagsKey and the First-person /
// Third-person Close / Far FVCameraSettingData); while a state's tag holds, the game re-applies that object's data every
// frame, so CCM's per-frame writes to CurrentCameraSettingData never reached the arm while aiming (TestBench captures
// 00:58 / 01:00). There is no camera DataTable at run time (round 9's DT_CameraSettings lookup found nothing).
//
//   * the live states whose tags name "Aiming" or "Zooming" (Standing / Sneaking / Swimming _Aiming and _Zooming): their
//     Close and Far data get DesiredSocketOffset.Y (side, mirrored by the shoulder swap) and .Z (height) added,
//     DesiredArmLength the distance, DesiredOverrideFieldOfView the field of view - the Aiming a bow context's position;
//   * the originals are kept and every application starts from them, so a change never stacks, and turning the
//     context (or CCM) off puts the states back exactly; templates (sub-objects of class defaults) are never touched;
//   * a new camera manager (a load) has new states: the kept objects are checked by their slots every 250 ms and the
//     object array is scanned again, at most every 5 s, while none is usable.
// Game thread.
// ============================================================================================================

namespace camrows
{
	void Tick(bool a_enabled, bool a_shoulderLeft);   // every camera tick; applies when anything changed
	bool AimingInTable();                            // the aiming rows carry the Aiming a bow position now
	json State();
}
