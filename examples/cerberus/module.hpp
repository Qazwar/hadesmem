// Copyright (C) 2010-2014 Joshua Boyce.
// See the file COPYING for copying permission.

#pragma once

#include <functional>
#include <string>

#include <windows.h>

namespace hadesmem
{

namespace cerberus
{

void DetourNtMapViewOfSection();

void DetourNtUnmapViewOfSection();

void UndetourNtMapViewOfSection();

void UndetourNtUnmapViewOfSection();

typedef void OnMapCallback(HMODULE module,
                           std::wstring const& path,
                           std::wstring const& name);

std::size_t RegisterOnMapCallback(std::function<OnMapCallback> const& callback);

void UnregisterOnMapCallback(std::size_t id);

typedef void OnUnmapCallback(HMODULE module);

std::size_t
  RegisterOnUnmapCallback(std::function<OnUnmapCallback> const& callback);

void UnregisterOnUnmapCallback(std::size_t id);
}
}
