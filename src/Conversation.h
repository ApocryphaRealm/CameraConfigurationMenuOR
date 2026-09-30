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
//     crosshairRef, remembered through gameplay). Since 2026-09-29 (the owner: "reference how Ultimate Combat locks onto
//     a target and just apply that to the conversation offset camera" - SetControlRotation alone did not hold) it is
//     Ultimate Combat's lock-on: the speaker's Unreal body (the paired pawn with the speaker's form ID) and its
//     Head_Socket, the look-at rotation from the camera's place stepped toward at UC's rates, written to the controller's
//     ControlRotation and to the third-person arm's RelativeRotation with the arm frozen (no inherited rotation,
//     absolute) - put back when the conversation ends. A conversation the player did not start by activating someone
//     has no speaker, and the game's aim stands.
//   * [Framing.Conversation] bFirstPersonNoOffset - with first person on, the Conversation offset is not added while the
//     view is first person (Framing.cpp), so the view looks straight at the speaker.
// Game thread; called from the player's post tick. No UObject is kept across frames except by a slot-checked handle.
// ============================================================================================================

namespace conversation
{
	void Tick(UE::UObject* a_controller, UE::UObject* a_cameraManager, UE::UObject* a_arm, const std::string& a_cameraTag);
	bool FirstPersonNow();   // CCM switched the view to first person for the conversation running now
	json State();
}
