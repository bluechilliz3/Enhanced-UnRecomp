#include "bobsleigh_boost_overlay.h"
#include "imgui_utils.h"
#include <gpu/imgui/imgui_snapshot.h>
#include <gpu/video.h>
#include <patches/aspect_ratio_patches.h>
#include <decompressor.h>

#include <res/images/gameplay/bobsleigh_ribbon.dds.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

// --- Tunables (all in 1280x720 reference units, run through Scale()) ---------
namespace
{
    // The banner is two blue "rank ribbon" parallelograms, stacked with a small offset. 
    // The label sits on the upper one, the coefficient on the lower one; both are squashed 
    // well below the sprite's native ~2.7:1 aspect into thin HUD bars just above the 
    // SPEED/RING-ENERGY gauge.
    //
    // These constants are the single source of truth for the watermark's look. They
    // were dialed in via temporary System-tab knobs (since removed); edit here if needed
    constexpr float PARA_WIDTH     = 224.0f; // lower ribbon width
    constexpr float TOP_PARA_WIDTH = 200.0f; // upper ribbon width
    constexpr float PARA_HEIGHT    = 27.0f;  // lower ribbon height (anchors the stack); flat, not the sprite's aspect
    constexpr float TOP_PARA_HEIGHT = 16.0f; // upper ribbon height, usually a bit shorter
    constexpr float STACK_OFFSET_X = -12.0f; // lower banner shift (negative = left of the upper)
    constexpr float STACK_OFFSET_Y = 12.0f;  // lower banner shift (down)

    constexpr float MARGIN_X = 112.0f;  // gap from the left screen edge (HUD-anchored, ALIGN_LEFT)
    constexpr float MARGIN_Y = 126.0f;  // banner bottom above the bottom edge (clears the HUD)

    constexpr float DESLANT = 56.0f;     // lower ribbon counter-shear, less pointy (0 = raw)
    constexpr float TOP_DESLANT = 56.0f; // upper ribbon counter-shear

    constexpr float LABEL_FONT_SIZE = 13.0f;
    constexpr float VALUE_FONT_SIZE = 20.0f;
    constexpr float VALUE_RAISE     = 1.0f;  // nudge the coefficient number up (× stays put)
    constexpr float VALUE_OFFSET_Y  = 3.0f;  // move the whole value text (× and number) up (+) / down (-)
    
    // Fit text within the flat middle band of the slanted parallelogram.
    constexpr float TEXT_MAX_WIDTH_RATIO = 0.66f;

    constexpr float FADE_IN_SECONDS   = 0.15f;
    constexpr float FADE_OUT_RELEASE  = 1.0f;  // ~1s after the boost button is released
    constexpr float FADE_OUT_RIDE_END = 0.25f; // snappier when the ride itself ends

    constexpr float RIBBON_TEX_W = 585.0f, RIBBON_TEX_H = 142.0f;
    constexpr float VALUE_SPRITE_X = 244.0f, VALUE_SPRITE_Y = 16.0f, VALUE_SPRITE_W = 316.0f, VALUE_SPRITE_H = 122.0f;
    constexpr float TEXT_SPRITE_X  = 22.0f,  TEXT_SPRITE_Y  = 38.0f, TEXT_SPRITE_W  = 192.0f, TEXT_SPRITE_H  = 84.0f;
    // Horizontal 3-slice seams (absolute sheet x): fixed left/right caps hold the slanted
    // tips, the flat middle stretches. The sprites' top/bottom edges are horizontal, so a
    // plain vertical slice has no kink. (Tuned in the slice tuner.)
    constexpr float VALUE_SPRITE_V0 = 348.0f, VALUE_SPRITE_V1 = 442.0f;
    constexpr float TEXT_SPRITE_V0  = 102.0f, TEXT_SPRITE_V1  = 136.0f;

    // The real ribbon parallelogram sat inset inside its sprite box with a built-in
    // rightward lean. These corner fractions (measured from the atlas: TL/TR = top
    // edge, BL/BR = bottom edge) let the procedural ribbon land exactly where the
    // sprite did, so tuned geometry AND Deslant values keep the same meaning.
    constexpr float RIBBON_TL_X = 0.307f, RIBBON_TL_Y = 0.027f;
    constexpr float RIBBON_TR_X = 0.960f, RIBBON_TR_Y = 0.027f;
    constexpr float RIBBON_BR_X = 0.683f, RIBBON_BR_Y = 0.919f;
    constexpr float RIBBON_BL_X = 0.059f, RIBBON_BL_Y = 0.919f;

    // The drive hook pings SetBoosting() every frame during a ride. If pings stop
    // (pause, or a ride end we somehow miss) treat the ride as over so the banner
    // can never get stuck on screen.
    constexpr int64_t PING_TIMEOUT_MS = 250;

    constexpr ImU32 COLOUR_LABEL = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 COLOUR_VALUE = IM_COL32(252, 243, 5, 255); // results-screen yellow
}

static std::unique_ptr<GuestTexture> g_upTexture;
static ImFont* g_fntHeader;

// Shared between the bobsleigh drive-hook thread and the ImGui draw thread.
static std::atomic<bool>    g_boostHeld{ false };
static std::atomic<bool>    g_rideActive{ false };
static std::atomic<int64_t> g_lastPingMs{ 0 };

// Animation state, only ever touched on the ImGui draw thread.
static float g_alpha = 0.0f;

static int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static ImU32 WithAlpha(ImU32 colour, uint8_t alpha)
{
    return (colour & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);
}

void BobsleighBoostOverlay::Init()
{
    // The chunky display font used for the options-menu category headers.
    g_fntHeader = ImFontAtlasSnapshot::GetFont("DFSoGeiStd-W7.otf");
    g_upTexture = LOAD_ZSTD_TEXTURE(g_bobsleigh_ribbon);
}

void BobsleighBoostOverlay::SetBoosting(bool boosting)
{
    g_lastPingMs.store(NowMs(), std::memory_order_relaxed);
    g_rideActive.store(true, std::memory_order_relaxed);
    g_boostHeld.store(boosting, std::memory_order_relaxed);
}

void BobsleighBoostOverlay::EndRide()
{
    g_rideActive.store(false, std::memory_order_relaxed);
    g_boostHeld.store(false, std::memory_order_relaxed);
}

// Draws one ribbon from a results-screen sprite (bobsleigh_ribbon.dds) as a horizontal
// 3-slice.
static void DrawRibbonTexture(const ImVec2& min, float w, float h,
    float sx, float sy, float sw, float sh, float v0, float v1, uint8_t alpha)
{
    auto dl = ImGui::GetBackgroundDrawList();
    auto W = Scale(w), H = Scale(h);
    auto k = H / sh;                                 // uniform vertical scale
    auto capL = (v0 - sx) * k;                       // caps scale with height (keep aspect)
    auto capR = (sx + sw - v1) * k;
    auto mid  = std::max(0.0f, W - capL - capR);
    ImU32 col = IM_COL32(255, 255, 255, alpha);
    auto seg = [&](float srcX, float srcW, float dstX, float dstW)
    {
        auto uv = PIXELS_TO_UV_COORDS(RIBBON_TEX_W, RIBBON_TEX_H, srcX, sy, srcW, sh);
        dl->AddImage(g_upTexture.get(), { min.x + dstX, min.y }, { min.x + dstX + dstW, min.y + H },
            GET_UV_COORDS(uv), col);
    };
    seg(sx, v0 - sx,     0.0f,        capL); // left cap
    seg(v0, v1 - v0,     capL,        mid);  // stretched middle
    seg(v1, sx + sw - v1, capL + mid, capR); // right cap
}

// Bilinear interpolation across a quad (tl,tr,br,bl) at fractional (fx,fy).
static ImVec2 Bilerp(const ImVec2& tl, const ImVec2& tr, const ImVec2& br, const ImVec2& bl, float fx, float fy)
{
    ImVec2 top = { tl.x + (tr.x - tl.x) * fx, tl.y + (tr.y - tl.y) * fx };
    ImVec2 bot = { bl.x + (br.x - bl.x) * fx, bl.y + (br.y - bl.y) * fx };
    return { top.x + (bot.x - top.x) * fy, top.y + (bot.y - top.y) * fy };
}

// Draws text centred on a parallelogram whose top-left is paraMin, horizontally
// squeezed if it would overflow the flat inner band of the slant.
static void DrawFittedText(const ImVec2& paraMin, float paraW, float paraH, float cxRatio, float cyRatio,
    const char* text, float refFontSize, ImU32 colour, uint8_t alpha, float outlineSize)
{
    auto fontSize = Scale(refFontSize);
    auto maxWidth = Scale(paraW * TEXT_MAX_WIDTH_RATIO);
    auto textSize = g_fntHeader->CalcTextSizeA(fontSize, FLT_MAX, 0, text);
    auto textScale = textSize.x > maxWidth ? maxWidth / textSize.x : 1.0f;

    auto cx = paraMin.x + Scale(paraW) * cxRatio;
    auto cy = paraMin.y + Scale(paraH) * cyRatio;
    ImVec2 pos = { cx - (textSize.x * textScale) / 2.0f, cy - textSize.y / 2.0f };

    SetOrigin(pos);
    SetScale({ textScale, 1.0f });
    SetShaderModifier(IMGUI_SHADER_MODIFIER_LOW_QUALITY_TEXT);

    DrawTextWithOutline(g_fntHeader, fontSize, pos, WithAlpha(colour, alpha), text,
        outlineSize, IM_COL32(0, 0, 0, alpha));

    SetShaderModifier(IMGUI_SHADER_MODIFIER_NONE);
    SetScale({ 1.0f, 1.0f });
    SetOrigin({ 0.0f, 0.0f });
}

// Draws the coefficient as "<mult><number>" centred on the ribbon, with the number
// nudged up by `raise` (ref units) while the multiplication sign stays put — the
// glyph's math axis sits higher than the digits, so this evens them out.
static void DrawValueText(const ImVec2& paraMin, float paraW, float paraH, float cyRatio,
    const char* mult, const char* number, float refFontSize, ImU32 colour, uint8_t alpha,
    float outlineSize, float raise, float offsetY)
{
    auto fontSize = Scale(refFontSize);
    auto multSize = g_fntHeader->CalcTextSizeA(fontSize, FLT_MAX, 0, mult);
    auto numSize  = g_fntHeader->CalcTextSizeA(fontSize, FLT_MAX, 0, number);
    auto gap      = Scale(1.5f);
    auto totalW   = multSize.x + gap + numSize.x;

    auto cx = paraMin.x + Scale(paraW) * 0.5f;
    auto cy = paraMin.y + Scale(paraH) * cyRatio - Scale(offsetY); // whole-group vertical shift (+ = up)
    auto startX = cx - totalW * 0.5f;

    ImVec2 multPos = { startX, cy - multSize.y / 2.0f };
    ImVec2 numPos  = { startX + multSize.x + gap, cy - numSize.y / 2.0f - Scale(raise) };

    SetShaderModifier(IMGUI_SHADER_MODIFIER_LOW_QUALITY_TEXT);
    DrawTextWithOutline(g_fntHeader, fontSize, multPos, WithAlpha(colour, alpha), mult, outlineSize, IM_COL32(0, 0, 0, alpha));
    DrawTextWithOutline(g_fntHeader, fontSize, numPos,  WithAlpha(colour, alpha), number, outlineSize, IM_COL32(0, 0, 0, alpha));
    SetShaderModifier(IMGUI_SHADER_MODIFIER_NONE);
}

void BobsleighBoostOverlay::Draw()
{
    // Resolve whether the watermark should currently be showing.
    bool rideActive = g_rideActive.load(std::memory_order_relaxed);
    if (rideActive && (NowMs() - g_lastPingMs.load(std::memory_order_relaxed)) > PING_TIMEOUT_MS)
        rideActive = false; // pings stopped, so the ride is effectively over

    bool show = rideActive && g_boostHeld.load(std::memory_order_relaxed);

    // Advance the fade, clamping the delta so a long hitch can't snap it.
    auto dt = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);

    if (show)
    {
        g_alpha = std::min(1.0f, g_alpha + dt / FADE_IN_SECONDS);
    }
    else
    {
        auto fadeOut = rideActive ? FADE_OUT_RELEASE : FADE_OUT_RIDE_END;
        g_alpha = std::max(0.0f, g_alpha - dt / fadeOut);
    }

    if (g_alpha <= 0.0f || g_fntHeader == nullptr || g_upTexture == nullptr)
        return;

    auto alpha = static_cast<uint8_t>(std::lround(g_alpha * 255.0f));

    // All values come from the constants at the top of this file (edit there to tweak).
    constexpr auto paraW      = PARA_WIDTH;
    constexpr auto topW       = TOP_PARA_WIDTH;
    constexpr auto paraH      = PARA_HEIGHT;
    constexpr auto topH       = TOP_PARA_HEIGHT;
    constexpr auto stackX     = STACK_OFFSET_X;
    constexpr auto stackY     = STACK_OFFSET_Y;
    constexpr auto marginX    = MARGIN_X;
    constexpr auto marginY    = MARGIN_Y;
    constexpr auto deslant    = DESLANT;
    constexpr auto topDeslant = TOP_DESLANT;
    constexpr auto labelFont  = LABEL_FONT_SIZE;
    constexpr auto valueFont  = VALUE_FONT_SIZE;
    constexpr auto valueRaise = VALUE_RAISE;
    constexpr auto valueOffsetY = VALUE_OFFSET_Y;

    // Anchor to the bottom-left screen edge, matching the game's HUD (ALIGN_LEFT +
    // ALIGN_BOTTOM). Left uses no g_aspectRatioOffsetX (that is the *centred* anchor,
    // which made the banner drift toward centre as the window widened); bottom keeps
    // g_aspectRatioOffsetY * 2 for the ALIGN_BOTTOM edge.
    auto groupLeft   = Scale(marginX);
    auto groupBottom = g_aspectRatioOffsetY * 2.0f + Scale(720.0f - marginY);
    auto groupTop    = groupBottom - Scale(paraH + stackY);

    // Subtle slide-in from the left as it fades in.
    groupLeft += Hermite(-Scale(20.0f), 0.0f, g_alpha);

    ImVec2 upperMin = { groupLeft, groupTop };
    ImVec2 lowerMin = { groupLeft + Scale(stackX), groupTop + Scale(stackY) };

    // Lower ribbon (coefficient) first, then the upper one (label) overlaps it, text on top.
    DrawRibbonTexture(lowerMin, paraW, paraH, VALUE_SPRITE_X, VALUE_SPRITE_Y, VALUE_SPRITE_W, VALUE_SPRITE_H, VALUE_SPRITE_V0, VALUE_SPRITE_V1, alpha);
    DrawRibbonTexture(upperMin, topW, topH, TEXT_SPRITE_X, TEXT_SPRITE_Y, TEXT_SPRITE_W, TEXT_SPRITE_H, TEXT_SPRITE_V0, TEXT_SPRITE_V1, alpha);

    DrawFittedText(upperMin, topW, topH, 0.50f, 0.44f, "BOBSLEIGH BOOST", labelFont, COLOUR_LABEL, alpha, 3.0f);

    char number[16];
    std::snprintf(number, sizeof(number), "%.2f", BobsleighBoostOverlay::Coefficient); // "1.75"
    DrawValueText(lowerMin, paraW, paraH, 0.52f, "\xC3\x97" /* × */, number, valueFont, COLOUR_VALUE, alpha, 3.0f, valueRaise, valueOffsetY);
}
