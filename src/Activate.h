#pragma once

// ============================================================================================================
// The Activate press, watched (game thread): the keys the game's Activate action (IA_Game_Actions_Activate) has in
// IMC_Game_Actions (IsInputKeyDown edges, as Tween Menu watches its own), so the log can say whether the game acted on
// this mod's choice. Nothing is activated from here: the game's own activation acts on the activateRef write (round 5,
// the arrow taken in 94 ms), and a reference's state changed off the TES simulation thread - this tick runs on the UE
// game thread - is trapped by the engine (the fallback that called TESForm::Activate crashed on a bench, 11:32:40).
// ============================================================================================================

namespace activate
{
	// true on the tick the Activate action's key went down (keyboard or pad, following rebinds)
	bool PressedThisTick(UE::UObject* a_playerController);

	json State();   // the action, its keys, presses (any thread)
}
