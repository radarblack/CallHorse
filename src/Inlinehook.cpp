#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>

#include "InlineHook.h"
#include "Log.h"

namespace
{
    void WriteJump(std::uint8_t* destination, const void* address)
    {
        destination[0] = 0xFF;
        destination[1] = 0x25;
        destination[2] = 0x00;
        destination[3] = 0x00;
        destination[4] = 0x00;
        destination[5] = 0x00;

        const std::uint64_t value = reinterpret_cast<std::uint64_t>(address);
        std::memcpy(destination + 6, &value, sizeof(value));
    }
}

namespace inlinehook
{
    bool Install(Hook& hook, void* target, std::size_t stolen, void* detour, void** original)
    {
        if (!target || !detour || stolen < kJumpSize || stolen > kMaxStolen)
        {
            Log("inline hook rejected: invalid arguments");
            return false;
        }

        std::uint8_t* code = static_cast<std::uint8_t*>(target);

        std::uint8_t* trampoline = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, kMaxStolen + kJumpSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
        {
            Log("inline hook failed: could not allocate the trampoline");
            return false;
        }

        std::memcpy(hook.saved, code, stolen);
        std::memcpy(trampoline, code, stolen);
        WriteJump(trampoline + stolen, code + stolen);

        DWORD oldProtect = 0;
        if (!VirtualProtect(code, stolen, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            Log("inline hook failed: could not unprotect %p", target);
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        if (original)
            *original = trampoline;

        WriteJump(code, detour);
        for (std::size_t index = kJumpSize; index < stolen; ++index)
            code[index] = 0xCC;

        DWORD ignored = 0;
        VirtualProtect(code, stolen, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), code, stolen);

        hook.target     = target;
        hook.trampoline = trampoline;
        hook.stolen     = stolen;
        return true;
    }

    void Remove(Hook& hook)
    {
        if (!hook.target)
            return;

        DWORD oldProtect = 0;
        if (VirtualProtect(hook.target, hook.stolen, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            std::memcpy(hook.target, hook.saved, hook.stolen);

            DWORD ignored = 0;
            VirtualProtect(hook.target, hook.stolen, oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), hook.target, hook.stolen);
        }

        VirtualFree(hook.trampoline, 0, MEM_RELEASE);

        hook.target     = nullptr;
        hook.trampoline = nullptr;
        hook.stolen     = 0;
    }
}
