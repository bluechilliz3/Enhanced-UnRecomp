#include <user/config.h>
#include <kernel/function.h>
#include <os/media.h>
#include <os/version.h>
#include <patches/audio_patches.h>
#include <api/SWA.h>

int AudioPatches::m_isAttenuationSupported = -1;

static be<float>* GetVolume(bool isMusic = true)
{
    auto ppUnkClass = (be<uint32_t>*)g_memory.Translate(0x83362FFC);

    if (!ppUnkClass->get())
        return nullptr;

    // NOTE (Hyper): This is fine, trust me. See 0x82E58728.
    return (be<float>*)g_memory.Translate(4 * ((int)isMusic + 0x1C) + ((be<uint32_t>*)g_memory.Translate(ppUnkClass->get() + 4))->get());
}

bool AudioPatches::CanAttenuate()
{
#if _WIN32
    if (m_isAttenuationSupported >= 0)
        return m_isAttenuationSupported;

    auto version = os::version::GetOSVersion();

    m_isAttenuationSupported = version.Major >= 10 && version.Build >= 17763;

    return m_isAttenuationSupported;
#else
    return false;
#endif
}

void AudioPatches::Update(float deltaTime)
{
    auto pMusicVolume = GetVolume();
    auto pEffectsVolume = GetVolume(false);

    if (!pMusicVolume || !pEffectsVolume)
        return;

    if (Config::MusicAttenuation && CanAttenuate())
    {
        auto time = 1.0f - expf(2.5f * -deltaTime);

        if (os::media::IsExternalMediaPlaying())
        {
            *pMusicVolume = std::lerp(*pMusicVolume, 0.0f, time);
        }
        else
        {
            *pMusicVolume = std::lerp(*pMusicVolume, Config::MusicVolume, time);
        }
    }
    else
    {
        *pMusicVolume = Config::MusicVolume;
    }

    *pEffectsVolume = Config::EffectsVolume;
}

// Stub volume setter.
GUEST_FUNCTION_STUB(sub_82E58728);

// HORRIBLE HACK ZONE
// The options menu uses se_system_worldmap.csb, which is stored in Title.ar.00.
// This archive gets unloaded in stages, which causes sounds to not play in the options menu.
// To solve this, once the CSB gets loaded at title, we'll keep it PERMANENTLY loaded.
// This'll make the SFX not work if Title never gets loaded, but that'll only happen when quick booting to stages.
static bool g_loadedWorldMapCsb;

bool MakeCueSheetDataMidAsmHook(PPCRegister& r31)
{
    uint8_t* base = g_memory.base;
    uint32_t str = PPC_LOAD_U32(r31.u32);

    if (str != NULL && strcmp(reinterpret_cast<const char*>(base + str), "se_system_worldmap") == 0)
    {
        if (!g_loadedWorldMapCsb)
        {
            g_loadedWorldMapCsb = true;
            return false; // Allow load for the first and only time.
        }

        // Already loaded before, skip all the loading and name assignment code.
        // Not assigning the name prevents it from unloading the CSB file.
        return true;
    }

    return false;
}

// DisableMusicRestartOnDeath.
//
// A flag that the sound manager's handler consumes.
static int32_t g_skipNextBgmRestartParam = -1; // -1 = no skip pending

PPC_FUNC_IMPL(__imp__sub_827B62E0);
PPC_FUNC(sub_827B62E0)
{
    // r4 = restart command: byte +29 gates the broadcast, +24 = message param.
    if (Config::DisableMusicRestartOnDeath &&
        ctx.r4.u32 && PPC_LOAD_U8(ctx.r4.u32 + 29) != 0 && PPC_LOAD_U32(ctx.r4.u32 + 24) == 1)
    {
        g_skipNextBgmRestartParam = 1;
    }

    __imp__sub_827B62E0(ctx, base);
}

// Eggmanland's BGM (CSoundBGMActEggman) plays the Sonic and Werehog versions of
// its theme as two separate tracks, and picks one from the player's form whenever
// it (re)starts. When a restart puts the player back in the other form, the
// player switch manager tells the sound manager silently (MsgFinishSonic /
// MsgFinishEvil with +0x18 set: only the form flag changes) and leaves it to the
// restart's BGM reset to start the right track. That switch finishes before the
// restart is broadcast, so here the flag already holds the new form.
//
// Returns true when the track playing doesn't match the form, i.e. skipping
// the reset would leave the other form's music playing.
static bool IsEggmanlandTrackForOtherForm(uint8_t* base, uint32_t pSoundManager)
{
    constexpr uint32_t CSoundBGMActEggmanVftable = 0x820C4C84;

    auto pBgmHolder = PPC_LOAD_U32(pSoundManager + 0xC4);

    if (!pBgmHolder)
        return false;

    auto pBgmNode = PPC_LOAD_U32(pBgmHolder + 8);

    if (!pBgmNode)
        return false;

    auto pBgm = PPC_LOAD_U32(pBgmNode + 4);

    if (!pBgm || PPC_LOAD_U32(pBgm) != CSoundBGMActEggmanVftable)
        return false;

    auto pImpl = PPC_LOAD_U32(pBgm + 0x9C);

    // +0x48 = state; 2 = playing a track. In the other states it hasn't picked
    // one yet (or is already fading to the other form's), so nothing is stale.
    if (!pImpl || PPC_LOAD_U32(pImpl + 0x48) != 2)
        return false;

    // +0x98 = form flag (1 = Werehog). The Sonic track's handle is at impl+0x0,
    // the Werehog track's at impl+0x8; only the playing one is set.
    auto isWerehog = PPC_LOAD_U8(pBgm + 0x98) != 0;

    return PPC_LOAD_U32(pImpl + (isWerehog ? 0x8 : 0x0)) == 0;
}

// Sound manager's MsgRestartStage handler. All it does is reset the BGM,
// so returning early only keeps the music going.
PPC_FUNC_IMPL(__imp__sub_82B43558);
PPC_FUNC(sub_82B43558)
{
    if (g_skipNextBgmRestartParam >= 0)
    {
        auto param = uint32_t(g_skipNextBgmRestartParam);
        g_skipNextBgmRestartParam = -1;

        // If the restart changed the player's form in Eggmanland, let the reset
        // through so it starts the new form's track, like vanilla.
        if (PPC_LOAD_U32(ctx.r4.u32 + 24) == param && !IsEggmanlandTrackForOtherForm(base, ctx.r3.u32))
            return;
    }

    __imp__sub_82B43558(ctx, base);
}

// DisableMusicRestartOnDeath, pause menu Restart and mission-failed Retry.
//
// Holding left stick click, Back (or the DualSense touchpad, which maps to Back)
// or X / Square while picking Restart in the pause menu, or Retry on the
// mission-failed menu, also keeps the music.
//
// How a menu restart reaches the BGM:
//   1. Pause menu: CHudPause stores eActionType_Restart; CGameModeStage's pause
//      state (sub_8253B7C0) collects it once through sub_824AA208.
//      Mission-failed menu: CStateMissionRestartMenu's update (sub_8253DFF0)
//      reads Retry, sets CGameModeStage+0x1D0 ("restart requested").
//      Both then push CStateRestartWaitFadeOut with fromMenu = 1.
//   2. After the fade, that state calls CGameModeStage vtable slot 20
//      (sub_8253E718, the restart, shared with death respawns with fromMenu = 0).
//   3. Slot 20 calls slot 21 (sub_82539A18), which sends MsgRemakeAllSetObject:
//      param 0 for a menu restart, param 1 for a death respawn. Both are gated
//      (+29 = 1), so sub_827B62E0 broadcasts MsgRestartStage with that param and
//      the sound manager's handler above resets the BGM.
// So a menu restart is the same latch with param 0, armed in slot 21.
//
// fromMenu = 1 is also used by restart requests that don't come from a menu
// (CGameModeStage request handler sub_8253EE10, cases 0 and 15). So the buttons
// are checked only at the two menus' choice points, and the result is used up
// by the next slot 21 call.
static bool g_isKeepMusicButtonHeldOnConfirm; // held on the frame A confirmed the latest menu choice
static bool g_keepMusicOnMenuRestart;         // pause Restart / mission Retry picked with a button held

bool AudioPatches::IsKeepMusicButtonHeld()
{
    auto pInputState = SWA::CInputState::GetInstance();

    if (!pInputState)
        return false;

    constexpr uint32_t keepMusicButtons = SWA::eKeyState_LeftStick | SWA::eKeyState_Select | SWA::eKeyState_X;

    return (pInputState->GetPadState().DownState & keepMusicButtons) != 0;
}

void AudioPatches::OnMenuConfirm()
{
    g_isKeepMusicButtonHeldOnConfirm = IsKeepMusicButtonHeld();
}

// Accept the button if it was held when A was pressed, or is still held now.
// Uses up the A-press sample.
static bool ShouldKeepMusicOnMenuRestart()
{
    auto isHeld = g_isKeepMusicButtonHeldOnConfirm || AudioPatches::IsKeepMusicButtonHeld();

    g_isKeepMusicButtonHeldOnConfirm = false;

    return Config::DisableMusicRestartOnDeath && isHeld;
}

// CHudPause: returns true once the chosen action is ready, writing it to r4.
// Only caller is CGameModeStage's pause state (sub_8253B7C0).
PPC_FUNC_IMPL(__imp__sub_824AA208);
PPC_FUNC(sub_824AA208)
{
    auto pAction = ctx.r4.u32;

    __imp__sub_824AA208(ctx, base);

    if ((ctx.r3.u32 & 0xFF) == 0)
        return;

    auto keepMusic = ShouldKeepMusicOnMenuRestart();

    g_keepMusicOnMenuRestart = keepMusic && PPC_LOAD_U32(pAction) == SWA::eActionType_Restart;
}

// CGameModeStage::CStateMissionRestartMenu update. Runs every frame while the
// mission-failed menu is up. On Retry it sets and pushes the restart fade;
// on Quit it leaves the stage.
PPC_FUNC_IMPL(__imp__sub_8253DFF0);
PPC_FUNC(sub_8253DFF0)
{
    auto pGameModeStage = PPC_LOAD_U32(ctx.r3.u32 + 8);
    auto wasRestartRequested = pGameModeStage && PPC_LOAD_U8(pGameModeStage + 0x1D0) != 0;

    if (auto pInputState = SWA::CInputState::GetInstance())
    {
        if (pInputState->GetPadState().IsTapped(SWA::eKeyState_A))
            AudioPatches::OnMenuConfirm();
    }

    __imp__sub_8253DFF0(ctx, base);

    if (pGameModeStage && !wasRestartRequested && PPC_LOAD_U8(pGameModeStage + 0x1D0) != 0)
        g_keepMusicOnMenuRestart = ShouldKeepMusicOnMenuRestart();
}

// Whether the restart in progress keeps the music. Decided in slot 21, then
// published by IsRestartKeepingMusic() while CStateReviveWaitLoad is active.
static bool g_willRestartKeepMusic;
static bool g_isRestartKeepingMusic;

// CGameModeStage vtable slot 21: sends the restart's MsgRemakeAllSetObject.
// r4 = fromMenu (pause Restart, mission-failed Retry, or a non-menu request).
// Slot 20 calls it, then pushes CStateReviveWaitLoad.
PPC_FUNC_IMPL(__imp__sub_82539A18);
PPC_FUNC(sub_82539A18)
{
    auto isFromMenu = (ctx.r4.u32 & 0xFF) != 0;

    if (g_keepMusicOnMenuRestart && isFromMenu)
        g_skipNextBgmRestartParam = 0;

    // A death respawn always keeps the music with the option on; a menu restart
    // only with a button held. Either way, not if Eggmanland's music has to
    // switch to the other form's track (the player switch is done by now).
    g_willRestartKeepMusic = (isFromMenu ? g_keepMusicOnMenuRestart : bool(Config::DisableMusicRestartOnDeath));

    if (g_willRestartKeepMusic)
    {
        auto pGameDocument = SWA::CGameDocument::GetInstance();
        auto pSoundAdministrator = pGameDocument ? pGameDocument->m_pMember->m_pSoundAdministrator.ptr.get() : 0;

        if (pSoundAdministrator && IsEggmanlandTrackForOtherForm(base, pSoundAdministrator))
            g_willRestartKeepMusic = false;
    }

    g_keepMusicOnMenuRestart = false;

    __imp__sub_82539A18(ctx, base);
}

// CGameModeStage::CStateReviveWaitLoad enter, slot 2. Unless CGameModeStage says
// no loading is needed, it queues MsgRequestStartLoading(eLoadingDisplayType_NowLoading) 
// to CLoading; the message is delivered later, while this state waits.
PPC_FUNC_IMPL(__imp__sub_8253D168);
PPC_FUNC(sub_8253D168)
{
    g_isRestartKeepingMusic = g_willRestartKeepMusic;
    g_willRestartKeepMusic = false;

    __imp__sub_8253D168(ctx, base);
}

// CGameModeStage::CStateReviveWaitLoad leave, slot 5.
PPC_FUNC_IMPL(__imp__sub_8253D2D0);
PPC_FUNC(sub_8253D2D0)
{
    g_isRestartKeepingMusic = false;

    __imp__sub_8253D2D0(ctx, base);
}

bool AudioPatches::IsRestartKeepingMusic()
{
    return g_isRestartKeepingMusic;
}
