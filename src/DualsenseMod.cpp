/*
 * Copyright (C) 2025 Thanasis Petsas <thanpetsas@gmail.com>
 * Licence: MIT Licence
 */

#include "DualsenseMod.h"

#include "Logger.h"
#include "Config.h"
#include "Utils.h"
#include "rva/RVA.h"
#include "minhook/include/MinHook.h"

// headers needed for dualsensitive
#include <udp.h>
#include <dualsensitive.h>
#include <IO.h>
#include <Device.h>
#include <Helpers.h>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <cinttypes>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <string>
#include <sstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>

#define INI_LOCATION "./mods/dualsense-mod.ini"

// TODO: move the following to a server utils file

PROCESS_INFORMATION serverProcInfo;


#include <shellapi.h>

bool launchServerElevated(const std::wstring& exePath = L"./mods/dualsensitive-service.exe") {
    wchar_t fullExePath[MAX_PATH];
    if (!GetFullPathNameW(exePath.c_str(), MAX_PATH, fullExePath, nullptr)) {
        _LOG("Failed to resolve full path");
        return false;
    }

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = fullExePath;
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NO_CONSOLE;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        _LOG("ShellExecuteEx failed: %lu", err);
        return false;
    }

    return true;
}

bool launchServer(PROCESS_INFORMATION& outProcInfo, const std::wstring& exePath = L"./mods/dualsensitive-service.exe") {
    STARTUPINFOW si = { sizeof(si) };
    ZeroMemory(&outProcInfo, sizeof(outProcInfo));

    //TODO: add check here for checking if the exe exists!

    BOOL success = CreateProcessW(
        exePath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        DETACHED_PROCESS,
        nullptr,
        nullptr,
        &si,
        &outProcInfo
    );

    if (!success) {
        _LOG("Failed to launch server.exe. Error: %s\n", GetLastError());
        return false;
    }

    // You can close thread handle immediately; we keep process handle
    CloseHandle(outProcInfo.hThread);
    return true;
}


bool terminateServer(PROCESS_INFORMATION& procInfo) {
    if (procInfo.hProcess != nullptr) {
        BOOL result = TerminateProcess(procInfo.hProcess, 0); // 0 = exit code
        CloseHandle(procInfo.hProcess);
        procInfo.hProcess = nullptr;
        return result == TRUE;
    }
    return false;
}


void logLastError(const char* context) {
    DWORD errorCode = GetLastError();
    wchar_t* msgBuf = nullptr;

    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        0,
        (LPWSTR)&msgBuf,
        0,
        nullptr
    );

    // Convert wchar_t* msgBuf to char*
    char narrowBuf[1024] = {};
    if (msgBuf) {
        WideCharToMultiByte(CP_UTF8, 0, msgBuf, -1, narrowBuf, sizeof(narrowBuf) - 1, nullptr, nullptr);
    }

    _LOG("[ERROR] %s failed (code %lu): %s", context, errorCode, msgBuf ? narrowBuf : "(Unknown error)");

    if (msgBuf) {
        LocalFree(msgBuf);
    }
}

struct TriggerSetting {
    TriggerProfile profile;
    bool isCustomTrigger = false;
    TriggerMode mode = TriggerMode::Off;
    std::vector<uint8_t> extras;

    TriggerSetting(TriggerProfile profile, std::vector<uint8_t> extras) :
        profile(profile), extras(extras) {}

    TriggerSetting(TriggerMode mode, std::vector<uint8_t> extras) :
        mode(mode), extras(extras), isCustomTrigger(true) {}

};

struct Triggers {
    TriggerSetting *L2;
    TriggerSetting *R2;
};

// Globals
Config g_config;
Logger g_logger;

std::map<std::string, Triggers> g_TriggerSettings ;

void InitTriggerSettings() {
    g_TriggerSettings =
    {
        {
            "weapon/zion/player/sp/fists_berserk",
            {

                .L2 = new TriggerSetting (
                        TriggerProfile::Choppy, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Choppy, {}
                )
            }
        },
        {
            "weapon/zion/player/sp/fists",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Choppy, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Choppy, {}
                )
            }
        },
        {
            "weapon/zion/player/sp/pistol",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Galloping,
                        {3, 9, 1, 2, 30}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Bow,
                        {1, 4, 3, 2}
                )
            }
        },
        {
            "weapon/zion/player/sp/shotgun",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Choppy, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Bow,
                        {0, 4, 8, 8}
                )
            }
        },
        {
            "weapon/zion/player/sp/shotgun_mod",
            {
                .L2 = new TriggerSetting (
                        TriggerMode::Rigid, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::MultiplePositionFeeback,
                        {4, 7, 0, 2, 4, 6, 0, 3, 6, 0}
                )
            }
        },
        {
            "weapon/zion/player/sp/plasma_rifle",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Choppy, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Vibration,
                        {0, 4, 10}
                )
            }
        },
        {
            "weapon/zion/player/sp/heavy_rifle_heavy_ar",
            {
                .L2 = new TriggerSetting (
                        TriggerMode::Rigid, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::MultiplePositionVibration,
                        {14, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8}
                )
            }
        },
        {
            "weapon/zion/player/sp/heavy_rifle_heavy_ar_mod",
            {
                .L2 = new TriggerSetting (
                        TriggerMode::Rigid, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::MultiplePositionVibration,
                        {15, 0, 1, 4, 6, 7, 8, 8, 7, 6, 4}
                )
            }
        },
        {
            "weapon/zion/player/sp/rocket_launcher",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Choppy, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Bow,
                        {0, 3, 8, 8}
                )

            }
        },
        {
            "weapon/zion/player/sp/rocket_launcher_mod",
            {
                .L2 = new TriggerSetting (
                        TriggerMode::Rigid, {}
                ),
                .R2 = new TriggerSetting (
                        TriggerMode::Rigid_A,
                        {209, 42, 232, 192, 232, 209, 232}
                )
            }
        },
        {
            "weapon/zion/player/sp/double_barrel",
            {
                .L2 = new TriggerSetting (
                        TriggerMode::Rigid_A,
                        {60, 71, 56, 128, 195, 210, 255}
                ),
                .R2 = new TriggerSetting (
                    TriggerProfile::SlopeFeedback,
                    {0, 8, 8, 1}
                )
            }
        },
        {
            "weapon/zion/player/sp/chaingun",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Vibration,
                        {1, 10, 8}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::MultiplePositionVibration,
                        {11, 1, 3, 5, 7, 7, 8, 8, 8, 8, 8}
                )
            }
        },
        {
            "weapon/zion/player/sp/chaingun_mod",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::SlopeFeedback,
                        {0, 5, 1, 8}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::MultiplePositionVibration,
                        {21, 1, 3, 5, 7, 7, 8, 8, 8, 8, 8}
                )
            }
        },
        {
            "weapon/zion/player/sp/chainsaw",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Machine,
                        {1, 9, 1, 5, 100, 0}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Machine,
                        {1, 9, 7, 7, 65, 0}
                )
            }
        },
        {
            "weapon/zion/player/sp/gauss_rifle",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Machine,
                        {7, 9, 0, 1, 8, 1}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Galloping,
                        {1, 3, 1, 6, 40}
                )
            }
        },
        {
            "weapon/zion/player/sp/gauss_rifle_mod",
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Machine,
                        {4, 9, 1, 2, 40, 0}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::Galloping,
                        {1, 3, 1, 6, 40}
                )
            }
        },
        {
            "weapon/zion/player/sp/bfg",
            {
                .L2 = new TriggerSetting (
                    TriggerProfile::Choppy, {}
                ),
                .R2 = new TriggerSetting (
                    TriggerMode::Pulse_AB,
                    {18, 197, 35, 58, 90, 120, 138}
                )
            }
        },
    };
}

bool hasModSettings(std::string weaponName);
void SendTriggers(std::string weaponName, bool mod = false);
void SendTriggers(std::string weaponName, bool mod) {
    std::string weaponId = std::string(weaponName);
    if (mod) {
        if (!hasModSettings(weaponName)) {
           _LOGD("* No mod settings found for %s", weaponName.c_str());
           return;
        }
        weaponId += "_mod";
    }
    Triggers t = g_TriggerSettings[weaponId];
    if (t.L2->isCustomTrigger)
        dualsensitive::setLeftCustomTrigger(t.L2->mode, t.L2->extras);
    else
        dualsensitive::setLeftTrigger (t.L2->profile, t.L2->extras);
    if (t.R2->isCustomTrigger)
        dualsensitive::setRightCustomTrigger(t.R2->mode, t.R2->extras);
    else
        dualsensitive::setRightTrigger (t.R2->profile, t.R2->extras);
    _LOGD("Adaptive Trigger settings sent successfully!");
}


// Game structures
using Player  = void;
using Weapon  = void;

// Game global vars

HMODULE g_doomBaseAddr = nullptr;


// found using FindCurrWeaponHandle (Player *player)
static size_t g_currWeaponOffset = 0x9788;
// = SIZE_MAX; // to search for the offset using FindCurrWeaponHandle

static Weapon *g_currWeapon = nullptr;

static Player *g_currPlayer = nullptr;

static unsigned int g_previousMode = 0;

enum class GameState : uint8_t {
    Idle,       // Game hasn't started yet
    InGame,     // Gameplay is on
    Paused      // Game is paused
};

static std::atomic<GameState> g_state{GameState::Idle};

void print_state()
{
    switch(g_state) {
        case GameState::Idle:
            _LOGD("*current state: Idle");
            break;
        case GameState::InGame:
            _LOGD("*current state: InGame");
            break;
        case GameState::Paused:
            _LOGD("*current state: Paused");
            break;
        default:
            _LOGD("*current state: Unknown");
    }
}

std::vector<std::string> g_WeaponList = {
    "weapon/zion/player/sp/pistol",
    "weapon/zion/player/sp/shotgun",
    "weapon/zion/player/sp/chainsaw",
    "weapon/zion/player/sp/heavy_rifle_heavy_ar",
    "weapon/zion/player/sp/plasma_rifle",
    "weapon/zion/player/sp/gauss_rifle",
    "weapon/zion/player/sp/rocket_launcher",
    "weapon/zion/player/sp/chaingun",
    "weapon/zion/player/sp/double_barrel",
    "weapon/zion/player/sp/bfg",
};


static std::vector<std::string> g_WeaponsWithModSettings = {
    "weapon/zion/player/sp/shotgun",
    "weapon/zion/player/sp/chaingun",
    "weapon/zion/player/sp/rocket_launcher",
    "weapon/zion/player/sp/heavy_rifle_heavy_ar",
    "weapon/zion/player/sp/gauss_rifle",
};

static bool hasModSettings(std::string weaponName) {
    return std::find (
            g_WeaponsWithModSettings.begin(),
            g_WeaponsWithModSettings.end(),
            weaponName
    ) != g_WeaponsWithModSettings.end();
}

std::vector<std::string> g_AmmoList = {
    "bullets",  // heavy_rifle_heavy_ar, chaingun
    "shells",   // shotgun, double_barrel
    "rockets",  // rocket_launcher
    "plasma",   // plasma_rifle, gauss_rifle
    "cells",    // bfg
    "fuel"      // chainsaw
};

// weapon name to ammo
static std::unordered_map<std::string, std::string> g_WeaponToAmmoType = {
  {"weapon/zion/player/sp/fists",                    "infinite"},
  {"weapon/zion/player/sp/fists_berserk",            "infinite"},
  {"weapon/zion/player/sp/pistol",                   "infinite"},
  {"weapon/zion/player/sp/heavy_rifle_heavy_ar",     "bullets"},
  {"weapon/zion/player/sp/heavy_rifle_heavy_ar_mod", "bullets"},
  {"weapon/zion/player/sp/chaingun",                 "bullets"},
  {"weapon/zion/player/sp/chaingun_mod",             "bullets"},
  {"weapon/zion/player/sp/shotgun",                  "shells"},
  {"weapon/zion/player/sp/shotgun_mod",              "shells"},
  {"weapon/zion/player/sp/double_barrel",            "shells"},
  {"weapon/zion/player/sp/rocket_launcher",          "rockets"},
  {"weapon/zion/player/sp/rocket_launcher_mod",      "rockets"},
  {"weapon/zion/player/sp/plasma_rifle",             "plasma"},
  {"weapon/zion/player/sp/gauss_rifle",              "plasma"},
  {"weapon/zion/player/sp/gauss_rifle_mod",          "plasma"},
  {"weapon/zion/player/sp/bfg",                      "cells"},
  {"weapon/zion/player/sp/chainsaw",                 "fuel"},
};


static std::unordered_map<std::string, void*> g_AmmoPtrs = {
    {"bullets",  nullptr},
    {"shells",   nullptr},
    {"rockets",  nullptr},
    {"plasma",   nullptr},
    {"cells",    nullptr},
    {"fuel",     nullptr}
};

static void resetAmmoPtrs() {
    g_AmmoPtrs = {
        {"bullets",  nullptr},
        {"shells",   nullptr},
        {"rockets",  nullptr},
        {"plasma",   nullptr},
        {"cells",    nullptr},
        {"fuel",     nullptr}
    };
}

static const unsigned int g_AmmoCountOffset = 0x38;

// this is constructed dynamically
static std::unordered_map<void*, bool> g_HasAmmo = {};

static bool HasAmmo(std::string weaponName) {
    std::string ammoType = g_WeaponToAmmoType[ weaponName ];
    if (ammoType == "infinite")
        return true;
    void * ammo = g_AmmoPtrs[ammoType];
    if (!ammo) {
        _LOGD("HasAmmo - Ptr for %s found null!", ammoType.c_str());
        return false;
    }
    return g_HasAmmo[ammo];
}


// Game functions
using _OnWeaponSelected =
                    void(__fastcall*)(void *player, long long *weapon);

using _SelectWeaponByDeclExplicit =
                    unsigned long long(__fastcall*) (
                            long long *player, long long param_2,
                            char param_3, char param_4
                    );

using _UpdateWeapon =
                    void(__fastcall*) (void *player);


using _Damage = void(__fastcall*)(
    void* player,
    void* inflictor,
    void* attacker,
    long long damageDecl,
    float damageScale,
    const float* dir,
    const float* hitInfo
);

// Virtual: bool idPlayer::IsDead() const;  // found at vtable + 0x6C8
using _IsDead = bool(__fastcall*)(void* player);

using MenuCtor_t   = void (__fastcall*)(void* self);
using MenuDtor_t   = void (__fastcall*)(void* self);
using MenuUpdate_t = void (__fastcall*)(void* self /*maybe*/, float /*maybe*/, void* /*ctx?*/);
using MenuDraw_t   = void (__fastcall*)(void* self /*maybe*/, void* /*renderCtx?*/);

using _LevelLoadCompleted =
                    void (__fastcall*)(long long *this_idLoadScreen);


using _UpdateAmmo =
                    int (__fastcall *)(void *ammo, int delta, char clamp);


using _GetWeaponFromDecl =
                    void* (__fastcall*)(long long* mgr, long long decl);

using _SetFireMode =
                    bool(__fastcall*)(void* weapon, uint32_t mode, char allowSame);

using _idMenuManagerShell_Activate =
                    void (__fastcall *)(void *self, uint32_t stateId);

using _idHandsUpdate =
                    void(__fastcall*) (void *self, void *state);

// handle to pointer resolution. It takes a 64-bit handle/id and returns a real
// object pointer (FUN_14142C870)
using _HandleToPointer  = Weapon* (__fastcall*)(uint64_t);

// idPlayer's vtable + 0xB20
using PlayerState  = uint32_t(__fastcall*)(Player*);

// idPlayer's vtable +0x678
using GetHandle    = uint64_t(__fastcall*)(Player*, uint32_t state);


// Game Addresses

// This function is called every time a weapon switch takes place
RVA<_OnWeaponSelected>
OnWeaponSelected (
    "48 85 D2 74 ? 48 89 74 24 10 57 48 83 EC 20 83 3D ? ? ? ? 00 48 8B FA 48 8B F1 74 ?"
);
_OnWeaponSelected OnWeaponSelected_Original = nullptr;

// This function inits player with all the weapons owned before game starts
RVA<_SelectWeaponByDeclExplicit>
SelectWeaponByDeclExplicit (
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 41 56 41 57 48 83 ec 20 83 3d 91 73 d8 04 00 45 0f b6 f1 45 0f b6 f8 48 8b ea 48 8b f9 74 2c 48 8b 0d fa af cc 04 48 8b 5a 08 48 8b 01 ff 90 10 02 00 00 4c 8b cb 4c 8d 05 1b e5 46 01 8b d0"
);
_SelectWeaponByDeclExplicit SelectWeaponByDeclExplicit_Original = nullptr;

// This function signals that level loading is complete
RVA<_LevelLoadCompleted>
LevelLoadCompleted (
        "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b d9 48 8d 0d 3f 8d 1d 01 e8 e2 c6 c0 fe"
);
_LevelLoadCompleted LevelLoadCompleted_Original = nullptr;

// Function that applies damage to player
RVA<_Damage>
Damage (
    "48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48 8d a8 18 f2 ff ff 48 81 ec a8 0e 00 00 48 c7 45 e0 fe ff ff ff 0f 29 70 a8 0f 29 78 98 44 0f 29 40 88"
);
_Damage Damage_Original = nullptr;

// Function that gets a handle and resolves it to a pointer, useful to get
// weapon object from player object
RVA<_HandleToPointer>
HandleToPointer (
    "40 53 48 83 ec 20 48 8b d9 48 85 c9 74 21 48 8b 01 ff 10 8b 48 68 3b 0d 0c 63 8b 04 7c 11 3b 0d 08 63 8b 04 7f 09 48 8b c3 48 83 c4 20 5b"
);

// Function that updates weapon on idPlayer; we're using it to just get a
// reference of the idPlayer object
RVA<_UpdateWeapon>
UpdateWeapon (
    "40 55 53 57 48 8d ac 24 f0 fb ff ff 48 81 ec 10 05 00 00 48 8b 05 b6 f2 8f 04 48 33 c4 48 89 85 c0 03 00 00 48 8b f9 e8 e4 b8 f8 ff 48 8b 17 48 8b cf 48 8b d8 48 89 44 24 30 ff 92 c8 06 00 00 84 c0 74 1c 48 8d 8f 78 70 01 00 48 8b d3 e8 fd 51 f2 ff 48 8b cf e8 d5 fe ff ff e9 e8 13 00 00 8b 0d 42 87 d1 04 8b c1 48 89 b4 24 38 05 00 00 c1 e8 12 4c 89 a4 24 40 05 00 00 4c 89 b4 24 08 05 00 00 a8 01 74 1c 0f ba f1 12 83 3d de 86 d1 04 00 89 0d 10 87 d1 04 48 8b cf 0f 95 c2"
);
_UpdateWeapon UpdateWeapon_Original = nullptr;

// Function that updates weapon's ammo; we hook it to keep track of the
// capability of the weapnons to fire or not
RVA<_UpdateAmmo>
UpdateAmmo(
    "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 33 ff 48 8b d9 45 84 c0 74 08 39 79 38"
);
_UpdateAmmo UpdateAmmo_Original = nullptr;

// Function to get initiate weapons; we're using it to get the first weapon that
// slayer has when enters the game
RVA<_GetWeaponFromDecl>
GetWeaponFromDecl(
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56 48 83 ec 20 33 ff 48 8b ea 4c 8b f1 39 79 08 7e 57 8b f7 0f 1f 80 00 00 00 00"
);

RVA<_SetFireMode>
SetFireMode(
    "44 88 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 ec 60 48 c7 44 24 40 fe ff ff ff 48 89 9c 24 b8 00 00 00 44 8b f2 48 8b f9 8b 89 d4 08 00 00 44 8b c9 83 f9 ff 44 0f 44 c9 48 8b 77 30"
);
_SetFireMode SetFireMode_Original = nullptr;

RVA<_idHandsUpdate>
idHandsUpdate(
"48 8b c4 55 56 57 41 54 41 55 41 56 41 57 48 8d a8 38 f3 ff ff 48 81 ec 90 0d 00 00 48 c7 85 a0 00 00 00 fe ff ff ff 48 89 58 18 0f 29 70 b8 0f 29 78 a8 44 0f 29 40 98"
);
_idHandsUpdate idHandsUpdate_Original = nullptr;

// Utility functions

using GetMgr_t = void* (__fastcall*)(Player* player);
inline void* GetWeaponMgr(Player* player) {
    auto vtbl = *reinterpret_cast<void***>(player);
    auto fn   = reinterpret_cast<GetMgr_t>(vtbl[0x660/8]);  // 0x660 / 8 = 0xCC
    return fn(player);
}

static inline uint32_t GetPlayerState(Player* player) {
    auto vtable = *reinterpret_cast<void***>(player);
    return reinterpret_cast<PlayerState>(vtable[0xB20/8])(player);
}

static inline uint64_t GetPlayerHandle(Player* player, uint32_t state) {
    auto vtable = *reinterpret_cast<void***>(player);
    return reinterpret_cast<GetHandle>(vtable[0x678/8])(player, state);
}

static inline char *GetWeaponName(long long* weapon) {
    if (!weapon) return nullptr;
    return (char *)(*(long long *)(weapon[6] + 8));
}

// Utility functions to get the idPlayer's current weapon handle

static size_t FindOffsetByQword(void* base, uint64_t value, size_t limit=0x20000) {
    auto b = reinterpret_cast<const uint8_t*>(base);
    for (size_t off = 0; off + 8 <= limit; off += 8) {
        if (*reinterpret_cast<const uint64_t*>(b + off) == value) return off;
    }
    return SIZE_MAX;
}

// XXX This function is unused, it's only needed in case of a game update
// to find out where the new weapon offset is
static void FindCurrWeaponHandle (Player *player) {
    auto state = GetPlayerState(player);
    auto handle = GetPlayerHandle(player, state);
    if (!handle) {
        _LOGD("Player handle is NULL");
        return;
    }
    _LOGD("Player state=%u handle=%p", state, (void*)handle);

    if (handle && g_currWeaponOffset==SIZE_MAX ) {
        g_currWeaponOffset = FindOffsetByQword(player, handle);
        _LOGD("player->currentWeaponHandle offset = %zu\n", g_currWeaponOffset);
        // Now at any time: weapon = *(Weapon**)((uint8_t*)player + g_currWeaponOffset);
    }
}

static Weapon *GetCurrentWeaponAlter (Player *player) {
    uint64_t h2  = *(uint64_t*)((uint8_t*)player + 0x9788);
    void*    p2  = HandleToPointer(h2);
    return (Weapon*)p2;
}

static inline Weapon *GetCurrentWeapon (Player *player) {
    if (!player || !HandleToPointer) {
        _LOGD("Player or HandleToPointer is NULL");
        return nullptr;
    }

    // read 64-bit handle the engine stores in idPlayer
    const auto handle = *reinterpret_cast <const uint64_t *> (
        reinterpret_cast <const uint8_t *> (player) + g_currWeaponOffset
    );

    if (!handle) {
        _LOGD("Player handle is NULL");
        return nullptr;
    }

    return HandleToPointer(handle);
}

static size_t g_weapon_to_ammo = 0;   // discovered at runtime
static size_t g_ammo_to_count = 0x38; // from your write site; adjust if you see 0x34/0x3C

void TryDeriveWeaponToAmmoOffset(void* weapon, void* ammo_from_hook) {
    if (!weapon || !ammo_from_hook || g_weapon_to_ammo) return;
    // brute-force scan the first 0x800 bytes on 8-byte boundaries
    for (size_t off = 0; off < 0x8000; off += 8) {
        void* cand = *(void**)((uint8_t*)weapon + off);
        if (cand == ammo_from_hook) {
            g_weapon_to_ammo = off;
            _LOGD("* Found weapon ammo offset: %u", g_weapon_to_ammo);
            break;
        }
    }
    _LOGD("* Could not find weapon ammo offset!");
}

int ReadAmmoFromWeapon(void* weapon) {
    if (!weapon || !g_weapon_to_ammo) return -1;
    auto ammo = *(uint8_t**)((uint8_t*)weapon + g_weapon_to_ammo);
    return ammo ? *(int*)(ammo + g_ammo_to_count) : -1;
}

// Globals

// This is to make sure that the PID will be sent to the server after
// the server has started
std::mutex g_serverLaunchMutex;
bool g_serverStarted = false;

namespace DualsenseMod {

    // Read and populate offsets and addresses from game code
    bool PopulateOffsets() {
        /*
        HMODULE hMod = GetModuleHandleA("coherentuigt.dll");
        DSM_ModelHandle_GetPropertyHandle =
            (_DSM_ModelHandle_GetPropertyHandle) GetProcAddress (
                hMod,
                "?GetPropertyHandle@ModelHandle@UIGT@Coherent@@QEBA?AUPropertyHandle@23@PEBD@Z"
            );
        */

        _LOG("OnWeaponSelected at %p",
            OnWeaponSelected.GetUIntPtr()
        );

        _LOG("SelectWeaponByDeclExplicit at %p",
            SelectWeaponByDeclExplicit.GetUIntPtr()
        );

        _LOG("HandleToPointer at %p",
            HandleToPointer.GetUIntPtr()
        );

        _LOG("UpdateWeapon at %p",
            UpdateWeapon.GetUIntPtr()
        );

        _LOG("UpdateAmmo at %p",
            UpdateAmmo.GetUIntPtr()
        );

        _LOG("SetFireMode at %p",
            SetFireMode.GetUIntPtr()
        );

        _LOG("idHandsUpdate at %p",
            idHandsUpdate.GetUIntPtr()
        );

        _LOG("GetWeaponFromDecl at %p",
            GetWeaponFromDecl.GetUIntPtr()
        );

        _LOG("Damage at %p",
            Damage.GetUIntPtr()
        );

        _LOG("LevelLoadCompleted at %p",
            LevelLoadCompleted.GetUIntPtr()
        );

        if (!g_doomBaseAddr) {
            _LOGD("DOOM base address is not set!");
            return false;
        }

        if (!OnWeaponSelected || !SelectWeaponByDeclExplicit || !HandleToPointer || !LevelLoadCompleted || !UpdateWeapon || !UpdateAmmo || !SetFireMode || !idHandsUpdate || !GetWeaponFromDecl)
            return false;

        return true;
    }

    void resetAdaptiveTriggers() {
        dualsensitive::setLeftTrigger(TriggerProfile::Normal);
        dualsensitive::setRightTrigger(TriggerProfile::Normal);
        _LOGD("Adaptive Triggers reset successfully!");
    }


    void noAmmoAdaptiveTriggers() {
        dualsensitive::setLeftTrigger(TriggerProfile::Normal);
        dualsensitive::setRightTrigger(TriggerProfile::GameCube);
        _LOGD("No Ammo Adaptive Triggers set successfully!");
    }

    void sendAdaptiveTriggersForCurrentWeapon(bool mod = false);
    void sendAdaptiveTriggersForCurrentWeapon(bool mod) {
        char * currWeaponName = GetWeaponName (
                reinterpret_cast<long long*>(g_currWeapon)
        );
        _LOGD("* curr weapon: %s!", currWeaponName);
        if (currWeaponName && HasAmmo(currWeaponName)){
            _LOGD("* Sending adaptive trigger setting!");
            SendTriggers(currWeaponName, mod);
            return;
        }
        _LOGD("* No valid weapon name or no ammo - resetting triggers!");
        noAmmoAdaptiveTriggers();
    }

    void OnWeaponSelected_Hook(void *player, long long *weapon) {
        _LOGD("* OnWeaponSelected hook!!!");

        if (weapon == nullptr) {
            OnWeaponSelected_Original(player, weapon);
            return;
        }

        char *weaponName =  GetWeaponName(weapon);
        g_currWeapon = weapon;
        bool hasAmmo = HasAmmo(weaponName);
        _LOGD (
                "idPlayer::OnWeaponSelected - newWeapon = %s, hasAmmo: %s\n",
                weaponName, hasAmmo ? "true" : "false"
        );
        sendAdaptiveTriggersForCurrentWeapon();
        OnWeaponSelected_Original(player, weapon);

        // XXX uncomment to find the idPlayer's current weapon handler
        // if there is a new update of the game out
        //FindCurrWeaponHandle((Player *) player);

        return;
    }

    void UpdateWeapon_Hook (void *player) {

        if (!g_currPlayer) {
            _LOGD("* set idPlayer!");
            g_currPlayer = player;
        }

        UpdateWeapon_Original(player);
        return;
    }


int UpdateAmmo_Hook (void *ammo, int delta, char clamp) {
    int ret = UpdateAmmo_Original(ammo, delta, clamp);
    if (!ammo)
        return ret;
    int* pCount = (int*)((uint8_t*)ammo + g_AmmoCountOffset);
    int  count  = *pCount;
    _LOGD("* UpdateAmmo hook! ammo ptr: %p, delta: %d, clamp: %d, AMMO: %d",
            ammo,
            delta,
            clamp,
            count
    );

    if (count <= 0 && g_HasAmmo[ammo])
        g_HasAmmo[ammo] = false;
    if (count > 0 && !g_HasAmmo[ammo])
        g_HasAmmo[ammo] = true;

    if (!g_currWeapon) {
        return ret;
    }

    char *weapon_str = GetWeaponName(reinterpret_cast<long long*>(g_currWeapon));
    if (!weapon_str) {
        return ret;
    }

    std::string weaponName = std::string(weapon_str);
    std::string ammoType = g_WeaponToAmmoType[weaponName];
    if (ammoType == "infinite")
        return ret;
    if (!g_AmmoPtrs[ammoType]) {
        g_AmmoPtrs[ammoType] = ammo;
        _LOGD("g_AmmoPtrs[%s] = %p, |(%p)|", ammoType.c_str(), ammo, g_AmmoPtrs[ammoType]);
    }

    return ret;
}

enum TriggerState : int { TS_Idle=0, TS_Pressed=1, TS_Held=2, TS_Released=3 /* guess */ };

static inline int GetSelectedMode(void* weapon) {
    return weapon ? *reinterpret_cast<int*>((uint8_t*)weapon + 0x8D4) : -1;
}

static inline TriggerState GetSecondaryState(void* weapon) {
    return (TriggerState)*reinterpret_cast<int*>((uint8_t*)weapon + 0x8FC);
}
static inline TriggerState GetAltState(void* weapon) {
    return (TriggerState)*reinterpret_cast<int*>((uint8_t*)weapon + 0x584);
}

bool SetFireMode_Hook(void* weapon, uint32_t mode, char allowSame) {
    // This tells us if mod is active (i.e., if left trigger is pressed)
    // XXX NOTE: we should check allowSame and other fields based on the
    // Ghidra source as we might discover which mode is active, if the gun
    // can still fire or if the mod is in charging state, etc.
    bool ok = SetFireMode_Original(weapon, mode, allowSame);
    if (!g_currPlayer || !g_currWeapon)
        return ok;
    //int wMode = GetSelectedMode(g_currWeapon);
    //TriggerState secTrigger = GetSecondaryState(g_currWeapon);
    if (ok && mode != g_previousMode) {
        print_state();
        _LOGD("* SetFireMode hook! weapon: %p, curr weapon: %p  | g_previousMode: %d, current: %d",
                weapon, g_currWeapon,
                g_previousMode, mode
        );
        sendAdaptiveTriggersForCurrentWeapon((bool)mode);
    }
    g_previousMode = mode;
    return ok;
}



static inline bool CallIsDead(void* player);
static std::atomic<uint64_t> g_lastHandsBeat{0}; // perf counter in ms
//static std::atomic<bool>     g_inLoad{false};
static inline uint64_t NowMs();

void idHandsUpdate_Hook(void *self, void * state) {
    idHandsUpdate_Original(self, state);
    if (g_state.load() == GameState::Idle)
        return;
    // Always tick the heartbeat when we are truly in gameplay.
    if (g_currPlayer && !CallIsDead(g_currPlayer)) {
        g_lastHandsBeat.store(NowMs(), std::memory_order_relaxed);
        if (g_state.load() != GameState::InGame) {
            g_state.store(GameState::InGame);
            _LOGD("[FSM] -> InGame (hands ticking)");
            // replay last trigger settings here
            sendAdaptiveTriggersForCurrentWeapon();
        }
    }
    return;
}


static void MenuPauseWatcher() {
 const uint64_t THRESHOLD_MS = 350;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        //if (g_inLoad.load(std::memory_order_relaxed)) continue;
        if (g_state.load(std::memory_order_relaxed) == GameState::Idle) continue;

        const uint64_t last = g_lastHandsBeat.load(std::memory_order_relaxed);
        const uint64_t age  = NowMs() - last;

        // purely heartbeat-based pause detection
        if (age > THRESHOLD_MS) {
            if (g_state.load(std::memory_order_relaxed) != GameState::Paused) {
                g_state.store(GameState::Paused, std::memory_order_relaxed);
                _LOGD("[FSM] -> Paused (hands stalled %.0f ms)", double(age));
                resetAdaptiveTriggers();
            }
        }
        // Resume is handled in idHandsUpdate_Hook when it ticks again.
    }
}

// optional global flag for your mod
static std::atomic<bool> g_PlayerDead{false};
// reentrancy + once-only flag
static thread_local bool g_inDamage = false;
static std::atomic<bool> g_pendingDeath{false};


static inline bool CallIsDead(void* player)
{
    if (!player) return false;
    void** vtbl = *reinterpret_cast<void***>(player);
    if (!vtbl) return false;
    auto IsDeadFn = reinterpret_cast<_IsDead>(vtbl[0x6C8 / 8]); // index, not byte offset
    return IsDeadFn ? IsDeadFn(player) : false;
}

void Damage_Hook(
    void* player,
    void* inflictor,
    void* attacker,
    long long damageDecl,
    float damageScale,
    const float* dir,
    const float* hitInfo)
{
    if (g_inDamage) {
        Damage_Original(player, inflictor, attacker, damageDecl, damageScale, dir, hitInfo);
        return;
    }

    g_inDamage = true;

    // let the game apply damage
    Damage_Original(player, inflictor, attacker, damageDecl, damageScale, dir, hitInfo);

    if (player == g_currPlayer) {
        // state after damage
        const bool isDead = CallIsDead(player);

        if (isDead)
        {
            g_state.store(GameState::Idle, std::memory_order_release);
            g_lastHandsBeat.store(NowMs(), std::memory_order_relaxed);
            resetAdaptiveTriggers();
            g_currWeapon = nullptr;
            g_HasAmmo.clear(); // reset ammo info
            resetAmmoPtrs();
            _LOGD("* Damage hook, Player is DEAD! Switching to Idle state...");
        }
    }
    g_inDamage = false;
}

unsigned long long SelectWeaponByDeclExplicit_Hook(long long *player,
                            long long decl, char param_3, char param_4) {
    _LOGD("* idPlayer::SelectWeaponByDeclExplicit hook!!!");

    Weapon *weapon = nullptr;
    if (long long *mgr = (long long *) GetWeaponMgr(player)) {
        weapon = (Weapon *)GetWeaponFromDecl(mgr, decl);
        if (weapon) {
            const char* name = GetWeaponName(reinterpret_cast<long long*>(weapon));
            if (name && name[0]) {
                _LOGD("* (init) curr weapon: %s", name);
            }
            g_currWeapon = weapon;
        }
    }
    unsigned long long ret = SelectWeaponByDeclExplicit_Original(player, decl, param_3, param_4);

    // reset player here (paused used for load the latest checkpoint from the main menu)
    if (g_currPlayer && (g_state == GameState::Idle || g_state == GameState::Paused)) {
        g_state.store(GameState::Idle, std::memory_order_release);
        g_currPlayer = nullptr;
    }
    return ret;
}

    // util
    bool startsWith(const char* s, const char* prefix) {
        if (!s || !prefix) return false;
        const size_t m = std::strlen(prefix);
        return !std::strncmp(s, prefix, m);
    }

    static inline uint64_t NowMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    void LevelLoadCompleted_Hook (long long *this_idLoadScreen) {
        _LOGD("idLoadScreen::LevelLoadCompleted hook!");

        LevelLoadCompleted_Original(this_idLoadScreen);
        if (g_currPlayer && g_state == GameState::Paused) {
            g_state.store(GameState::Idle, std::memory_order_release);
            resetAdaptiveTriggers();
            g_currWeapon = nullptr;
            g_lastHandsBeat.store(NowMs(), std::memory_order_relaxed);
            //g_HasAmmo.clear(); // reset ammo info
            //resetAmmoPtrs();
            _LOGD("* Exiting to main menu! Switching to Idle state...");
            return;
        }
        if (g_currPlayer && g_state == GameState::Idle) {
            g_state.store(GameState::InGame, std::memory_order_release);
            Weapon* weapon = GetCurrentWeaponAlter(g_currPlayer);
            if (weapon) {
                const char* name = GetWeaponName(reinterpret_cast<long long*>(weapon));
                if (name && name[0]) {
                    g_currWeapon = weapon;
                    bool hasAmmo = HasAmmo(name);
                    _LOGD (
                            "* curr weapon = %s, hasAmmo: %s\n",
                            name, hasAmmo ? "true" : "false"
                    );

                }
                // enable triggers
                sendAdaptiveTriggersForCurrentWeapon();
            } else {
                    _LOGD("* curr weapon: (not found!)");
            }
        }
        return;
    }

    bool ApplyHooks() {
        _LOG("Applying hooks...");
        // Hook loadout type registration to obtain pointer to the model handle
        MH_Initialize();

        MH_CreateHook (
            OnWeaponSelected,
            OnWeaponSelected_Hook,
            reinterpret_cast<LPVOID *>(&OnWeaponSelected_Original)
        );
        if (MH_EnableHook(OnWeaponSelected) != MH_OK) {
            _LOG("FATAL: Failed to install OnWeaponSelected hook.");
            return false;
        }

        MH_CreateHook (
            SelectWeaponByDeclExplicit,
            SelectWeaponByDeclExplicit_Hook,
            reinterpret_cast<LPVOID *>(&SelectWeaponByDeclExplicit_Original)
        );
        if (MH_EnableHook(SelectWeaponByDeclExplicit) != MH_OK) {
            _LOG("FATAL: Failed to install SelectWeaponByDeclExplicit hook.");
            return false;
        }

        MH_CreateHook (
            UpdateWeapon,
            UpdateWeapon_Hook,
            reinterpret_cast<LPVOID *>(&UpdateWeapon_Original)
        );
        if (MH_EnableHook(UpdateWeapon) != MH_OK) {
            _LOG("FATAL: Failed to install UpdateWeapon hook.");
            return false;
        }
        MH_CreateHook (
            UpdateAmmo,
            UpdateAmmo_Hook,
            reinterpret_cast<LPVOID *>(&UpdateAmmo_Original)
        );
        if (MH_EnableHook(UpdateAmmo) != MH_OK) {
            _LOG("FATAL: Failed to install UpdateAmmo hook.");
            return false;
        }

        MH_CreateHook (
            SetFireMode,
            SetFireMode_Hook,
            reinterpret_cast<LPVOID *>(&SetFireMode_Original)
        );
        if (MH_EnableHook(SetFireMode) != MH_OK) {
            _LOG("FATAL: Failed to install SetFireMode hook.");
            return false;
        }
        MH_CreateHook (
            idHandsUpdate,
            idHandsUpdate_Hook,
            reinterpret_cast<LPVOID *>(&idHandsUpdate_Original)
        );
        if (MH_EnableHook(idHandsUpdate) != MH_OK) {
            _LOG("FATAL: Failed to install idHandsUpdate hook.");
            return false;
        }
        MH_CreateHook (
            Damage,
            Damage_Hook,
            reinterpret_cast<LPVOID *>(&Damage_Original)
        );
        if (MH_EnableHook(Damage) != MH_OK) {
            _LOG("FATAL: Failed to install Damage hook.");
            return false;
        }
        MH_CreateHook (
            LevelLoadCompleted,
            LevelLoadCompleted_Hook,
            reinterpret_cast<LPVOID *>(&LevelLoadCompleted_Original)
        );
        if (MH_EnableHook(LevelLoadCompleted) != MH_OK) {
            _LOG("FATAL: Failed to install LevelLoadCompleted hook.");
            return false;
        }
        _LOG("Hooks applied successfully!");

        return true;
    }

    bool InitAddresses() {
        _LOG("Sigscan start");
        RVAUtils::Timer tmr; tmr.start();
        RVAManager::UpdateAddresses(0);
        _LOG("Sigscan elapsed: %llu ms.", tmr.stop());

        // Check if all addresses were resolved
        for (auto rvaData : RVAManager::GetAllRVAs()) {
            if (!rvaData->effectiveAddress) {
                _LOG("Signature: %s was not resolved!", rvaData->sig);
            }
        }
        if (!RVAManager::IsAllResolved())
            return false;

        return true;
    }


#define DEVICE_ENUM_INFO_SZ 16
#define CONTROLLER_LIMIT 16
std::string wstring_to_utf8(const std::wstring& ws) {
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                                   nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte (
            CP_UTF8, 0, ws.c_str(), -1, &s[0], len, nullptr, nullptr
    );
    s.resize(len - 1);
    return s;
}

    constexpr int MAX_ATTEMPTS = 5;
    constexpr int RETRY_DELAY_MS = 2000;

    void Init() {
        g_logger.Open("./mods/dualsensemod.log");
        _LOG("DOOM (2016) DualsenseMod v1.0 by Thanos Petsas (SkyExplosionist)");
        g_doomBaseAddr = GetModuleHandle(NULL);
        _LOG("Module base: %p", g_doomBaseAddr);

        // Sigscan
        if (!InitAddresses() || !PopulateOffsets()) {
            MessageBoxA (
                NULL,
                "DualsenseMod is not compatible with this version of DOOM (2016).\nPlease visit the mod page for updates.",
                "DualsenseMod",
                MB_OK | MB_ICONEXCLAMATION
            );
            _LOG("FATAL: Incompatible version");
            return;
        }

        _LOG("Addresses set");

        // init config
        g_config = Config(INI_LOCATION);
        g_config.print();

        InitTriggerSettings();

        ApplyHooks();

        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            _LOG("Client starting DualSensitive Service...\n");
            if (!launchServerElevated()) {
                _LOG("Error launching the DualSensitive Service...\n");
                return 1;
            }
            g_serverLaunchMutex.lock();
            g_serverStarted = true;
            g_serverLaunchMutex.unlock();
            _LOG("DualSensitive Service launched successfully...\n");
            return 0;
        }, nullptr, 0, nullptr);


        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            // wait for server to start first
            do {
                g_serverLaunchMutex.lock();
                bool started = g_serverStarted;
                g_serverLaunchMutex.unlock();
                if (started) break;
                std::this_thread::sleep_for(std::chrono::seconds(2));
            } while (true);

            _LOG("Client starting DualSensitive Service...\n");
            auto status = dualsensitive::init(AgentMode::CLIENT, "./mods/duaslensitive-client.log", g_config.isDebugMode);
            if (status != dualsensitive::Status::Ok) {
                _LOG("Failed to initialize DualSensitive in CLIENT mode, status: %d", static_cast<std::underlying_type<dualsensitive::Status>::type>(status));
            }
                _LOG("DualSensitive Service launched successfully...\n");
            dualsensitive::sendPidToServer();
            return 0;
        }, nullptr, 0, nullptr);

        std::thread([]{
            MenuPauseWatcher();
        }).detach();

        _LOG("Ready.");
    }
}
