#include "imgui_common.h"

static std::vector<std::unique_ptr<ImGuiCallbackData>> g_callbackData;
static uint32_t g_callbackDataIndex = 0;

// Where callbacks go instead of the background draw list (see ImGuiCallbackRedirect).
static ImGuiInFrameDrawList* g_callbackRedirect = nullptr;

ImGuiCallbackData* AddImGuiCallback(ImGuiCallback callback)
{
    if (g_callbackRedirect != nullptr)
    {
        auto& callbackData = g_callbackRedirect->callbackData.emplace_back();
        g_callbackRedirect->drawList.AddCallback(reinterpret_cast<ImDrawCallback>(callback), &callbackData);
        return &callbackData;
    }

    if (g_callbackDataIndex >= g_callbackData.size())
        g_callbackData.emplace_back(std::make_unique<ImGuiCallbackData>());

    auto& callbackData = g_callbackData[g_callbackDataIndex];
    ++g_callbackDataIndex;

    ImGui::GetBackgroundDrawList()->AddCallback(reinterpret_cast<ImDrawCallback>(callback), callbackData.get());

    return callbackData.get();
}

void ResetImGuiCallbacks()
{
    g_callbackDataIndex = 0;
}

ImGuiInFrameDrawList::ImGuiInFrameDrawList(ImVec2 displaySize)
    : drawList(ImGui::GetDrawListSharedData())
{
    // Set up like the frame's own draw lists: the font atlas bound, clipped to the display.
    drawList._ResetForNewFrame();
    drawList.PushTextureID(ImGui::GetIO().Fonts->TexID);
    drawList.PushClipRect({ 0.0f, 0.0f }, displaySize);
}

ImGuiCallbackRedirect::ImGuiCallbackRedirect(ImGuiInFrameDrawList* target)
    : m_previous(g_callbackRedirect)
{
    g_callbackRedirect = target;
}

ImGuiCallbackRedirect::~ImGuiCallbackRedirect()
{
    g_callbackRedirect = m_previous;
}