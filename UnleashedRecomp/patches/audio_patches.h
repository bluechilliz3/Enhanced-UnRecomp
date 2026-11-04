#pragma once

class AudioPatches
{
protected:
    static int m_isAttenuationSupported;

public:
    static bool CanAttenuate();
    static void Update(float deltaTime);

    // DisableMusicRestartOnDeath: true while a "keep the music" button is held during a
    // pause menu Restart.
    static bool IsKeepMusicButtonHeld();

    static void OnMenuConfirm();
    static bool IsRestartKeepingMusic();
};
