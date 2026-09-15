<p align="leading">
    <img src="https://raw.githubusercontent.com/hedge-dev/UnleashedRecompResources/refs/heads/main/images/logo/Logo.png" width="512"/>
</p>

---

Enhanced-UnRecomp is a derivation of Unleashed Recompiled. It contains enahancements on top of [hedge-dev's Unleashed Recompiled](github.com/hedge-dev/UnleashedRecomp).

**This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build Enhanced-UnRecomp.**

## Table of Contents

- [Features](#features)
- [Minimum System Requirements](#minimum-system-requirements)
- [How do I run Enhanced-UnRecomp?](#how-do-i-run-enhanced-unrecomp?)

- [Update Roadmap](#update-roadmap)
- [Known Issues](#known-issues)
- [FAQ](#faq)
- [Building](#building)
- [Credits](#credits)

## Features

### Boost with Right Trigger

Like in the modern games you can natively use Right Trigger to boost! Enable it in the options under the <mark>Input</mark> tab and enjoy. No more needing to flip between controller remapping software when you swap between Day & Night Sonic (especially in Eggmanland).

Here's a video example

```
[![Right Trigger Boost video example](https://img.youtube.com/vi/D4bx5LojE20/0.jpg)](https://www.youtube.com/watch?v=D4bx5LojE20)
```

## Minimum System Requirements

- CPU with support for the AVX instruction set:
  - Intel: Sandy Bridge (Intel Core 2nd Generation)
  - AMD: Bulldozer (AMD FX series)
- GPU with support for Direct3D 12.0 (Shader Model 6) or Vulkan 1.2:
  - NVIDIA: GeForce GT 630 (Kepler)
  - AMD: Radeon HD 7750 (2012, not the RX 7000)
  - Intel: HD Graphics 510 (Skylake)
- Memory:
  - 8 GB minimum
- Operating System:
  - Windows 10 (version 1909)
  - A modern Linux distro such as Ubuntu 22.04 LTS
  - macOS 14 (arm64 only)
- Storage:
  - With DLC: 10 GiB required
  - Without DLC: 6 GiB required

> [!NOTE]
> More storage space may be required if uncompressed game files are provided during installation.

# How do I run Enhanced-UnRecomp?

1. You run hedge-dev's Unleashed Recompiled to install the assets of the game if you haven't played the game on PC before. You can find [their guide on their repo](https://github.com/hedge-dev/UnleashedRecomp#how-to-install).

2. You need build this project. Go to the [building instruction](/docs/BUILDING.md). 

3. Run the game, enjoy!

> [!NOTE]
> 
> At the time being, there's no direct link for an `.exe`, `.dmg`, `.appimage` or `.app` file.
> 
> If you're stuck after reading the building instruction, ask a software developer friend (or Linux veteran) if they can help with the instructions.

# Update Roadmap

## Bobsleigh Boosting

Keep on boosting while on the bobsleigh in stages like Cool Edge Act 1 or the second day section of Eggmanland! Never stay out of that momentum like in Unwiished.

## No BGM Restart on Respawn

In the morden games the music longer restarts, so it's in the roadmap! If you played the modern games you'll less like to restart the stage as the grove hasn't moved.

## More Colors on Controller LED

If you have a DualShock 4 or DualSense controller, more colors are coming. Green on Chip and Yellow on Super Sonic.

# Known Issues

## Chip's Control Overlay

When Right Trigger Action is set to Boost, the placement displayed may look out of place.

# FAQ

## Why does the installer say my files are invalid?

The installer may display this error for several reasons. Please check the following to ensure your files are valid:

- Please read the [How to Install](#how-to-install) section and make sure you've acquired all of the necessary files correctly.

- Verify that you're not trying to add compressed files such as `.zip`, `.7z`, `.rar` or other formats.

- Only use the **Add Folder** option if you're sure you have a directory with the content's files already extracted, which means it'll only contain files like `.xex`, `.ar.00`, `.arl` and others. **This option will not scan your folder for compatible content**.

- Ensure that the files you've acquired correspond to the same region. **Discs and Title Updates from different regions can't be used together** and will fail to generate a patch.

- The installer will only accept **original and unmodified files**. Do not attempt to provide modified files to the installer.

## Where is the save data and configuration file stored?

The save data and configuration files are stored at the following locations:

- Windows: `%APPDATA%\UnleashedRecomp\`
- Linux: `~/.config/UnleashedRecomp/`
- macOS: `~/Library/Application Support/UnleashedRecomp/`

You will find the save data under the `save` folder (or `mlsave`, if using Hedge Mod Manager's save file redirection). The configuration file is named `config.toml`.

## I want to update the game. How can I avoid losing my save data? Do I need to reinstall the game?

Updating the game can be done by simply fetching the updated project then running the build. **Your save data and configuration will not be lost.** You won't need to reinstall the game, as the game files will always remain the same across versions of Enhanced-UnRecomp & hedge-dev's Unleashed Recompiled.

> [!CAUTION]
> 
> Your save & mods are interchangable across hedge-dev's Unleashed Recompiled & Enhanced-UnRecomp but the extra settings from Enhanced-UnRecomp **will be wiped** (such as `RightTriggerAction`) if you run hedge-dev's Unleashed Recompiled (or other forks of Unleashed Recompiled that doesn't support the extra settings).

## How can I force the game to store the save data and configuration in the installation folder?

You can make the game ignore the [default configuration paths](#where-is-the-save-data-and-configuration-file-stored) and force it to save everything in the installation directory by creating an empty `portable.txt` file. You are directly responsible for the safekeeping of your save data and configuration if you choose this option.

## How can I force the game to run the installation again?

While it's unlikely you'll need to do this unless you've modified your game files by accident, you can force the installer to run again by using the launch argument: `--install`.

## How can I force the game to run under X11 or Wayland?

Use either of the following arguments to force SDL to run under the video driver you want:

- X11: `--sdl-video-driver x11`
- Wayland: `--sdl-video-driver wayland`

The second argument will be passed directly to SDL as a hint to try to initialize the game with your preferred option.

## Where is the game data for the Flatpak version installed?

Given it is not possible to run the game where the Flatpak is stored, the game data will be installed to `~/.var/app/io.github.hedge_dev.unleashedrecomp/data`. The Flatpak build will only recognize this directory as valid. Feel free to reuse this data directory with a native Linux build if you wish to switch in the future.

If you wish to move this data to another location, you can do so by creating a symlink from this directory to the one where you'll migrate your installation to.

# Building

[Check out the building instructions here](/docs/BUILDING.md).

# Credits

## Unleashed Recompiled

This project won't be possible without hedge-dev! Please look at their [offical github page](https://github.com/hedge-dev/UnleashedRecomp#credits) for the thanks they'd want to give to their own individuals and team.
