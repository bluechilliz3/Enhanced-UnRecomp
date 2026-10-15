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

### No Titan Music

The titan music (the option calls it "the mini boss")  will follow the same rule as the `BattleTheme` option. This matches how the original Xbox 360/PS3 game would handle the jazzy battle music being disabled (in Eggmanland).

# Known Issues

None.

# FAQ

## How to turn on the feature?

Good news! The feature is default option (meaning it's opt-out) if `BattleTheme` is disabled. 

## So how do I opt-out?

You need to edit `ModsDB.ini` under the mod folder and add `CodeX="TitanThemeException"` (where X is the highest number) and update the `CodeCount`. Now you can download `ExtraCodes.hmm` (from the [releases](https://github.com/bluechilliz3/Enhanced-UnRecomp/releases) tab) to your mod folder and it'll show the option in the HedgeModManager.

> [!NOTE]
> 
> Using the `ExtraCodes.hmm` will show other options that aren't compatible with this feature branch. The game will ignore it.
> 
> Also if `BattleTheme` is enabled the titan music will still play like in the original game.

# Building

[Check out the building instructions here](https://github.com/bluechilliz3/Enhanced-UnRecomp/blob/all-enhancements/docs/BUILDING.md).

# Credits

## Unleashed Recompiled

This project won't be possible without hedge-dev! Please look at their [official Github page](https://github.com/hedge-dev/UnleashedRecomp#credits) for the thanks they'd want to give to their own individuals and team.
