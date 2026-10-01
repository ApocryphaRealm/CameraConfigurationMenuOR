Camera Configuration Menu (CCM)
===============================
Version 1.0.0

Set up Oblivion Remastered's third-person camera from a settings page in Apocrypha Menu Framework: where the
camera sits in every situation, a free camera that lets you look around your character, a conversation camera,
a crosshair that shows only when it means something, and selecting what you look at in third person.

WHAT YOU GET
------------
  * Camera style: the game's own camera, a free camera (the body turns to face where you look when you block,
    cast or attack, and for how long is set per action), or the free camera only with the weapon sheathed.
    The compass can follow the camera instead of the body. Optional on horseback.
  * Framing per situation: standing, moving, sprinting, sneaking, a weapon drawn, holding a bow, aiming a bow
    (with its own field of view), swimming, on horseback and in conversation. Each moves the camera sideways,
    up and back from where the game puts it, or uses Standing's position. Shoulder swap on a key.
  * Conversation camera: the game's first-person view, or a third-person view held on the person you talk to.
  * Zoom: start at the far zoom after loading, or replace the game's two zoom distances.
  * Smoothing: new positions slide in on a chosen curve; how tightly the camera follows movement, looking up
    and down and turning; the sprint screen shake on or off; the blend between the game's camera states.
  * Third-person selection (Better Third-Person Selection, merged in): Activate uses what you are roughly
    looking at, within reach and angle, closest to the aim first, with its name shown where it stands.
  * Contextual crosshair: the game's crosshair, hidden, or shown only while a bow is drawn or a spell is cast,
    while there is something to activate, while a weapon is drawn, or always in first person.
  * The game's idle "vanity" camera on or off, the game's own camera during dialogue and while sitting.
  * The game's own values are recorded first and put back when CCM is turned off.
  * Eleven languages.

KNOWN ISSUES
------------
  * The crosshair can stay hidden while aiming a bow (the game hides it while drawing). Being worked on.
  * No target lock-on of its own yet. With Ultimate Combat Redux installed, its lock-on is used and CCM's free
    camera stands down while it is locked on.
  * Keys and controller buttons are set in the INI (scan codes and XInput masks); there is no binding row on the
    page yet. Two keys ship bound: [ (shoulder swap) and ] (switch the free camera style). No controller button
    ships bound - every button already has a game action.
  * Do not install Better Third-Person Selection beside CCM - it is merged in.

INSTALLATION
------------
  * Everything goes under OblivionRemastered\Binaries\Win64\OBSE\Plugins\: CameraConfigurationMenu.dll, .pdb,
    .ini, and the translation files under ApocryphaMenuFramework\Translations\.
  * Install with your mod manager or drop the OblivionRemastered folder over the game's own. Mod Organizer 2
    users need Root Builder, as for every OBSE64 plugin.
  * Start the game through OBSE64. Requires OBSE64, Address Library for OBSE Plugins and Apocrypha Menu
    Framework for Oblivion Remastered (1.0.1 or newer) for the settings page; without the framework the INI
    still applies.

SETTINGS
--------
  CameraConfigurationMenu.ini beside the DLL, written by the page. Edit it by hand only with the game closed.
  Every offset ships at 0 - the game's own camera - so nothing changes until you change it.

DEBUGGING
---------
  Send the log with any bug report:
  Documents\My Games\Oblivion Remastered\OBSE\Logs\CameraConfigurationMenu.log
  Set uLogLevel=1 in the INI's [Log] section for more detail. The page's Status tab shows what CCM found and is
  doing. The debug symbols (.pdb) ship in this download, so a crash log names this mod's functions.

REQUIREMENTS
------------
  * OBSE64 (Nexus 282), 0.2.2 or newer
  * Address Library for OBSE Plugins (Nexus 4475)
  * Apocrypha Menu Framework for Oblivion Remastered, for the settings page
  * Optional: Ultimate Combat Redux, for target lock-on

CREDITS
-------
  The conversation camera holds on the speaker the way Ultimate Combat's lock-on holds a target (Kramer7046's
  Ultimate Combat, whose permissions allow modification with credit).

LICENCE
-------
  GPL-3.0-or-later (LICENSE, NOTICE.md). Built on CommonLibOB64 (GPL-3.0); Dear ImGui (MIT) through the
  framework's own context. Source: https://github.com/ApocryphaRealm/CameraConfigurationMenuOR
