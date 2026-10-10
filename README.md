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

### Bobsleigh Boosting

I like the boost effect and it's annoying you can't do it in the bobsleigh so I'm bringing it back. It's compatible with Right Trigger Boosting.

# Known Issues

To name a few (yikes)

## Sonic Escapes the Bobsleigh

If you boost at least once, he has the tendency to escape the bobsleigh. This can be from:

- Hitting a hazard

- Jump or Dash panel

- Turning very sharply

## Duplicated Damages

Sonic will have damage against him at least twice (one on the bobsleigh and again after escaping). If Sonic's ring count is low enough rings (where it will got from some amount to zero on the first hit) he will die.

## Sonic Still Gets Damages from the Bobsleigh after Escaping

This derives from both issues above. If the bobsleigh didn't despawn due to sonic prematurely escaping it, Sonic will still get damages from the hazards the bobsleigh has encountered. If the bobsleigh falls it will trigger a respawn on sonic like if he was still on it.

# FAQ

## How to turn on the feature?

You need to edit `ModsDB.ini` under the mod folder and add `CodeX="AllowBobsleighBoost"` (where X is the highest number) and update the `CodeCount`. How you can download `ExtraCodes.hmm` (from the [releases](https://github.com/bluechilliz3/Enhanced-UnRecomp/releases) tab) to your mod folder and it'll show the option in the HedgeModManager.

> [!NOTE]
> 
> Using the `ExtraCodes.hmm` will show other options that aren't compatible with this feature branch. The game will ignore it.

# Building

[Check out the building instructions here](https://github.com/bluechilliz3/Enhanced-UnRecomp/blob/all-enhancements/docs/BUILDING.md).

# Credits

## Unleashed Recompiled

This project won't be possible without hedge-dev! Please look at their [official Github page](https://github.com/hedge-dev/UnleashedRecomp#credits) for the thanks they'd want to give to their own individuals and team.
