/*
 * Copyright (C) 2024 Thanasis Petsas <thanpetsas@gmail.com>
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

// Game global vars

static DWORD mainThread = -1;
HMODULE g_doomBaseAddr = nullptr;

int RVA_weapon_offset = 0x05B0F6D0;
constexpr uint64_t GHIDRA_IMG_BASE = 0x140000000;   // Image Base (Ghidra)
constexpr uint64_t DAT_VA          = 0x145B0F6D0;
constexpr size_t   SLOT_OFFSET     = 0x210;


// Game functions
using _OnWeaponSelected =
                    void(*)(int entityComponentState, long long *weaponState);

using _GetCurrentWeaponName =
                    char* (__fastcall*)();

using _SelectWeapon =
                    uint64_t*(__fastcall*)(long long *param_1, long long *param_2, long long *param_3, uint8_t param_4);

// Game Addresses


// This function is called every time a weapon switch takes place
RVA<_OnWeaponSelected>
OnWeaponSelected (
    "48 85 D2 74 ? 48 89 74 24 10 57 48 83 EC 20 83 3D ? ? ? ? 00 48 8B FA 48 8B F1 74 ?"
);
_OnWeaponSelected OnWeaponSelected_Original = nullptr;


RVA<_SelectWeapon>
SelectWeapon (
    "48 89 6C 24 18 56 41 56 41 57 48 83 EC 30 45 0F B6 F9 49 8B E8 4C 8B F2 48 8B F1 4D 85 C0 75 ? 44 38 81 80 CD 00 00 74 ? 41 B9 B2 01 00 00 4C 8D 05 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 0D ? ? ? ? E8 ? ? ? ?"
);
_SelectWeapon SelectWeapon_Original = nullptr;

// Ghidra: DAT_145b0f6d0
// Ghidra image base address: 0x140000000
// RVA = (0x145b0f6d0 - 0x140000000) = 0x05B0F6D0
RVA<_GetCurrentWeaponName>
GetCurrentWeaponName (
       (uintptr_t) 0x05B0F6D0
);


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

        if (!g_doomBaseAddr) {
            _LOGD("DOOM base address is not set!");
            return false;
        }

        // resolve current weapon function address
        auto doomBase   = reinterpret_cast<uint8_t*>(g_doomBaseAddr);

        _LOG("GetCurrentWeaponName at %p",
            GetCurrentWeaponName.GetUIntPtr()
        );

        if (!OnWeaponSelected || !SelectWeapon  || !GetCurrentWeaponName)
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


    void OnWeaponSelected_Hook(int entityComponentState, long long *weaponState) {
        _LOGD("* OnWeaponSelected hook!!!");
        if (weaponState != nullptr) {
            char *weaponName = (char *)(*(long long *)(weaponState[6] + 8));

            //char *currentWeaponName = ((_GetCurrentWeaponName)((uintptr_t)(*GetCurrentWeaponName) + 0x210))();

            _LOGD("idPlayer::OnWeaponSelected - newWeapon = %s\n", weaponName);
        }
        OnWeaponSelected_Original(entityComponentState, weaponState);

        return;
    }

    uint64_t *SelectWeapon_Hook(long long *param_1, long long *param_2, long long *param_3, uint8_t param_4) {
        _LOGD("* SelectWeapon hook!!!");
        return SelectWeapon_Original(param_1, param_2, param_3, param_4);
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
        _LOG("DOOM (2015) DualsenseMod v1.0 by Thanos Petsas (SkyExplosionist)");
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
