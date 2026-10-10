Enhanced-UnRecomp is a derivation of Unleashed Recompiled. It contains enhancements on top of [hedge-dev's Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp).

**This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build Enhanced-UnRecomp.**

---

# If you're NOT Familiar with Github

You're not in the right branch if you want all the enhancements. **This area is for developers wanting the ins and out of *one* specific feature and if there's any bugs this where we do the fixing.**

Please [click here](https://github.com/bluechilliz3/Enhanced-UnRecomp/tree/all-enhancements) for the default branch please.

---

## Table of Contents

- [Highlighted Feature](#highlighted-feature)

- [Known Issues](#known-issues)

- [FAQ](#faq)

- [Building](#building)

- [Credits](#credits)

## Highlighted Feature

### No BGM Restart on Respawn

In the modern games the music longer restarts, so it's in the roadmap! If you played the modern games you'll less like to restart the stage as the grove hasn't moved.

> [!NOTE]
> 
> Using the restart button in the pause menu will restart the BGM as before. So as clicking retry in the after failing a Hot Dog Mission.

# Known Issues

## Eggmanland

If you die as the other form of Sonic before hitting the checkpoint, respawning to the previous form won't update the BGM. 

For example when finishing the first Werehog section and _hitting the hourglass_ to switch back to Day Sonic, if you miss the spring and fall, you'll respawn back to the start of the Werehog section as the Werehog but it'll still play the Day version of Eggmanland.

> [!NOTE]
> 
> This is even with `FixEggmanlandUsingEventGalleryTransition` being turn on or off.

# FAQ

## How to turn on the feature?

You need to edit `ModsDB.ini` under the mod folder and add `CodeX="DisableMusicRestartOnDeath"` (where X is the highest number) and update the `CodeCount`. Now you can download `ExtraCodes.hmm` (from the [releases](https://github.com/bluechilliz3/Enhanced-UnRecomp/releases) tab) to your mod folder and it'll show the option in the HedgeModManager.

> [!NOTE]
> 
> Using the `ExtraCodes.hmm` will show other options that aren't compatible with this feature branch. The game will ignore it.

# Building

[Check out the building instructions here](https://github.com/bluechilliz3/Enhanced-UnRecomp/blob/all-enhancements/docs/BUILDING.md).

# Credits

## Unleashed Recompiled

This project won't be possible without hedge-dev! Please look at their [official Github page](https://github.com/hedge-dev/UnleashedRecomp#credits) for the thanks they'd want to give to their own individuals and team.
