// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include "steam_overlay.h"

steam_overlay* overlay = new steam_overlay();
RECT game_rect;

void menu_routine()
{
    ImGui::Begin(u8"Menu", nullptr, ImGuiWindowFlags_NoResize);

    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::Separator();
    ImGui::PopStyleVar();
    ImGui::Spacing();

    ImGui::Text(u8"Home key show/hide");
    ImGui::Text(u8"Number of render frames %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::End();
}
void esp_routine()
{
    ImGui::GetForegroundDrawList()->AddText(ImVec2(0, 0), ImColor(249, 179, 75), u8"hello,world");
}
BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        overlay->setup_hook(menu_routine, esp_routine, &game_rect);

        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

