#include <api/SWA.h>
#include <cpu/guest_stack_var.h>
#include <hid/hid.h>
#include <kernel/function.h>
#include <patches/CHudPause_patches.h>
#include <patches/input_patches.h>
#include <ui/achievement_menu.h>
#include <ui/button_guide.h>
#include <ui/message_window.h>
#include <ui/options_menu.h>
#include <app.h>
#include <exports.h>
#include <sdl_listener.h>

constexpr double WORLD_MAP_ROTATE_DEADZONE = 0.69999999;
constexpr double WORLD_MAP_CURSOR_DEADZONE = 0.30000001;

class WorldMapCursorParams
{
public:
    float Damping{ 0.99f };
    float FlickAccelX{ 0.25f };
    float FlickAccelY{ 0.1f };
    float FlickTerminalVelocity{ 40.0f };
    float FlickThreshold{ 2.25f };
    float SensitivityX{};
    float SensitivityY{};
    float Smoothing{ 0.8f };
};

class WorldMapCursorParamsProspero : public WorldMapCursorParams
{
public:
    WorldMapCursorParamsProspero()
    {
        SensitivityX = 1.15f;
        SensitivityY = 1.05f;
    }
}
g_worldMapCursorParamsProspero;

class WorldMapCursorParamsOrbis : public WorldMapCursorParams
{
public:
    WorldMapCursorParamsOrbis()
    {
        SensitivityX = 0.95f;
        SensitivityY = 1.0f;
    }
}
g_worldMapCursorParamsOrbis;

#ifdef UNLEASHED_RECOMP_UI_KBM_SUPPORT
class WorldMapCursorParamsMouse : public WorldMapCursorParams
{
public:
    WorldMapCursorParamsMouse()
    {
        FlickAccelX = 0.025f;
        FlickAccelY = 0.025f;
        FlickThreshold = 7.5f;
        SensitivityX = 0.15f;
        SensitivityY = 0.15f;
    }
}
g_worldMapCursorParamsMouse;
#endif

WorldMapCursorParams g_worldMapCursorParams{};

static bool g_isCursorActive;

static float g_worldMapCursorVelocityX;
static float g_worldMapCursorVelocityY;

class SDLEventListenerForInputPatches : public SDLEventListener
{
    static inline bool ms_isMouseDown;

    static inline int ms_touchpadFingerCount;

    static inline float ms_cursorX;
    static inline float ms_cursorY;
    static inline float ms_cursorDeltaX;
    static inline float ms_cursorDeltaY;
    static inline float ms_cursorPrevX;
    static inline float ms_cursorPrevY;

public:
    static void Update(float deltaTime)
    {
        /* NOTE (Hyper): this code was written at 144Hz and was
           discovered later to be faulty at any other frame rate,
           so this is here to account for that without changing
           all the constants that I had tuned. */
        constexpr auto referenceDeltaTime = 1.0f / 144.0f;

        if (g_isCursorActive)
        {
            auto dxNorm = ms_cursorDeltaX / referenceDeltaTime;
            auto dyNorm = ms_cursorDeltaY / referenceDeltaTime;
            auto dxSens = dxNorm * g_worldMapCursorParams.SensitivityX;
            auto dySens = dyNorm * g_worldMapCursorParams.SensitivityY;

            auto smoothing = powf(g_worldMapCursorParams.Smoothing, deltaTime / referenceDeltaTime);

            g_worldMapCursorVelocityX = smoothing * g_worldMapCursorVelocityX + (1.0f - smoothing) * dxSens;
            g_worldMapCursorVelocityY = smoothing * g_worldMapCursorVelocityY + (1.0f - smoothing) * dySens;

            auto flickThreshold = g_worldMapCursorParams.FlickThreshold;

            if (fabs(dxSens) > flickThreshold || fabs(dySens) > flickThreshold)
            {
                g_worldMapCursorVelocityX += dxNorm * g_worldMapCursorParams.FlickAccelX * (deltaTime / referenceDeltaTime);
                g_worldMapCursorVelocityY += dyNorm * g_worldMapCursorParams.FlickAccelY * (deltaTime / referenceDeltaTime);
            }

            auto terminalVelocity = g_worldMapCursorParams.FlickTerminalVelocity;

            g_worldMapCursorVelocityX = std::clamp(g_worldMapCursorVelocityX, -terminalVelocity, terminalVelocity);
            g_worldMapCursorVelocityY = std::clamp(g_worldMapCursorVelocityY, -terminalVelocity, terminalVelocity);
        }
        else
        {
            auto dampingFactor = powf(g_worldMapCursorParams.Damping, deltaTime / referenceDeltaTime);

            g_worldMapCursorVelocityX *= dampingFactor;
            g_worldMapCursorVelocityY *= dampingFactor;
        }
    }

    bool OnSDLEvent(SDL_Event* event) override
    {
        if (!hid::IsInputAllowed())
            return false;

        switch (event->type)
        {
#ifdef UNLEASHED_RECOMP_UI_KBM_SUPPORT
            case SDL_MOUSEMOTION:
            {
                if (!ms_isMouseDown)
                    break;
            
                g_isCursorActive = true;
            
                ms_cursorDeltaX = (float)event->motion.xrel / 100.0f;
                ms_cursorDeltaY = (float)event->motion.yrel / 100.0f;
            
                break;
            }
            
            case SDL_MOUSEBUTTONDOWN:
            {
                if (event->button.button == SDL_BUTTON_LEFT)
                {
                    g_worldMapCursorParams = g_worldMapCursorParamsMouse;
                    ms_isMouseDown = true;
                }
            
                break;
            }
            
            case SDL_MOUSEBUTTONUP:
            {
                if (event->button.button == SDL_BUTTON_LEFT)
                {
                    g_isCursorActive = false;
                    ms_isMouseDown = false;
                }
            
                break;
            }
#endif
            case SDL_CONTROLLERTOUCHPADMOTION:
            {
                g_isCursorActive = true;

                if (ms_touchpadFingerCount > 1)
                {
                    g_isCursorActive = false;
                    break;
                }

                ms_cursorX = event->ctouchpad.x;
                ms_cursorY = event->ctouchpad.y;
                ms_cursorDeltaX = ms_cursorX - ms_cursorPrevX;
                ms_cursorDeltaY = ms_cursorY - ms_cursorPrevY;
                ms_cursorPrevX = ms_cursorX;
                ms_cursorPrevY = ms_cursorY;

                break;
            }

            case SDL_CONTROLLERTOUCHPADDOWN:
            {
                g_worldMapCursorParams = hid::g_inputDeviceExplicit == hid::EInputDeviceExplicit::DualSense
                    ? (WorldMapCursorParams)g_worldMapCursorParamsProspero
                    : (WorldMapCursorParams)g_worldMapCursorParamsOrbis;

                ms_touchpadFingerCount++;
                ms_cursorPrevX = event->ctouchpad.x;
                ms_cursorPrevY = event->ctouchpad.y;

                break;
            }

            case SDL_CONTROLLERTOUCHPADUP:
                g_isCursorActive = false;
                ms_touchpadFingerCount--;
                break;
        }

        return false;
    }
}
g_sdlEventListenerForInputPatches;

// -------------- COMMON --------------- //

static bool IsDPadThreshold(const SWA::SPadState* pPadState)
{
    if (Config::DisableDPadMovement)
        return false;

    return pPadState->IsDown(SWA::eKeyState_DpadUp)   ||
           pPadState->IsDown(SWA::eKeyState_DpadDown) ||
           pPadState->IsDown(SWA::eKeyState_DpadLeft) ||
           pPadState->IsDown(SWA::eKeyState_DpadRight);
}

static bool IsLeftStickThreshold(const SWA::SPadState* pPadState, double deadzone = 0)
{
    return sqrtl((pPadState->LeftStickHorizontal * pPadState->LeftStickHorizontal) +
        (pPadState->LeftStickVertical * pPadState->LeftStickVertical)) > deadzone;
}

static bool IsCursorThreshold(double deadzone = 0, bool isBelowThreshold = false)
{
    auto sqrt = sqrtl((g_worldMapCursorVelocityX * g_worldMapCursorVelocityX) +
        (g_worldMapCursorVelocityY * g_worldMapCursorVelocityY));

    if (isBelowThreshold)
        return sqrt < deadzone;

    return sqrt >= deadzone;
}

static void SetDPadAnalogDirectionX(PPCRegister& pPadState, PPCRegister& x, bool invert, float max = 1.0f)
{
    if (Config::DisableDPadMovement)
        return;

    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadLeft))
        x.f64 = invert ? max : -max;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadRight))
        x.f64 = invert ? -max : max;
}

static void SetDPadAnalogDirectionY(PPCRegister& pPadState, PPCRegister& y, bool invert, float max = 1.0f)
{
    if (Config::DisableDPadMovement)
        return;

    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadUp))
        y.f64 = invert ? -max : max;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadDown))
        y.f64 = invert ? max : -max;
}

// -------------- PLAYER --------------- //

void PostureDPadSupportMidAsmHook(PPCRegister& pPadState, PPCRegister& x, PPCRegister& y)
{
    SetDPadAnalogDirectionX(pPadState, x, false);
    SetDPadAnalogDirectionY(pPadState, y, false);
}

void PostureDPadSupportInvertYMidAsmHook(PPCRegister& pPadState, PPCRegister& x, PPCRegister& y)
{
    SetDPadAnalogDirectionX(pPadState, x, false);
    SetDPadAnalogDirectionY(pPadState, y, true);
}

void PostureDPadSupportXMidAsmHook(PPCRegister& pPadState, PPCRegister& x)
{
    SetDPadAnalogDirectionX(pPadState, x, false);
}

void PostureDPadSupportYMidAsmHook(PPCRegister& pPadState, PPCRegister& y)
{
    SetDPadAnalogDirectionY(pPadState, y, false);
}

void PostureSpaceHurrierDPadSupportXMidAsmHook(PPCRegister& pPadState, PPCVRegister& vector)
{
    if (Config::DisableDPadMovement)
        return;
    
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadLeft))
        vector.f32[3] = -1.0f;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadRight))
        vector.f32[3] = 1.0f;
}

void PostureSpaceHurrierDPadSupportYMidAsmHook(PPCRegister& pPadState, PPCVRegister& vector)
{
    if (Config::DisableDPadMovement)
        return;

    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadUp))
        vector.f32[3] = 1.0f;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadDown))
        vector.f32[3] = -1.0f;
}

void SetXButtonHomingMidAsmHook(PPCRegister& r1)
{
    auto pXButtonHoming = (bool*)(g_memory.base + r1.u32 + 0x63);

    *pXButtonHoming = !Config::HomingAttackOnJump;
}

bool IsHomingAttackOnJump()
{
    return Config::HomingAttackOnJump;
}

// ------------- WORLD MAP ------------- //

bool WorldMapDeadzoneMidAsmHook(PPCRegister& pPadState)
{
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (IsDPadThreshold(pGuestPadState) || IsLeftStickThreshold(pGuestPadState))
    {
        g_worldMapCursorVelocityX = 0;
        g_worldMapCursorVelocityY = 0;
    }
    else
    {
        SDLEventListenerForInputPatches::Update(App::s_deltaTime);

        /* Reduce noise if the cursor is resting in
           place, but allow much precise values for
           proper interpolation to zero. */
        if (IsCursorThreshold(0.05, true))
            return !g_isCursorActive;

        return IsCursorThreshold();
    }

    return IsDPadThreshold(pGuestPadState) || IsLeftStickThreshold(pGuestPadState, WORLD_MAP_ROTATE_DEADZONE);
}

bool WorldMapMagnetismMidAsmHook(PPCRegister& f0)
{
    if (IsCursorThreshold(f0.f64, true))
    {
        g_worldMapCursorVelocityX = 0;
        g_worldMapCursorVelocityY = 0;

        return true;
    }

    return false;
}

void WorldMapHidSupportXMidAsmHook(PPCRegister& pPadState, PPCRegister& x)
{
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (IsDPadThreshold(pGuestPadState))
    {
        SetDPadAnalogDirectionX(pPadState, x, false);
    }
    else if (fabs(g_worldMapCursorVelocityX) > 0)
    {
        x.f64 = -g_worldMapCursorVelocityX;
    }
}

// --------- WORLD MAP SHORTCUTS -------- //

/* 
   Bumper buttons swing the globe to the previous/next unlocked country. Y swaps
   the hovered country (or the open stage list) between its day and night stages
   straight away, then spins the sun round to match, since a country's time of
   day is whichever side of the globe the sun is lighting.
*/

enum EWorldMapCountry
{
    eWorldMapCountry_Apotos,
    eWorldMapCountry_Spagonia,
    eWorldMapCountry_ChunNan,
    eWorldMapCountry_Mazuri,
    eWorldMapCountry_Holoska,
    eWorldMapCountry_Shamar,
    eWorldMapCountry_Adabat,
    eWorldMapCountry_EmpireCity,
    eWorldMapCountry_Eggmanland
};

// Story order.
static constexpr int32_t g_worldMapCountryOrder[] =
{
    eWorldMapCountry_Apotos,
    eWorldMapCountry_Mazuri,
    eWorldMapCountry_Spagonia,
    eWorldMapCountry_ChunNan,
    eWorldMapCountry_Holoska,
    eWorldMapCountry_Shamar,
    eWorldMapCountry_EmpireCity,
    eWorldMapCountry_Adabat,
    eWorldMapCountry_Eggmanland
};

// The game's own degree to radian constant, slightly off from pi / 180.
constexpr float WORLD_MAP_DEG_TO_RAD = 0.017444444f;
constexpr float WORLD_MAP_PITCH_LIMIT = 80.0f;

// Seconds for bumper buttons to turn the globe to a country (plus a minimum),
// for a half turn or more; shorter turns scale down.
constexpr float WORLD_MAP_COUNTRY_FOCUS_MIN_DURATION = 0.25f;
constexpr float WORLD_MAP_COUNTRY_FOCUS_HALF_TURN_DURATION = 0.35f;

// Seconds to spin the sun a full turn (plus a minimum), it's placed at noon or 
// midnight over the country.
constexpr float WORLD_MAP_SUN_SPIN_MIN_DURATION = 0.15f;
constexpr float WORLD_MAP_SUN_SPIN_TURN_DURATION = 0.5f;

// Fallback if the sun didn't land where expected: hold X until the country is 
// this far past the boundary.
constexpr float WORLD_MAP_SUN_SPIN_MARGIN = 0.2f;
constexpr double WORLD_MAP_SUN_SPIN_TIMEOUT = 10.0;

// Button guide side margins (1280x720 space, the guide's default is 379). 
// Bumper buttons' wider prompt overlapped the native A/B footer on the globe 
// at the default, so it's nudged left.
constexpr float WORLD_MAP_PROMPT_SIDE_MARGINS = 379.0f;
constexpr float WORLD_MAP_COUNTRY_PROMPT_SIDE_MARGINS = 299.0f;

// Frames the shortcuts stay armed without a browse update, to drop them 
// if the world map is left.
constexpr int WORLD_MAP_SHORTCUT_KEEP_ALIVE = 2;

struct alignas(16) WorldMapVector
{
    be<float> X;
    be<float> Y;
    be<float> Z;
    be<float> W;
};

static struct
{
    bool IsActive;
    bool IsStarted;
    bool IsInterruptible;
    int32_t Country;
    int KeepAlive;
    float Time;
    float Duration;
    float StartPitch;
    float StartYaw;
    float TargetPitch;
    float TargetYaw;
}
g_worldMapCountryFocus;

static struct
{
    bool IsActive;
    bool IsFallback;
    bool IsDay;
    int32_t Country;
    uint32_t pWorldMap;
    int KeepAlive;
    float Time;
    float Duration;
    float StartYaw;
    float Distance;
    double FallbackTime;
}
g_worldMapSunSpin;

template<typename T>
static T WorldMapLoad(uint32_t address)
{
    return *(be<T>*)g_memory.Translate(address);
}

static uint32_t WorldMapFindFlag(uint32_t pWorldMap, int32_t country)
{
    auto begin = WorldMapLoad<uint32_t>(pWorldMap + 0x14);
    auto end = WorldMapLoad<uint32_t>(pWorldMap + 0x18);

    if (!begin)
        return 0;

    for (auto it = begin; it < end; it += 8)
    {
        auto pFlag = WorldMapLoad<uint32_t>(it);

        if (pFlag && WorldMapLoad<int32_t>(pFlag + 0x24) == country)
            return pFlag;
    }

    return 0;
}

static int32_t WorldMapFindMostFacingCountry(uint32_t pWorldMap)
{
    auto begin = WorldMapLoad<uint32_t>(pWorldMap + 0x14);
    auto end = WorldMapLoad<uint32_t>(pWorldMap + 0x18);

    int32_t result = -1;
    float maxFacing = -1.0f;

    if (!begin)
        return result;

    for (auto it = begin; it < end; it += 8)
    {
        auto pFlag = WorldMapLoad<uint32_t>(it);

        if (!pFlag)
            continue;

        auto facing = WorldMapLoad<float>(pFlag);

        if (facing > maxFacing)
        {
            maxFacing = facing;
            result = WorldMapLoad<int32_t>(pFlag + 0x24);
        }
    }

    return result;
}

// Normalised world space direction from the globe's centre to the country's flag.
static bool WorldMapGetFlagDirection(uint32_t pWorldMap, int32_t country, float& x, float& y, float& z)
{
    // Borrow the game's focus flag lookup (world space flag position) by pointing it 
    // at the country.
    auto pFocusCountry = (be<int32_t>*)g_memory.Translate(pWorldMap + 0x88);
    auto focusCountry = pFocusCountry->get();

    guest_stack_var<WorldMapVector> position;

    *pFocusCountry = country;
    GuestToHostFunction<void>(sub_82574188, position.get(), pWorldMap);
    *pFocusCountry = focusCountry;

    x = position->X;
    y = position->Y;
    z = position->Z;

    auto length = sqrtf(x * x + y * y + z * z);

    if (length < FLT_EPSILON)
        return false;

    x /= length;
    y /= length;
    z /= length;

    return true;
}

static bool WorldMapIsDayNightEnabled(uint32_t pWorldMap)
{
    // Day and night aren't split until later in the story.
    return *(uint8_t*)g_memory.Translate(pWorldMap + 0xA2) != 0;
}

static void WorldMapCancelShortcuts()
{
    g_worldMapCountryFocus.IsActive = false;
    g_worldMapSunSpin.IsActive = false;
}

// Next unlocked country (has a flag) from current in story order that the filter accepts, or -1.
template<typename TFilter>
static int32_t WorldMapFindNextCountry(uint32_t pWorldMap, int32_t current, int direction, TFilter filter)
{
    constexpr int32_t count = std::size(g_worldMapCountryOrder);

    auto index = std::find(g_worldMapCountryOrder, g_worldMapCountryOrder + count, current) - g_worldMapCountryOrder;

    if (index >= count)
        index = 0;

    for (int32_t i = 1; i < count; i++)
    {
        auto country = g_worldMapCountryOrder[((index + direction * i) % count + count) % count];

        if (WorldMapFindFlag(pWorldMap, country) && filter(country))
            return country;
    }

    return -1;
}

static bool WorldMapStartCountryFocus(uint32_t pWorldMap, int32_t country, bool isInterruptible)
{
    float x, y, z;

    if (!WorldMapGetFlagDirection(pWorldMap, country, x, y, z))
        return false;

    // The camera orbits the globe at (0, 0, distance) rotated by pitch around X
    // then by yaw around Y, so its direction is (cos(p) sin(y), -sin(p), cos(p) cos(y)).
    auto pitch = -asinf(std::clamp(y, -1.0f, 1.0f)) / WORLD_MAP_DEG_TO_RAD;
    auto yaw = atan2f(x, z) / WORLD_MAP_DEG_TO_RAD;

    g_worldMapCountryFocus.IsActive = true;
    g_worldMapCountryFocus.IsStarted = false;
    g_worldMapCountryFocus.IsInterruptible = isInterruptible;
    g_worldMapCountryFocus.Country = country;
    g_worldMapCountryFocus.KeepAlive = WORLD_MAP_SHORTCUT_KEEP_ALIVE;
    g_worldMapCountryFocus.TargetPitch = std::clamp(pitch, -WORLD_MAP_PITCH_LIMIT, WORLD_MAP_PITCH_LIMIT);
    g_worldMapCountryFocus.TargetYaw = yaw;

    return true;
}

static void WorldMapFocusCountry(uint32_t pWorldMap, int32_t current, int direction)
{
    if (current < 0)
        current = WorldMapFindMostFacingCountry(pWorldMap);

    if (current < 0)
        return;

    auto target = WorldMapFindNextCountry(pWorldMap, current, direction, [](int32_t) { return true; });

    if (target >= 0 && WorldMapStartCountryFocus(pWorldMap, target, true))
        Game_PlaySound("sys_worldmap_cursor");
}

static bool WorldMapIsCountryDay(uint32_t pWorldMap, int32_t country, bool& isDay)
{
    auto pFlag = WorldMapFindFlag(pWorldMap, country);

    if (!pFlag)
        return false;

    auto sunlight = WorldMapLoad<float>(pFlag + 0x04);

    // Not a dot product (501) while day and night aren't split yet.
    if (fabs(sunlight) > 1.01f)
        return false;

    isDay = sunlight >= 0.0f;

    return true;
}

static bool WorldMapStartSunSpin(uint32_t pWorldMap, int32_t country, bool isDay)
{
    auto pSun = WorldMapLoad<uint32_t>(pWorldMap + 0x20);

    float x, y, z;

    if (!pSun || !WorldMapGetFlagDirection(pWorldMap, country, x, y, z))
        return false;

    // The poles are lit (or not) whatever the sun's yaw is.
    if (sqrtf(x * x + z * z) < 0.01f)
        return false;

    //The sun's direction is (sin(yaw), 0, cos(yaw)), so a flag's sunlight peaks
    // with the sun over its longitude (noon) and bottoms out opposite (midnight).
    auto longitude = atan2f(x, z) / WORLD_MAP_DEG_TO_RAD;
    auto targetYaw = isDay ? longitude : longitude + 180.0f;
    auto startYaw = WorldMapLoad<float>(pSun + 0x94);

    // The sun's yaw counts down, so keep spinning that way.
    auto distance = fmodf(startYaw - targetYaw, 360.0f);

    if (distance < 0.0f)
        distance += 360.0f;

    auto& spin = g_worldMapSunSpin;

    spin.IsActive = true;
    spin.IsFallback = false;
    spin.IsDay = isDay;
    spin.Country = country;
    spin.pWorldMap = pWorldMap;
    spin.KeepAlive = WORLD_MAP_SHORTCUT_KEEP_ALIVE;
    spin.Time = 0.0f;
    spin.Duration = WORLD_MAP_SUN_SPIN_MIN_DURATION + WORLD_MAP_SUN_SPIN_TURN_DURATION * (distance / 360.0f);
    spin.StartYaw = startYaw;
    spin.Distance = distance;
    spin.FallbackTime = 0.0;

    return true;
}

static void WorldMapUpdateSunSpin(uint32_t pWorldMap)
{
    auto& spin = g_worldMapSunSpin;

    if (!spin.IsActive)
        return;

    if (pWorldMap != spin.pWorldMap)
    {
        spin.IsActive = false;
        return;
    }

    spin.KeepAlive = WORLD_MAP_SHORTCUT_KEEP_ALIVE;

    // Still spinning to noon/midnight.
    if (!spin.IsFallback && spin.Time < spin.Duration)
        return;

    auto pFlag = WorldMapFindFlag(pWorldMap, spin.Country);

    if (!pFlag)
    {
        spin.IsActive = false;
        return;
    }

    auto sunlight = WorldMapLoad<float>(pFlag + 0x04);
    auto isAgreeing = (sunlight >= 0.0f) == spin.IsDay;

    if (!spin.IsFallback)
    {
        // Landed on the right side, the page and the globe agree now.
        if (isAgreeing)
        {
            spin.IsActive = false;
            return;
        }

        // Didn't land where expected, fast forward the sun like holding X until it does.
        spin.IsFallback = true;
        spin.FallbackTime = 0.0;

        return;
    }

    spin.FallbackTime += App::s_deltaTime;

    if ((isAgreeing && fabs(sunlight) >= WORLD_MAP_SUN_SPIN_MARGIN) || spin.FallbackTime > WORLD_MAP_SUN_SPIN_TIMEOUT)
        spin.IsActive = false;
}

static void WorldMapToggleCountryTime(uint32_t pWorldMap, int32_t country)
{
    // Eggmanland is always day.
    if (country < 0 || country == eWorldMapCountry_Eggmanland || !WorldMapIsDayNightEnabled(pWorldMap))
        return;

    auto& spin = g_worldMapSunSpin;

    bool isDay;

    // Pressed again mid-spin, go back.
    if (spin.IsActive && spin.pWorldMap == pWorldMap && spin.Country == country)
        isDay = spin.IsDay;
    else if (!WorldMapIsCountryDay(pWorldMap, country, isDay))
        return;

    // The info panel reads the time, which reports the new time while the sun spins
    // so it switches pages (and plays the sunrise/sunset sound) on the next frame.
    WorldMapStartSunSpin(pWorldMap, country, !isDay);
}

static struct
{
    uint32_t pStageList;
    int32_t Country;
    bool IsDay;
    bool CanToggle;
}
g_worldMapStageListToggle;

static bool WorldMapIsStageListIdle(uint32_t pStageList)
{
    if (!pStageList)
        return false;

    auto flags = WorldMapLoad<uint32_t>(pStageList + 0x04);

    return (flags & 0x04) && !(flags & 0x39);
}

// Anything cleared to list, an empty list isn't safe to show.
static bool WorldMapHasClearedStages(uint32_t pStageList, int32_t country, bool isDay)
{
    return (GuestToHostFunction<uint32_t>(sub_82570890, pStageList, country, isDay) & 0xFF) != 0;
}

static bool WorldMapCanToggleStageList(uint32_t pWorldMap, uint32_t pStageList)
{
    if (!WorldMapIsStageListIdle(pStageList))
        return false;

    auto country = WorldMapLoad<int32_t>(pStageList + 0x10);
    auto isDay = *(uint8_t*)g_memory.Translate(pStageList + 0x14) != 0;

    if (country < 0 || country == eWorldMapCountry_Eggmanland || !WorldMapIsDayNightEnabled(pWorldMap))
        return false;

    auto& cache = g_worldMapStageListToggle;

    // Cached, the prompt asks every frame.
    if (cache.pStageList != pStageList || cache.Country != country || cache.IsDay != isDay)
    {
        cache.pStageList = pStageList;
        cache.Country = country;
        cache.IsDay = isDay;
        cache.CanToggle = WorldMapHasClearedStages(pStageList, country, !isDay);
    }

    return cache.CanToggle;
}

static void WorldMapRebuildStageList(uint32_t pStageList, int32_t country, bool isDay)
{
    // Rebuilds the list in place (it's already open, so it doesn't reopen).
    GuestToHostFunction<void>(sub_82570610, pStageList, country, isDay);

    // The rows (stage name patterns, count, scroll bar) are only laid out once the list
    // box finishes opening (vtable slot 0x14), and otherwise just redrawn on scroll, so
    // lay them out again for the new entries. It resets all 7 rows first, so rows past
    // the end of a shorter list are blanked too. */
    if (auto pListBox = WorldMapLoad<uint32_t>(pStageList + 0x38))
        GuestToHostFunction<void>(sub_8256F1A8, pListBox);
}

static void WorldMapToggleStageList(uint32_t pWorldMap, uint32_t pStageList)
{
    if (!WorldMapCanToggleStageList(pWorldMap, pStageList))
        return;

    auto country = WorldMapLoad<int32_t>(pStageList + 0x10);
    auto isDay = *(uint8_t*)g_memory.Translate(pStageList + 0x14) != 0;

    if (!WorldMapStartSunSpin(pWorldMap, country, !isDay))
        return;

    WorldMapRebuildStageList(pStageList, country, !isDay);

    Game_PlaySound(isDay ? "sys_worldmap_sunset" : "sys_worldmap_sunrise");
}

static void WorldMapCycleStageList(uint32_t pWorldMap, uint32_t pStageList, int direction)
{
    if (!WorldMapIsStageListIdle(pStageList))
        return;

    auto current = WorldMapLoad<int32_t>(pStageList + 0x10);
    auto isDay = *(uint8_t*)g_memory.Translate(pStageList + 0x14) != 0;
    auto isDayNightEnabled = WorldMapIsDayNightEnabled(pWorldMap);
    auto targetIsDay = isDay;

    // Keep the page's time of day where something's cleared then, otherwise 
    // show the country's other time.
    auto target = WorldMapFindNextCountry(pWorldMap, current, direction, [&](int32_t country)
    {
        // Eggmanland is always day.
        if (country == eWorldMapCountry_Eggmanland)
        {
            targetIsDay = true;
            return WorldMapHasClearedStages(pStageList, country, true);
        }

        targetIsDay = isDay;

        if (WorldMapHasClearedStages(pStageList, country, isDay))
            return true;

        targetIsDay = !isDay;

        return isDayNightEnabled && WorldMapHasClearedStages(pStageList, country, !isDay);
    });

    if (target < 0)
        return;

    // Spin the sun round unless the country's already there (and it's not mid-spin for another).
    if (isDayNightEnabled && target != eWorldMapCountry_Eggmanland)
    {
        bool isTargetDay;

        if (g_worldMapSunSpin.IsActive || !WorldMapIsCountryDay(pWorldMap, target, isTargetDay) || isTargetDay != targetIsDay)
            WorldMapStartSunSpin(pWorldMap, target, targetIsDay);
    }

    WorldMapRebuildStageList(pStageList, target, targetIsDay);

    // Turn the globe to it too. The stick scrolls the list here, so it doesn't interrupt.
    WorldMapStartCountryFocus(pWorldMap, target, false);

    Game_PlaySound("sys_worldmap_cursor");
}

static bool g_worldMapPromptsOpen;
static int g_worldMapPromptsKeepAlive;

static void WorldMapClosePrompts()
{
    if (g_worldMapPromptsOpen)
    {
        ButtonGuide::Close();
        g_worldMapPromptsOpen = false;
    }

    // Progress may change before the world map is back, so check the stage list again then.
    g_worldMapStageListToggle = {};
}

static void WorldMapShowPrompt(const char* name, EButtonIcon icon, float sideMargins = WORLD_MAP_PROMPT_SIDE_MARGINS)
{
    // Other menus own the button guide while they're up.
    auto isOtherMenuVisible = g_isPauseMenuVisible.load(std::memory_order_relaxed) || OptionsMenu::s_isVisible ||
        AchievementMenu::s_isVisible || MessageWindow::s_isVisible || App::s_isLoading || !*SWA::SGlobals::ms_IsRenderHud;

    if (isOtherMenuVisible)
    {
        WorldMapClosePrompts();
        return;
    }

    ButtonGuide::Open(Button(name, FLT_MAX, icon, EButtonAlignment::Left, EFontQuality::Low));
    ButtonGuide::SetSideMargins(sideMargins);

    g_worldMapPromptsOpen = true;
    g_worldMapPromptsKeepAlive = WORLD_MAP_SHORTCUT_KEEP_ALIVE;
}

void InputPatches::Update()
{
    // The world map stopped refreshing its prompts (intro, going somewhere, or left).
    if (g_worldMapPromptsOpen && --g_worldMapPromptsKeepAlive < 0)
        WorldMapClosePrompts();
}

static void WorldMapUpdateShortcuts(uint32_t pState)
{
    auto pContext = WorldMapLoad<uint32_t>(pState + 0x08);
    auto pWorldMap = pContext ? WorldMapLoad<uint32_t>(pContext + 0x178) : 0;

    if (!pWorldMap)
    {
        WorldMapCancelShortcuts();
        WorldMapClosePrompts();
        return;
    }

    auto subState = WorldMapLoad<int32_t>(pState + 0x94);

    WorldMapUpdateSunSpin(pWorldMap);

    auto isInputAllowed = !WorldMapLoad<uint32_t>(pState + 0x88) && !OptionsMenu::s_isVisible && hid::IsInputAllowed();
    auto isBrowsing = isInputAllowed && subState == 1 && !*(uint8_t*)g_memory.Translate(pContext + 0x180);

    auto isInStageList = isInputAllowed && subState == 3;

    if (g_worldMapCountryFocus.IsActive)
    {
        if (isBrowsing || isInStageList)
            g_worldMapCountryFocus.KeepAlive = WORLD_MAP_SHORTCUT_KEEP_ALIVE;
        else
            g_worldMapCountryFocus.IsActive = false;
    }

    auto hoveredCountry = WorldMapLoad<int32_t>(pState + 0xBC);
    auto pStageList = WorldMapLoad<uint32_t>(pState + 0x80);

    // Set once travelling to a town or stage has been decided.
    auto isLeaving = *(uint8_t*)g_memory.Translate(pState + 0xD0) != 0;

    if (isLeaving)
    {
        WorldMapClosePrompts();
    }
    else if (isBrowsing && (WorldMapLoad<uint32_t>(pWorldMap + 0x18) - WorldMapLoad<uint32_t>(pWorldMap + 0x14)) / 8 > 1)
    {
        WorldMapShowPrompt("WorldMap_Country", EButtonIcon::LBRB, WORLD_MAP_COUNTRY_PROMPT_SIDE_MARGINS);
    }
    else if (isInStageList && WorldMapCanToggleStageList(pWorldMap, pStageList))
    {
        WorldMapShowPrompt("WorldMap_DayNight", EButtonIcon::Y);
    }
    else
    {
        WorldMapClosePrompts();
    }

    auto pInputState = SWA::CInputState::GetInstance();

    if (!isInputAllowed || !pInputState)
        return;

    auto& rPadState = pInputState->GetPadState();

    if (isBrowsing)
    {
        auto isPrevious = rPadState.IsTapped(SWA::eKeyState_LeftBumper);
        auto isNext = rPadState.IsTapped(SWA::eKeyState_RightBumper);

        if (isPrevious != isNext)
        {
            auto current = g_worldMapCountryFocus.IsActive ? g_worldMapCountryFocus.Country : hoveredCountry;

            WorldMapFocusCountry(pWorldMap, current, isNext ? 1 : -1);
        }
        else if (rPadState.IsTapped(SWA::eKeyState_Y) && !g_worldMapCountryFocus.IsActive)
        {
            WorldMapToggleCountryTime(pWorldMap, hoveredCountry);
        }
    }
    else if (isInStageList)
    {
        auto isPrevious = rPadState.IsTapped(SWA::eKeyState_LeftBumper);
        auto isNext = rPadState.IsTapped(SWA::eKeyState_RightBumper);

        if (isPrevious != isNext)
            WorldMapCycleStageList(pWorldMap, pStageList, isNext ? 1 : -1);
        else if (rPadState.IsTapped(SWA::eKeyState_Y))
            WorldMapToggleStageList(pWorldMap, pStageList);
    }
}

static void WorldMapUpdateCountryFocus(SWA::CWorldMapCamera* pWorldMapCamera)
{
    auto& focus = g_worldMapCountryFocus;

    if (!focus.IsActive)
        return;

    auto isUserMoving = focus.IsInterruptible && g_isCursorActive;

    if (auto pInputState = SWA::CInputState::GetInstance(); pInputState && focus.IsInterruptible)
    {
        auto& rPadState = pInputState->GetPadState();

        isUserMoving |= IsDPadThreshold(&rPadState) || IsLeftStickThreshold(&rPadState, WORLD_MAP_CURSOR_DEADZONE);
    }

    if (isUserMoving || focus.KeepAlive-- <= 0)
    {
        focus.IsActive = false;
        return;
    }

    if (!focus.IsStarted)
    {
        focus.IsStarted = true;
        focus.Time = 0.0f;
        focus.StartPitch = pWorldMapCamera->m_Pitch;
        focus.StartYaw = pWorldMapCamera->m_Yaw;

        // Yaw is unbounded, so take the shortest way around.
        focus.TargetYaw = focus.StartYaw + remainderf(focus.TargetYaw - focus.StartYaw, 360.0f);

        auto distance = std::max(fabs(focus.TargetPitch - focus.StartPitch), fabs(focus.TargetYaw - focus.StartYaw));

        focus.Duration = WORLD_MAP_COUNTRY_FOCUS_MIN_DURATION + WORLD_MAP_COUNTRY_FOCUS_HALF_TURN_DURATION * std::min(distance / 180.0f, 1.0f);
    }

    focus.Time += App::s_deltaTime;

    auto t = std::min(focus.Time / focus.Duration, 1.0f);
    auto ease = t * t * (3.0f - 2.0f * t);

    pWorldMapCamera->m_Pitch = focus.StartPitch + (focus.TargetPitch - focus.StartPitch) * ease;
    pWorldMapCamera->m_Yaw = focus.StartYaw + (focus.TargetYaw - focus.StartYaw) * ease;

    g_worldMapCursorVelocityX = 0;
    g_worldMapCursorVelocityY = 0;

    if (t >= 1.0f)
        focus.IsActive = false;
}

// SWA::CTitleStateWorldMap: browsing the globe.
PPC_FUNC_IMPL(__imp__sub_82589F38);
PPC_FUNC(sub_82589F38)
{
    auto pState = ctx.r3.u32;

    __imp__sub_82589F38(ctx, base);

    WorldMapUpdateShortcuts(pState);
}

// SWA::CTitleStateWorldMap: country window.
PPC_FUNC_IMPL(__imp__sub_825898B8);
PPC_FUNC(sub_825898B8)
{
    auto pState = ctx.r3.u32;

    __imp__sub_825898B8(ctx, base);

    WorldMapUpdateShortcuts(pState);
}

// SWA::CTitleStateWorldMap: stage list.
PPC_FUNC_IMPL(__imp__sub_82589AE0);
PPC_FUNC(sub_82589AE0)
{
    auto pState = ctx.r3.u32;

    __imp__sub_82589AE0(ctx, base);

    WorldMapUpdateShortcuts(pState);
}

// SWA::CTitleStateWorldMap: go to the stage picked in the stage list.
PPC_FUNC_IMPL(__imp__sub_82588E78);
PPC_FUNC(sub_82588E78)
{
    // The stage's country (where you return to, and at what time) is taken from the
    // globe's cursor, which only matches the list while its country is left alone, so
    // take the list's, in case bumper buttons changed it or the camera's still turning.
    auto pState = ctx.r3.u32;
    auto pContext = WorldMapLoad<uint32_t>(pState + 0x08);
    auto pWorldMap = pContext ? WorldMapLoad<uint32_t>(pContext + 0x178) : 0;
    auto pStageList = WorldMapLoad<uint32_t>(pState + 0x80);

    if (pWorldMap && pStageList)
    {
        auto country = WorldMapLoad<int32_t>(pStageList + 0x10);

        if (country >= 0 && country <= eWorldMapCountry_Eggmanland)
        {
            *(be<int32_t>*)g_memory.Translate(pState + 0xA4) = country;

            // Goes through the hook below, so it's the time the sun is spinning to.
            *(uint8_t*)g_memory.Translate(pState + 0xA0) = GuestToHostFunction<uint32_t>(sub_8256F780, pWorldMap, country) & 0xFF;
        }
    }

    __imp__sub_82588E78(ctx, base);
}

// World map object: advances the sun, f1 is delta time, r6 is "X held" (fast forward).
PPC_FUNC_IMPL(__imp__sub_82578980);
PPC_FUNC(sub_82578980)
{
    auto& spin = g_worldMapSunSpin;
    auto pWorldMap = ctx.r4.u32;

    // The world map stopped updating (left it), drop the spin so its time override doesn't linger.
    if (spin.IsActive && spin.pWorldMap == pWorldMap && spin.KeepAlive-- <= 0)
        spin.IsActive = false;

    if (!spin.IsActive || spin.pWorldMap != pWorldMap)
    {
        __imp__sub_82578980(ctx, base);
        return;
    }

    if (spin.IsFallback)
    {
        ctx.r6.u64 = 1;
    }
    else if (auto pSun = WorldMapLoad<uint32_t>(pWorldMap + 0x20))
    {
        spin.Time += ctx.f1.f64;

        auto t = std::min(spin.Time / spin.Duration, 1.0f);
        auto ease = t * t * (3.0f - 2.0f * t);

        // The game only wraps the yaw below -360, so keep it within a turn.
        *(be<float>*)g_memory.Translate(pSun + 0x94) = fmodf(spin.StartYaw - spin.Distance * ease, 360.0f);
    }

    __imp__sub_82578980(ctx, base);

    // Plays the sun's whoosh, as when holding X.
    *(uint8_t*)g_memory.Translate(pWorldMap + 0xA3) = 1;
}

// World map object: is the country on the sunlit side (day)?
PPC_FUNC_IMPL(__imp__sub_8256F780);
PPC_FUNC(sub_8256F780)
{
    auto& spin = g_worldMapSunSpin;

    if (spin.IsActive && spin.pWorldMap == ctx.r3.u32 && spin.Country == ctx.r4.s32)
    {
        ctx.r3.u64 = spin.IsDay;
        return;
    }

    __imp__sub_8256F780(ctx, base);
}

void WorldMapHidSupportYMidAsmHook(PPCRegister& pPadState, PPCRegister& y)
{
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (IsDPadThreshold(pGuestPadState))
    {
        SetDPadAnalogDirectionY(pPadState, y, false);
    }
    else if (fabs(g_worldMapCursorVelocityY) > 0)
    {
        y.f64 = g_worldMapCursorVelocityY;
    }
}

// SWA::CWorldMapCamera::Update
PPC_FUNC_IMPL(__imp__sub_82486968);
PPC_FUNC(sub_82486968)
{
    auto pWorldMapCamera = (SWA::CWorldMapCamera*)g_memory.Translate(ctx.r3.u32);

    WorldMapUpdateCountryFocus(pWorldMapCamera);

    // Reset vertical velocity if maximum pitch reached.
    if (fabs(pWorldMapCamera->m_Pitch) >= 80.0f)
        g_worldMapCursorVelocityY = 0;

    __imp__sub_82486968(ctx, base);
}


// World Map cursor move hook.
PPC_FUNC(sub_8256C938)
{
    auto pWorldMapCursor = (SWA::CWorldMapCursor*)g_memory.Translate(ctx.r3.u32);

    pWorldMapCursor->m_IsCursorMoving = g_isCursorActive && IsCursorThreshold(1.0);

    if (ctx.r4.u8)
    {
        pWorldMapCursor->m_LeftStickVertical = 0;
        pWorldMapCursor->m_LeftStickHorizontal = 0;
    }
    else if (auto pInputState = SWA::CInputState::GetInstance())
    {
        auto& rPadState = pInputState->GetPadState();

        pWorldMapCursor->m_LeftStickVertical = rPadState.LeftStickVertical;
        pWorldMapCursor->m_LeftStickHorizontal = rPadState.LeftStickHorizontal;

        if (!Config::DisableDPadMovement)
        {
            if (rPadState.IsDown(SWA::eKeyState_DpadUp))
                pWorldMapCursor->m_LeftStickVertical = 1.0f;
    
            if (rPadState.IsDown(SWA::eKeyState_DpadDown))
                pWorldMapCursor->m_LeftStickVertical = -1.0f;
    
            if (rPadState.IsDown(SWA::eKeyState_DpadLeft))
                pWorldMapCursor->m_LeftStickHorizontal = -1.0f;
    
            if (rPadState.IsDown(SWA::eKeyState_DpadRight))
                pWorldMapCursor->m_LeftStickHorizontal = 1.0f;
        }

        if (sqrtl((pWorldMapCursor->m_LeftStickHorizontal * pWorldMapCursor->m_LeftStickHorizontal) +
            (pWorldMapCursor->m_LeftStickVertical * pWorldMapCursor->m_LeftStickVertical)) > WORLD_MAP_ROTATE_DEADZONE)
        {
            pWorldMapCursor->m_IsCursorMoving = true;
        }
    }
}
