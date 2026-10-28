#pragma once

// CommonLibSSE-NG must come before <Windows.h>/<d3d11.h>: its WinAPI shim declares
// constants (FILE_ATTRIBUTE_*, MEM_*, ...) that Windows.h would turn into macros.
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::literals;
