#pragma once

class InputPatches
{
public:
    // Called once per frame from the App::Update hook. Closes the world map's
    // button prompts once the world map stops refreshing them.
    static void Update();
};
