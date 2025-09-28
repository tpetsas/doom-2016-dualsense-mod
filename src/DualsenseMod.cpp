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
            "WEAPON_PISTOL_DEFAULT", // Grip
            {
                .L2 = new TriggerSetting(TriggerProfile::Choppy, {}),
                .R2 = new TriggerSetting(TriggerProfile::Soft, {})
            }
        },
        {
            "WEAPON_SHOTGUN_SINGLESHOT", // Shatter
            {
                .L2 = new TriggerSetting (
                        TriggerMode::Rigid_A,
                        {60, 71, 56, 128, 195, 210, 255}
                ),
                .R2 = new TriggerSetting (
                        TriggerProfile::SlopeFeedback,
                        {0, 5, 1, 8}
                )
            }
        },
        {
            "WEAPON_SMG_STANDARD", // Spin
            {
                .L2 = new TriggerSetting (
                        TriggerMode::Rigid_A,
                        {71, 96, 128, 128, 128, 128, 128}
                ),
                .R2 = new TriggerSetting(
                        TriggerProfile::Vibration,
                        {3, 4, 14}
                )
            }
        },
        {
            "WEAPON_RAILGUN_STANDARD", // Pierce
            {
                .L2 = new TriggerSetting (
                        TriggerProfile::Machine,
                        {1, 8, 3, 3, 184, 0}
                ),
                .R2 = new TriggerSetting (
                        TriggerMode::Pulse_B,
                        {238, 215, 66, 120, 43, 160, 215}
                )
            }
        },
        {
            "WEAPON_ROCKETLAUNCHER_TRIPLESHOT", // Charge
            {
                .L2 = new TriggerSetting(TriggerMode::Rigid, {}),
                .R2 = new TriggerSetting (
                        TriggerMode::Rigid_A,
                        {209, 42, 232, 192, 232, 209, 232}
                )
            }
        },
        {
            "WEAPON_DLC2_STICKYLAUNCHER", // Surge
            {
                .L2 = new TriggerSetting(TriggerProfile::Feedback, {3, 3}),
                .R2 = new TriggerSetting(TriggerProfile::VeryHard, {})
            }
        }
    };
}

void SendTriggers(std::string weaponType) {
    Triggers t = g_TriggerSettings[weaponType];
#if 1
    if (t.L2->isCustomTrigger)
        dualsensitive::setLeftCustomTrigger(t.L2->mode, t.L2->extras);
    else
        dualsensitive::setLeftTrigger (t.L2->profile, t.L2->extras);
    if (t.R2->isCustomTrigger)
        dualsensitive::setRightCustomTrigger(t.R2->mode, t.R2->extras);
    else
        dualsensitive::setRightTrigger (t.R2->profile, t.R2->extras);
#endif
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

enum class GameState : uint8_t {
    Idle,       // Game hasn't started yet
    InGame,     // Gameplay is on
    Paused      // Game is paused
};

static std::atomic<GameState> g_state{GameState::Idle};

//"weapon/zion/player/sp/fists"
//"weapon/zion/player/sp/fists_berserk"
//"weapon/zion/player/sp/fists_virtualGUI"

std::vector<std::string> g_WeaponList = {
    "weapon/zion/player/sp/pistol",
    "weapon/zion/player/sp/shotgun",
    "weapon/zion/player/sp/chainsaw",
    "weapon/zion/player/sp/heavy_rifle_heavy_ar",
    "weapon/zion/player/sp/plasma_rifle",
    "weapon/zion/player/sp/gauss_cannon",
    "weapon/zion/player/sp/rocket_launcher",
    "weapon/zion/player/sp/chaingun",
    "weapon/zion/player/sp/double_barrel",
    "weapon/zion/player/sp/bfg",
};

// XXX NOTE: pistol has infinite ammo so it's not in the following list
std::vector<std::string> g_AmmoList = {
    "bullets",  // heavy_rifle_heavy_ar, chaingun
    "shells",   // shotgun, double_barrel
    "rockets",  // rocket_launcher
    "plasma",   // plasma_rifle, gauss_cannon
    "cells",    // bfg
    "fuel"      // chainsaw
};

// weapon name to ammo
static std::unordered_map<std::string, std::string> g_WeaponToAmmoType = {
  {"weapon/zion/player/sp/pistol",               "infinite"},
  {"weapon/zion/player/sp/heavy_rifle_heavy_ar", "bullets"},
  {"weapon/zion/player/sp/chaingun",             "bullets"},
  {"weapon/zion/player/sp/shotgun",              "shells"},
  {"weapon/zion/player/sp/double_barrel",        "shells"},
  {"weapon/zion/player/sp/rocket_launcher",      "rockets"},
  {"weapon/zion/player/sp/plasma_rifle",         "plasma"},
  {"weapon/zion/player/sp/gauss_cannon",         "plasma"},
  {"weapon/zion/player/sp/bfg",                  "cells"},
  {"weapon/zion/player/sp/chainsaw",             "fuel"},
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

using _EventTriggered =
            void* (__fastcall*)(
    void*        screenInfo,     // &DAT_145bedc20
    const char*  screenName,     // "idMenuScreen_Start"
    const char*  baseTypeName,   // "idMenuScreen"
    uint32_t     sizeBytes,      // 0x1C0 in the snippet
    uint8_t      flags,          // looked like 0 in my call
    MenuCtor_t   ctor,           // FUN_140FC6060
    MenuDtor_t   dtor,           // LAB_140FC3E90
    MenuUpdate_t update,         // FUN_140FCA440
    MenuDraw_t   draw,           // LAB_1415033F0
    const uint32_t* initBlob,    // &uStack_28 (small init template)
    void*        guardFunc       // _guard_check_icall
);

using _LevelLoadCompleted =
                    void (__fastcall*)(long long *this_idLoadScreen);

using _EventIdFromName =
                    void (__fastcall*)(void *eventSystem, const char *eventName);


using _UpdateAmmo =
                    int (__fastcall *)(void *ammo, int delta, char clamp);


using _GetWeaponFromDecl =
                    void* (__fastcall*)(long long* mgr, long long decl);

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

RVA<_SelectWeaponByDeclExplicit>
SelectWeaponByDeclExplicit (
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 41 56 41 57 48 83 ec 20 83 3d 91 73 d8 04 00 45 0f b6 f1 45 0f b6 f8 48 8b ea 48 8b f9 74 2c 48 8b 0d fa af cc 04 48 8b 5a 08 48 8b 01 ff 90 10 02 00 00 4c 8b cb 4c 8d 05 1b e5 46 01 8b d0"

    //"48 8B 0D ? ? ? ? 48 8B 5A 08 48 8B 01 FF 90 10 02 00 00 4C 8B CB 4C 8D 05 ? ? ? ? 8B D0 48 8D 0D ? ? ? ? E8 ? ? ? ?"
);
_SelectWeaponByDeclExplicit SelectWeaponByDeclExplicit_Original = nullptr;

RVA<_LevelLoadCompleted>
LevelLoadCompleted (
        "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 48 8b d9 48 8d 0d 3f 8d 1d 01 e8 e2 c6 c0 fe"
);
_LevelLoadCompleted LevelLoadCompleted_Original = nullptr;

RVA<_EventIdFromName>
EventIdFromName (
    "40 57 48 83 ec 70 48 c7 44 24 28 fe ff ff ff 48 89 9c 24 90 00 00 00 48 8b 05 02 f0 ff 03 48 33 c4 48 89 44 24 60"
);
_EventIdFromName EventIdFromName_Original = nullptr;

RVA<_Damage>
Damage (
    "48 8b c4 55 53 56 57 41 54 41 55 41 56 41 57 48 8d a8 18 f2 ff ff 48 81 ec a8 0e 00 00 48 c7 45 e0 fe ff ff ff 0f 29 70 a8 0f 29 78 98 44 0f 29 40 88"
);
_Damage Damage_Original = nullptr;



// Check weapon mod
// FUN_140f11670
// 48 83 ec 08 4c 8b d1 4c 8b da 48 63 89 d4 08 00 00 83 f9 ff 8b d1 4d 8b 4a 30 0f 44 d1

//140b44840
//48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56 48 83 ec 30 48 8b 84 24 98 00 00 00

// FUN_140e46970 noisy
// FUN_140e446a0 loads all the weapons
// FUN_140e44090 x
// FUN_140e44000 x
// FUN_140da88c0 x
// FUN_140da8830 x
// FUN_1406adc70 x

// FUN_140e446a0 loads all the weapons (xrefs:)
// FUN_140a5d850: idTarget_EquipItems
// FUN_140e445e0: idPlayer::SelectWeaponByDecl

RVA<_HandleToPointer>
HandleToPointer (
    "40 53 48 83 ec 20 48 8b d9 48 85 c9 74 21 48 8b 01 ff 10 8b 48 68 3b 0d 0c 63 8b 04 7c 11 3b 0d 08 63 8b 04 7f 09 48 8b c3 48 83 c4 20 5b"
);

RVA<_UpdateWeapon>
UpdateWeapon (
    "40 55 53 57 48 8d ac 24 f0 fb ff ff 48 81 ec 10 05 00 00 48 8b 05 b6 f2 8f 04 48 33 c4 48 89 85 c0 03 00 00 48 8b f9 e8 e4 b8 f8 ff 48 8b 17 48 8b cf 48 8b d8 48 89 44 24 30 ff 92 c8 06 00 00 84 c0 74 1c 48 8d 8f 78 70 01 00 48 8b d3 e8 fd 51 f2 ff 48 8b cf e8 d5 fe ff ff e9 e8 13 00 00 8b 0d 42 87 d1 04 8b c1 48 89 b4 24 38 05 00 00 c1 e8 12 4c 89 a4 24 40 05 00 00 4c 89 b4 24 08 05 00 00 a8 01 74 1c 0f ba f1 12 83 3d de 86 d1 04 00 89 0d 10 87 d1 04 48 8b cf 0f 95 c2"
);
_UpdateWeapon UpdateWeapon_Original = nullptr;

RVA<_EventTriggered>
EventTriggered (
    "48 89 4c 24 08 56 57 41 56 48 83 ec 30 48 c7 44 24 20 fe ff ff ff 48 89 5c 24 58 48 89 6c 24 60 41 8b f9 49 8b d8 48 8b ea 48 8b f1 45 33 f6 4c 89 b1 88 00 00 00 4c 89 71 70 4c 89 71 78 4c 89 b1 80 00 00 00 44 88 71 61 48 89 11 48 8d 15 3d 93 c9 00 48 8b cd e8 85 84 db fe 48 8b cb 85 c0 49 0f 44 ce 48 89 4e 08 48 8b 84 24 98 00 00 00 0f 10 00 0f 11 46 10 f2 0f 10 48 10 f2 0f 11 4e 20 48 8b 44 24 78 48 89 46 28 48 8b 84 24 80 00 00 00 48 89 46 30 48 8b 84 24 88 00 00 00 48 89 46 38 48 8b 84 24 90 00 00 00 48 89 46 40 48 8b 84 24 a0 00 00 00 48 89 46 48 48 8b cb e8 1e 02 00 00 48 89 46 50 0f b6 44 24 70"
);
_EventTriggered EventTriggered_Original = nullptr;

RVA<_UpdateAmmo>
UpdateAmmo(
    "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 20 33 ff 48 8b d9 45 84 c0 74 08 39 79 38"
);
_UpdateAmmo UpdateAmmo_Original = nullptr;

// get weapon from weapon decl
RVA<_GetWeaponFromDecl>
GetWeaponFromDecl(
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 48 89 7c 24 20 41 56 48 83 ec 20 33 ff 48 8b ea 4c 8b f1 39 79 08 7e 57 8b f7 0f 1f 80 00 00 00 00"
);


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

#if 1
static Weapon *GetCurrentWeaponAlter (Player *player) {
    uint64_t h2  = *(uint64_t*)((uint8_t*)player + 0x9788);
    void*    p2  = HandleToPointer(h2);
    return (Weapon*)p2;
}
#endif

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

        _LOG("GetWeaponFromDecl at %p",
            GetWeaponFromDecl.GetUIntPtr()
        );

        _LOG("Damage at %p",
            Damage.GetUIntPtr()
        );

        _LOG("EventTriggered at %p",
            EventTriggered.GetUIntPtr()
        );

        _LOG("LevelLoadCompleted at %p",
            LevelLoadCompleted.GetUIntPtr()
        );

        _LOG("EventIdFromName at %p",
            EventIdFromName.GetUIntPtr()
        );

        if (!g_doomBaseAddr) {
            _LOGD("DOOM base address is not set!");
            return false;
        }

        if (!OnWeaponSelected || !SelectWeaponByDeclExplicit || !HandleToPointer || !EventTriggered || !LevelLoadCompleted || !EventIdFromName || !UpdateWeapon || !UpdateAmmo || !GetWeaponFromDecl)
            return false;

        return true;
    }

    void setAdaptiveTriggersForCurrrentWeapon();

    void resetAdaptiveTriggers() {
        // TODO: do that in a separate thread to avoid stuttering
        // reset triggers to Normal mode
        dualsensitive::setLeftTrigger(TriggerProfile::Normal);
        dualsensitive::setRightTrigger(TriggerProfile::Normal);
        _LOGD("Adaptive Triggers reset successfully!");
    }

    void OnWeaponSelected_Hook(void *player, long long *weapon) {
        _LOGD("* OnWeaponSelected hook!!!");

        if (weapon != nullptr) {
            char *weaponName =  GetWeaponName(weapon);
            g_currWeapon = weapon;
            bool hasAmmo = HasAmmo(weaponName);
            _LOGD (
                    "idPlayer::OnWeaponSelected - newWeapon = %s, hasAmmo: %s\n",
                    weaponName, hasAmmo ? "true" : "false"
            );
        }
        OnWeaponSelected_Original(player, weapon);

        // XXX uncomment to find the idPlayer's current weapon handler
        //FindCurrWeaponHandle((Player *) player);

        //Weapon *w = GetCurrentWeaponAlter(player);
        //_LOGD("* Startup weapon: %s", GetWeaponName((long long *)w));

        return;
    }

    void FSM(const char *eventName);
    void UpdateWeapon_Hook (void *player) {

        if (!g_currPlayer) {
            _LOGD("* set idPlayer!");
            g_currPlayer = player;
        }
#if 0
        if (!g_currWeapon) {
            Weapon* weapon = GetCurrentWeaponAlter(player); // uses 0x9788 + HandleToPointer
            if (weapon) {
                const char* name = GetWeaponName(reinterpret_cast<long long*>(weapon));
                if (name && name[0]) {
                    _LOGD("* idPlayer::UpdateWeapon hook - curr weapon: %s", name);
                    g_currWeapon = weapon;
                }
            }
        }
#endif

        UpdateWeapon_Original(player);
        return;
    }


int UpdateAmmo_Hook (void *ammo, int delta, char clamp) {
    int ret = UpdateAmmo_Original(ammo, delta, clamp);
    if (!ammo)
        ret;
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

#if 0
    if (!g_weapon_to_ammo)
        TryDeriveWeaponToAmmoOffset(g_currWeapon, ammo);
#endif
    return ret;
}

// optional global flag for your mod
static std::atomic<bool> g_PlayerDead{false};

static inline bool CallIsDead(void* player)
{
    // vtable-based call to function pointer at offset 0x6C8
    auto** vtbl = reinterpret_cast<uintptr_t**>(player);
    auto addr   = *vtbl ? *vtbl : nullptr;
    if (!addr) return false;

    auto fnPtr = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(*vtbl) + 0x6C8);
    if (!fnPtr) return false;

    auto IsDead = reinterpret_cast<_IsDead>(fnPtr);
    return IsDead(player);
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
    //_LOGD("* Damage hook!");
    // state before damage is applied
    const bool wasDead = CallIsDead(player);

    // let the game apply damage
    Damage_Original(player, inflictor, attacker, damageDecl, damageScale, dir, hitInfo);

    // state after damage
    const bool isDead = CallIsDead(player);

    // alive -> dead detection
    if (!wasDead && isDead)
    {
        g_state.store(GameState::Idle, std::memory_order_release);
        g_currWeapon = nullptr;
        g_HasAmmo = {}; // reset ammo info
        resetAmmoPtrs();
        _LOGD("* Damage hook, Player is DEAD! Switching to Idle state...");
    }
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
/*
        Weapon* weapon = GetCurrentWeaponAlter(player);
        if (weapon) {
            const char* name = GetWeaponName(reinterpret_cast<long long*>(weapon));
            if (name && name[0]) {
                _LOGD("* (init) curr weapon: %s", name);
            }
        } else {
                _LOGD("* (init) curr weapon: (not found!)");
        }
*/
        // reset player here
        if (g_currPlayer && g_state == GameState::Idle)
            g_currPlayer = nullptr;
        return ret;
    }

    void* EventTriggered_Hook(
    void*        screenInfo,     // &DAT_145bedc20
    const char*  screenName,     // "idMenuScreen_Start"
    const char*  baseTypeName,   // "idMenuScreen"
    uint32_t     sizeBytes,      // 0x1C0 in the snippet
    uint8_t      flags,          // looked like 0 in my call
    MenuCtor_t   ctor,           // FUN_140FC6060
    MenuDtor_t   dtor,           // LAB_140FC3E90
    MenuUpdate_t update,         // FUN_140FCA440
    MenuDraw_t   draw,           // LAB_1415033F0
    const uint32_t* initBlob,    // &uStack_28 (small init template)
    void*        guardFunc       // _guard_check_icall (or similar)
    ){
        _LOGD("* EventTriggered hook!");
        //_LOGD("event screen method: %s, screen name: %s, screenName, baseTypeName);
        return EventTriggered_Original(screenInfo, screenName, baseTypeName,
                sizeBytes, flags, ctor, dtor, update, draw, initBlob,
                guardFunc);
    }

    // util
    bool startsWith(const char* s, const char* prefix) {
        if (!s || !prefix) return false;
        const size_t m = std::strlen(prefix);
        return !std::strncmp(s, prefix, m);
    }

    void FSM(const char *eventName) {
        switch(g_state) {
            case GameState::Idle:
                // idle state can be changed only from LevelLoadCompleted hook
                if (startsWith(eventName, "GameStarted")) {
                    g_state.store(GameState::InGame, std::memory_order_release);
                    _LOGD("* Game started!");
                    if (g_currPlayer) {
                        _LOGD("  - idPlayer already set. Check for current weapon...");
                        // TODO: create a function that does that
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
                        } else {
                                _LOGD("* curr weapon: (not found!)");
                        }
                    }

                    // enable triggers
                }
                break;
            case GameState::InGame:
                if (startsWith(eventName, "off")) {
                    // skip this state
                    ;
                    //g_state.store(GameState::Idle, std::memory_order_release);
                    //_LOGD("* Player died! Switching to Idle state...");
                    //g_currWeapon = nullptr;
                    // disable triggers
                } else if (startsWith(eventName, "select")) {
                    g_state.store(GameState::Paused, std::memory_order_release);
                    if (g_currPlayer)
                        _LOGD("* Game paused!");
                    // disable triggers
                }
                break;
            case GameState::Paused:
                if (startsWith(eventName, "off")) {
                    g_state.store(GameState::Idle, std::memory_order_release);
                    g_currWeapon = nullptr;
                    g_HasAmmo = {}; // reset ammo info
                    resetAmmoPtrs();
                    _LOGD("* Rare, but game ended! Switching to Idle state...");
                    // disable triggers
                } else if (startsWith(eventName, "on")) {
                    g_state.store(GameState::InGame, std::memory_order_release);
                    if (g_currPlayer) {
                        _LOGD("* Back in game again!");
                        // TODO: create a function that does that
                        Weapon* weapon = GetCurrentWeaponAlter(g_currPlayer);
                        if (weapon) {
                            const char* name = GetWeaponName(reinterpret_cast<long long*>(weapon));
                            if (name && name[0]) {
                                _LOGD("* curr weapon: %s", name);
                                g_currWeapon = weapon;
                            }
                        } else {
                                _LOGD("* curr weapon: (not found!)");
                        }

                    }
                    // enable triggers
                }
                break;
            default:
                _LOG("* Unsupported state! How did we get there?!");
        }
    }

    void EventIdFromName_Hook (void *eventSystem, const char *eventName) {
        //_LOGD("EventIdFromName hook, event name: %s", eventName);
        FSM(eventName);
        EventIdFromName_Original(eventSystem, eventName);
        return;
    }

    void LevelLoadCompleted_Hook (long long *this_idLoadScreen) {
        _LOGD("idLoadScreen::LevelLoadCompleted hook!");
        if (g_currPlayer) {
            FSM("GameStarted");
        }
        LevelLoadCompleted_Original(this_idLoadScreen);
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
            Damage,
            Damage_Hook,
            reinterpret_cast<LPVOID *>(&Damage_Original)
        );
        if (MH_EnableHook(Damage) != MH_OK) {
            _LOG("FATAL: Failed to install Damage hook.");
            return false;
        }

        MH_CreateHook (
            EventTriggered,
            EventTriggered_Hook,
            reinterpret_cast<LPVOID *>(&EventTriggered_Original)
        );
        if (MH_EnableHook(EventTriggered) != MH_OK) {
            _LOG("FATAL: Failed to install EventTriggered hook.");
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

        MH_CreateHook (
            EventIdFromName,
            EventIdFromName_Hook,
            reinterpret_cast<LPVOID *>(&EventIdFromName_Original)
        );
        if (MH_EnableHook(EventIdFromName) != MH_OK) {
            _LOG("FATAL: Failed to install EventIdFromName hook.");
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
        //_LOG("Game version: %" PRIX64, Utils::GetGameVersion());
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
#if 0
        std::thread([]{
    for (;;) {
        // wait for player to be captured by your SelectWeaponByDeclExplicit hook
        auto player = g_currPlayer;
        if (!player) { Sleep(10); continue; }

        // optional: make sure resolver is ready
        if (!HandleToPointer) { Sleep(10); continue; }

        // read handle@player+0x9788 -> Weapon*
        Weapon* weapon = GetCurrentWeaponAlter(player); // uses 0x9788 + HandleToPointer
        if (weapon) {
            const char* name = GetWeaponName(reinterpret_cast<long long*>(weapon));
            if (name && name[0]) {
                g_currWeapon = weapon;
                _LOGD("* Startup weapon: %s", name);
                break; // done
            }
        }
        Sleep(10);
    }
}).detach();
#endif


#if 0

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
#endif

        _LOG("Ready.");
    }
}
