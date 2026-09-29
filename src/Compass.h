#pragma once

// ============================================================================================================
// The compass follows the camera (plan 7.1 bCompassFollowsCamera; UCR's CompassBridge is the reference for the value).
// The HUD reads the heading through the NATIVE getter VHUDMainViewModel::GetCompassDirectionValue (a float), which a
// Blueprint calls through UFunction::func, never ProcessEvent - so its func pointer is swapped for a thunk that calls
// the original and, while the free camera is on in third person, returns the camera's heading instead of the body's:
// the controller's yaw + 90, less the cell's north marker (BP_NorthMarker_C) yaw indoors - UCR's formula. The heading
// is worked out on the game thread each camera tick and handed to the thunk through atomics.
// ============================================================================================================

namespace compass
{
	void Install();   // game thread: swaps the getter once it exists (retried by the caller)
	void Update(UE::UObject* a_controller, bool a_active);   // game thread, every camera tick
	json State();     // any thread
}
