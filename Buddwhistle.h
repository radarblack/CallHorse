#pragma once

extern "C" {
#include "lua.h"
}

int __cdecl l_WhistleBuddy(lua_State* L);
int __cdecl l_IsBuddyWhistleAvailable(lua_State* L);
