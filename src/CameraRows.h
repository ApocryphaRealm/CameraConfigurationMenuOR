#pragma once

// ============================================================================================================
// The game's camera table, edited in place (the owner, 2026-09-30: the Aiming a bow position "doesn't seem to do
// anything"). The camera of each state is a row of /Game/Dev/Data/DT_CameraSettings (FVCameraSettings: CameraTagsKey
// and a ThirdPersonCameraSettingDataClose / Far / FirstPersonCameraSettingData, each an FVCameraSettingData). Two
// captures while aiming (TestBench, 00:58 and 01:00) showed the aiming states re-reading their row every frame: CCM's
// writes to the camera manager's CurrentCameraSettingData landed there, and the live spring arm stayed at the row's own
// values (side 45) - the framing rebased thousands of times. So for the aiming rows CCM changes the ROW: the game then
// applies CCM's position itself, with no per-frame fight.
//
//   * rows whose CameraTagsKey names "Aiming" (State.Camera.Standing_Aiming, Sneaking_Aiming, ...): the Close and Far
//     data get DesiredSocketOffset.Y (side, mirrored by the shoulder swap) and .Z (height) added, DesiredArmLength the
//     distance, DesiredOverrideFieldOfView the field of view - from the Aiming a bow context's own position;
//   * every row's original values are kept and each application starts from them, so a change of setting never
//     stacks, and turning the context off (or CCM off) puts the table back exactly;
//   * the table's RowMap is not reflected: it is read where UE 5.3 declares it (right after RowStruct) and checked
//     against the reflected GetDataTableRowNames before a single row is written (TestBench's ue.datatable read).
// Game thread; Tick() costs a comparison unless a setting changed or the table was reloaded.
// ============================================================================================================

namespace camrows
{
	void Tick(bool a_enabled, bool a_shoulderLeft);   // every camera tick; applies when anything changed
	bool AimingInTable();                            // the aiming rows carry the Aiming a bow position now
	json State();
}
