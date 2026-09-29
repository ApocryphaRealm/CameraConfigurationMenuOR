# CCM - Camera Configuration Menu - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Started 2026-09-29; the history before this file is
in `git log`. A version number is issued by the version gate only once a build is seen working in game (rule 48).

## Unreleased - 2026-09-29 - untested (resumed)

The owner, 2026-09-29: "work on completing the better third person camera project ... incorporate better third person
selection into that mod and also add on a contextual crosshair that hides the crosshair optionally" - CCM, keeping the
name Camera Configuration Menu. Plan: 4. plans\Camera Configuration Menu (CCM)\PLAN.md section 13.

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
