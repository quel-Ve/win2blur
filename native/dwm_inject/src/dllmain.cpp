/**
 * Frosted DWM — hook DWM internal blur via SymFromName
 */
#include <windows.h>
#include <cstdio>
#include <algorithm>
#include <dbghelp.h>
#include "MinHook.h"

// ============================================================
// Narrow-char logging (reliable in DWM process space)
// ============================================================
static void Log(const char* msg) {
    OutputDebugStringA(msg);
    HANDLE f = CreateFileA("C:\\Temp\\frosted_dwm.log",
        FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wr; SYSTEMTIME st; GetLocalTime(&st);
        char line[512];
        int n = sprintf_s(line, sizeof(line),
            "[%02d:%02d:%02d.%03d] %s\r\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        WriteFile(f, line, n, &wr, NULL);
        CloseHandle(f);
    }
}

// ============================================================
// Find DWM internal function via symbols
// ============================================================
static FARPROC FindFunc(const char* dll, const char* name) {
    HMODULE mod = GetModuleHandleA(dll);
    if (!mod) return nullptr;

    // Try export first
    FARPROC addr = GetProcAddress(mod, name);
    if (addr) {
        char buf[128]; sprintf_s(buf, "FindFunc(%s!%s) = export 0x%p", dll, name, addr);
        Log(buf);
        return addr;
    }

    // PDB symbols
    static bool symOk = false;
    if (!symOk) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        symOk = SymInitialize(GetCurrentProcess(),
            "SRV*C:\\Temp\\symbols*https://msdl.microsoft.com/download/symbols", TRUE) != 0;
        if (symOk) Log("SymInitialize OK (invading)");
        else {
            char buf[128]; sprintf_s(buf, "SymInitialize FAIL (err %lu)", GetLastError());
            Log(buf); return nullptr;
        }
    }

    // Force load module symbols (triggers PDB download)
    DWORD64 base = SymLoadModuleEx(GetCurrentProcess(), NULL, dll, NULL,
                                    (DWORD64)mod, 0, NULL, 0);
    if (!base) {
        DWORD err = GetLastError();
        char buf[128];
        sprintf_s(buf, "SymLoadModuleEx(%s) FAILED (err %lu)", dll, err);
        Log(buf);
        if (err == ERROR_SUCCESS) base = (DWORD64)mod;
        else return nullptr;
    }

    // Verify symbols loaded by enumerating a few entries
    static bool verified = false;
    if (!verified && strcmp(dll, "uDwm") == 0) {
        verified = true;
        SymEnumSymbols(GetCurrentProcess(), base, "CAccent*",
            [](PSYMBOL_INFO info, ULONG, PVOID) -> BOOL {
                char buf[256];
                sprintf_s(buf, "  SYM: %s @ 0x%llX", info->Name, info->Address);
                Log(buf);
                return TRUE;
            }, nullptr);
    }

    char search[256];
    sprintf_s(search, "%s!%s", dll, name);

    char symBuf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;

    if (SymFromName(GetCurrentProcess(), search, sym)) {
        char buf[128];
        sprintf_s(buf, "  -> 0x%016llX", sym->Address);
        Log(buf);
        return (FARPROC)sym->Address;
    }
    Log("  NOT FOUND");
    return nullptr;
}

// ============================================================
// Hooks
// ============================================================
// Hook: CAccent::UpdateAccentPolicy
// Called when DWM processes accent/blur changes for a window
typedef void* CAccent;
typedef void (__thiscall *UpdateAccentPolicy_t)(CAccent* self);
static UpdateAccentPolicy_t g_origUpdateAccentPolicy = nullptr;

void __fastcall Hooked_UpdateAccentPolicy(CAccent* self) {
    Log("Hooked_UpdateAccentPolicy called! Modifying blur params...");
    // Forward to original
    g_origUpdateAccentPolicy(self);
}

void SetupHooks() {
    Log("=== SetupHooks ===");
    MH_Initialize();

    // Find dwmcore and uDwm modules
    HMODULE dwmcore = GetModuleHandleA("dwmcore.dll");
    HMODULE uDwm    = GetModuleHandleA("uDwm.dll");
    char buf[256];
    sprintf_s(buf, "dwmcore: 0x%p, uDwm: 0x%p", dwmcore, uDwm);
    Log(buf);

    // Try hooking CAccent::UpdateAccentPolicy (from uDwm.dll)
    FARPROC updateAccent = FindFunc("uDwm", "CAccent::UpdateAccentPolicy");
    if (updateAccent) {
        if (MH_CreateHook((void*)updateAccent, (void*)Hooked_UpdateAccentPolicy,
                          (void**)&g_origUpdateAccentPolicy) == MH_OK) {
            MH_EnableHook((void*)updateAccent);
            Log("Hooked CAccent::UpdateAccentPolicy OK");
        } else Log("MH_CreateHook FAILED for UpdateAccentPolicy");
    }

    // Also try CRenderingTechnique::ExecuteBlur
    FARPROC execBlur = FindFunc("dwmcore", "CRenderingTechnique::ExecuteBlur");
    if (execBlur) {
        Log("Found CRenderingTechnique::ExecuteBlur for future hook");
    }

    // Try CTopLevelWindow::OnAccentPolicyUpdated
    FARPROC onAccent = FindFunc("uDwm", "CTopLevelWindow::OnAccentPolicyUpdated");
    if (onAccent) {
        Log("Found CTopLevelWindow::OnAccentPolicyUpdated for future hook");
    }

    Log("=== SetupHooks done ===");
}

void RemoveHooks() {
    Log("RemoveHooks");
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

extern "C" __declspec(dllexport) LRESULT CALLBACK HookProc(
    int code, WPARAM wParam, LPARAM lParam) {
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        SetupHooks();
    } else if (reason == DLL_PROCESS_DETACH) {
        RemoveHooks();
    }
    return TRUE;
}
