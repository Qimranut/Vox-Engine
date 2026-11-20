#pragma once

// ---------------------------------------------------------------------------
//  Precompiled header.
//
//  Stable, rarely-edited headers ONLY - STL and platform. Nothing from
//  Engine/ belongs here: a PCH containing your own headers means every edit
//  rebuilds every translation unit.
//
//  NOTE: a PCH also hides missing includes. Every header must still include
//  what it uses - do not rely on this file to supply it.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <any>
#include <array>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>
