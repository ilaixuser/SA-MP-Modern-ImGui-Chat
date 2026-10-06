// ============================================================
//  dllmain.cpp
//  SA-MP Modern ImGui Chat — DLL entry point
// ============================================================

#define SAMP_CHAT_IMGUI_IMPL

#define _WIN32_WINNT 0x0601
#define WINVER       0x0601

#include <windows.h>
#include <winuser.h>
#include <d3d9.h>
#include <MinHook.h>

#ifndef GWL_WNDPROC
#   define GWL_WNDPROC (-4)
#endif

#include "samp_utils.h"
#include "gui.h"

SampVersion g_SampVersion = SampVersion::Unknown;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
//  Types & Structures
// ============================================================
struct SAMPOffsets {
    DWORD INPUT_PTR;
    DWORD GAME_PTR;
    DWORD ADDENTRY;
    DWORD ENABLE_BOX;
    DWORD DISABLE_BOX;
    DWORD ENABLE_BOX_JMP;
    DWORD DISABLE_BOX_JMP;
    DWORD DISABLE_ORIGINAL; // CChat::Render
    DWORD KEYPRESS_HANDLER;
    DWORD CHARINPUT_HANDLER;
};

using PFN_Present  = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using PFN_Reset    = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using PFN_AddEntry       = int(__fastcall*)(void*,void*,int,const char*,const char*,DWORD,DWORD);
using PFN_KeyPressHandler = BOOL(__cdecl*)(unsigned int);
using PFN_CharInputHandler = BOOL(__cdecl*)(unsigned int);
using PFN_GetAsyncKeyState = SHORT(WINAPI*)(int);
using PFN_GetKeyState       = SHORT(WINAPI*)(int);

// ============================================================
//  Globals
// ============================================================
PFN_Present         g_Present         = nullptr;
PFN_Reset           g_Reset           = nullptr;
PFN_AddEntry        g_AddEntry        = nullptr;
PFN_KeyPressHandler g_KeyPressHandler = nullptr;
PFN_CharInputHandler g_CharInputHandler = nullptr;
PFN_GetAsyncKeyState g_GetAsyncKeyState = nullptr;
PFN_GetKeyState       g_GetKeyState       = nullptr;

HMODULE      g_hModule     = nullptr;

WNDPROC      g_OrigWndProc = nullptr;
DWORD        g_dwSAMP      = 0;
DWORD        g_jmpEnableBox  = 0;
DWORD        g_jmpDisableBox = 0;

// ============================================================
//  Configuration — toggle features on/off here
// ============================================================
static constexpr bool HIDE_HUD   = false;   // true = ซ่อน HUD (เงิน, เลือด, อาวุธ ฯลฯ)  |  false = แสดง HUD ปกติ
static constexpr bool HIDE_RADAR = false;  // true = ซ่อน Radar ตลอด  |  false = แสดง Radar ตามเงื่อนไข (อยู่ในรถ=เห็น, เดิน=ซ่อน)

// ============================================================
//  Memory Patching
// ============================================================
static void SafeNopCallSite(DWORD addr) {
    if (!addr) return;
    BYTE first = *reinterpret_cast<BYTE*>(addr);
    if (first == 0xE8 || first == 0xFF) {
        constexpr SIZE_T PATCH_LEN = 5;
        DWORD oldProt;
        if (VirtualProtect(reinterpret_cast<LPVOID>(addr), PATCH_LEN, PAGE_EXECUTE_READWRITE, &oldProt)) {
            memset(reinterpret_cast<LPVOID>(addr), 0x90, PATCH_LEN);
            VirtualProtect(reinterpret_cast<LPVOID>(addr), PATCH_LEN, oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPVOID>(addr), PATCH_LEN);
        }
    } else {
        DWORD oldProt;
        if (VirtualProtect(reinterpret_cast<LPVOID>(addr), 1, PAGE_EXECUTE_READWRITE, &oldProt)) {
            *reinterpret_cast<BYTE*>(addr) = 0xC3; // RET
            VirtualProtect(reinterpret_cast<LPVOID>(addr), 1, oldProt, &oldProt);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPVOID>(addr), 1);
        }
    }
}

// ============================================================
//  Helpers
// ============================================================
static bool IsSampDialogOpen() {
    return samp_utils::IsDialogOpen();
}

// ============================================================
//  Hooks
// ============================================================
LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& chat = SAMPChatImGui::Get();

    // Close input when window loses focus (minimize, alt-tab, etc.)
    if (msg == WM_KILLFOCUS && chat.IsInputOpen()) {
        chat.OnInputClose();
        samp_utils::CloseInputBox();
    }

    // Input closed: intercept 'T' to open ImGui input instead of SAMP native
    if (!chat.IsInputOpen()) {
        // Block ALL keys during cooldown to prevent double-Enter from leaking to SAMP dialogs
        if (chat.IsInputCooldown()) {
            // Let ImGui see the key but block SAMP
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            return TRUE;
        }
        // F6 opens ImGui chat. 'T' opens it only if SAMP dialog is NOT active.
        if (msg == WM_KEYDOWN && (wParam == VK_F6 || (wParam == 'T' && !IsSampDialogOpen())) &&
            !(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU)    & 0x8000)) {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            chat.OnInputOpen();
            return TRUE;
        }
        // If a SAMP dialog is active, pass other keys through normally
        if (IsSampDialogOpen()) {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            return CallWindowProcA(g_OrigWndProc, hWnd, msg, wParam, lParam);
        }
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return CallWindowProcA(g_OrigWndProc, hWnd, msg, wParam, lParam);
    }

    // Input open: ESC closes chat unconditionally (even over SAMP dialogs)
    if (chat.IsInputOpen()) {
        if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
            chat.OnInputClose();
            samp_utils::CloseInputBox();
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
            return TRUE;
        }
    }

    // Input open: let ImGui handle keyboard, block from SAMP
    // Block both key-down AND key-up so game never sees partial key events
    // (e.g. Shift release was leaking through and causing the player to jump)
    if (msg == WM_KEYDOWN || msg == WM_KEYUP ||
        msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP || msg == WM_SYSCHAR) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        return TRUE;
    }

    if (msg == WM_CHAR) {
        if (wParam > 0 && wParam < 256) {
            char ch = (char)wParam;
            wchar_t wch = 0;
            if (MultiByteToWideChar(874, 0, &ch, 1, &wch, 1) > 0) {
                ImGui::GetIO().AddInputCharacterUTF16((unsigned short)wch);
            }
        } else {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        }
        return TRUE;
    }

    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    return CallWindowProcA(g_OrigWndProc, hWnd, msg, wParam, lParam);
}

int __fastcall Hooked_AddEntry(void* pThis, void* EDX, int nType, const char* szText, const char* szPrefix, DWORD textColor, DWORD prefixColor) {
    SAMPChatImGui::Get().OnNewEntry(nType, szText, szPrefix, textColor, prefixColor);
    return g_AddEntry(pThis, EDX, nType, szText, szPrefix, textColor, prefixColor);
}

void __declspec(naked) HOOK_EnableInputBox() {
    __asm pushad
    // Always open ImGui chat when SAMP triggers EnableInputBox (includes T key and dialog-triggered opens).
    SAMPChatImGui::Get().OnInputOpen();
    __asm popad
    __asm mov dword ptr [esi + 0x14E0], 1
    __asm jmp [g_jmpEnableBox]
}

void __declspec(naked) HOOK_DisableInputBox() {
    __asm pushad
    SAMPChatImGui::Get().OnInputClose();
    __asm popad
    __asm mov dword ptr [esi + 0x14E0], 0
    __asm jmp [g_jmpDisableBox]
}

// ---- InputHandler hooks: intercept keys at SAMP handler level ----

BOOL __cdecl Hooked_KeyPressHandler(unsigned int nKey) {
    auto& chat = SAMPChatImGui::Get();

    if (chat.IsInputOpen()) {
        // ESC closes ImGui chat explicitly
        if (nKey == VK_ESCAPE) {
            chat.OnInputClose();
            samp_utils::CloseInputBox();
            return TRUE;
        }
        // ImGui input open — block ALL keys from SAMP native input
        return TRUE;
    }

    // Input closed: intercept keys to open ImGui input or scroll chat
    bool noMod = !(GetKeyState(VK_CONTROL) & 0x8000) &&
                 !(GetKeyState(VK_MENU)    & 0x8000);

    // Block ALL keys during cooldown to prevent dialog auto-submit from leaked Enter
    if (chat.IsInputCooldown()) return TRUE;

    // F6: always open ImGui input. T: only if SAMP dialog is NOT active.
    if (nKey == VK_F6 && noMod) {
        chat.OnInputOpen();
        return TRUE;
    }
    if (nKey == 'T' && noMod && !IsSampDialogOpen()) {
        chat.OnInputOpen();
        return TRUE;
    }

    if (nKey == VK_F2 && noMod && !IsSampDialogOpen()) {
        samp_utils::SendCommand("/f2");
        return TRUE;
    }
    if (nKey == VK_F3 && noMod && !IsSampDialogOpen()) {
        samp_utils::SendCommand("/f3");
        return TRUE;
    }

    // If a SAMP dialog is active, pass other keys through (e.g. Enter, number keys)
    if (IsSampDialogOpen()) {
        // Still handle Page Up/Down for chat scrolling
        if (nKey == VK_PRIOR) { chat.ScrollChat(-5); return TRUE; }
        if (nKey == VK_NEXT)  { chat.ScrollChat( 5); return TRUE; }
        return g_KeyPressHandler(nKey);
    }

    // Page Up / Page Down: scroll chat history
    if (nKey == VK_PRIOR) { chat.ScrollChat(-5); return TRUE; }
    if (nKey == VK_NEXT)  { chat.ScrollChat( 5); return TRUE; }

    return g_KeyPressHandler(nKey);
}

BOOL __cdecl Hooked_CharInputHandler(unsigned int nChar) {
    if (SAMPChatImGui::Get().IsInputOpen()) {
        // ImGui input open — block all character input from SAMP
        return TRUE;
    }
    return g_CharInputHandler(nChar);
}

// Hook GetAsyncKeyState to block all game keyboard input while chat is open
SHORT WINAPI Hooked_GetAsyncKeyState(int vKey) {
    auto& chat = SAMPChatImGui::Get();
    if (chat.IsInputOpen()) {
        // Return "not pressed" for every key while typing in chat
        return 0;
    }
    return g_GetAsyncKeyState(vKey);
}

// Hook GetKeyState to block keys while chat is open, but allow modifiers
// so ImGui can detect Ctrl+C / Ctrl+V / Ctrl+X / Ctrl+A inside the input box.
SHORT WINAPI Hooked_GetKeyState(int vKey) {
    auto& chat = SAMPChatImGui::Get();
    if (chat.IsInputOpen()) {
        // Let ImGui see modifier and clipboard-related keys
        switch (vKey) {
            case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            case VK_MENU:    case VK_LMENU:    case VK_RMENU:
            case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:
            case VK_CAPITAL: case VK_NUMLOCK:  case VK_SCROLL:
                return g_GetKeyState(vKey);
            default:
                return 0;
        }
    }
    return g_GetKeyState(vKey);
}

// ---- Server IP lock ----
static const char* const ALLOWED_IP   = "127.0.0.1";
static const int         ALLOWED_PORT = 7777;

static void EnforceServerIPLock() {
    auto check = [](auto* pNet) {
        if (!pNet) return;
        if (pNet->m_szHostAddress[0] == '\0') return; // not connected yet

        bool ipOk   = (strcmp(pNet->m_szHostAddress, ALLOWED_IP) == 0);
        bool portOk = (pNet->m_nPort == ALLOWED_PORT);
        if (!ipOk || !portOk) {
            MessageBoxA(NULL,
                "This mod is not authorized for this server.",
                "Server Lock", MB_OK | MB_ICONERROR | MB_TOPMOST);
            TerminateProcess(GetCurrentProcess(), 0);
        }
    };

    if (g_SampVersion == SampVersion::R1) check(sampapi::v037r1::RefNetGame());
    else if (g_SampVersion == SampVersion::R3_1) check(sampapi::v037r3::RefNetGame());
}

HRESULT STDMETHODCALLTYPE Hooked_Present(IDirect3DDevice9* pDev, const RECT* pSrc, const RECT* pDst, HWND hWnd, const RGNDATA* pDirty) {
    EnforceServerIPLock();

    // Radar: dynamic (show in vehicle, hide on foot) or always hidden
    if (HIDE_RADAR) {
        *(BYTE*)0xBAA3FB = 2;  // Always hide radar
    } else {
        bool inVehicle = samp_utils::IsPlayerInVehicle();
        *(BYTE*)0xBAA3FB = inVehicle ? 0 : 2;  // 0 = show radar, nonzero = hide
    }

    // HUD: hide or show based on config
    if (HIDE_HUD) {
        *(BYTE*)0xBA6769 = 0;   // Hide HUD (money, health, weapon, etc.)
    }

    static bool once = false;
    if (!once) {
        HWND gta = **reinterpret_cast<HWND**>(0xC17054);
        g_OrigWndProc = reinterpret_cast<WNDPROC>(SetWindowLongA(gta, GWL_WNDPROC, (LONG)WndProcHook));
        SAMPChatImGui::Get().Init(gta, pDev);
        once = true;
    }
    // Throttle the IP check to avoid every-frame string compare
    static DWORD lastCheck = 0;
    DWORD now = GetTickCount();
    if (now - lastCheck > 1000) {
        lastCheck = now;
        EnforceServerIPLock();
    }
    if (pDev->TestCooperativeLevel() == D3D_OK) {
        SAMPChatImGui::Get().Tick(pDev);
    }
    return g_Present(pDev, pSrc, pDst, hWnd, pDirty);
}

HRESULT STDMETHODCALLTYPE Hooked_Reset(IDirect3DDevice9* pDev, D3DPRESENT_PARAMETERS* pp) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_Reset(pDev, pp);
    if (SUCCEEDED(hr)) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return hr;
}

// ============================================================
//  Initialization
// ============================================================
static void* GetVTableFunc(int idx) {
    char buf[MAX_PATH];
    GetSystemDirectoryA(buf, MAX_PATH);
    strcat_s(buf, "\\d3d9.dll");
    DWORD base = (DWORD)LoadLibraryA(buf);
    DWORD end  = base + 0x128000;
    for (DWORD p = base; p < end; ++p) {
        if (*(WORD*)(p+0x00) == 0x06C7 && *(WORD*)(p+0x06) == 0x8689 && *(WORD*)(p+0x0C) == 0x8689) {
            PDWORD vt = *(PDWORD*)(p + 2);
            return reinterpret_cast<void*>(vt[idx]);
        }
    }
    return nullptr;
}

static bool GetOffsets(DWORD ep, SAMPOffsets& o) {
    switch (ep) {
    case 0x31DF13:  // 0.3.7-R1
        g_SampVersion = SampVersion::R1;
        o = { 0x21A0E8, 0x21A10C, 0x64010, 0x658C7, 0x6591C, 0x658D1, 0x65926, 0x63D70, 0x5D850, 0x5DA80 };
        return true;
    case 0xCC4D0:   // 0.3.7-R3-1
        g_SampVersion = SampVersion::R3_1;
        o = { 0x26E8CC, 0x26E8F4, 0x67460, 0x68DF7, 0x68E4C, 0x68E01, 0x68E56, 0x671C0, 0x60BF0, 0x60E20 };
        return true;
    default: return false;
    }
}

static void InstallHooks(const SAMPOffsets& o) {
    // Unprotect radar and HUD display bytes
    DWORD oldProt;
    VirtualProtect(reinterpret_cast<LPVOID>(0xBAA3FB), 1, PAGE_EXECUTE_READWRITE, &oldProt);
    VirtualProtect(reinterpret_cast<LPVOID>(0xBA6769), 1, PAGE_EXECUTE_READWRITE, &oldProt);

    SafeNopCallSite(g_dwSAMP + o.DISABLE_ORIGINAL);

    MH_CreateHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.ADDENTRY), Hooked_AddEntry, reinterpret_cast<LPVOID*>(&g_AddEntry));
    MH_EnableHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.ADDENTRY));

    g_jmpEnableBox  = g_dwSAMP + o.ENABLE_BOX_JMP;
    g_jmpDisableBox = g_dwSAMP + o.DISABLE_BOX_JMP;
    MH_CreateHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.ENABLE_BOX), HOOK_EnableInputBox, nullptr);
    MH_EnableHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.ENABLE_BOX));
    MH_CreateHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.DISABLE_BOX), HOOK_DisableInputBox, nullptr);
    MH_EnableHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.DISABLE_BOX));

    // Hook InputHandler to intercept 'T' at SAMP handler level
    MH_CreateHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.KEYPRESS_HANDLER), Hooked_KeyPressHandler, reinterpret_cast<LPVOID*>(&g_KeyPressHandler));
    MH_EnableHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.KEYPRESS_HANDLER));
    MH_CreateHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.CHARINPUT_HANDLER), Hooked_CharInputHandler, reinterpret_cast<LPVOID*>(&g_CharInputHandler));
    MH_EnableHook(reinterpret_cast<LPVOID>(g_dwSAMP + o.CHARINPUT_HANDLER));

    MH_CreateHook(GetVTableFunc(17), Hooked_Present, reinterpret_cast<LPVOID*>(&g_Present));
    MH_EnableHook(GetVTableFunc(17));
    MH_CreateHook(GetVTableFunc(16), Hooked_Reset, reinterpret_cast<LPVOID*>(&g_Reset));
    MH_EnableHook(GetVTableFunc(16));

    // Hook GetAsyncKeyState / GetKeyState so GTA:SA doesn't see keys while ImGui chat is open
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        FARPROC pGetAsyncKeyState = GetProcAddress(hUser32, "GetAsyncKeyState");
        if (pGetAsyncKeyState) {
            MH_CreateHook(pGetAsyncKeyState, Hooked_GetAsyncKeyState, reinterpret_cast<LPVOID*>(&g_GetAsyncKeyState));
            MH_EnableHook(pGetAsyncKeyState);
        }
        FARPROC pGetKeyState = GetProcAddress(hUser32, "GetKeyState");
        if (pGetKeyState) {
            MH_CreateHook(pGetKeyState, Hooked_GetKeyState, reinterpret_cast<LPVOID*>(&g_GetKeyState));
            MH_EnableHook(pGetKeyState);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    g_hModule = hModule;
    g_dwSAMP = (DWORD)GetModuleHandleA("samp.dll");
    if (!g_dwSAMP) return FALSE;
    auto* nth = reinterpret_cast<IMAGE_NT_HEADERS*>(g_dwSAMP + reinterpret_cast<IMAGE_DOS_HEADER*>(g_dwSAMP)->e_lfanew);
    DWORD ep = nth->OptionalHeader.AddressOfEntryPoint;
    SAMPOffsets offsets{};
    if (!GetOffsets(ep, offsets)) return FALSE;
    MH_Initialize();
    InstallHooks(offsets);
    return TRUE;
}
