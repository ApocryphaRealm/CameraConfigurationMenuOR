#pragma once

// UE headers BEFORE Windows.h: wingdi's TRUE/FALSE macros break the EName enum (TestBench, 2026-09-26).
#include <UE/Unreal.h>
#include <RE/Oblivion.h>
#include <OBSE/OBSE.h>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ShlObj.h>

// wingdi.h defines ERROR as 0, which turns every REX::ERROR log call into a syntax error
#ifdef ERROR
#	undef ERROR
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "Logger.h"

using json = nlohmann::json;
using namespace std::literals;
