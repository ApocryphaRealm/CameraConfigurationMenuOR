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
- ccm.status reports selection, aim (the camera calibration), the activate press, the marker and the crosshair.
- the previous launch's log is kept as CameraConfigurationMenu.prev.log.

### Fixed (round 1, 12:22)
- nothing in CCM ran: Unreal's property-layout self-check ran 2 s into the launch, read KeyIndex as -1 (the class's
  property chain not linked yet) and latched a failure - so the camera was never read and no class was watched. A
  property not found is now "not ready" (asked again); only a wrong offset latches.
- the crosshair was left alone: the first instance of the holder class had no built tree (a template). Every instance is
  now tried, and the one whose own bound `Crosshair` property is set is used.

### Known
- never run in game with a save loaded (the pause came before that): the free camera, the action facing, the shoulder
  swap and the new Selection / Crosshair all wait for the owner's first round.
- only English strings so far (the other ten languages are owed before any release - rule 66).
