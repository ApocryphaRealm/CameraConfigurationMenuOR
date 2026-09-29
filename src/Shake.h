#pragma once

// ============================================================================================================
// "Screen shake while sprinting" (the owner, 2026-09-29: "I don't see any setting for camera smoothing so that whenever
// you are sprinting, the camera is stable and not shaking"; the name is his). On = the game's shake, off = a steady
// camera.
//
// The shake is two looping LegacyCameraShake Blueprints that DefaultAltar.ini's [/Script/Altar.VOblivionRuntimeSettings]
// CameraShakeThirdPerson / CameraShakeFirstPerson lists play on the tags Input.Locomotion.Mode.Sprint +
// Actor.Locomotion.MoveMode.Standing (read from the pak, 2026-09-29 - scratchpad or-sprint-shake-report.md):
//   /Game/Dev/CameraShake/BP_SprintCameraShake.BP_SprintCameraShake_C       (third person)
//   /Game/Dev/CameraShake/BP_SprintCameraShake_FP.BP_SprintCameraShake_FP_C (first person)
// Off zeroes their oscillation amplitudes (RotOscillation Pitch / Yaw / Roll, LocOscillation X / Y / Z, FOVOscillation)
// on the class default object - which every new instance copies - and on every live or pooled instance (a playing
// shake reads its own amplitudes each frame, so it stops at once). The default object's values are recorded first and
// written back when the switch goes on. ShakeScale is not used: every play call overwrites it.
// ============================================================================================================

namespace shake
{
	void Update(bool a_steady);   // game thread, every camera tick: a_steady = the sprint shake should be silenced
	json State();                 // any thread
}
