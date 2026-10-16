#include "BuddyWhistle.h"
#include "GameAddresses.h"
#include "Log.h"
#include "LuaBridge.h"

namespace
{
    constexpr const char* kLibraryName = "CallHorseNative";
    constexpr int         kLuaTypeNil  = 0;

    struct LuaReg
    {
        const char* name;
        int       (*function)(void* L);
    };

    int WhistleBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        for (int index = 1; index <= 5; ++index)
        {
            if (!lua.isnumber(L, index))
            {
                lua.pushboolean(L, 0);
                return 1;
            }
        }

        const double typeValue = lua.tonumber(L, 1);
        if (!(typeValue >= 0.0 && typeValue <= 255.0))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        const float x               = static_cast<float>(lua.tonumber(L, 2));
        const float y               = static_cast<float>(lua.tonumber(L, 3));
        const float z               = static_cast<float>(lua.tonumber(L, 4));
        const float rotationRadians = static_cast<float>(lua.tonumber(L, 5));
        const bool  force           = lua.type(L, 6) <= kLuaTypeNil || lua.toboolean(L, 6) != 0;

        const bool accepted = buddy::Whistle(static_cast<int>(typeValue), x, y, z, rotationRadians, force);
        lua.pushboolean(L, accepted ? 1 : 0);
        return 1;
    }

    bool InRange(double value)
    {
        return value >= 0.0 && value <= 255.0;
    }

    int GetHorseCommandStateBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        std::uint8_t state[4] = {};
        if (!buddy::GetHorseCommandState(state))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        for (int index = 0; index < 4; ++index)
            lua.pushnumber(L, static_cast<double>(state[index]));

        return 4;
    }

    int SetHorseCommandStateBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        double values[4] = {};
        for (int index = 0; index < 4; ++index)
        {
            if (!lua.isnumber(L, index + 1))
            {
                lua.pushboolean(L, 0);
                return 1;
            }

            values[index] = lua.tonumber(L, index + 1);
            if (!InRange(values[index]))
            {
                lua.pushboolean(L, 0);
                return 1;
            }
        }

        const bool trigger = lua.type(L, 5) <= kLuaTypeNil || lua.toboolean(L, 5) != 0;

        const bool done = buddy::SetHorseCommandState(
            static_cast<int>(values[0]),
            static_cast<int>(values[1]),
            static_cast<int>(values[2]),
            static_cast<int>(values[3]),
            trigger);

        lua.pushboolean(L, done ? 1 : 0);
        return 1;
    }

    int IsAvailableBinding(void* L)
    {
        game::Lua().pushboolean(L, buddy::Available() ? 1 : 0);
        return 1;
    }

    const LuaReg kLibrary[] =
    {
        { "Whistle",     WhistleBinding },
        { "IsAvailable", IsAvailableBinding },
        { "GetHorseCommandState", GetHorseCommandStateBinding },
        { "SetHorseCommandState", SetHorseCommandStateBinding },
        { nullptr,       nullptr }
    };
}

namespace bridge
{
    bool RegisterLibrary(void* L)
    {
        if (!L)
            return false;

        if (!game::Resolve())
            return false;

        game::Lua().registerLibrary(L, kLibraryName, kLibrary);
        Log("registered Lua library %s", kLibraryName);
        return true;
    }
}
