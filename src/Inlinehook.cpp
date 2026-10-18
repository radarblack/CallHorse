#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>

#include "InlineHook.h"
#include "Log.h"

namespace
{
    constexpr std::size_t kTrampolineSize = 96;

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

    bool Patch(inlinehook::Hook& hook, std::uint8_t* code, std::size_t stolen, void* detour,
               void** original, std::uint8_t* trampoline)
    {
        std::memcpy(hook.saved, code, stolen);

        DWORD oldProtect = 0;
        if (!VirtualProtect(code, stolen, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            Log("inline hook failed: could not unprotect %p", static_cast<void*>(code));
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        if (original)
            *original = trampoline;

        WriteJump(code, detour);
        for (std::size_t index = inlinehook::kJumpSize; index < stolen; ++index)
            code[index] = 0xCC;

        DWORD ignored = 0;
        VirtualProtect(code, stolen, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), code, stolen);

        hook.target     = code;
        hook.trampoline = trampoline;
        hook.stolen     = stolen;
        return true;
    }

    std::uint8_t* AllocateTrampoline()
    {
        std::uint8_t* trampoline = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, kTrampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
            Log("inline hook failed: could not allocate the trampoline");

        return trampoline;
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

        std::uint8_t* trampoline = AllocateTrampoline();
        if (!trampoline)
            return false;

        std::memcpy(trampoline, code, stolen);
        WriteJump(trampoline + stolen, code + stolen);

        return Patch(hook, code, stolen, detour, original, trampoline);
    }

    bool InstallWithRipLoad(Hook& hook, void* target, std::size_t ripLoadOffset, void* detour, void** original)
    {
        const std::size_t stolen = ripLoadOffset + kRipLoadSize;

        if (!target || !detour || stolen < kJumpSize || stolen > kMaxStolen)
        {
            Log("inline hook rejected: invalid arguments");
            return false;
        }

        std::uint8_t* code = static_cast<std::uint8_t*>(target);

        if (code[ripLoadOffset] != 0x48 || code[ripLoadOffset + 1] != 0x8B || code[ripLoadOffset + 2] != 0x05)
        {
            Log("inline hook rejected: no mov rax,[rip+disp] at offset %u", static_cast<unsigned>(ripLoadOffset));
            return false;
        }

        std::int32_t displacement = 0;
        std::memcpy(&displacement, code + ripLoadOffset + 3, sizeof(displacement));

        const std::uint8_t* global = code + stolen + displacement;

        std::uint8_t* trampoline = AllocateTrampoline();
        if (!trampoline)
            return false;

        std::memcpy(trampoline, code, ripLoadOffset);

        std::uint8_t* cursor = trampoline + ripLoadOffset;
        cursor[0] = 0x48;
        cursor[1] = 0xB8;
        const std::uint64_t address = reinterpret_cast<std::uint64_t>(global);
        std::memcpy(cursor + 2, &address, sizeof(address));
        cursor[10] = 0x48;
        cursor[11] = 0x8B;
        cursor[12] = 0x00;

        WriteJump(cursor + 13, code + stolen);

        return Patch(hook, code, stolen, detour, original, trampoline);
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
