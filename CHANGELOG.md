# CCM - Camera Configuration Menu - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Started 2026-09-29; the history before this file is
in `git log`. A version number is issued by the version gate only once a build is seen working in game (rule 48).

## Unreleased - 2026-10-01 - untested (built, not run)

Two of the 1.0.0 page's known issues, at the owner's request (relayed 2026-10-01: "CCM 1.0.1, your mod").

### Added
- **Press-to-bind rows** for the three actions CCM performs - move the camera to the other shoulder, switch the free
  camera style, turn CCM on or off - each with a keyboard key and a controller button, on the Camera tab (the standing
  rule: a listener that takes ANY controller button, never an INI mask only). Through the framework's key capture
  (AMF 1.0.2+; older frameworks get a note and the INI still works). Refused, with the reason, and the capture armed
  again for the next press: a key the framework uses, Tab (the game's quick keys), Minimap Menu's default K and L
  (.MD\DEFAULT-KEYS.md), and a key or button already on another CCM action. A Clear button per binding and a Cancel
  button while waiting.
- **The controller is read** for those three buttons - 1.0.0 had the INI masks but never read a pad. From the game's own
  XInput module (never loaded by CCM, never through the game's import slot); quiet in menus (menuMode != 1), while the
  framework's window is up (AMF_IsMenuOpen) and while CCM's page drew in the last 250 ms. Nothing is taken from the
  game: a bound button also does what the game does with it. Still no button ships bound.

### Fixed
- **The crosshair while aiming a bow** (1.0.0's known issue). The crosshair image's material, MIC_CrossHair_SneakEye
  (read from the paks with uetex --params), draws the ranged reticle by its BowDrawAlpha scalar, which defaults to 0 -
  so the reticle stayed invisible while a bow was drawn whatever opacity CCM gave the image. While CCM shows the
  crosshair for a drawn bow, BowDrawAlpha is held at 1, only when the material says ranged (IsMelee? 0), and the game's
  value is put back afterwards. Logged: "crosshair: the image's material is ...", "bow reticle raised".

### Changed
- The framework header (include/AMF.h) updated from the framework's SDK (key capture, IsMenuOpen).
- Translations: the 22 new strings in the ten languages; the retired keys hint removed.

## 1.0.0 - 2026-10-01 - working

### Release (2026-10-01)
- The owner, 2026-10-01: "go ahead and try and do a finalize of the CCM mod. As it's not perfect currently, but it is
  functional. And just set it to default base values." Version 1.0.0 issued by the version gate; the package ships the
  compiled defaults (every framing offset 0 = the game's own camera, the CCM camera style on, only the two standard
  keys bound) - the owner's own tuned INI in MO2 is not touched.
- Known issues at release, stated on the page and not claimed as fixed:
  - **The crosshair while aiming a bow.** The Round 11 change below (a zero opacity is never taken as the shown value;
    the bow's Zooming state counts as aiming) went after the wrong cause: the game's crosshair material carries
    BowDrawAlpha 0 while aiming, so the crosshair can still stay hidden while a bow is drawn. Open.
  - **No target lock-on yet.** CCM is to own lock-on with a design of its own (planned); until then Ultimate Combat
    Redux keeps its lock-on, and its defaults are unchanged by this release. 3e97073 only fixed CCM's view restore after
    a conversation, which had left UCR's lock-on in first person.

### Resumed 2026-09-29

The owner, 2026-09-29: "work on completing the better third person camera project ... incorporate better third person
selection into that mod and also add on a contextual crosshair that hides the crosshair optionally" - CCM, keeping the
name Camera Configuration Menu. Plan: 4. plans\Camera Configuration Menu (CCM)\PLAN.md section 13.

### Changed (later the same day)
- the conversation camera now holds on the speaker the way Ultimate Combat's lock-on holds a target (the owner: "The
  conversation camera isn't locking on to the NPC. So why don't you have the agent reference how Ultimate Combat locks
  onto a target and just apply that to the conversation offset camera?"). Before, it only called SetControlRotation,
  and the game replaced that every tick. Now it works as UC's LockOn.lua does (Kramer7046, modification allowed with
  credit):
  - it finds the speaker's Unreal body (the paired pawn carrying the speaker's form ID, or else the nearest within 3 m)
    and aims at its Head_Socket;
  - it turns toward the look-at rotation from the camera's position at UC's rates: a fifth of the way each tick, at most
    300 degrees/s of yaw and 180 of pitch, with pitch kept within 75 degrees;
  - it writes that rotation to the controller's ControlRotation and to the third-person arm's RelativeRotation, with the
    arm frozen (no inherited rotation, absolute rotation). The arm is put back when the conversation ends.
  The Status page shows which body and socket were found.

### Round 11 (the owner's report, 2026-09-30)
- **Fixed: the contextual crosshair never showed while aiming** (the owner: "I can't see my contextual crosshair at the
  moment. Even though it's set to turn on with a bow drawn for aiming"). Two causes, both closed:
  - What "shown" means was the crosshair's opacity when the mod took control. Taken while the HUD was still fading in
    after a load, it read 0, and "shown" stayed invisible all session (the log's only crosshair line was the find,
    early in the load). A reading below 0.05 now counts as fully shown, and taking control is logged with both values.
  - The crosshair's bow test missed the fully drawn bow's *_Zooming camera state; it now uses the same signal as the
    framing's "Aiming a bow" (Aiming or Zooming).
  - Each shown / hidden change is logged at info with the reason and the camera state.
- **Fixed: Ultimate Combat Redux's lock-on refused after a conversation** (the owner: "the lock-on feature isn't
  working now"; UCR's log: "engage refused - first-person active" on every press after a conversation). The
  conversation held in third person is locked with ForceAndLockPOV once the camera state says Dialogue - by then the
  game has already switched to the conversation's first person, so UnlockAndRestorePOV at the end restored first
  person as the saved view, and the switch back (not the new default) left the controller wanting first person. The
  player's view now goes back as the default, is read back ten ticks later (POV and WantedPOV, both logged) and is
  set again if either is not the player's view.

### Round 10 (the owner's report, 2026-09-30)
- **Changed: "Move the conversation camera" holds the conversation in third person** (the owner: "the third person
  conversation camera while standing closely to the NPC still destroys the player body").
  - Cause: the game's conversation camera is first person (POV 0) and draws the player's body in its first-person form.
    Moved away from the head, that form showed cut apart up close.
  - Fix: with the switch on (and first person in conversations off), the controller is held with
    ForceAndLockPOV(the view the player came in with) for the conversation, so the body is drawn whole. The Conversation
    position then moves that third-person camera, and UnlockAndRestorePOV hands the view back when the conversation
    ends.
  - The "Aiming a bow" context works (the owner, the same launch).
- **Changed: the Aiming a bow position is written into the live camera states, not a table.** The primary session found
  there is NO camera DataTable at run time. Each camera state of the camera manager's state machine keeps its camera in
  an ASP_CameraSettings_C object: 20 live ones, from Standing and Dialogue to Standing_Aiming and Sneaking_Zooming. The
  live arm while aiming was exactly Standing_Aiming's Far data.
  - What changes: the live states whose tags name "Aiming" or "Zooming" get the context's side (mirrored by the shoulder
    swap), height, distance and field of view added to their Close and Far data. The originals are kept, so turning the
    context or CCM off restores them exactly. Class-default templates are never touched.
  - After a load: the camera manager is new, so its states are found again (their slots are checked every 250 ms, and
    the object array is scanned at most every 5 s while none is usable).
  - Field of view: the aiming states carry no field of view of their own (0), and the view while aiming read 75, so the
    Aiming zoom is laid on 75 there.
  - The bow's zoom states (*_Zooming) now count as aiming in the framing too. The log says "camera states: ...".
- **Fixed: the Aiming a bow position never reached the camera table** (the owner: "The while aiming a bow camera context
  is only firing when I release the arrow instead of while I'm aiming it").
  - Cause: round 9 looked DT_CameraSettings up by its path only. The path missed, and the miss was silent: no "camera
    table:" line in the log, and ccm.status camera_table read "the camera table is not loaded" (the primary session's
    reading). The aiming rows were never edited, and the scan never retried.
  - Fix: the table is looked for by path, then among the loaded DataTables by name, at most every 5 s until found. A
    failed scan retries every 5 s, and every outcome is logged ("camera table: ...").
- **Fixed: the conversation lock sometimes aimed at the player's own head** (the owner: "the conversation camera does
  definitely track the target properly now, but depending on the distance I am away from them ... it messes the
  character body up" - screenshots 01:30, the player's own arm, bow and quiver cut across the view).
  - Cause: the speaker's body was matched by the form ID in a pawn's TESRefComponent, which never matched. Every
    conversation fell back to "the nearest body (88 cm from the speaker)", often the player's own body.
  - Fix: the speaker's body now comes from the game's own pairing (every reference is an IVPairableItem whose pairing
    entry holds its Unreal actor). The fallback is the nearest pawn within 60 cm, never the player's body.

### Round 9 (2026-09-30) - from the primary session's TestBench captures while aiming and in dialogue
- **Fixed: the Aiming a bow position did nothing.** Two captures (00:58 sneaking, 01:00 standing) showed the aiming
  states re-read their row of the game's camera table every frame. CCM's values landed in CurrentCameraSettingData, but
  the live spring arm stayed at the row's own values (side 45), and the framing rebased thousands of times.
  - The Aiming a bow position now goes into the table itself (CameraRows.cpp): the Close and Far data of every row of
    /Game/Dev/Data/DT_CameraSettings whose tags name "Aiming" get the side (mirrored by the shoulder swap), height,
    distance and field of view added.
  - The originals are kept, so a change never stacks, and turning the context or CCM off puts the table back exactly.
  - The row map is read where UE 5.3 declares it and checked against GetDataTableRowNames before anything is written
    (TestBench's ue.datatable read).
  - The per-frame framing stands aside in that context.
- **Fixed: the conversation camera was never first person and ignored the Conversation tab.** The dialogue capture
  (01:01) showed the game's conversation camera is the first-person arm (POV 0, arm length 0 in its data). CCM itself
  caused what the owner saw ("zoomed in while still in third person ... at a left offset"): its 20 cm floor on the arm
  length pushed that camera behind the head. There were two further causes:
  - The "no offset in first person" switch applied whenever the view was first person, which is every conversation, so
    every Conversation value was zeroed.
  - Round 8 routed the Conversation position to OffsetWhenInDialogue, which the game leaves at 0; the capture showed
    CCM's usual socket and arm writes do reach the dialogue camera.
  - Now: no floor on a first-person view, and nothing is added to a first-person view except the Conversation context's
    own position. "No offset" applies only when first person in conversations is chosen. The Conversation position goes
    through the socket offset and arm length again. First person in conversations switches no view: the game's view
    already is first person.
- **Fixed: the speaker lock stood down for the whole conversation.** The dialogue camera reports POV 0 all through a
  conversation (log 00:48:52: "already in first person" from third person). The lock was gated on POV != 0, so it ran
  only on the last frame, as the conversation ended. It now goes by the view the player had in gameplay before the
  conversation, and stays off when first person in conversations is chosen.
- **Fixed: the speaker lock stood down for the whole conversation.** The game's dialogue camera reports POV 0 (first
  person) all through a conversation while it places the camera itself (log 00:48:52: "already in first person" from
  third person). The lock was gated on POV != 0, so it ran only on the last frame, as the conversation ended. The lock
  and the first-person switch now go by the view the player had in gameplay before the conversation, which is followed
  every tick outside one.

### Round 8 (the owner's report, 2026-09-30)
- **Fixed: the conversation settings did nothing.** The owner: "no matter what setting I change in the conversation tab,
  it stays locked into this left offset for the conversation camera where it's not first person, but it's really zoomed
  in third person." There were two causes:
  - **The speaker was never found.** `LookupByID<TESObjectREFR>` tests the exact form type, and a person is an ACHR, so
    every lookup came back null (log: "in a conversation with (no name)", "body no speaker"). The name, the Unreal
    body and the lock never ran. The reference is now taken for REFR, ACHR and ACRE.
  - **The game's conversation camera is a camera of its own.** UpdateDialogueCamera is native: it frames the speaker's
    DialogueFocusBoneName plus `CurrentCameraSettingData.OffsetWhenInDialogue` and ignores the socket offset and arm
    length that CCM wrote. The Conversation position now moves that offset: X back by the distance, Y the side, Z the
    height. The axes are assumed and the Status page shows the game's value and ours, so the first test settles them.
  - The switch now reads "Move the conversation camera (otherwise the game's own)". Off leaves the game's camera alone
    instead of applying Standing's position.
- **First person in conversations is read back.** Three ticks after SwitchPOV the view is checked. If the conversation
  camera refused the switch, `ForceAndLockPOV(FirstPerson)` is used, and `UnlockAndRestorePOV` then the previous view
  when the conversation ends. The log shows each step.
- **Added: an "Aiming a bow" context** (the owner: "I don't want to necessarily get rid of the while holding a bow offset,
  but I do want another one for while aiming the bow"). It applies while the camera state is an aiming state
  (State.Camera.Standing_Aiming, seen in game), ranks above Bow, and has its own position plus an **Aiming zoom (field of
  view)** slider: degrees added to the game's aiming field of view. The owner: "the camera always zooms in to a specific
  way whenever I draw the bow, and the offset doesn't seem to affect it". Bow is now holding a bow without drawing it.
- **Added: a Zoom (field of view) slider in conversations** (`[Framing.Conversation] fFieldOfView`).

### Added (late on 2026-09-29) - the zoom
- `[Zoom] bStartZoomedOut` (on by default): after loading a game, the view starts at the game's far third-person zoom
  instead of the close one. The owner asked for this: "when you load into the game you're already fully zoomed out
  instead of starting in third person but slightly zoomed out".
  - A load gives the player a new pawn. One second into that pawn's first gameplay, a view at the close zoom is switched
    with the controller's own `SwitchPOV(ThirdPersonFar)`, which also makes the far zoom the new default state.
  - First person, or a view already at the far zoom, is left as it is.
- `[Zoom] bOwnDistances`, `fCloseDistance` (170 cm) and `fFarDistance` (310 cm): the two zoom distances as precise
  sliders (the owner: "sliders for both zoom settings to customize them to change between").
  - They replace the game's own base arm length for the zoom in use; each context's Distance is still added on top.
  - The camera eases to them over the offsets' slide time, and eases back when the switch is turned off.
  - They don't apply while aiming a bow (the game's aim zoom) or in a conversation.
  - They sit in a Zoom section on the Framing page, which also shows the zoom in use now and its distance.

### Added (later the same day)
- `[Framing.Conversation] bFirstPersonNoOffset` (on by default): with first person in conversations turned on, the
  Conversation offset is not added while the view is first person, so it looks straight at the speaker (the owner: "the
  conversation camera's first person mode have a toggle to not have an offset because you're in first person mode and
  you want to look directly at your target"). The switch is on the Conversation page, under First person.

### Changed
- the engine hook: the MinHook on the shared UObject::ProcessEvent body (which broke UE4SS's own hook - logic library
  7567) is replaced by per-class ProcessEvent vtable watches (pe::Watch, as Tween Menu and Improved Wheel Menu use):
  the player's class (ReceiveTick), the live controller's class (block, attack), every live spell-cast animation class.
  They are made on the game thread from a PeekMessageW frame tick - the detached install thread that scanned the object
  array off the game thread (logic library 7670) is gone.
- the page's switches use the framework's own red/green track (AMF's theme leaves Button / FrameBg clear).

### Added
- Selection: Better Third-Person Selection 1.0.0 (finalized, not posted) merged in - in third person the named usable
  reference within Reach (300) of the character and Angle (35 deg) of the camera's aim, closest to the aim first, goes
  into InterfaceManager::activateRef while the game's own pick is empty, and its name is drawn where it stands.
  [Selection] settings and a Selection page. The compatibility check's findings folded in: this mod's own activateRef
  write is taken back when a menu opens.
- the contextual crosshair: [Crosshair] iMode 0 = the game's (default), 1 = contextual (shown while aiming a bow or
  casting, while there is something to use, while a weapon is drawn, always in first person - each a switch), 2 = always
  hidden; the crosshair image's RenderOpacity faded (fFadeSeconds). A Crosshair page.
- Ultimate Combat's lock-on ends the free camera (the owner, 2026-09-29: "it needs to prevent you from being in free
  camera mode while locked on to a target as well. Otherwise, the dodge system doesn't really work all that well"):
  Ultimate Combat Redux now writes MadConfigs\Ultimate Combat.lockon (locked=1 / locked=0) on engage and release; CCM
  reads it at most every 100 ms and, while locked, writes the facing flags (UCR's own LOCKED_ON profile). [General]
  bFaceWhileLockedOn (on by default), a switch on the camera page; the change is logged once each way.
  **Seen working in game 2026-09-29** (the owner: "CCM is reading the lock-on file, and the dodging is working
  properly").
- Framing and smoothing, the SmoothCam settings surface (the owner, 2026-09-29: "the offsets for how far over the
  shoulder the camera is ... XY offsets for where the camera is located in relation to the character. And then you have
  the interpolation settings. for smoothing"). A Framing page and two INI sections:
  - [Framing] fSide / fHeight / fDistance (cm, added to the game's own value for each camera state - 0 is the unmodded
    camera), and bCombatOffsets with fCombatSide / fCombatHeight / fCombatDistance for a weapon drawn;
  - [Smoothing] bEaseOffsets, iOffsetEasing (22 curves, sine in-out by default), fOffsetSeconds (0.5): a new position,
    drawing a weapon or the shoulder swap slides the camera instead of cutting (the swap now slides across behind the
    head); fFollowSpeed, fMaxLagDistance, fRotationSpeedPitch / Yaw and fStateBlendSeconds replace the game's
    PositionLagSpeed, CameraLagMaxDistance, RotationLagSpeedPitch / Yaw and TransitionDuration while not -1.
  - All written to the camera manager's CurrentCameraSettingData and re-based on the game's value (Framing.cpp). A
    runaway guard logs a warning if the game re-writes the setting data every frame. ccm.status gains "framing".
- Framing per context (the owner, 2026-09-29: "different offsets for different contexts, like if you're sneaking
  versus if you're standing versus if you're sprinting versus if you're in combat with weapons drawn versus if you're
  with a bow and arrow specifically"): [Framing.Standing] plus [Framing.Moving / Sprinting / Sneaking / WeaponDrawn /
  BowAiming / Swimming / Horseback], each with bOwn and fSide / fHeight / fDistance; a context without its own position
  uses Standing's. Replaces the single weapon-drawn set ([Framing] bCombatOffsets / fCombat*). Priority when several
  hold: bow aiming, horseback, swimming, sneaking, sprinting, weapon drawn, moving, standing. Signals: the camera tag
  (State.Camera.*Aiming / Sprinting), OblivionActorStatePairingComponent.bIsSneaking and the paired movement component's
  IsSprinting / IsSwimming (as Ultimate Combat Redux reads them), CharacterMovement.Velocity for moving. Horseback
  matches a camera tag naming a horse or mount - not yet seen in game. The page picks the context to edit and shows the
  one you are in; ccm.status "framing" reports the context and every signal.
- tools/gen.py fails when it cannot read a CCM_ROW line (its pattern had silently dropped every dotted-section row).

### Fixed (round 3, from the primary agent's in-game report of the per-context build)
- The settings table pointed every [Framing.<Context>] row at the wrong memory: offsetof(Values, fmGroups[i].x)
  through std::array::operator[] resolved to offsets 0-12 under MSVC, so the rows read and wrote bEnabled,
  iCameraStyle, fBlockTurnSeconds and fSpellTurnSeconds (the INI showed 0.00 / 0.20 / 1.40 and bOwn=1 in every
  context; the next load would have set the camera style to the game's). fmGroups is a C array now, a static_assert
  checks the indexed offset, and the table refuses to load, save or take ccm.drive writes if any two rows share memory.
  The install strips the damaged [Framing.*] sections from the player's INI.
- The "re-wrote the camera offset N times in a second" warning measured the game resetting its own value each frame,
  which is harmless (the report's numbers matched base + offset exactly). Replaced by a real stacking test: the base
  creeping towards CCM's own offset for 90 frames without a camera state change holds the base until the state changes
  (logged; ccm.status framing.held_bases).
- The smoothing sliders write the spring arm (CameraLagSpeed, CameraLagMaxDistance, CameraRotationLagSpeedPitch / Yaw):
  the setting data's own lag fields read 0 in game.

### Added (conversation view and aim; precise sliders)
- First person in conversations ([Framing.Conversation] bFirstPerson, off by default; the owner: "a toggle for the
  conversation camera tab that lets you automatically go to first person in conversation. And then back to third person
  when leaving conversation"): the controller's own SwitchPOV(FirstPerson) when a conversation starts, and the view the
  player had put back when gameplay resumes (a conversation ends with the menu, not with the camera state - first person
  has one of its own).
- The camera kept on the person spoken to ([Framing.Conversation] bLockOnSpeaker, on by default; the owner: "locks the
  camera to the NPC that you're talking to so that the offset doesn't have you looking away from the NPC the further out
  you go"): the speaker is the reference the player activated to start the conversation (InterfaceManager.activateRef,
  or crosshairRef, remembered through gameplay); each tick of the conversation the controller is aimed from the camera's
  own place at the speaker's head (SetControlRotation). A conversation not started by activating someone keeps the
  game's aim. ccm.status "conversation" reports the speaker and how often the game's own aim replaced ours.
- Precise sliders everywhere on CCM's pages (include/PreciseSlider.h, shared with Minimap Menu): a keyboard or D-pad nudge
  moves exactly one unit of the last digit shown - the conversation's over-the-shoulder slider had stepped 3 cm (the
  owner: "all of our sliders are precise sliders and they don't jump more than one numerical unit per D-pad nudge").

### Added (vanity camera off)
- The idle vanity camera can be switched off (the owner, 2026-09-29: "I want a setting in CCM to disable the vanity
  camera that activates whenever you time out from inactivity"). [General] bVanityCamera had been declared but never
  read; now 0 keeps the camera manager's vanity timer stopped (StopVanityCameraTimer, re-applied once a second because
  input restarts it) and leaves a vanity camera that had already started (the controller's ExitVanityCamera); turned
  back on, the game's timer is set again (SetVanityCameraTimer). A switch on the Camera page: "The idle camera that
  circles you".

### Added (conversation camera)
- A Conversation tab (the owner, relayed 2026-09-29: "add to CCM a conversation camera tab"): a ninth framing context,
  Conversation, detected by the game's own camera state State.Camera.Dialogue (seen in CCM's log) and above every other
  context, with its own AMF page: its own position (side / height / distance added to the game's conversation camera,
  or Standing's) and the free camera's behaviour in conversations. [Framing.Conversation] bOwn / fSide / fHeight /
  fDistance; the Framing page's context list has it too.
- [General] bStandDownInDialogue now does what its description always said - it was declared but never read: while a
  conversation runs the game's own camera switches are put back (the free camera stands down); off keeps the free camera.

### Changed (keys)
- CCM's default keys moved off K and L, which go to Minimap Menu (the owner, 2026-09-29: "we haven't even released CCM,
  so we can change its camera shoulder keys. Let's use K and L" for the minimap): the shoulder swap is now [ (26) and
  the camera style ] (27). DEFAULT-KEYS.md updated. The test install moves a player INI still holding the old defaults.

### Added
- "Screen shake while sprinting" (the owner's name; [Smoothing] bSprintShake, on by default = the game's shake; off = a
  steady camera). The owner: "I don't see any setting for camera smoothing so that whenever you are sprinting, the
  camera is stable and not shaking". The shake is two looping LegacyCameraShake Blueprints the game plays from
  DefaultAltar.ini's VOblivionRuntimeSettings lists on the sprint tag (BP_SprintCameraShake_C / _FP_C - read from the
  pak, plan folder SPRINT-SHAKE.md); off zeroes their oscillation amplitudes on the class default object and every live
  or pooled instance, and on puts the recorded values back. Not yet seen in game.

### Changed
- The bow context is "a bow out" (the owner: "when holding a weapon that is a bow, just to separate it from other melee
  weapons. Not necessarily when it's being drawn"): weapon drawn and the held weapon's WeaponTypeTag names a bow (read
  as Ultimate Combat Redux reads it). [Framing.Bow] replaces [Framing.BowAiming]; the order is horseback, swimming, bow,
  sneaking, sprinting, weapon drawn, moving, standing. In game the other contexts were detected correctly (sneaking via
  State.Camera.DrawingWeapon.Sneaking / bIsSneaking, sprinting, weapon drawn).
- ccm.status reports selection, aim (the camera calibration), the activate press, the marker and the crosshair.
- the previous launch's log is kept as CameraConfigurationMenu.prev.log.

### Fixed (round 1, 12:22)
- nothing in CCM ran: Unreal's property-layout self-check ran 2 s into the launch, read KeyIndex as -1 (the class's
  property chain not linked yet) and latched a failure - so the camera was never read and no class was watched. A
  property not found is now "not ready" (asked again); only a wrong offset latches.
- the crosshair was left alone: the first instance of the holder class had no built tree (a template). Every instance is
  now tried, and the one whose own bound `Crosshair` property is set is used.

### Fixed (round 2 - the owner: "I turned off the Ultimate Combat Redux free camera and tried to use the CCM free camera and it doesn't work")
- the free camera did nothing: CCM wrote its four flags BEFORE the player's Blueprint ReceiveTick, which re-applies the
  stance flags (probe P2), so every write was undone the same frame. UCR writes the same flags in a POST hook; pe::Watch
  now has post handlers and the player's tick is watched after its body.
- first person was read from the first-person arm's bVisible; it now comes from PlayerCharacter::is3rdPerson (proven in
  Better Third-Person Selection), held 150 ms before a switch counts.

### Added (round 2)
- the compass follows the camera (bCompassFollowsCamera, UCR's CompassBridge formula): the native
  VHUDMainViewModel::GetCompassDirectionValue's function pointer is swapped for a thunk that returns the controller's yaw
  + 90 (less the cell's north marker indoors) while the free camera is on in third person. ccm.status reports it.

### Known
- never run in game with a save loaded (the pause came before that): the free camera, the action facing, the shoulder
  swap and the new Selection / Crosshair all wait for the owner's first round.
- only English strings so far (the other ten languages are owed before any release - rule 66).
