#pragma once

#include <string>
#include <cstdint>

// A single gameplay-status HUD row matching the game's own TIME/SCORE element: a small
// label ribbon sitting on a large value ribbon. Construct one per stat and Draw() it at a
// stack row.
class GameplayStatusHudRibbon
{
public:
    GameplayStatusHudRibbon(std::string label, std::string value);

    void SetLabel(std::string label);

    // Any text (letters, separators, signs). Drawn as text on the ribbon.
    void SetValue(std::string value);

    // A whole number, shown in the game's own digit font (so it follows a HUD mod). The font
    // only has 0-9 and no sign, comma or decimal point, which also sidesteps per-language
    // separators, so a negative number falls back to text.
    void SetValue(int value);

    // Draw at 1-based stack row `spot`: spot 1 aligns with the game's TIME row and
    // spot 2 with SCORE, so custom rows start at spot 3. `spot` is a float, so a
    // fractional value places the row partway between stack positions (used to slide
    // rows during animations).
    void Draw(float spot, uint8_t alpha) const;

    // Loads the shared font. Safe to call more than once.
    static void Init();

    // The first stack spot free below the game's own rows: 3 in a day stage (under TIME and
    // SCORE) or a Werehog stage (under SCORE and RING); in a mission stage, the first slot the
    // mission HUD left free.
    static float FirstFreeSpot();

    // Whether the rows are on screen: in a stage with the game's own HUD rows showing (not a
    // menu, cutscene, a pause that hides them or a HUD-off toggle) and, for native rows,
    // the game actually drawing their ribbons (it stops in sections that hide its HUD at the
    // render layer). Handy for suppressing other overlays that would duplicate a row (e.g.
    // the corner FPS counter), which then comes back whenever the rows are gone.
    static bool RibbonCanShow();

    // Called every paused frame by the CHudPause::Update hook (CHudPause_patches.cpp), on the
    // game thread: keeps the rows' native ribbons serviced while the HUD that owns them isn't
    // updating (a mission HUD stops while paused, but keeps its rows on screen).
    static void ServiceWhilePaused();

    // Called by the CScene::Render hook (aspect_ratio_patches.cpp) after the game has drawn a
    // CSD scene (one that drew at least one quad), on the guest thread issuing its draw calls.
    // When it's a row's clone, the row's text is drawn into the frame right there, over the
    // ribbon and under whatever the game draws next; and the row counts as on screen.
    static void NotifyCsdSceneRendered(uint32_t scene);

private:
    std::string m_label;
    std::string m_value;

    // The value as a native-digit number, or -1 when it is text (see SetValue).
    int m_digits = -1;

    // Which native ribbon clone this row owns (see "Native ribbons" in the .cpp), or -1 when
    // every clone slot is taken (the row isn't drawn).
    int m_nativeSlot = -1;
};

class GameplayStatusHud
{
public:
    static void Init();
    static void Draw();
};
