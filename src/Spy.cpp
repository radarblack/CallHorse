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

    constexpr std::size_t kStolenHelper     = 15;
    constexpr std::size_t kStolenWrapper    = 14;
    constexpr std::size_t kStolenExecutor   = 15;
    constexpr std::size_t kStolenGate       = 15;
    constexpr std::size_t kDispatcherRipOffset = 12;

    constexpr std::size_t kCommandDumpOffset = 0x14;
    constexpr std::size_t kCommandDumpLength = 0x14;
    constexpr std::size_t kCommandBytes      = 0x68;
    constexpr std::size_t kRingSize          = 64;

    using HelperFn     = void (*)(void*, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t);
    using WrapperFn    = std::uint8_t (*)(void*, std::uint8_t, std::uint8_t);
    using ExecutorFn   = std::uint8_t (*)(void*, std::uint8_t, const std::uint8_t*);
    using GateFn       = std::uint8_t (*)(void*, std::uint8_t, std::uint8_t, std::uint8_t);
    using DispatcherFn = int* (*)(void*, int*, std::uint32_t, const std::uint8_t*);

    HelperFn     g_helperOriginal     = nullptr;
    WrapperFn    g_wrapperOriginal    = nullptr;
    ExecutorFn   g_executorOriginal   = nullptr;
    GateFn       g_gateOriginal       = nullptr;
    DispatcherFn g_dispatcherOriginal = nullptr;

    inlinehook::Hook g_helperHook;
    inlinehook::Hook g_wrapperHook;
    inlinehook::Hook g_executorHook;
    inlinehook::Hook g_gateHook;
    inlinehook::Hook g_dispatcherHook;

    bool g_installed = false;
    bool g_enabled   = false;
    int  g_ownDepth  = 0;

    struct Dedup
    {
        char               key[320]  = {};
        unsigned           repeats   = 0;
        unsigned long long firstTick = 0;
        bool               has       = false;
    };

    Dedup g_helperDedup;
    Dedup g_wrapperDedup;
    Dedup g_executorDedup;
    Dedup g_gateDedup;
    Dedup g_dispatcherDedup;

    struct LastHorseCommand
    {
        std::uint32_t sequence   = 0;
        std::uint8_t  command[4] = {};
        bool          valid      = false;
    };

    LastHorseCommand g_lastHorse;

    spy::BuddyCommandRecord g_lastBuddy;
    spy::BuddyCommandRecord g_ring[kRingSize];
    bool                    g_haveBuddy = false;

    unsigned long long Preferred(const void* address)
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        return static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) - base + kPreferredBase);
    }

    const char* Source()
    {
        return g_ownDepth > 0 ? "plugin" : "game";
    }

    void Emit(Dedup& state, const char* name, const char* key, const char* line)
    {
        if (state.has && std::strcmp(state.key, key) == 0)
        {
            ++state.repeats;
            return;
        }

        if (state.repeats > 0)
        {
            Log("spy: previous %s line repeated %u more times over %llu ms", name, state.repeats,
                static_cast<unsigned long long>(GetTickCount64() - state.firstTick));
            state.repeats = 0;
        }

        std::snprintf(state.key, sizeof(state.key), "%s", key);
        state.has       = true;
        state.firstTick = GetTickCount64();
        Log("%s", line);
    }

    void OnHelper(std::uint8_t e61, std::uint8_t e62, std::uint8_t e63, std::uint8_t e64,
                  std::uint8_t trigger, const void* caller)
    {
        char key[320];
        char line[480];

        std::snprintf(key, sizeof(key), "e61=%u e62=%u e63=%u e64=%u trigger=%u source=%s",
            static_cast<unsigned>(e61), static_cast<unsigned>(e62), static_cast<unsigned>(e63),
            static_cast<unsigned>(e64), static_cast<unsigned>(trigger), Source());

        std::snprintf(line, sizeof(line), "spy: helper %s caller=0x%llX", key, Preferred(caller));

        Emit(g_helperDedup, "helper", key, line);

        if (trigger != 0 && g_ownDepth == 0)
        {
            ++g_lastHorse.sequence;
            g_lastHorse.command[0] = e61;
            g_lastHorse.command[1] = e62;
            g_lastHorse.command[2] = e63;
            g_lastHorse.command[3] = e64;
            g_lastHorse.valid      = true;
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
            char key[320];
            char line[480];

            std::snprintf(key, sizeof(key), "type=%u force=%u -> %u source=%s",
                static_cast<unsigned>(buddyType), static_cast<unsigned>(force), static_cast<unsigned>(result), Source());

            std::snprintf(line, sizeof(line), "spy: wrapper %s caller=0x%llX", key, Preferred(caller));

            Emit(g_wrapperDedup, "wrapper", key, line);
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

            char key[320];
            char line[480];

            std::snprintf(key, sizeof(key), "type=%u command[+0x14..+0x27]=%s source=%s",
                static_cast<unsigned>(buddyType), dump, Source());

            std::snprintf(line, sizeof(line), "spy: executor %s caller=0x%llX", key, Preferred(caller));

            Emit(g_executorDedup, "executor", key, line);
        }

        return g_executorOriginal(self, buddyType, command);
    }

    std::uint8_t GateDetour(void* controller, std::uint8_t buddyType, std::uint8_t mode, std::uint8_t force)
    {
        const void* caller = CALLER_ADDRESS();
        const std::uint8_t result = g_gateOriginal(controller, buddyType, mode, force);

        if (g_enabled)
        {
            char key[320];
            char line[480];

            std::snprintf(key, sizeof(key), "type=%u mode=%u force=%u -> %u source=%s",
                static_cast<unsigned>(buddyType), static_cast<unsigned>(mode), static_cast<unsigned>(force),
                static_cast<unsigned>(result), Source());

            std::snprintf(line, sizeof(line), "spy: gate %s caller=0x%llX", key, Preferred(caller));

            Emit(g_gateDedup, "gate", key, line);
        }

        return result;
    }

    void OnDispatcher(std::uint32_t kind, const std::uint8_t* command, std::int32_t result, const void* caller)
    {
        if (!command || !game::IsReadable(command, kCommandBytes))
        {
            Log("spy: dispatcher kind=%u with an unreadable command", static_cast<unsigned>(kind));
            return;
        }

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float rotation = 0.0f;
        std::memcpy(&x, command + 0x00, sizeof(float));
        std::memcpy(&y, command + 0x04, sizeof(float));
        std::memcpy(&z, command + 0x08, sizeof(float));
        std::memcpy(&rotation, command + 0x10, sizeof(float));

        const std::uint8_t type = command[0x14];
        const std::uint8_t a18  = command[0x18];
        const std::uint8_t a1c  = command[0x1C];
        const std::uint8_t a20  = command[0x20];
        const std::uint8_t a24  = command[0x24];
        const std::uint8_t f64  = command[0x64];

        char key[320];
        char line[480];

        std::snprintf(key, sizeof(key), "kind=%u type=%u bytes=%u,%u,%u,%u flag=%u -> %d source=%s",
            static_cast<unsigned>(kind), static_cast<unsigned>(type), static_cast<unsigned>(a18),
            static_cast<unsigned>(a1c), static_cast<unsigned>(a20), static_cast<unsigned>(a24),
            static_cast<unsigned>(f64), static_cast<int>(result), Source());

        std::snprintf(line, sizeof(line), "spy: dispatcher %s pos=(%.2f, %.2f, %.2f) rot=%.2f caller=0x%llX",
            key, x, y, z, rotation, Preferred(caller));

        Emit(g_dispatcherDedup, "dispatcher", key, line);

        if (g_ownDepth > 0)
            return;

        const bool sameAsLast = g_haveBuddy && g_lastBuddy.kind == kind && g_lastBuddy.type == type &&
            g_lastBuddy.a18 == a18 && g_lastBuddy.a1c == a1c && g_lastBuddy.a20 == a20 &&
            g_lastBuddy.a24 == a24 && g_lastBuddy.f64 == f64;

        if (!sameAsLast || g_lastBuddy.result == 0)
        {
            const std::uint32_t sequence = g_lastBuddy.sequence + 1;

            g_lastBuddy          = spy::BuddyCommandRecord();
            g_lastBuddy.sequence = sequence;
            g_lastBuddy.kind     = kind;
            g_lastBuddy.type     = type;
            g_lastBuddy.x        = x;
            g_lastBuddy.y        = y;
            g_lastBuddy.z        = z;
            g_lastBuddy.rotation = rotation;
            g_lastBuddy.a18      = a18;
            g_lastBuddy.a1c      = a1c;
            g_lastBuddy.a20      = a20;
            g_lastBuddy.a24      = a24;
            g_lastBuddy.f64      = f64;
            g_haveBuddy          = true;
        }

        g_lastBuddy.result = result;
        g_ring[g_lastBuddy.sequence % kRingSize] = g_lastBuddy;
    }

    int* DispatcherDetour(void* self, int* result, std::uint32_t kind, const std::uint8_t* command)
    {
        const void* caller = CALLER_ADDRESS();
        int* returned = g_dispatcherOriginal(self, result, kind, command);

        if (g_enabled && result)
            OnDispatcher(kind, command, static_cast<std::int32_t>(*result), caller);

        return returned;
    }

    struct Spec
    {
        const char*       name;
        void*             target;
        std::size_t       stolen;
        std::size_t       ripLoadOffset;
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
            { "helper",     reinterpret_cast<void*>(buddy.horseCommand), kStolenHelper,   0, reinterpret_cast<void*>(&HelperDetour),     reinterpret_cast<void**>(&g_helperOriginal),     &g_helperHook },
            { "wrapper",    reinterpret_cast<void*>(buddy.callWrapper),  kStolenWrapper,  0, reinterpret_cast<void*>(&WrapperDetour),    reinterpret_cast<void**>(&g_wrapperOriginal),    &g_wrapperHook },
            { "executor",   spy.executor,                                kStolenExecutor, 0, reinterpret_cast<void*>(&ExecutorDetour),   reinterpret_cast<void**>(&g_executorOriginal),   &g_executorHook },
            { "gate",       spy.gate,                                    kStolenGate,     0, reinterpret_cast<void*>(&GateDetour),       reinterpret_cast<void**>(&g_gateOriginal),       &g_gateHook },
            { "dispatcher", spy.dispatcher,                              0, kDispatcherRipOffset, reinterpret_cast<void*>(&DispatcherDetour), reinterpret_cast<void**>(&g_dispatcherOriginal), &g_dispatcherHook },
        };

        std::size_t installed = 0;
        for (Spec& spec : specs)
        {
            const bool ok = spec.ripLoadOffset != 0
                ? inlinehook::InstallWithRipLoad(*spec.hook, spec.target, spec.ripLoadOffset, spec.detour, spec.original)
                : inlinehook::Install(*spec.hook, spec.target, spec.stolen, spec.detour, spec.original);

            if (!ok)
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
        if (!g_lastHorse.valid)
            return false;

        sequence = g_lastHorse.sequence;
        for (std::size_t index = 0; index < 4; ++index)
            command[index] = g_lastHorse.command[index];

        return true;
    }

    bool GetLastGameBuddyCommand(BuddyCommandRecord& record)
    {
        if (!g_haveBuddy)
            return false;

        record = g_lastBuddy;
        return true;
    }

    bool GetGameBuddyCommand(std::uint32_t sequence, BuddyCommandRecord& record)
    {
        if (!g_haveBuddy || sequence == 0 || sequence > g_lastBuddy.sequence ||
            g_lastBuddy.sequence - sequence >= kRingSize)
        {
            return false;
        }

        const BuddyCommandRecord& stored = g_ring[sequence % kRingSize];
        if (stored.sequence != sequence)
            return false;

        record = stored;
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
