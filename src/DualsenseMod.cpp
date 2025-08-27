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
std::vector<std::string> g_WeaponList = {
    "WEAPON_PISTOL_DEFAULT",
    "WEAPON_SHOTGUN_SINGLESHOT",
    "WEAPON_SMG_STANDARD",
    "WEAPON_RAILGUN_STANDARD",
    "WEAPON_ROCKETLAUNCHER_TRIPLESHOT",
    "WEAPON_DLC2_STICKYLAUNCHER"
};

std::vector<std::string> g_WeaponListINI = {
    "Grip",
    "Shatter",
    "Spin",
    "Pierce",
    "Charge",
    "Surge"
};

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

static DWORD mainThread = -1;
HMODULE g_doomBaseAddr = nullptr;

int RVA_weapon_offset = 0x05B0F6D0;
constexpr uint64_t GHIDRA_IMG_BASE = 0x140000000;   // Image Base (Ghidra)
constexpr uint64_t DAT_VA          = 0x145B0F6D0;
constexpr size_t   SLOT_OFFSET     = 0x210;

static size_t g_currWeaponOffset = 0x9788;
// = SIZE_MAX; // to search for the offset using FindCurrWeaponHandle

static Weapon *g_currWeapon = nullptr;

static Player *g_currPlayer = nullptr;



// Game functions
using _OnWeaponSelected =
                    void(__fastcall*)(void *player, long long *weapon);

using _SelectWeapon =
                    uint64_t*(__fastcall*) (
                            long long *param_1,long long *param_2,
                            long long *param_3, uint8_t param_4
                    );

using _SelectWeaponByDeclExplicit =
                    unsigned long long(__fastcall*) (
                            long long *player, long long param_2,
                            char param_3, char param_4
                    );

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


RVA<_SelectWeapon>
SelectWeapon (
    "48 89 6c 24 18 56 41 56 41 57 48 83 ec 30 45 0f b6 f9 49 8b e8 4c 8b f2 48 8b f1 4d 85 c0 75 3a 44 38 81 80 cd 00 00 74 20 41 b9 b2 01 00 00 4c 8d 05 f2 e6 46 01 48 8d 15 cb e7 46 01 48 8d 0d 34 e7 46 01 e8 f7 ea 43 ff 32 c0 48 8b 6c 24 60 48 83 c4 30 41 5f 41 5e 5e c3 83 3d bf 76 d8 04 00 48 89 5c 24 50 48 89 7c 24 58 74 69 49 8b 40 30 48 8b 0d 28 b3 cc 04 4d 85 f6 75 27 48 8b 58 08 48 8b 01 ff 90 10 02 00 00"

);
_SelectWeapon SelectWeapon_Original = nullptr;

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


// Utility functions

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

        _LOG("SelectWeapon at %p",
            SelectWeapon.GetUIntPtr()
        );

        _LOG("SelectWeaponByDeclExplicit at %p",
            SelectWeaponByDeclExplicit.GetUIntPtr()
        );

        _LOG("HandleToPointer at %p",
            HandleToPointer.GetUIntPtr()
        );

        if (!g_doomBaseAddr) {
            _LOGD("DOOM base address is not set!");
            return false;
        }

        // resolve current weapon function address
        auto doomBase   = reinterpret_cast<uint8_t*>(g_doomBaseAddr);

        if (!OnWeaponSelected || !SelectWeapon || !SelectWeaponByDeclExplicit || !HandleToPointer)
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
            _LOGD("idPlayer::OnWeaponSelected - newWeapon = %s\n", weaponName);
        }
        OnWeaponSelected_Original(player, weapon);

        // XXX uncomment to find the idPlayer's current weapon handler
        //FindCurrWeaponHandle((Player *) player);

        return;
    }

    uint64_t *SelectWeapon_Hook(long long *param_1, long long *param_2, long long *param_3, uint8_t param_4) {
        _LOGD("* idPlayer::SelectWeapon hook!!!");
        char *currentWeaponName = (char *)(*(long long *)(param_2[6] + 8));
        char *newWeaponName = (char *)(*(long long *)(param_3[6] + 8));
        _LOGD("idPlayer::SelectWeapon - currentWeapon: %s, newWeapon = %s\n", currentWeaponName, newWeaponName);
        return SelectWeapon_Original(param_1, param_2, param_3, param_4);
    }

    unsigned long long SelectWeaponByDeclExplicit_Hook(long long *player,
                                long long param_2, char param_3, char param_4) {
        _LOGD("* idPlayer::SelectWeaponByDeclExplicit hook!!!");

        if (g_currPlayer == nullptr) {
            g_currPlayer = player;
            _LOGD("* Set current player addr: 0x%p", g_currPlayer);

            // Get current equipped weapon so we can send out our first
            // trigger effect profile
            Weapon *weapon = GetCurrentWeapon(g_currPlayer);
            _LOGD("* Startup weapon: %s", GetWeaponName((long long *)weapon));
        }
        return SelectWeaponByDeclExplicit_Original(player, param_2, param_3, param_4);

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
            SelectWeapon,
            SelectWeapon_Hook,
            reinterpret_cast<LPVOID *>(&SelectWeapon_Original)
        );
        if (MH_EnableHook(SelectWeapon) != MH_OK) {
            _LOG("FATAL: Failed to install SelectWeapon hook.");
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
