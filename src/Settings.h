#pragma once

// CCM's settings. ONE table (Settings.cpp, kTable - one row per line) names every INI key, its section, its default
// and its range; Load, Save, the page and ccm.drive all go through it. tools/write-ini.py generates the shipped INI
// from the same rows, and at start the plugin checks that Values{} holds exactly the table's defaults - so the
// compiled defaults and the shipped file cannot drift (rule 16).

namespace settings
{
	enum class CameraStyle : std::int32_t
	{
		kVanilla = 0,     // the game's camera; CCM's framing still applies
		kFree = 1,        // Player Camera Main: free orbit, the body turns to face on block / cast / attack
		kFreeSheathed = 2 // Player Camera Alt: free orbit only with the weapon sheathed
	};

	struct Values
	{
		// [General] - plan 7.1
		bool        enabled = true;
		std::int32_t cameraStyle = 1;   // the owner, 2026-09-28: "Default to free cam"
		float       blockTurnSeconds = 0.20f;
		float       spellTurnSeconds = 1.40f;
		float       attackTurnSeconds = 0.60f;
		bool        faceWhileHeld = false;
		bool        faceWhileLockedOn = true;   // Ultimate Combat Redux's lock-on: the body faces the camera (its dodge needs it)
		float       bodyTurnSpeed = 0.0f;   // 0 = the game's own rotation rate
		bool        compassFollowsCamera = true;
		bool        freeCameraOnHorse = false;
		bool        standDownInDialogue = true;
		bool        conversationFirstPerson = false;   // first person while a conversation runs, the previous view after
		bool        conversationLockOnSpeaker = true;  // the camera aims at the person spoken to, whatever offset CCM adds
		bool        conversationFirstPersonNoOffset = true;   // first person in a conversation: no Conversation offset added
		bool        standDownSitting = true;
		bool        vanityCamera = true;

		// [Zoom] - the game's two third-person zooms, EVPlayerPOVType ThirdPersonClose (1) and ThirdPersonFar (2) (the owner,
		// 2026-09-29: "when you load into the game you're already fully zoomed out ... sliders for both zoom settings")
		bool        startZoomedOut = true;      // after a load, the far zoom instead of the close one
		bool        ownZoomDistances = false;   // the two distances below replace the game's own
		float       zoomCloseDistance = 170.0f; // cm behind the character (the game's close zoom read 170 with a weapon drawn, probe P2)
		float       zoomFarDistance = 310.0f;   // cm (the game's far zoom read 310)

		// [Keys] - DirectInput scan codes / XInput masks, 0 = unbound (plan 7.4, .MD\DEFAULT-KEYS.md)
		std::int32_t shoulderSwapKey = 0x1A;   // [ - K and L went to Minimap Menu (the owner, 2026-09-29: "we haven't even released CCM, so we can change its camera shoulder keys")
		std::int32_t cycleStyleKey = 0x1B;     // ]
		std::int32_t toggleKey = 0;            // the four other keys wait on probe P15 (the vanilla keyboard map)
		std::int32_t nextPresetKey = 0;
		std::int32_t heightOffsetKey = 0;
		std::int32_t customGroupKey = 0;
		std::int32_t shoulderSwapButton = 0;   // gamepad rows ship unbound: every button has a vanilla meaning
		std::int32_t cycleStyleButton = 0;
		std::int32_t toggleButton = 0;
		std::int32_t nextPresetButton = 0;
		std::int32_t heightOffsetButton = 0;
		std::int32_t customGroupButton = 0;

		// [Framing.<Context>] - where the camera sits, in centimetres ADDED to the game's own value for the state (plan
		// 7.2): 0 = the unmodded camera. One entry per framing::Group; [0] Standing is also used by every context whose
		// bOwn is off. Side mirrors with the shoulder swap.
		struct FramingGroup
		{
			bool  own = false;       // this context has its own position (Standing ignores it)
			float side = 0.0f;       // -150-150, + = further to the right of the character
			float height = 0.0f;     // -100-150
			float distance = 0.0f;   // -300-600, + = further back
			float fov = 0.0f;        // degrees added to the field of view (Aiming a bow and Conversation have a row)
		};
		// a C array, never std::array: offsetof(Values, fmGroups[3].side) through std::array::operator[] resolved to
		// offset 0-12 under MSVC and the table read and wrote enabled / cameraStyle / the turn times (2026-09-29)
		FramingGroup fmGroups[10]{};   // [8] Conversation (2026-09-29), [9] Aiming a bow (2026-09-30)

		// [Smoothing] - plan 7.3. -1 = the game's own value for the state.
		bool         smEaseOffsets = true;     // a new position slides in instead of cutting
		std::int32_t smOffsetEasing = 15;      // framing::Ease curve, 15 = sine in-out
		float        smOffsetSeconds = 0.5f;   // 0-5
		float        smFollowSpeed = -1.0f;    // the arm's CameraLagSpeed, 0 = rigid (no position smoothing), -1 = the game's
		float        smMaxLagDistance = -1.0f; // the arm's CameraLagMaxDistance, 0 = no limit
		float        smRotationPitch = -1.0f;  // the arm's CameraRotationLagSpeedPitch, 0 = no rotation smoothing
		float        smRotationYaw = -1.0f;    // the arm's CameraRotationLagSpeedYaw
		bool         smSprintShake = true;     // "Screen shake while sprinting" (the owner's name): 1 = the game's shake, 0 = a steady camera
		float        smStateBlendSeconds = -1.0f;   // TransitionDuration: the blend between the game's camera states

		// [Selection] - Better Third-Person Selection 1.0.0, merged (plan 13.2; the owner's defaults, 2026-09-29)
		bool  selEnabled = true;
		bool  selThirdPerson = true;
		bool  selFirstPerson = false;
		bool  selShowMarker = true;
		float selRange = 300.0f;      // game units from the character, 50-400
		float selMaxAngle = 35.0f;    // degrees either side of the camera's aim, 5-75
		bool  selLogTargets = false;  // a log line per change of the game's pick or this choice (a test aid)

		// [Crosshair] - the contextual crosshair (plan 13.3; "hides the crosshair optionally": the game's by default)
		std::int32_t xhMode = 0;      // 0 = the game's, 1 = contextual, 2 = always hidden
		bool  xhWhenAiming = true;    // contextual: shown while a bow is drawn or a spell is being cast
		bool  xhWhenTarget = true;    //             ... while there is something to activate
		bool  xhWhenWeaponDrawn = false;
		bool  xhInFirstPerson = true; //             ... always in first person
		float xhFadeSeconds = 0.15f;  // how long it takes to fade in or out

		// [Presets]
		std::int32_t activePreset = 0;   // 0 = none, 1-6 = the user slots

		// [Log]
		std::int32_t logLevel = 2;   // info (rule 14, amended 2026-09-26)
	};

	// One row of the table: where a value lives in the INI and in Values.
	struct Field
	{
		const char* section;
		const char* key;
		enum class Kind { kBool, kInt, kFloat } kind;
		std::size_t offset;   // into Values
		double      def;      // the compiled default; Load() checks Values{} agrees with it at start (rule 16)
		double      min;
		double      max;
		const char* comment;  // the line written above the key in the shipped INI
	};

	const std::vector<Field>& Table();

	Values& Get();                     // game thread and page both read it; writes go through Set / Load
	Values  Defaults();
	std::filesystem::path IniPath();   // <game>\Binaries\Win64\OBSE\Plugins\CameraConfigurationMenu.ini
	std::filesystem::path PluginFolder();

	void Load();   // missing keys keep the compiled default and are logged at debug
	bool Save();   // rewrites only CCM's keys, in place; comments and unknown lines are kept

	// Generic access by "Section.Key" for ccm.drive: returns false (and a reason) for an unknown key or a value out
	// of range. The value is clamped to the table's range, never silently wrapped.
	bool SetByName(const std::string& a_name, const json& a_value, std::string& a_why);
	json GetAll();

	// The full INI text as the compiled defaults write it - what tools/write-ini.py and the gate compare against.
	std::string DefaultIniText();
}
