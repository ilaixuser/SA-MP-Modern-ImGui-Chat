#pragma once
#include <windows.h>
#include <string>

#include "sampapi/CChat.h"
#include "sampapi/CInput.h"
#include "sampapi/CNetGame.h"
#include "sampapi/CGame.h"
#include "sampapi/CLocalPlayer.h"
#include "sampapi/CDialog.h"
#include "sampapi/Commands.h"
#include "sampapi/CPlayerPool.h"

enum class SampVersion {
    Unknown,
    R1,
    R3_1
};

extern SampVersion g_SampVersion;

namespace samp_utils {

inline void CloseInputBox() {
    if (g_SampVersion == SampVersion::R1) {
        if (auto* p = sampapi::v037r1::RefInputBox()) {
            p->Close();
            memset(p->m_szInput, 0, sizeof(p->m_szInput));
        }
    } else if (g_SampVersion == SampVersion::R3_1) {
        if (auto* p = sampapi::v037r3::RefInputBox()) {
            p->Close();
            memset(p->m_szInput, 0, sizeof(p->m_szInput));
        }
    }
}

inline bool IsDialogOpen() {
    if (g_SampVersion == SampVersion::R1) {
        auto* p = sampapi::v037r1::RefDialog();
        return p && p->m_bIsActive;
    } else if (g_SampVersion == SampVersion::R3_1) {
        auto* p = sampapi::v037r3::RefDialog();
        return p && p->m_bIsActive;
    }
    return false;
}

inline void SetCursorMode(int mode, bool bImmediatelyHideCursor) {
    if (g_SampVersion == SampVersion::R1) {
        if (auto* p = sampapi::v037r1::RefGame()) {
            p->SetCursorMode(mode, bImmediatelyHideCursor);
        }
    } else if (g_SampVersion == SampVersion::R3_1) {
        if (auto* p = sampapi::v037r3::RefGame()) {
            p->SetCursorMode(mode, bImmediatelyHideCursor);
        }
    }
}

inline IDirect3DDevice9* GetGameDevice() {
    if (g_SampVersion == SampVersion::R1) {
        if (auto* p = sampapi::v037r1::RefGame()) return p->GetDevice();
    } else if (g_SampVersion == SampVersion::R3_1) {
        if (auto* p = sampapi::v037r3::RefGame()) return p->GetDevice();
    }
    return nullptr;
}

inline bool IsMenuVisible() {
    if (g_SampVersion == SampVersion::R1) {
        if (auto* p = sampapi::v037r1::RefGame()) return p->IsMenuVisible();
    } else if (g_SampVersion == SampVersion::R3_1) {
        if (auto* p = sampapi::v037r3::RefGame()) return p->IsMenuVisible();
    }
    return false;
}

inline bool IsPlayerInVehicle() {
    DWORD ped = *(DWORD*)0xB6F5F0;
    if (ped) {
        DWORD vehicle = *(DWORD*)(ped + 0x58C);
        bool bInVehicle = (*(DWORD*)(ped + 0x46C) & 0x100) != 0;
        return (vehicle != 0 && bInVehicle);
    }
    return false;
}

inline void SendCommand(const std::string& text) {
    if (g_SampVersion == SampVersion::R1) {
        if (auto* p = sampapi::v037r1::RefInputBox()) {
            strncpy(p->m_szInput, text.c_str(), sizeof(p->m_szInput) - 1);
            p->m_szInput[sizeof(p->m_szInput) - 1] = '\0';
            p->Send(text.c_str());
        }
    } else if (g_SampVersion == SampVersion::R3_1) {
        if (auto* p = sampapi::v037r3::RefInputBox()) {
            strncpy(p->m_szInput, text.c_str(), sizeof(p->m_szInput) - 1);
            p->m_szInput[sizeof(p->m_szInput) - 1] = '\0';
            p->Send(text.c_str());
        }
    }
}

inline void SendChat(const std::string& text) {
    if (g_SampVersion == SampVersion::R1) {
        auto* pNet = sampapi::v037r1::RefNetGame();
        if (pNet) {
            auto* pPool = pNet->GetPlayerPool();
            if (pPool) {
                auto* pLocal = pPool->GetLocalPlayer();
                if (pLocal) pLocal->Chat(text.c_str());
            }
        }
    } else if (g_SampVersion == SampVersion::R3_1) {
        auto* pNet = sampapi::v037r3::RefNetGame();
        if (pNet) {
            auto* pPool = pNet->GetPlayerPool();
            if (pPool) {
                auto* pLocal = pPool->GetLocalPlayer();
                if (pLocal) pLocal->Chat(text.c_str());
            }
        }
    }
}

template <typename Func>
inline void ProcessChatHistory(Func f) {
    if (g_SampVersion == SampVersion::R1) {
        f(sampapi::v037r1::RefChat());
    } else if (g_SampVersion == SampVersion::R3_1) {
        f(sampapi::v037r3::RefChat());
    }
}

#define DISPATCH_COMMAND(Cmd, Args) \
    do { \
        if (g_SampVersion == SampVersion::R1) sampapi::v037r1::Commands::Cmd(Args); \
        else if (g_SampVersion == SampVersion::R3_1) sampapi::v037r3::Commands::Cmd(Args); \
    } while(0)

} // namespace samp_utils
