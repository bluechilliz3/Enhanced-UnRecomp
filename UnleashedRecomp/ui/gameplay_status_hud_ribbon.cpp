#include "gameplay_status_hud_ribbon.h"
#include "imgui_utils.h"
#include <gpu/imgui/imgui_common.h> 
#include <gpu/imgui/imgui_snapshot.h>
#include <gpu/video.h>
#include <patches/aspect_ratio_patches.h>
#include <user/config.h>
#include <locale/locale.h>
#include <SWA/Globals.h>
#include <api/SWA.h>
#include <patches/CHudPause_patches.h>
#include <patches/inspire_patches.h>
#include <kernel/memory.h>
#include <kernel/function.h>
#include <cpu/guest_stack_var.h>
#include <app.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

// --- Tunables (all in 1280x720 reference units, run through ScaleHud()) ------
namespace
{
    // The reusable pair: a large "value" ribbon with a small "label" ribbon on top. The ribbons
    // are the game's own; these place the text on them.
    constexpr float VALUE_W    = 279.0f; // large ribbon width (holds the value)
    constexpr float LABEL_W    = 210.0f; // small ribbon width (holds the label)
    constexpr float VALUE_H    = 34.0f;  // large ribbon height (centres the value text)
    constexpr float LABEL_H    = 21.0f;  // small ribbon height (centres the label text)
    constexpr float INTRA_DX   = 0.0f;   // label shift right of the value's left edge
    constexpr float LABEL_RISE = 13.0f;  // label lift above the value's top edge

    constexpr float STRIDE   = 50.0f;
    constexpr float ANCHOR_X = 0.0f, ANCHOR_Y = 126.0f;

    constexpr float TEXT_MAX_WIDTH_RATIO = 0.82f;

    constexpr float LABEL_FONT_SIZE    = 13.0f;
    constexpr float VALUE_FONT_SIZE    = 18.0f;
    constexpr float LABEL_TEXT_STRETCH = 1.32f; // stretches wider
    constexpr float VALUE_TEXT_STRETCH = 1.40f; // stretches taller

    /*
       Both texts are leading (left) aligned, like the game's own digits: pads from the ribbon's
       left end. The value starts at VALUE_LEADING (where the native digits start). A value too
       long to end VALUE_TRAILING before the ribbon's right end (clearing the tapered tip)
       slides left to fit, no further than VALUE_LEADING_MIN and past that it's squeezed. 

       The label's pad clears the clipped left point, and a long label (a translated one, e.g.
       "ENEMIGOS") flows the same way: it ends LABEL_TRAILING before the label ribbon's end,
       starting no further left than LABEL_LEADING_MIN. Short labels (ENEMY, FPS) don't move.
    */
    constexpr float LABEL_LEADING     = 128.0f;
    constexpr float LABEL_LEADING_MIN = 96.0f;
    constexpr float LABEL_TRAILING    = 8.0f;
    constexpr float VALUE_LEADING     = 138.0f;
    constexpr float VALUE_LEADING_MIN = 24.0f;
    constexpr float VALUE_TRAILING    = 34.0f;

    constexpr float LABEL_TEXT_OFFSET_Y = 2.0f;
    constexpr float VALUE_TEXT_OFFSET_Y = 6.0f;
    constexpr float VALUE_BOLD          = 0.0f;

    // Whether each overlay text uses the recomp's low-resolution text effect (see
    // DrawRibbonText). The label matches the game's own labels, which are small pre-drawn
    // images and upscale soft; the value renders at full resolution to sit beside the
    // native digits, which stay sharp.
    constexpr bool LABEL_TEXT_LOW_RES = true;
    constexpr bool VALUE_TEXT_LOW_RES = false;

    constexpr ImU32 COLOUR_LABEL = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 COLOUR_VALUE = IM_COL32(255, 255, 255, 255);
}

static ImFont* g_fntHeader;

// Guest address of the live day-stage HUD (CHudSonicStage).
static std::atomic<uint32_t> g_hudSonicStagePtr{ 0 };

// The Werehog's stage HUD (CHudEvilStage, ui_playscreen_ev): SCORE and RING rows in the
// Werehog's purple, with an EXP row that slides.
static std::atomic<uint32_t> g_hudEvilStagePtr{ 0 };

// Mission stages run CHudMissionStage, its own ui_missionscreen and row layout.
static std::atomic<uint32_t> g_hudMissionStagePtr{ 0 };

// Whichever gameplay HUD ran its Update most recently.
enum class HudKind : int { None, SonicStage, EvilStage, MissionStage };
static std::atomic<uint32_t> g_activeHudPtr{ 0 };
static std::atomic<int>      g_activeHudKind{ static_cast<int>(HudKind::None) };

static HudKind ActiveHudKind()
{
    return static_cast<HudKind>(g_activeHudKind.load(std::memory_order_relaxed));
}

// HUD's numbering: TIME = 1, SCORE = 2; the Werehog's SCORE and RING sit on the same two
// spots, and whether a mission HUD has laid its rows out yet (it does so once its archive has
// loaded).
static std::atomic<float> g_firstFreeSpot{ 3.0f };
static std::atomic<bool>  g_missionHudReady{ false };

// Published by the mission HUD's Update: its objective row is the enemy count.
// Uses the EnemyCounter.
static std::atomic<bool>  g_missionShowsEnemyCount{ false };

static ImU32 WithAlpha(ImU32 colour, uint8_t alpha)
{
    return (colour & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
}

// The gameplay HUDs heartbeat here: CHudSonicStage::Update (guest sub_824D7100) runs every
// frame that a day stage's HUD is alive.
static std::atomic<double> g_hudLastSeen{ -1.0e9 };

static double NowSeconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static bool IsGameplayActive()
{
    // While paused a mission HUD stops updating yet keeps its rows on screen, so the heartbeat
    // goes quiet; the HUD object is still alive until its destructor clears g_activeHudPtr.
    if (g_isPauseMenuVisible.load(std::memory_order_relaxed) && g_activeHudPtr.load(std::memory_order_relaxed) != 0)
        return true;

    // ~A few frames of slack: bridges a hitch, and clears within ~0.15s once the HUD stops
    // updating (a menu, or the results screen).
    return (NowSeconds() - g_hudLastSeen.load(std::memory_order_relaxed)) < 0.15;
}

// Returns false when the game is not showing the ribbons so we can honor it here such as:
// - pause is active
// - not in a stage
// - in the (recomps) options menu
static bool IsNativeGameHudVisible()
{
    if (App::s_isLoading)
        return false;

    const bool* loading = SWA::SGlobals::ms_IsLoading;
    if (loading != nullptr && *loading)
        return false;

    // Cutscene check.
    if (!InspirePatches::s_sceneName.empty())
        return false;

    const HudKind kind = ActiveHudKind();

    if (g_isPauseMenuVisible.load(std::memory_order_relaxed) && kind != HudKind::MissionStage)
        return false;

    // Some in-stage sections reconfigure the day HUD instead of hiding it: the flag word at
    // CHudSonicStage+0x1B4 says which readouts are enabled, and they drop the core ones. In
    // normal play it reads 0x7F; the Dark Gaia "chip" phase reads 0x24, the RINGS/SCORE bits
    // (0 and 1) cleared. When neither core readout is enabled, the native TIME/SCORE are gone.
    const uint32_t inst = g_hudSonicStagePtr.load(std::memory_order_relaxed);
    if (kind == HudKind::SonicStage && inst != 0)
    {
        const uint32_t cfg = *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(inst + 0x1B4));
        if ((cfg & 0x3u) == 0)
            return false;
    }

    if (kind == HudKind::MissionStage && !g_missionHudReady.load(std::memory_order_relaxed))
        return false;

    const bool* mainHud = SWA::SGlobals::ms_IsRenderGameMainHud;
    const bool* allHud  = SWA::SGlobals::ms_IsRenderHud;
    return mainHud != nullptr && *mainHud && allHud != nullptr && *allHud;
}

static bool RibbonGateOpen()
{
    return IsGameplayActive() && IsNativeGameHudVisible() && g_fntHeader != nullptr;
}

constexpr float FADE_IN_SECONDS = 0.05f;

static float RibbonFade()
{
    static uint32_t s_fadeInstance = 0;
    static double   s_fadeSince    = -1.0; // wall-clock seconds this stage's fade began; <0 = none

    if (!RibbonGateOpen())
        return 0.0f; // hidden, keep the fade state so a pause/menu return doesn't restart it


    const uint32_t hud = g_activeHudPtr.load(std::memory_order_relaxed);
    const double   now = NowSeconds();

    if (hud != s_fadeInstance || s_fadeSince < 0.0) // a different (new) stage -> fade from scratch
    {
        s_fadeInstance = hud;
        s_fadeSince    = now;
    }

    return std::clamp(static_cast<float>((now - s_fadeSince) / FADE_IN_SECONDS), 0.0f, 1.0f);
}

// Convert a 720p reference length to screen pixels the way the native day HUD does.
static float ScaleHud(float v)
{
    return Scale(v) * g_aspectRatioGameplayScale;
}

enum class TextAlign { Center, Leading, Trailing };

// Draws text into `dl` on a ribbon whose top-left is min. `stretch` (1.0 = natural) stretches one
// axis by rendering the glyphs `stretch`x bigger and squeezing the OTHER axis back by
// 1/stretch, so the raster is only ever scaled DOWN and a stretched typeface stays crisp
// (the trick the game itself uses). `stretchHeight` picks the axis: false = wider
// (default), true = taller. It's then fit within the flat band, and aligned: Center,
// Leading (left edge `pad` in from the ribbon's left) or Trailing (right edge `pad` in).
//
// Leading with `padMin` >= 0 flows long text: it starts at `pad`, but when it wouldn't end
// `padEnd` before the ribbon's right end it slides left to fit, no further than `padMin`;
// only text too long even from there is squeezed.
//
// `lowRes` renders it through the low-resolution text effect (IMGUI_SHADER_MODIFIER_LOW_QUALITY_TEXT).
static void DrawRibbonText(ImDrawList* dl, const ImVec2& min, float w, float h, float cyRatio,
    const char* text, float refFontSize, ImU32 colour, uint8_t alpha, float outlineSize,
    float stretch, TextAlign align, float pad, bool stretchHeight = false, float bold = 0.0f,
    float offsetY = 0.0f, float padMin = -1.0f, float padEnd = 0.0f, bool lowRes = true)
{
    const bool flow = align == TextAlign::Leading && padMin >= 0.0f;

    stretch = std::max(1.0f, stretch);
    auto renderSize = ScaleHud(refFontSize) * stretch;   // render big so the stretch stays crisp
    auto textSize = g_fntHeader->CalcTextSizeA(renderSize, FLT_MAX, 0, text);
    auto maxWidth = flow ? ScaleHud(std::max(0.0f, w - padEnd - std::min(pad, padMin)))
                         : ScaleHud(w * TEXT_MAX_WIDTH_RATIO);

    float scaleX, scaleY;
    if (stretchHeight)
    {
        auto counterX  = 1.0f / stretch;
        auto baseWidth = textSize.x * counterX;
        auto fit       = baseWidth > maxWidth ? maxWidth / baseWidth : 1.0f;
        scaleX = counterX * fit;
        scaleY = 1.0f;
    }
    else
    {
        scaleX = textSize.x > maxWidth ? maxWidth / textSize.x : 1.0f;
        scaleY = 1.0f / stretch;
    }
    auto drawnWidth  = textSize.x * scaleX;
    auto drawnHeight = textSize.y * scaleY;

    auto cy = min.y + ScaleHud(h) * cyRatio - ScaleHud(offsetY); // offsetY: + = up
    float x;
    switch (align)
    {
        case TextAlign::Leading:
            x = min.x + ScaleHud(pad);
            if (flow)
            {
                // Slide a long string left so it ends by padEnd, but never past padMin (and
                // never right of where it would start anyway).
                const float endLimit = min.x + ScaleHud(w - padEnd);
                if (x + drawnWidth > endLimit)
                    x = std::max(std::min(x, min.x + ScaleHud(padMin)), endLimit - drawnWidth);
            }
            break;
        case TextAlign::Trailing: x = min.x + ScaleHud(w) - ScaleHud(pad) - drawnWidth; break;
        default:                  x = min.x + ScaleHud(w) * 0.5f - drawnWidth * 0.5f; break;
    }
    ImVec2 pos = { x, cy - drawnHeight / 2.0f };

    SetOrigin(pos);
    SetScale({ scaleX, scaleY });
    SetShaderModifier(lowRes ? IMGUI_SHADER_MODIFIER_LOW_QUALITY_TEXT : IMGUI_SHADER_MODIFIER_NONE);

    SetOutline(outlineSize + bold);
    dl->AddText(g_fntHeader, renderSize, pos, IM_COL32(0, 0, 0, alpha), text);
    SetOutline(bold);
    dl->AddText(g_fntHeader, renderSize, pos, WithAlpha(colour, alpha), text);
    ResetOutline();

    SetShaderModifier(IMGUI_SHADER_MODIFIER_NONE);
    SetScale({ 1.0f, 1.0f });
    SetOrigin({ 0.0f, 0.0f });
}

// Where a row sits at stack spot `spot`: the top-left of its value ribbon and of the label
// ribbon above it, in the overlay's coordinates.
struct RowLayout
{
    ImVec2 valueMin;
    ImVec2 labelMin;
};

static RowLayout LayoutRow(float spot)
{
    // Match the native TIME/SCORE HUD's anchor (a top-left, gameplay-scaled CSD element).
    // ScaleHud() already carries the gameplay shrink and the only remaining piece is the
    // horizontal alignment, which follows the UI Alignment video option exactly as the game
    // does (aspect_ratio_patches.cpp): 
    //   - Edge hugs the left *screen* edge (no offset), while
    //   - Centre re-centres into the 16:9 area past 16:9 by adding g_aspectRatioOffsetX (there
    //     the gameplay scale is 1.0). 
    // Vertical always hugs the top edge. Spot 1 sits at ANCHOR_Y, each later one a STRIDE lower.
    float alignOffsetX = 0.0f;
    if (Config::UIAlignmentMode == EUIAlignmentMode::Centre && g_aspectRatio >= WIDE_ASPECT_RATIO)
        alignOffsetX = g_aspectRatioOffsetX;

    const auto groupLeft = alignOffsetX + ScaleHud(ANCHOR_X);
    const auto elemTop   = ScaleHud(ANCHOR_Y) + ScaleHud((spot - 1.0f) * STRIDE);

    return { { groupLeft, elemTop }, { groupLeft + ScaleHud(INTRA_DX), elemTop - ScaleHud(LABEL_RISE) } };
}

// Draws a row's label into `dl`, and its value unless `value` is empty (a native clone showing
// it in the game's digit font).
static void DrawRowText(ImDrawList* dl, const RowLayout& layout, const char* label, const char* value, uint8_t alpha)
{
    DrawRibbonText(dl, layout.labelMin, LABEL_W, LABEL_H, 0.46f, label, LABEL_FONT_SIZE, COLOUR_LABEL, alpha, 2.5f,
        LABEL_TEXT_STRETCH, TextAlign::Leading, LABEL_LEADING, /*stretchHeight*/ false, /*bold*/ 0.0f,
        LABEL_TEXT_OFFSET_Y, LABEL_LEADING_MIN, LABEL_TRAILING, LABEL_TEXT_LOW_RES);

    // Value stretched taller and leading-aligned like the game's own digits: it starts where the
    // native digits start.
    if (value[0] != '\0')
    {
        DrawRibbonText(dl, layout.valueMin, VALUE_W, VALUE_H, 0.52f, value, VALUE_FONT_SIZE, COLOUR_VALUE, alpha, 3.0f,
            VALUE_TEXT_STRETCH, TextAlign::Leading, VALUE_LEADING, /*stretchHeight*/ true, VALUE_BOLD,
            VALUE_TEXT_OFFSET_Y, VALUE_LEADING_MIN, VALUE_TRAILING, VALUE_TEXT_LOW_RES);
    }
}

/*
   --- Native ribbons -----------------------------------------------------------
   Each row clones the game's own "score_count" scene out of the live ui_playscreen project
   that CHudSonicStage holds (m_rcPlayScreen, +0xE0). The game's CSD pipeline renders the
   clone, so it uses whatever textures, colours and layout the loaded ui_playscreen provides
   including the a user's HUD mod included. 

   It inherits TIME/SCORE's resize handling for free:
   aspect_ratio_patches keys its modifiers on the scene *data*, which the clone shares.   
   Guest calls must run on the game thread, so rows only *request* a ribbon from Draw()
   (the ImGui pass) and the HUD's Update hook services the requests  

   Mission stages run CHudMissionStage instead, with its own ui_missionscreen project. Its
   "score_count" carries two ribbons: position_S, the regular small one, and position_L, the
   slightly larger silver one the HUD uses for the mission's objective rows (it creates them
   with the "conditional_*" motions). The mission HUD fills those slots top-down (lives, time, score, items), so
   the rows start at the first slot it left free.
*/
namespace
{
    // One clone slot per row, assigned as rows are constructed (the example's four: ENEMY,
    // POS, the TEXT demo and FPS). A row past the last slot isn't drawn, so raise this when
    // adding rows.
    constexpr int NATIVE_SLOT_COUNT = 4;

    constexpr uint32_t GUEST_STR_SCORE_COUNT = 0x8202D06C; // "score_count"
    constexpr uint32_t GUEST_STR_SCORE       = 0x8202D16C; // "score" - the digits text cast
    constexpr uint32_t GUEST_STR_TXT         = 0x8202D508; // "txt"   - the localized label sprite
    constexpr uint32_t HUD_PLAYSCREEN_RCPTR  = 0xE0;       // CHudSonicStage::m_rcPlayScreen

    // Where score_count is laid out: SCORE is spot 2 in ui_playscreen (under TIME), and spot 1
    // in ui_playscreen_ev (above RING).
    constexpr float NATIVE_SCENE_SPOT      = 2.0f;
    constexpr float NATIVE_SCENE_SPOT_EVIL = 1.0f;

    // CHudEvilStage (setup sub_8249CE70, registered by sub_8249F288; Update sub_8249DAD0).
    constexpr uint32_t EVIL_PROJECT_RCPTR = 0x108; // the ui_playscreen_ev project

    // The native anchor (1280x720 px) of spot 1.
    constexpr float NATIVE_SPOT1_Y = 150.0f;

    // CHudMissionStage (setup sub_824E8590, run once from its Update sub_824E9650).
    constexpr uint32_t MISSION_PROJECT_RCPTR = 0xD4;  // the ui_missionscreen project
    constexpr uint32_t MISSION_SETUP_DONE    = 0xC0;  // u8, set once the rows are laid out
    constexpr uint32_t MISSION_SLOTS         = 0x12C; // "position" scene 01..05 as {x, y} px
    constexpr uint32_t MISSION_IS_SONIC      = 0xC1;  // u8: the player its setup found was Sonic (0 = the Werehog)
    constexpr int      MISSION_SLOT_COUNT    = 5;
    // The rows the setup may create, each taking the next slot, in its order: the lives
    // counter (its "player" node), then the time, score and item scenes.
    constexpr uint32_t MISSION_ROW_RCPTRS[] = { 0xF4, 0xEC, 0xE4, 0xDC };

    // The item row counts one mission condition: its type picks the icon (1 ring, 2 item, 3 chao,
    // 4 enemy, 5/6 box) and the count it asks the mission manager for (MsgGetConditionCount);
    // the target is the "/N" (0 = no item row).
    constexpr uint32_t MISSION_CONDITION_TYPE   = 0x154; // u32
    constexpr uint32_t MISSION_CONDITION_TARGET = 0x158; // u32
    constexpr uint32_t CONDITION_TYPE_ENEMY     = 4;

    // A guest float[2], for CNode::SetPosition.
    struct GuestVec2
    {
        be<float> x;
        be<float> y;
    };

    // How recently the game must have drawn a clone for its row to count as on screen (see
    // RibbonCanShow). Covers a few frames of jitter even at low frame rates.
    constexpr double NATIVE_DRAWN_WINDOW = 0.1;

    // A guest Chao::CSD::RCPtr: { vftable, RCObject* }; RCObject::m_pMemory (+4) is the pointee.
    struct GuestRCPtr
    {
        be<uint32_t> vftable;
        be<uint32_t> object;
    };

    // Draw() (the ImGui pass) -> Update hook (game thread): where each row wants its ribbon,
    // and the number to show in the native digit font (-1 = none, the value is text).
    struct NativeRequest
    {
        std::atomic<float>  spot{ 0.0f };
        std::atomic<int>    digits{ -1 };
        std::atomic<double> stamp{ -1.0e9 };
    };

    NativeRequest         g_nativeRequests[NATIVE_SLOT_COUNT];
    std::atomic<uint32_t> g_nativeSceneAddr[NATIVE_SLOT_COUNT]; // live clone per slot (0 = none)
    std::atomic<double>   g_nativeDrawnAt[NATIVE_SLOT_COUNT];   // last time the game drew it
    std::atomic<int>      g_nativeSlotCounter{ 0 };

    // Game thread only.
    struct NativeClone
    {
        uint32_t scene      = 0;
        uint32_t digitsNode = 0;     // the clone's "score" text cast
        uint32_t placeNode  = 0;     // mission: the position_S node it's moved by (0 = day)
        bool     hidden     = false;
        float    y          = -1.0f; // last placement y (day: normalized; mission: px)
        int      digits     = -1;    // number the digits node shows (-1 = hidden)
    };

    NativeClone g_nativeClones[NATIVE_SLOT_COUNT];
    uint32_t    g_nativeHud     = 0;
    uint32_t    g_nativeProject = 0;
    HudKind     g_nativeKind    = HudKind::None;
    bool        g_nativeSonic   = true; // mission clones: made in Sonic's blue, or the Werehog's purple
}

static uint32_t GuestU32(uint32_t address)
{
    return *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(address));
}

// The pointee of a guest RCPtr, or 0 (guarding the null-guard page like the other reads).
static uint32_t GuestRCPtrGet(const GuestRCPtr& rcPtr)
{
    const uint32_t object = rcPtr.object;
    return object >= 0x10000u ? GuestU32(object + 4) : 0;
}

static uint32_t ReadHudProject(uint32_t hud, HudKind kind)
{
    const uint32_t offset = kind == HudKind::MissionStage ? MISSION_PROJECT_RCPTR
                          : kind == HudKind::EvilStage    ? EVIL_PROJECT_RCPTR
                                                          : HUD_PLAYSCREEN_RCPTR;
    return GuestRCPtrGet(*reinterpret_cast<GuestRCPtr*>(g_memory.Translate(hud + offset)));
}

static bool GuestNameIs(uint32_t guestName, const char* name)
{
    return guestName >= 0x10000u &&
        std::strcmp(reinterpret_cast<const char*>(g_memory.Translate(guestName)), name) == 0;
}

// A scene's CSD data (CScene::m_pResource, +0xC) indexes its casts by name as {name, group,
// index} (count +0x2C, entries +0x30) and its animations as {name, index} (+0x34, +0x3C).
static uint32_t SceneResource(uint32_t scene)
{
    const uint32_t resource = GuestU32(scene + 0xC);
    return resource >= 0x10000u ? resource : 0;
}

// The guest address of `scene`'s cast-index entry for `name` in cast group `group` (-1 = any),
// or 0. Names can repeat across groups (ui_missionscreen's two ribbons both have a "txt").
static uint32_t FindCastEntry(uint32_t scene, const char* name, int group)
{
    const uint32_t resource = SceneResource(scene);
    if (resource == 0)
        return 0;

    const uint32_t count = GuestU32(resource + 0x2C), entries = GuestU32(resource + 0x30);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t entry = entries + i * 12;
        if (GuestNameIs(GuestU32(entry), name) && (group < 0 || GuestU32(entry + 4) == static_cast<uint32_t>(group)))
            return entry;
    }

    return 0;
}

// Like FindNativeNode, but from a cast-index entry, exact even when the name repeats.
static uint32_t FindNativeNodeByEntry(uint32_t scene, uint32_t entry)
{
    if (entry == 0)
        return 0;

    guest_stack_var<GuestRCPtr> rcNode;
    GuestToHostFunction<void>(sub_830BC9F0, g_memory.MapVirtual(rcNode.get()), scene, entry);

    const uint32_t node = GuestRCPtrGet(*rcNode.get());
    GuestToHostFunction<void>(sub_830BA1D8, g_memory.MapVirtual(rcNode.get()));
    return node;
}

// A named cast of `scene` (0 if missing, e.g. a HUD mod that renamed it). The scene caches the
// node, so the raw pointer stays valid for the scene's lifetime after we drop our reference.
static uint32_t FindNativeNode(uint32_t scene, uint32_t guestName)
{
    guest_stack_var<GuestRCPtr> rcNode;
    GuestToHostFunction<void>(sub_830BCCA8, g_memory.MapVirtual(rcNode.get()), scene, guestName);

    const uint32_t node = GuestRCPtrGet(*rcNode.get());
    GuestToHostFunction<void>(sub_830BA1D8, g_memory.MapVirtual(rcNode.get()));
    return node;
}

static void SetNativeNodeHidden(uint32_t node, bool hidden)
{
    if (node != 0)
        GuestToHostFunction<void>(sub_830BF080, node, hidden ? 1u : 0u);
}

static void SetNativeDigits(uint32_t node, int value)
{
    if (node == 0)
        return;

    // SetText copies the string, so a guest-stack buffer is enough.
    struct GuestText { char text[16]; };
    guest_stack_var<GuestText> text;
    std::snprintf(text.get()->text, sizeof(text.get()->text), "%d", value);
    GuestToHostFunction<void>(sub_830BF640, node, g_memory.MapVirtual(text.get()));
}

// Whether a mission's rows take Sonic's blue (true) or the Werehog's purple. The mission HUD asks
// the player which character it is once, at its setup , and keeps the answer for as long  as the
// mission runs. Neither a character switch (MsgOnSwitchedPlayer, in Eggmanland's missions) nor a 
// retry rebuilds it. Only MsgKillMissionHud does, when the mission ends.
static bool MissionRowsAreSonic(uint32_t hud)
{
    if (SWA::Player::CEvilSonicContext::GetInstance() != nullptr)
        return false;
    if (SWA::Player::CSonicContext::GetInstance() != nullptr)
        return true;

    const bool dayHud   = g_hudSonicStagePtr.load(std::memory_order_relaxed) != 0;
    const bool nightHud = g_hudEvilStagePtr.load(std::memory_order_relaxed) != 0;
    if (dayHud != nightHud)
        return dayHud;

    return *reinterpret_cast<uint8_t*>(g_memory.Translate(hud + MISSION_IS_SONIC)) != 0;
}

// Selects `scene`'s motion by name. The game's SetMotion takes the motion's index, so look it up
// in the scene data's motion index ({name, index} entries, count +0x34, entries +0x3C). False if
// the scene has no such motion (e.g. a HUD mod's project).
static bool SetNativeMotion(uint32_t scene, const char* name)
{
    const uint32_t resource = SceneResource(scene);
    if (resource == 0)
        return false;

    const uint32_t count = GuestU32(resource + 0x34), entries = GuestU32(resource + 0x3C);
    for (uint32_t i = 0; i < count; i++)
    {
        if (GuestNameIs(GuestU32(entries + i * 8), name))
            return GuestToHostFunction<bool>(sub_830BA760, scene, GuestU32(entries + i * 8 + 4));
    }

    return false;
}

// `sonic` picks a mission clone's colour (see MissionRowsAreSonic); stage clones take their
// project's.
static NativeClone CreateNativeClone(uint32_t project, HudKind kind, bool sonic)
{
    NativeClone clone;

    // The game constructs the new scene's RCPtr in guest memory, like a return value, and the
    // name is its own .rdata string. Our reference is dropped at the end; the project's scene
    // maps keep the clone alive.
    guest_stack_var<GuestRCPtr> rcScene;
    GuestToHostFunction<void>(sub_830BEE00, g_memory.MapVirtual(rcScene.get()), project, GUEST_STR_SCORE_COUNT, 0u);

    clone.scene = GuestRCPtrGet(*rcScene.get());
    if (clone.scene != 0 && kind == HudKind::MissionStage)
    {
        // The regular small ribbon in Sonic's blue, or the Werehog's purple while he's the
        // player, like the character's own stage rows. With the silver objective ribbon hidden.
        SetNativeMotion(clone.scene, sonic ? "normal_so" : "normal_ev");
        SetNativeNodeHidden(FindNativeNodeByEntry(clone.scene, FindCastEntry(clone.scene, "position_L", -1)), true);

        // Resolve the small ribbon's own casts by group: "txt" repeats in the silver one.
        const uint32_t small = FindCastEntry(clone.scene, "position_S", -1);
        const int group = small != 0 ? static_cast<int>(GuestU32(small + 4)) : 0;
        clone.placeNode = FindNativeNodeByEntry(clone.scene, small);

        SetNativeNodeHidden(FindNativeNodeByEntry(clone.scene, FindCastEntry(clone.scene, "txt", group)), true);
        clone.digitsNode = FindNativeNodeByEntry(clone.scene, FindCastEntry(clone.scene, "score", group));
        SetNativeNodeHidden(clone.digitsNode, true);
    }
    else if (clone.scene != 0)
    {
        // Our label replaces the pre-drawn "SCORE"; the digits start hidden until the row
        // gives them a number (UpdateNativeRibbons).
        SetNativeNodeHidden(FindNativeNode(clone.scene, GUEST_STR_TXT), true);
        clone.digitsNode = FindNativeNode(clone.scene, GUEST_STR_SCORE);
        SetNativeNodeHidden(clone.digitsNode, true);
    }

    GuestToHostFunction<void>(sub_830BA1D8, g_memory.MapVirtual(rcScene.get()));
    return clone;
}

static void ForgetNativeClones()
{
    for (int i = 0; i < NATIVE_SLOT_COUNT; i++)
    {
        g_nativeClones[i] = {};
        g_nativeSceneAddr[i].store(0, std::memory_order_relaxed);
    }
}

static void DestroyNativeRibbons(uint32_t hud);

// Game thread, every Update of the HUD that owns the rows: create, place and show/hide the
// clones the rows asked for this frame.
static void UpdateNativeRibbons(uint32_t hud, HudKind kind)
{
    if (hud == 0)
        return;

    const uint32_t project = ReadHudProject(hud, kind);
    const bool     sonic   = kind != HudKind::MissionStage || MissionRowsAreSonic(hud);
    if (hud != g_nativeHud || project != g_nativeProject || sonic != g_nativeSonic)
    {
        const uint32_t oldHud = g_nativeHud;
        const bool oldAlive = oldHud != 0 &&
            (oldHud == g_hudSonicStagePtr.load(std::memory_order_relaxed) ||
             oldHud == g_hudEvilStagePtr.load(std::memory_order_relaxed) ||
             oldHud == g_hudMissionStagePtr.load(std::memory_order_relaxed));

        if (oldAlive)
            DestroyNativeRibbons(oldHud);
        else
            ForgetNativeClones();

        g_nativeHud     = hud;
        g_nativeProject = project;
        g_nativeKind    = kind;
        g_nativeSonic   = sonic;
    }

    if (project == 0)
        return;

    const bool   hudVisible = IsNativeGameHudVisible();
    const bool   paused     = g_isPauseMenuVisible.load(std::memory_order_relaxed);
    const double now        = NowSeconds();

    for (int i = 0; i < NATIVE_SLOT_COUNT; i++)
    {
        auto& clone   = g_nativeClones[i];
        auto& request = g_nativeRequests[i];
        const bool wanted = hudVisible && (now - request.stamp.load(std::memory_order_relaxed)) < 0.15;

        // A new clone starts at its scene origin (the screen's top-left) until the placement
        // below has been applied by a scene update, so keep it hidden for its first frame.
        bool justCreated = false;
        if (wanted && clone.scene == 0)
        {
            clone = CreateNativeClone(project, kind, g_nativeSonic);
            g_nativeSceneAddr[i].store(clone.scene, std::memory_order_relaxed);

            if (clone.scene != 0)
            {
                GuestToHostFunction<void>(sub_830BB378, clone.scene, 1u);
                clone.hidden = true;
                justCreated  = true;
            }
        }

        if (clone.scene == 0)
            continue;

        // Whether this clone's placement or text changed this frame (see the scene update below).
        bool changed = justCreated;

        if (wanted)
        {
            const float spot = request.spot.load(std::memory_order_relaxed);
            if (clone.placeNode != 0)
            {
                // Mission: move the small ribbon onto the spot the way the mission HUD moves its
                // own rows onto its slots, a cast position in 1280x720 px, inside the scene, so
                // the aspect patch scales it like theirs. X is the slots' column.
                const float y = NATIVE_SPOT1_Y + (spot - 1.0f) * STRIDE;
                if (y != clone.y)
                {
                    guest_stack_var<GuestVec2> position;
                    position.get()->x = *reinterpret_cast<be<float>*>(g_memory.Translate(hud + MISSION_SLOTS));
                    position.get()->y = y;
                    GuestToHostFunction<void>(sub_830BF630, clone.placeNode, g_memory.MapVirtual(position.get()));
                    clone.y = y;
                    changed = true;
                }
            }
            else
            {
                // Day / night: move the clone from SCORE's spot down to its own. A scene
                // translation is scaled by the window scale but NOT the gameplay-HUD shrink
                // (aspect_ratio_patches uses it as the SCALE pivot), so fold that shrink in here
                // to keep exactly one stride per spot.
                const float sceneSpot = kind == HudKind::EvilStage ? NATIVE_SCENE_SPOT_EVIL : NATIVE_SCENE_SPOT;
                const float y = (spot - sceneSpot) * STRIDE * g_aspectRatioGameplayScale / 720.0f;
                if (y != clone.y)
                {
                    GuestToHostFunction<void>(sub_830BB550, clone.scene, 0.0f, y);
                    clone.y = y;
                    changed = true;
                }
            }

            // A numeric value goes in the clone's own digit font. Set the text before showing
            // the cast so it never flashes the placeholder digits.
            const int digits = request.digits.load(std::memory_order_relaxed);
            if (digits != clone.digits)
            {
                if (digits >= 0)
                    SetNativeDigits(clone.digitsNode, digits);
                if ((digits >= 0) != (clone.digits >= 0))
                    SetNativeNodeHidden(clone.digitsNode, digits < 0);
                clone.digits = digits;
                changed = true;
            }
        }

        // A clone only takes on a new placement or text in a scene update (SetText blanks the
        // cast until its glyphs are rebuilt there). Its project normally does that, but a mission
        // HUD's project stops updating while paused with its rows still on screen, so the FPS
        // digits blanked or froze. Update the clone ourselves then, with no time step: it rebuilds
        // the casts at the current motion frame without advancing the animation.
        if (changed && paused)
            GuestToHostFunction<void>(sub_830BC1D8, clone.scene, 0.0f);

        const bool hide = !wanted || justCreated;
        if (clone.hidden != hide)
        {
            GuestToHostFunction<void>(sub_830BB378, clone.scene, hide ? 1u : 0u);
            clone.hidden = hide;
        }
    }
}

// Game thread: queue this HUD's clones for destruction while its project is still alive.
static void DestroyNativeRibbons(uint32_t hud)
{
    if (hud == 0 || hud != g_nativeHud)
        return;

    if (g_nativeProject != 0 && g_nativeProject == ReadHudProject(hud, g_nativeKind))
    {
        for (auto& clone : g_nativeClones)
        {
            if (clone.scene != 0)
                GuestToHostFunction<void>(sub_830BE298, g_nativeProject, clone.scene);
        }
    }

    ForgetNativeClones();
    g_nativeHud     = 0;
    g_nativeProject = 0;
    g_nativeKind    = HudKind::None;
}

// Game thread, every CHudMissionStage::Update: whether its rows are laid out, and the first
// spot below them. Its setup fills the "position" slots top-down, one per row it created, so
// the count of those rows is the first free slot (past the 5th, keep stepping a STRIDE down).
static void PublishMissionLayout(uint32_t hud)
{
    // A mission with a "defeat N enemies" objective already shows the enemy count in its item row.
    g_missionShowsEnemyCount.store(GuestU32(hud + MISSION_CONDITION_TYPE) == CONDITION_TYPE_ENEMY &&
        GuestU32(hud + MISSION_CONDITION_TARGET) != 0, std::memory_order_relaxed);

    const bool ready = *reinterpret_cast<uint8_t*>(g_memory.Translate(hud + MISSION_SETUP_DONE)) != 0;
    g_missionHudReady.store(ready, std::memory_order_relaxed);
    if (!ready)
        return;

    int used = 0;
    for (uint32_t offset : MISSION_ROW_RCPTRS)
    {
        if (reinterpret_cast<GuestRCPtr*>(g_memory.Translate(hud + offset))->object != 0)
            used++;
    }

    auto slotY = [&](int slot)
    {
        return static_cast<float>(*reinterpret_cast<be<float>*>(g_memory.Translate(hud + MISSION_SLOTS + slot * 8 + 4)));
    };

    const float y = used < MISSION_SLOT_COUNT ? slotY(used)
                                              : slotY(MISSION_SLOT_COUNT - 1) + STRIDE * (used - MISSION_SLOT_COUNT + 1);
    g_firstFreeSpot.store((y - NATIVE_SPOT1_Y) / STRIDE + 1.0f, std::memory_order_relaxed);
}

// --- Text drawn in the game's frame -------------------------------------------
//
// A native row's text isn't drawn over the finished frame with the rest of the ImGui overlay:
// it's drawn into the game's frame right after the game draws the row's ribbon clone. The CSD
// render hook reports each scene it has drawn (NotifyCsdSceneRendered), and
// Video::DrawInGameFrame queues the text right behind the clone's own draw calls. So the text
// layers exactly like the ribbon it sits on: over it, and under everything the game draws
// afterwards (the pause banner and panels, the pause dim, the death fade), pixel for pixel. It's
// also drawn only when the ribbon is.
//
// Draw() (the ImGui pass) publishes what each row wants: its text, spot and alpha. The next
// time the game draws that row's clone, the text is built from it. Both run on the guest thread
// that issues the game's draw calls (Video::Present is one of them), so the text uses the row's
// latest Draw(), like the clone's own placement does.
struct RowText
{
    std::string label;
    std::string value;      // empty when the clone shows the value in its digit font
    float       spot  = 0.0f;
    uint8_t     alpha = 0;
    int         frame = -1; // the ImGui frame it was published in
};

static std::mutex g_rowTextMutex;
static RowText    g_rowTexts[NATIVE_SLOT_COUNT];

// ImGui pass: ask for the clone in `slot` at `spot` (with `digits` for the native digit font, -1
// for none) and publish its text for when the game draws the clone.
static void RequestNativeRibbon(int slot, float spot, int digits, const std::string& label,
    const std::string& value, uint8_t alpha)
{
    auto& request = g_nativeRequests[slot];
    request.spot.store(spot, std::memory_order_relaxed);
    request.digits.store(digits, std::memory_order_relaxed);
    request.stamp.store(NowSeconds(), std::memory_order_relaxed);

    std::lock_guard lock(g_rowTextMutex);
    auto& text = g_rowTexts[slot];
    text.label = label;
    text.value = digits >= 0 ? std::string() : value;
    text.spot  = spot;
    text.alpha = alpha;
    text.frame = ImGui::GetFrameCount();
}

void GameplayStatusHudRibbon::NotifyCsdSceneRendered(uint32_t scene)
{
    if (scene == 0)
        return;

    for (int slot = 0; slot < NATIVE_SLOT_COUNT; slot++)
    {
        if (g_nativeSceneAddr[slot].load(std::memory_order_relaxed) != scene)
            continue;

        g_nativeDrawnAt[slot].store(NowSeconds(), std::memory_order_relaxed);

        RowText text;
        {
            std::lock_guard lock(g_rowTextMutex);
            text = g_rowTexts[slot];
        }

        // Only text from the latest ImGui pass: once a row stops drawing (the gate closed, or
        // the caller stopped showing it), its clone is left bare until it's hidden a frame later.
        const int sincePublished = ImGui::GetFrameCount() - text.frame;
        if (text.frame < 0 || sincePublished < 0 || sincePublished > 1 || text.alpha == 0 || g_fntHeader == nullptr)
            return;

        auto drawList = std::make_unique<ImGuiInFrameDrawList>(
            ImVec2(float(Video::s_viewportWidth), float(Video::s_viewportHeight)));
        {
            ImGuiCallbackRedirect redirect(drawList.get());
            DrawRowText(&drawList->drawList, LayoutRow(text.spot), text.label.c_str(), text.value.c_str(), text.alpha);
        }

        Video::DrawInGameFrame(std::move(drawList));
        return;
    }
}

// --- GameplayStatusHudRibbon -------------------------------------------------

GameplayStatusHudRibbon::GameplayStatusHudRibbon(std::string label, std::string value)
    : m_label(std::move(label)), m_value(std::move(value))
{
    const int slot = g_nativeSlotCounter.fetch_add(1, std::memory_order_relaxed);
    m_nativeSlot = slot < NATIVE_SLOT_COUNT ? slot : -1;
}

void GameplayStatusHudRibbon::SetLabel(std::string label) { m_label = std::move(label); }
void GameplayStatusHudRibbon::SetValue(std::string value)
{
    m_value  = std::move(value);
    m_digits = -1;
}

void GameplayStatusHudRibbon::SetValue(int value)
{
    // Keep the text form too: a negative number is drawn as text.
    m_value  = std::to_string(value);
    m_digits = value >= 0 ? value : -1; // the native digit font has no minus sign
}

void GameplayStatusHudRibbon::Init()
{
    if (g_fntHeader == nullptr)
        g_fntHeader = ImFontAtlasSnapshot::GetFont("DFSoGeiStd-W7.otf");
}

float GameplayStatusHudRibbon::FirstFreeSpot()
{
    return g_firstFreeSpot.load(std::memory_order_relaxed);
}

void GameplayStatusHudRibbon::ServiceWhilePaused()
{
    // A mission HUD's Update stops while paused (its rows stay up), so its clones would freeze
    // as they were, e.g. not hidden when a menu opened from the pause hides the HUD. Service
    // them from the pause menu's update instead.
    const uint32_t hud = g_activeHudPtr.load(std::memory_order_relaxed);
    const HudKind kind = ActiveHudKind();
    if (hud != 0 && kind != HudKind::None)
        UpdateNativeRibbons(hud, kind);
}

bool GameplayStatusHudRibbon::RibbonCanShow()
{
    if (!RibbonGateOpen())
        return false;

    const double now = NowSeconds();
    for (int i = 0; i < NATIVE_SLOT_COUNT; i++)
    {
        if (g_nativeSceneAddr[i].load(std::memory_order_relaxed) != 0 &&
            now - g_nativeDrawnAt[i].load(std::memory_order_relaxed) < NATIVE_DRAWN_WINDOW)
        {
            return true;
        }
    }

    return false;
}

void GameplayStatusHudRibbon::Draw(float spot, uint8_t alpha) const
{
    // Out of clone slots: nothing to draw with.
    if (m_nativeSlot < 0)
        return;

    // Hide outside a live stage and whenever the game has blanked its HUD rows (pause, HUD-off,
    // loading); otherwise fade in. RibbonFade() folds in the gate (it returns 0 while hidden),
    // so this single check covers both.
    const float fade = RibbonFade();
    if (fade <= 0.0f)
        return;

    // Ease the whole element in by scaling the caller's alpha.
    alpha = static_cast<uint8_t>(alpha * fade + 0.5f);

    // The game draws the ribbons (and a numeric value) from the clone, and the text is drawn into
    // the game's frame along with it (see "Text drawn in the game's frame").
    RequestNativeRibbon(m_nativeSlot, spot, m_digits, m_label, m_value, alpha);
}

// --- GameplayStatusHud (example) ---------------------------------------------

namespace
{
    constexpr float OPACITY = 0.90f; // matched against the real ribbons in the tuner

    // This example has no natural in-game trigger; it simply draws while gameplay is
    // up. Flip to false (and rebuild) to disable it without removing the wiring.
    constexpr bool SHOW_EXAMPLE = true;

    // Whether the game's native "EXP" ribbon (the points popup shown when Sonic collects
    // the yellow diamonds enemies drop) is currently on screen. When it is, it takes the
    // row directly under SCORE, so the custom rows start one spot lower.
    constexpr uint32_t EXP_HUD_CONFIG_FLAGS   = 0x1B4;      // CHudSonicStage flag word
    constexpr uint32_t EXP_HUD_COUNTDOWN      = 0x1B8;      // float, time since the popup showed
    constexpr uint32_t EXP_HUD_ENABLED_BIT    = 5;          // "EXP display enabled" flag
    constexpr uint32_t EXP_SHOW_MESSAGE_ID    = 3;          // ProcessMessage id for "show EXP"
    constexpr uint32_t EXP_THRESHOLD_GUEST    = 0x8328A3BC; // float, how long the popup lingers
    constexpr uint32_t EVIL_EXP_TIMER         = 0x100;      // CHudEvilStage float, the EXP row's show timer

    // g_hudSonicStagePtr is declared at file scope above (shared with the fade-in logic).
    std::atomic<bool>     g_expHudArmed{false};  // popup shown at least once since it last cleared

    bool IsExpHudVisible()
    {
        // Night: the Werehog's EXP row slides in under RING while its show timer runs. The timer
        // climbs to 2 s when the stat it shows changes from its cached copy, then falls back to 0.
        const uint32_t evil = g_hudEvilStagePtr.load(std::memory_order_relaxed);
        if (evil != 0 && *reinterpret_cast<be<float>*>(g_memory.Translate(evil + EVIL_EXP_TIMER)) > 0.0f)
            return true;

        if (!g_expHudArmed.load(std::memory_order_relaxed))
            return false;

        const uint32_t hud = g_hudSonicStagePtr.load(std::memory_order_relaxed);
        if (hud == 0)
            return false;

        const float countdown = *reinterpret_cast<be<float>*>(g_memory.Translate(hud + EXP_HUD_COUNTDOWN));
        const float threshold = *reinterpret_cast<be<float>*>(g_memory.Translate(EXP_THRESHOLD_GUEST));

        // Same condition the game uses to start the popup's disappear (sub_824D6418).
        if (countdown > threshold)
        {
            // Hidden (or fading out). Disarm so the frozen countdown, and a later stage's
            // fresh 0, can't read as visible until the next real EXP pickup re-arms us.
            g_expHudArmed.store(false, std::memory_order_relaxed);
            return false;
        }

        return true;
    }

    bool TryReadSonicPosition(float& x, float& y, float& z)
    {
        void* ctx = SWA::Player::CSonicContext::GetInstance();
        if (ctx == nullptr)
            ctx = SWA::Player::CEvilSonicContext::GetInstance();
        if (ctx == nullptr)
            return false;

        const uint32_t guestObj = *reinterpret_cast<be<uint32_t>*>(reinterpret_cast<uint8_t*>(ctx) + 0x10);
        if (guestObj < 0x10000u)
            return false;

        const auto pos = reinterpret_cast<uint8_t*>(g_memory.Translate(guestObj)) + 0x70;
        x = *reinterpret_cast<be<float>*>(pos + 0x0);
        y = *reinterpret_cast<be<float>*>(pos + 0x4);
        z = *reinterpret_cast<be<float>*>(pos + 0x8);
        return true;
    }

    /*
       How many enemies have been defeated, for the EnemyCounter row: the game's own count, the
       one a "defeat N enemies" mission shows. From RE of the retail binary:
      
         Every stage load builds a CMissionManager in normal stages as well as missions. MsgNotifyMission {type, value} adds to its counters (handler
         sub_8259C590); type 4 adds to the enemy count at +0xFC:
         - Day enemies send it from sub_8281F090, called by their removal callback only once their
           hit points have run out, so an enemy that despawns alive isn't counted.
         - Werehog enemies send it from their defeat callback.
         A mission's enemy objective row reads the same counter back with MsgGetConditionCount(4). 
         The counters are zeroed when the manager is built
         and when it handles , which the [Codes]
         SaveScoreAtCheckpoints patch in player_patches.cpp follows up on, like the score.
    */
    constexpr uint32_t DOCUMENT_MISSION_MANAGER = 0x1C8; // CGameDocument::CMember, shared_ptr<CMissionManager>
    constexpr uint32_t MISSION_ENEMY_COUNT      = 0xFC;  // CMissionManager, u32

    bool TryReadEnemiesDefeated(int& count)
    {
        const auto pGameDocument = SWA::CGameDocument::GetInstance();
        if (pGameDocument == nullptr || pGameDocument->m_pMember.get() == nullptr)
            return false;

        // The shared_ptr's first word is the pointee.
        const auto pMember = reinterpret_cast<const uint8_t*>(pGameDocument->m_pMember.get());
        const uint32_t missionManager = *reinterpret_cast<const be<uint32_t>*>(pMember + DOCUMENT_MISSION_MANAGER);
        if (missionManager < 0x10000u)
            return false;

        count = static_cast<int>(GuestU32(missionManager + MISSION_ENEMY_COUNT));
        return true;
    }
}

// CHudSonicStage::ProcessMessage: We snoop the "show EXP" command to arm IsExpHudVisible()
// above, and keep the live HUD pointer fresh across stage reloads.
PPC_FUNC_IMPL(__imp__sub_824D78F0);
PPC_FUNC(sub_824D78F0)
{
    const uint32_t hud = ctx.r3.u32;
    const uint32_t msg = ctx.r4.u32;
    g_hudSonicStagePtr.store(hud, std::memory_order_relaxed);

    bool showExp = false;
    if (hud != 0 && msg != 0)
    {
        const uint32_t messageId = *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(msg + 0x18));
        if (messageId == EXP_SHOW_MESSAGE_ID)
        {
            const uint32_t flags = *reinterpret_cast<be<uint32_t>*>(g_memory.Translate(hud + EXP_HUD_CONFIG_FLAGS));
            showExp = ((flags >> EXP_HUD_ENABLED_BIT) & 1u) != 0;
        }
    }

    __imp__sub_824D78F0(ctx, base);

    // Arm only after the original ran: it resets the countdown to ~0 as part of showing the
    // popup.
    if (showExp)
        g_expHudArmed.store(true, std::memory_order_relaxed);
}

static void MarkActiveHud(uint32_t hud, HudKind kind)
{
    g_activeHudPtr.store(hud, std::memory_order_relaxed);
    g_activeHudKind.store(static_cast<int>(kind), std::memory_order_relaxed);
    g_hudLastSeen.store(NowSeconds(), std::memory_order_relaxed);
}

// A HUD is being destroyed: stop treating it as live.
static void ClearActiveHud(uint32_t hud)
{
    uint32_t expected = hud;
    if (g_activeHudPtr.compare_exchange_strong(expected, 0, std::memory_order_relaxed))
        g_activeHudKind.store(static_cast<int>(HudKind::None), std::memory_order_relaxed);
}

// CHudSonicStage::Update. Runs every frame the day-stage HUD is shown.
// clones and create new ones every frame which is an endless pile-up.
PPC_FUNC_IMPL(__imp__sub_824D7100);
PPC_FUNC(sub_824D7100)
{
    const uint32_t hud = ctx.r3.u32;
    g_hudSonicStagePtr.store(hud, std::memory_order_relaxed);

    // (The night HUD isn't expected alongside it, but if both ever lived at once, one owner
    // keeps them from taking each other's clones every frame.)
    const bool ownsRows = g_hudMissionStagePtr.load(std::memory_order_relaxed) == 0 &&
        g_hudEvilStagePtr.load(std::memory_order_relaxed) == 0;
    if (ownsRows)
    {
        g_firstFreeSpot.store(3.0f, std::memory_order_relaxed); // below TIME (1) and SCORE (2)
        MarkActiveHud(hud, HudKind::SonicStage);
    }

    __imp__sub_824D7100(ctx, base);

    // Service the rows' native ribbon requests on the game thread (see "Native ribbons").
    if (ownsRows)
        UpdateNativeRibbons(hud, HudKind::SonicStage);
}

// CHudSonicStage's destructor body  It releases the playscreen, so queue our clones
// for destruction first, then forget the HUD, so its day-only gates (the chip-phase
// flag word, the EXP popup) never read the freed object, e.g. from a mission
// stage that follows.
PPC_FUNC_IMPL(__imp__sub_824D8CE8);
PPC_FUNC(sub_824D8CE8)
{
    const uint32_t hud = ctx.r3.u32;
    DestroyNativeRibbons(hud);

    uint32_t expected = hud;
    if (g_hudSonicStagePtr.compare_exchange_strong(expected, 0, std::memory_order_relaxed))
        g_expHudArmed.store(false, std::memory_order_relaxed);
    ClearActiveHud(hud);

    __imp__sub_824D8CE8(ctx, base);
}

// CHudMissionStage::Update: Runs every frame the mission HUD is alive.
// Once the mission archive has loaded it runs the setup once which creates 
// and lays out the rows.
PPC_FUNC_IMPL(__imp__sub_824E9650);
PPC_FUNC(sub_824E9650)
{
    const uint32_t hud = ctx.r3.u32;
    g_hudMissionStagePtr.store(hud, std::memory_order_relaxed);
    MarkActiveHud(hud, HudKind::MissionStage);
    __imp__sub_824E9650(ctx, base);

    PublishMissionLayout(hud);
    UpdateNativeRibbons(hud, HudKind::MissionStage);
}

// CHudMissionStage's destructor body. It releases the ui_missionscreen project, so
// queue our clones for destruction first, then forget the HUD.
PPC_FUNC_IMPL(__imp__sub_824E82F0);
PPC_FUNC(sub_824E82F0)
{
    const uint32_t hud = ctx.r3.u32;
    DestroyNativeRibbons(hud);

    uint32_t expected = hud;
    if (g_hudMissionStagePtr.compare_exchange_strong(expected, 0, std::memory_order_relaxed))
    {
        g_missionHudReady.store(false, std::memory_order_relaxed);
        g_missionShowsEnemyCount.store(false, std::memory_order_relaxed);
    }
    ClearActiveHud(hud);

    __imp__sub_824E82F0(ctx, base);
}

// CHudEvilStage::Update: Runs every frame the Werehog's stage HUD is alive. Owns the rows unless a
// mission HUD exists, like the day HUD.
PPC_FUNC_IMPL(__imp__sub_8249DAD0);
PPC_FUNC(sub_8249DAD0)
{
    const uint32_t hud = ctx.r3.u32;
    g_hudEvilStagePtr.store(hud, std::memory_order_relaxed);

    const bool ownsRows = g_hudMissionStagePtr.load(std::memory_order_relaxed) == 0;
    if (ownsRows)
    {
        g_firstFreeSpot.store(3.0f, std::memory_order_relaxed); // below SCORE (1) and RING (2)
        MarkActiveHud(hud, HudKind::EvilStage);
    }

    __imp__sub_8249DAD0(ctx, base);

    if (ownsRows)
        UpdateNativeRibbons(hud, HudKind::EvilStage);
}

// CHudEvilStage's destructor body. It releases the ui_playscreen_ev, so
// queue our clones for destruction first, then forget the HUD.
PPC_FUNC_IMPL(__imp__sub_8249F0F8);
PPC_FUNC(sub_8249F0F8)
{
    const uint32_t hud = ctx.r3.u32;
    DestroyNativeRibbons(hud);

    uint32_t expected = hud;
    g_hudEvilStagePtr.compare_exchange_strong(expected, 0, std::memory_order_relaxed);
    ClearActiveHud(hud);

    __imp__sub_8249F0F8(ctx, base);
}

// CHudEvilStage's scene setup: like the day HUD's, it instantiates the project's scenes and
// removes some by name.
PPC_FUNC_IMPL(__imp__sub_8249CE70);
PPC_FUNC(sub_8249CE70)
{
    DestroyNativeRibbons(ctx.r3.u32);
    __imp__sub_8249CE70(ctx, base);
}

// CHudSonicStage's scene setup: It instantiates the project's scenes and removes the unused
// ones by name and the next Update re-creates them.
PPC_FUNC_IMPL(__imp__sub_824D7EE8);
PPC_FUNC(sub_824D7EE8)
{
    DestroyNativeRibbons(ctx.r3.u32);
    __imp__sub_824D7EE8(ctx, base);
}

void GameplayStatusHud::Init()
{
    GameplayStatusHudRibbon::Init();
}

void GameplayStatusHud::Draw()
{
    if (!SHOW_EXAMPLE)
        return;

    constexpr uint8_t alpha = static_cast<uint8_t>(OPACITY * 255.0f + 0.5f);

    // Whole frames per second, so it shows in the game's own digit font.
    const int fps = static_cast<int>(ImGui::GetIO().Framerate + 0.5f);

    // Custom rows start at the first spot free below the game's own rows: 3 in a day stage
    // (under TIME and SCORE) or a Werehog stage (under SCORE and RING), or wherever a mission
    // HUD's rows end.
    //
    // The EXP popup takes spot 3: the day HUD's under SCORE (in mission stages too, where the
    // day HUD runs alongside the mission HUD), and the Werehog's EXP row under RING. While it
    // shows, rows from that spot down move one lower to make room (then back up when it
    // clears).
    constexpr float EXP_SPOT = 3.0f;
    constexpr float EXP_SLIDE_SECONDS = 0.25f;
    static float s_expSlide = 0.0f; // 0 = rows in place, 1 = rows from EXP_SPOT down moved one lower
    const float target = IsExpHudVisible() ? 1.0f : 0.0f;
    const float step = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f) / EXP_SLIDE_SECONDS;
    s_expSlide = (s_expSlide < target) ? std::min(target, s_expSlide + step)
                                       : std::max(target, s_expSlide - step);
    const float expShift = Hermite(0.0f, 1.0f, s_expSlide);

    // Each shown row takes the next spot.
    float nextSpot = GameplayStatusHudRibbon::FirstFreeSpot();
    auto takeSpot = [&]()
    {
        const float spot = nextSpot;
        nextSpot += 1.0f;
        return spot >= EXP_SPOT ? spot + expShift : spot;
    };

    static GameplayStatusHudRibbon enemyRow("", "");
    static GameplayStatusHudRibbon posRow("POS", "");
    static GameplayStatusHudRibbon fpsRow("FPS", "");

    // [HUD] EnemyCounter: the enemies defeated, in the game's digit font, right under the
    // game's own rows. Not in a mission whose objective row already counts them.
    const bool missionShowsEnemies = ActiveHudKind() == HudKind::MissionStage &&
        g_missionShowsEnemyCount.load(std::memory_order_relaxed);
    if (Config::EnemyCounter &&  !missionShowsEnemies)
    {
        // The game's own word, like its results screen (see HUD_EnemyCounter in locale.cpp).
        enemyRow.SetLabel(Localise("HUD_EnemyCounter"));

        int count;
        if (TryReadEnemiesDefeated(count))
            enemyRow.SetValue(count);
        else
            enemyRow.SetValue("--");

        enemyRow.Draw(takeSpot(), alpha);
    }

    // [Codes] Sonic's world position (X Y Z), day or Werehog, on its own row, when the code is
    // enabled.
    // Text (commas and signs aren't in the native digit font), so it's also the long-string
    // case for the value text's leading flow.
    // if (Config::ShowSonicPosition)
    {
        char pos[48];
        float x, y, z;
        if (TryReadSonicPosition(x, y, z))
            std::snprintf(pos, sizeof(pos), "%04.0f, %04.0f, %04.0f", x, y, z);
        else
            std::snprintf(pos, sizeof(pos), "--");

        posRow.SetValue(pos);
        posRow.Draw(takeSpot(), alpha);
    }

    if (Config::ShowFPS && Config::FPSCounterStyleInStages == EFPSCounterStyle::Ribbon)
    {
        fpsRow.SetValue(fps);
        fpsRow.Draw(takeSpot(), alpha);
    }
}
