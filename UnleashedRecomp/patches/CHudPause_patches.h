#pragma once

#include <atomic>

// True while the in-stage pause is active.
inline std::atomic<bool> g_isPauseMenuVisible{ false };
