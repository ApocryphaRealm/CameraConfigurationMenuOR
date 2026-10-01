#pragma once

// ============================================================================================================
// The marker (game thread): the chosen reference's name, drawn in the game's own HUD text (the game's text prefab,
// WBP_AltarTextBlock) just above the reference where it stands - so the player sees what Activate will use and how
// far the reach goes (the owner, 2026-09-29: "build the activation prompt for wherever the item is at so that I know
// that it's capable of reaching that distance"). Shown only when this mod's choice is what Activate will act on (the
// game's own pick shows the game's own prompt).
//
// Built once like Tween Menu's menu: a user widget with a canvas root and one text on it, added to the viewport and
// then only shown and hidden - taken off the viewport, nothing holds it and the garbage collector frees it (logic
// library 7690). Rebuilt if a level change takes it away. The position is UMG's own projection
// (WidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition) of the reference's place in Unreal's world, which
// aim::ToUnreal gives from the calibration.
// ============================================================================================================

namespace marker
{
	// a_ref: what to mark, or nullptr to hide it
	void Show(RE::TESObjectREFR* a_ref);

	// The lock-on's target (1.0.1): marked in a warm colour, and while it is set the selection's Show calls are ignored -
	// one marker on screen, the lock's. nullptr hands the marker back to the selection.
	void Lock(RE::TESObjectREFR* a_ref);

	json State();   // any thread
}
