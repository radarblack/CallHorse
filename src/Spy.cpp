#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define CALLER_ADDRESS() _ReturnAddress()
#else
#define CALLER_ADDRESS() __builtin_return_address(0)
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "GameAddresses.h"
#include "InlineHook.h"
#include "Log.h"
#include "Spy.h"

namespace
{
    constexpr std::uintptr_t kPreferredBase = 0x140000000ull;

    constexpr std::size_t kStolenHelper   = 15;
    constexpr std::size_t kStolenWrapper  = 14;
    constexpr std::size_t kStolenExecutor = 15;
    constexpr std::size_t kStolenGate     = 15;

    constexpr std::size_t kCommandDumpOffset = 0x10;
    constexpr std::size_t kCommandDumpLength = 0x18;

    using HelperFn   = void (*)(void*, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t);
    using WrapperFn  = std::uint8_t (*)(void*, std::uint8_t, std::uint8_t);
    using ExecutorFn = std::uint8_t (*)(void*, std::uint8_t, const std::uint8_t*);
    using GateFn     = std::uint8_t (*)(void*, std::uint8_t, std::uint8_t, std::uint8_t);

    HelperFn   g_helperOriginal   = nullptr;
    WrapperFn  g_wrapperOriginal  = nullptr;
    ExecutorFn g_executorOriginal = nullptr;
    GateFn     g_gateOriginal     = nullptr;

    inlinehook::Hook g_helperHook;
    inlinehook::Hook g_wrapperHook;
    inlinehook::Hook g_executorHook;
    inlinehook::Hook g_gateHook;

    bool g_installed = false;
    bool g_enabled   = false;
    int  g_ownDepth  = 0;

    std::uint8_t g_lastTuple[6] = {};
    bool         g_haveTuple    = false;
    unsigned     g_repeats      = 0;

    struct LastCommand
    {
        std::uint32_t sequence   = 0;
        std::uint8_t  command[4] = {};
        bool          valid      = false;
    };

    LastCommand g_last;

    unsigned long long Preferred(const void* address)
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        return static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) - base + kPreferredBase);
    }

    const char* Source()
    {
        return g_ownDepth > 0 ? "plugin" : "game";
    }

    void OnHelper(std::uint8_t e61, std::uint8_t e62, std::uint8_t e63, std::uint8_t e64,
                  std::uint8_t trigger, const void* caller)
    {
        const bool own = g_ownDepth > 0;
        const std::uint8_t tuple[6] = { e61, e62, e63, e64, trigger, static_cast<std::uint8_t>(own ? 1 : 0) };
        const bool changed = !g_haveTuple || std::memcmp(tuple, g_lastTuple, sizeof(tuple)) != 0;

        if (trigger != 0 || changed)
        {
            if (g_repeats > 0)
            {
                Log("spy: previous helper call repeated %u more times", g_repeats);
                g_repeats = 0;
            }

            Log("spy: helper e61=%u e62=%u e63=%u e64=%u trigger=%u source=%s caller=0x%llX",
                static_cast<unsigned>(e61), static_cast<unsigned>(e62), static_cast<unsigned>(e63),
                static_cast<unsigned>(e64), static_cast<unsigned>(trigger), Source(), Preferred(caller));

            std::memcpy(g_lastTuple, tuple, sizeof(tuple));
            g_haveTuple = true;
        }
        else
        {
            ++g_repeats;
        }

        if (trigger != 0 && !own)
        {
            ++g_last.sequence;
            g_last.command[0] = e61;
            g_last.command[1] = e62;
            g_last.command[2] = e63;
            g_last.command[3] = e64;
            g_last.valid      = true;
        }
    }

    void HelperDetour(void* self, std::uint8_t e62, std::uint8_t e61, std::uint8_t e63, std::uint8_t e64, std::uint8_t trigger)
    {
        if (g_enabled)
            OnHelper(e61, e62, e63, e64, trigger, CALLER_ADDRESS());

        g_helperOriginal(self, e62, e61, e63, e64, trigger);
    }

    std::uint8_t WrapperDetour(void* manager, std::uint8_t buddyType, std::uint8_t force)
    {
        const void* caller = CALLER_ADDRESS();
        const std::uint8_t result = g_wrapperOriginal(manager, buddyType, force);

        if (g_enabled)
        {
            Log("spy: wrapper type=%u force=%u -> %u source=%s caller=0x%llX",
                static_cast<unsigned>(buddyType), static_cast<unsigned>(force), static_cast<unsigned>(result),
                Source(), Preferred(caller));
        }

        return result;
    }

    std::uint8_t ExecutorDetour(void* self, std::uint8_t buddyType, const std::uint8_t* command)
    {
        const void* caller = CALLER_ADDRESS();

        if (g_enabled)
        {
            char dump[kCommandDumpLength * 3 + 1] = {};
            if (command && game::IsReadable(command + kCommandDumpOffset, kCommandDumpLength))
            {
                for (std::size_t index = 0; index < kCommandDumpLength; ++index)
                    std::snprintf(dump + index * 3, 4, "%02X ", command[kCommandDumpOffset + index]);
            }
            else
            {
                std::snprintf(dump, sizeof(dump), "unreadable");
            }

            Log("spy: executor type=%u command[+0x10..+0x27]=%s source=%s caller=0x%llX",
                static_cast<unsigned>(buddyType), dump, Source(), Preferred(caller));
        }

        return g_executorOriginal(self, buddyType, command);
    }

    std::uint8_t GateDetour(void* controller, std::uint8_t buddyType, std::uint8_t mode, std::uint8_t force)
    {
        const void* caller = CALLER_ADDRESS();
        const std::uint8_t result = g_gateOriginal(controller, buddyType, mode, force);

        if (g_enabled)
        {
            Log("spy: gate type=%u mode=%u force=%u -> %u source=%s caller=0x%llX",
                static_cast<unsigned>(buddyType), static_cast<unsigned>(mode), static_cast<unsigned>(force),
                static_cast<unsigned>(result), Source(), Preferred(caller));
        }

        return result;
    }

    struct Spec
    {
        const char*       name;
        void*             target;
        std::size_t       stolen;
        void*             detour;
        void**            original;
        inlinehook::Hook* hook;
    };

    bool InstallAll()
    {
        if (!game::ResolveSpy())
            return false;

        const game::BuddyApi& buddy = game::Buddy();
        const game::SpyApi&   spy   = game::Spy();

        Spec specs[] =
        {
            { "helper",   reinterpret_cast<void*>(buddy.horseCommand), kStolenHelper,   reinterpret_cast<void*>(&HelperDetour),   reinterpret_cast<void**>(&g_helperOriginal),   &g_helperHook },
            { "wrapper",  reinterpret_cast<void*>(buddy.callWrapper),  kStolenWrapper,  reinterpret_cast<void*>(&WrapperDetour),  reinterpret_cast<void**>(&g_wrapperOriginal),  &g_wrapperHook },
            { "executor", spy.executor,                                kStolenExecutor, reinterpret_cast<void*>(&ExecutorDetour), reinterpret_cast<void**>(&g_executorOriginal), &g_executorHook },
            { "gate",     spy.gate,                                    kStolenGate,     reinterpret_cast<void*>(&GateDetour),     reinterpret_cast<void**>(&g_gateOriginal),     &g_gateHook },
        };

        std::size_t installed = 0;
        for (Spec& spec : specs)
        {
            if (!inlinehook::Install(*spec.hook, spec.target, spec.stolen, spec.detour, spec.original))
            {
                Log("spy: could not hook %s, rolling back", spec.name);

                for (std::size_t index = 0; index < installed; ++index)
                    inlinehook::Remove(*specs[index].hook);

                return false;
            }

            ++installed;
            Log("spy: hooked %s at %p", spec.name, spec.target);
        }

        return true;
    }
}

namespace spy
{
    bool Enable(bool enabled)
    {
        if (enabled && !g_installed)
        {
            if (!InstallAll())
                return false;

            g_installed = true;
        }

        g_enabled = enabled;
        Log("spy %s", enabled ? "enabled" : "disabled");
        return true;
    }

    bool GetLastGameHorseCommand(std::uint32_t& sequence, std::uint8_t (&command)[4])
    {
        if (!g_last.valid)
            return false;

        sequence = g_last.sequence;
        for (std::size_t index = 0; index < 4; ++index)
            command[index] = g_last.command[index];

        return true;
    }

    void BeginOwnCall()
    {
        ++g_ownDepth;
    }

    void EndOwnCall()
    {
        if (g_ownDepth > 0)
            --g_ownDepth;
    }
}
