#pragma once

// ============================================================================================================
// The contextual crosshair (plan section 13.3; the owner, 2026-09-29: "add on a contextual crosshair that hides the
// crosshair optionally"). Game thread, from the frame tick.
//
// [Crosshair] iMode 0 leaves the game's crosshair alone (the default); 2 hides it; 1 shows it only while it means
// something - a bow drawn or a spell being cast, something to activate (the game's or Selection's activateRef), a weapon
// drawn, first person - each a switch.
//
// What is changed: the RenderOpacity of ONE image - the crosshair inside the Reticle (WBP_ModernHud_Reticle -> child
// WBP_ModernHud_CrosshairV2, class WBP_ModernHud_CrosshairSneakEye_C -> the image named "Crosshair", the path Ultimate
// Combat Redux ships), found by walking its widget tree, kept by object-array slot and found again after a HUD rebuild.
// The sneak eye and the activate icon beside it are left alone, and the reticle root is HUD Position Manager's to move -
// the two mods never write the same property. In a menu, dialogue or a load the crosshair's own opacity is put back.
// ============================================================================================================

namespace crosshair
{
	void Tick();
	json State();   // any thread
}
