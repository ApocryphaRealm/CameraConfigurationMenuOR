#pragma once

// ============================================================================================================
// The conversation camera's two switches (the owner, 2026-09-29):
//   * [Framing.Conversation] bFirstPerson - "a toggle for the conversation camera tab that lets you automatically go to
//     first person in conversation. And then back to third person when leaving conversation." The controller's own
//     SwitchPOV(EVPlayerPOVType::FirstPerson) when a conversation starts (the camera state State.Camera.Dialogue), and
//     the view the player had put back when it ends. A conversation ends when gameplay resumes (menuMode == 1), not when
//     the camera state changes - first person has a camera state of its own.
//   * [Framing.Conversation] bLockOnSpeaker - "a setting for the conversation camera target [that] locks the camera to the
//     NPC that you're talking to so that the offset doesn't have you looking away from the NPC the further out you go."
//     The speaker is the reference the player activated to start the conversation (InterfaceManager.activateRef, or
//     crosshairRef, remembered through gameplay); each tick of the conversation the controller's rotation is aimed from
//     the camera's own place at the speaker's head (SetControlRotation), so an offset CCM adds keeps the NPC centred. A
//     conversation the player did not start by activating someone has no speaker, and the game's aim stands.
// Game thread; called from the player's post tick. No UObject is kept across frames except by a slot-checked handle.
// ============================================================================================================

namespace conversation
{
	void Tick(UE::UObject* a_controller, UE::UObject* a_cameraManager, const std::string& a_cameraTag);
	json State();
}
