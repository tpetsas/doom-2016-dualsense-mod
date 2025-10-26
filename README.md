# doom-2016-dualsense-mod

[installer-link]: https://github.com/tpetsas/doom-2016-dualsense-mod/releases/download/v1.0.0/DOOM-DualSensitive-Mod_Setup.exe 

A mod for DOOM (2016) that adds DualSense adaptive triggers for all the Weapons and its mods!

Rip and tear until it is done! With DualSense it's much more fun! :godmode::metal:

Built with the tools and technologies:

[![CMake](https://img.shields.io/badge/-CMake-darkslateblue?logo=cmake)](https://cmake.org/)
![C](https://img.shields.io/badge/C-A8B9CC?logo=C&logoColor=white)
![C++](https://img.shields.io/badge/-C++-darkblue?logo=cplusplus)
[![DualSensitive](https://tinyurl.com/DualSensitive)](https://github.com/tpetsas/dualsensitive)
[![Ghidra](https://tinyurl.com/yuv64wyh)](https://ghidra-sre.org/)
[![Radare2](https://tinyurl.com/52tue3ve)](https://rada.re/n/radare2.html)

<!-- <img width="512" alt="logo" src="https://github.com/user-attachments/assets/dc0e5cd3-47fa-4c78-a27d-d0524a01b67d" /> -->
<!-- <img width="1536" height="1024" alt="logo" src="https://github.com/user-attachments/assets/d7da38ec-ae70-4390-bcd7-def6b7af6402" /> -->
<img width="768" height="512" alt="logo" src="https://github.com/user-attachments/assets/d7da38ec-ae70-4390-bcd7-def6b7af6402" />


## Overview

This mod allows players with a Playstation 5 DualSense controller to play DOOM (2016) with adaptive triggers. The adaptive triggers are assigned based on the current weapon used.

Mod Page: [**Nexus Mods — DOOM (2016) DualSense Mod**](https://www.nexusmods.com/doom/mods/???/)

Installer: [**DOOM-DualSensitive-Mod_Setup.exe**][installer-link]

<!--
### Mod Showcase

<a href="https://www.youtube.com/watch?v=ubN_qF-uWRU">
    <img src="https://github.com/user-attachments/assets/ba60759b-e5c1-421c-ac23-d36a8a7841b0"
        width="600"
        title="DOOM DualSense Mod Showcase — Click to watch it!"
    />
</a>
-->

## Features

This mod adds the following features:
- Adaptive Triggers for both L2 and R2 for each weapon and its respective mod
- Adaptive triggers get disabled when the player is on an inner Menu or the game is paused

## Installation

[DOOM DualSense Mod (latest)]: https://github.com/tpetsas/doom-2016-dualsense-mod/releases/tag/v1.0.0

### :exclamation: Windows SmartScreen or Antivirus Warning

If Windows or your antivirus flags this installer or executable, it’s most likely because the file is **not digitally signed**.

This is a known limitation affecting many **open-source projects** that don't use paid code-signing certificates.

#### :white_check_mark: What you should know:
- This mod is **open source**, and you can inspect the full source code here on GitHub.
- It **does not contain malware or spyware**.
- Some antivirus programs may incorrectly flag unsigned software — these are known as **false positives**.

**1. Download the **[Doom-DualSensitive-Mod_Setup.exe][installer-link]** from the latest version ([Doom DualSense Mod (latest)])**


**2. Double click the installer to run it:**

<img width="476" height="707" alt="install" src="https://github.com/user-attachments/assets/ff00e1f4-055f-4635-bda5-3ea9e43af266" />

You may safely proceed by clicking:

> **More info → Run anyway** (for SmartScreen)  
> or temporarily allow the file in your antivirus software.
>
> If for any reason the "Run anyway" button is missing you can just do the process manually by:  
> Right-click setup.exe → Properties → Check “Unblock” → Apply

<!-- <img width="843" height="1133" alt="image" src="https://github.com/user-attachments/assets/180d8c3f-acb5-4186-b1df-460bed541d9e" /> -->
<img width="491" height="1133" alt="image" src="https://github.com/user-attachments/assets/180d8c3f-acb5-4186-b1df-460bed541d9e" />


**3. Accept the disclaimer and follow the prompts until the setup is complete:**

<!-- <img width="750" height="573" alt="image" src="https://github.com/user-attachments/assets/f6d0b9c7-94d0-4dbf-bb93-f4ddd1dcb49c" /> -->
<img width="491" alt="disclaimer" src="https://github.com/user-attachments/assets/f6d0b9c7-94d0-4dbf-bb93-f4ddd1dcb49c" />


**Once all steps are completed, you will reach the final screen indicating that the setup is finished:**

<!-- <img width="750" height="573" alt="image" src="https://github.com/user-attachments/assets/2d24c4f2-48c0-44b0-a433-8d5a792c3466" /> -->
<img width="491" alt="finished" src="https://github.com/user-attachments/assets/2d24c4f2-48c0-44b0-a433-8d5a792c3466" />



Now, you can experience the mod by just running the game.

> [!NOTE]
> If you have the game from Epic you need to add it to your Steam library first as the game doesn't have support for PS5 controller by default. Open your Steam client, go to **Games > Add a Non-Steam Game to My Library** and choose the game you want to add. If it's not listed, click Browse and find the game. Click Add Selected Programs and the game will now be listed in your Steam library.

<!--
### Manual Installation

[binaries-link]: https://github.com/tpetsas/doom-2016-dualsense-mod/releases/download/v1.1.0/DOOM-DualSensitive-Mod_Binaries.zip

Download the mod binaries ZIP file here: [DOOM-DualSensitive-Mod_Binaries.zip][binaries-link] and locate the directory of the game (for example for Steam this should be: `C:\Program Files (x86)\Steam\steamapps\common\DOOM`). Extract all the files straight in this directory so that the directory structure looks like:

```
DOOM
    dinput8.dll
    plugins/
        dualsense-mod.ini
        dualsense-mod.dll
        DualSensitive/
            dualsensitive-service.exe
            launch-service.vbs
```
Unblock the `dualsensitive-service.exe`:

Right-click the `DOOM/plugins/DualSensitive/dualsensitive-service.exe` and select **Properties → Check “Unblock” → Apply**
-->

## Uninstallation

To unisntall the game, simply go to **Settings > Add or remove programs**, locate the mod, choose uninstall and follow the prompts:

<!--<img width="1024" height="752" alt="uninstall" src="https://github.com/user-attachments/assets/c6943839-fe5b-459b-8b55-4305b2caf454" />-->
<img width="491" alt="uninstall" src="https://github.com/user-attachments/assets/c6943839-fe5b-459b-8b55-4305b2caf454" />

## Usage & Configuration
[tray-options]: https://github.com/tpetsas/dualsensitive/blob/main/README.md#tray-application-options

The mod will start as soon as the game is started. You can enable/disable the adaptive triggers feature any time from the tray app. For more information, check the [DualSensitive Tray Application Options section][tray-options]. The tray app closes automatically when the game exits.

The mod supports two configuration options as of now via an INI file stored in the `plugins` directory named `dualsense-mod.ini`

A sample content of the file is the following (also found in the current repo at `config/dualsense-mod.INI`:


```
[app]
debug=true
```

In this configuration, the `debug=true` option of the `[app]` section will make the mod to output a lot more information to its respective log file (`plugins\dualsensemod.log`). The default value of the above option (i.e., if no INI file is used) are `debug=false`.

## Issues :finnadie:

Please report any bugs or flaws! I recommend to grab a debug version of the mod (e.g., [**dualsense-mod-debug.dll**]([https://github.com/tpetsas/doom-2016-dualsense-mod](https://github.com/tpetsas/doom-2016-dualsense-mod/releases/download/1.0.0/dualsense-mod-debug.dll)) and enable the `debug` option in the configuration as described above ([Configuration](#usage--configuration)) in order to get a fully verbose log when trying to replicate the issue, which will help me a lot with debugging the issue. Feel free to open an issue [here](https://github.com/tpetsas/doom-2016-dualsense-mod/issues) on github.

## Credits

[Tsuda Kageyu](https://github.com/tsudakageyu), [Michael Maltsev](https://github.com/m417z) & [Andrey Unis](https://github.com/uniskz) for [MinHook](https://github.com/TsudaKageyu/minhook)! :syringe:
