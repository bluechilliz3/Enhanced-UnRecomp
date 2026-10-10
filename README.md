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

### Extra Ribbons in HUD

Supporting mods that changes the ribbon (within reason), more ribbons are added below TIME (day), SCORE and RINGS (night).

#### Enemy Counter on All Levels

Shows how many enemies you destroyed in any levels so you can see how your carnage is going. If you have `SaveScoreAtCheckpoints` turned from HedgeModManager the counter will accept this rule.

Most of the settings related to the ribbon is in the new HUD tab in the options menu.

#### FPS in a Ribbon

See the active FPS in the ribbon instead of the banner while in gameplay. When the option is set to `Ribbon` but not at a stage it will fallback to the banner format. This is because the SCORE ribbon isn't shown somewhere other than the stage.

#### Sonic Position

Useful for debugging a stage mod, show Sonic's **x, y, z** coordinate in the ribbon.

# Known Issues

## Eggmanland Hot Dog Mission Timer

Turns out **in the original game** the gray timer doesn't immediately change when Sonic changes his form until you fail the mission and retry. _Example: fail the mission while as the Werehog then the timer will use the Werehog shape even when starting all over as Day Sonic._

Using hedge-dev's philosophy, this particular issue won't be fixed by me as the issue came from the original game. The color of the ribbons added by the extra ribbon feature will actively adapt to Sonic's form.

## FPS Ribbon May Show Different Values than the Banner

As a side-effect of the value changing significantly more frequent than the banner, the method of getting FPS value is different. The value in the ribbon may be higher or lower than the banner.

# Building

[Check out the building instructions here](https://github.com/bluechilliz3/Enhanced-UnRecomp/blob/all-enhancements/docs/BUILDING.md).

# Credits

## Unleashed Recompiled

This project won't be possible without hedge-dev! Please look at their [official Github page](https://github.com/hedge-dev/UnleashedRecomp#credits) for the thanks they'd want to give to their own individuals and team.

## Features for this Fork

[Labreezy](https://github.com/Labreezy/UnleashedRecomp-Speedrun/tree/practice-tools) - Getting and displaying [Sonic's position](#sonic-position).
