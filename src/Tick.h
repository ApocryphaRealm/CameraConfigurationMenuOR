#pragma once

// ============================================================================================================
// The game-thread frame tick, by chaining the game executable's USER32!PeekMessageW import (the previous target is
// kept and called, so other plugins chaining the same slot work in either order). Unreal's message pump calls it
// every frame on the main thread; the callback runs at most once every 2 ms. OBSE64 has no main-loop interface.
// Taken from Weightless Menu (Tween Menu for Oblivion before it), without a controller hook - this mod never reads the pad.
// ============================================================================================================

namespace tick
{
	using FrameCallback = void (*)();
	bool Install(FrameCallback a_frame);   // at OBSE's post-load, on the game's main thread
	std::uint64_t Reads();
}
