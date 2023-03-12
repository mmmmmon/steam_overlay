#include "steam_overlay.h"

namespace present
{
    namespace context
    {
        RECT *rect = nullptr;
        steam_hook_routine_t menu = nullptr;
        steam_hook_routine_t rendering = nullptr;
        WNDPROC wnd_proc_orig = nullptr;
        Present_t present_orig = nullptr;
        std::once_flag IsInitialized;
        IDXGISwapChain* swap_chain = nullptr;
        ID3D11Device* d3d11_device = nullptr;
        ID3D11DeviceContext* d3d11_device_context = nullptr;
        ID3D11RenderTargetView* d3d11_render_target_view = nullptr;
        HWND Window = 0;
        bool is_show = false;

        ImFont* font = nullptr;
        ImFont* bg_font = nullptr;
    }
    LRESULT __stdcall wnd_proc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (uMsg == WM_KEYDOWN && LOWORD(wParam) == VK_HOME)
        {
            context::is_show ^= 1;
        }
        GetClientRect(hWnd, context::rect);
        if (context::is_show)
        {
            ImGuiIO& io = ImGui::GetIO();
            POINT position;
            GetCursorPos(&position);
            ScreenToClient(context::Window, &position);
            io.MousePos.x = (float)position.x;
            io.MousePos.y = (float)position.y;
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
            return true;
        }
        return CallWindowProc(context::wnd_proc_orig, hWnd, uMsg, wParam, lParam);
    }
    void imgui_init()
    {
        if (SUCCEEDED(context::swap_chain->GetDevice(__uuidof(ID3D11Device), (void**)&context::d3d11_device)))
        {
            context::d3d11_device->GetImmediateContext(&context::d3d11_device_context);
            DXGI_SWAP_CHAIN_DESC sd;
            context::swap_chain->GetDesc(&sd);
            context::Window = sd.OutputWindow;
            ID3D11Texture2D* pBackBuffer;
            context::swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            context::d3d11_device->CreateRenderTargetView(pBackBuffer, NULL, &context::d3d11_render_target_view);
            pBackBuffer->Release();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            ImGui::StyleColorsLight();
            ImFontConfig Font_cfg;
            Font_cfg.FontDataOwnedByAtlas = false;
            context::bg_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyhbd.ttc", 16.2F, &Font_cfg, io.Fonts->GetGlyphRangesChineseFull());
            context::font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyhbd.ttc", 16.0F, &Font_cfg, io.Fonts->GetGlyphRangesChineseFull());
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            context::wnd_proc_orig = (WNDPROC)SetWindowLongPtr(context::Window, GWLP_WNDPROC, (LONG_PTR)wnd_proc);
            ImGui_ImplWin32_Init(context::Window);
            ImGui_ImplDX11_Init(context::d3d11_device, context::d3d11_device_context);
        }
        else
        {
            MessageBoxW(nullptr, L"your game was not d3d11 mode", L"waring", MB_OK);
        }
    }
    HRESULT __fastcall present_scene(IDXGISwapChain* pSwapChain, unsigned int SyncInterval, unsigned int Flags)
    {
        context::swap_chain = pSwapChain;
        std::call_once(context::IsInitialized, [] {imgui_init(); });
        if (context::d3d11_device || context::d3d11_device_context)
        {
            ID3D11Texture2D* renderTargetTexture = nullptr;
            if (!context::d3d11_render_target_view)
            {
                if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&renderTargetTexture))))
                {
                    context::d3d11_device->CreateRenderTargetView(renderTargetTexture, nullptr, &context::d3d11_render_target_view);
                    renderTargetTexture->Release();
                }
            }
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        present::context::rendering();
        ImGui::GetIO().MouseDrawCursor = context::is_show;
        if (context::is_show)
        {
            present::context::menu();
        }
        ImGui::EndFrame();
        context::d3d11_device_context->OMSetRenderTargets(1, &context::d3d11_render_target_view, NULL);
        ImGui::Render();
        if (context::d3d11_render_target_view)
        {
            context::d3d11_render_target_view->Release();
            context::d3d11_render_target_view = nullptr;
        }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        return context::present_orig(pSwapChain, SyncInterval, Flags);
    }
}
steam_overlay::steam_overlay()
{

}
void steam_overlay::draw_outline_text(const ImVec2& pos, ImU32 col, const char* text)
{
    ImGui::PushFont(present::context::bg_font);
    ImGui::GetForegroundDrawList()->AddText(pos, ImColor(28, 28, 28), text);
    ImGui::PopFont();
    ImGui::PushFont(present::context::font);
    ImGui::GetForegroundDrawList()->AddText(pos, col, text);
    ImGui::PopFont();
}

bool steam_overlay::hook(__int64 addr, __int64 func, __int64* orig)
{
    const auto hook_addr = tools::sig_scan("GameOverlayRenderer64.dll", "48 ? ? ? ? 57 48 83 EC 30 33 C0");
    if (hook_addr == 0)
    {
        return false;
    }
    const auto hook_routine = ((__int64(__fastcall*)(__int64 addr, __int64 func, __int64* orig, __int64 smthng))(hook_addr));
    hook_routine((__int64)addr, (__int64)func, orig, (__int64)1);
    return true;
}
bool steam_overlay::menu_is_show()
{
    return present::context::is_show;
}
bool steam_overlay::setup_hook(steam_hook_routine_t menu_routine, steam_hook_routine_t rendering_routine, RECT* game_rect)
{
    auto result = false;
    do
    {
        if (GetModuleHandleA("GameOverlayRenderer64.dll") == nullptr)
        {
            break;
        }
        const auto steam_dxgi_presentscene = tools::sig_scan("GameOverlayRenderer64.dll", "48 89 6C 24 18 48 89 74 24 20 41 56 48 83 EC 20 41 8B E8");
        if (steam_dxgi_presentscene == 0)
        {
            break;
        }
        if (hook(steam_dxgi_presentscene, (__int64)present::present_scene, (__int64*)&present::context::present_orig) == false)
        {
            break;
        }
        present::context::menu = menu_routine;
        present::context::rendering = rendering_routine;
        present::context::rect = game_rect;
        result = true;
    } while (false);
    return result;
}

steam_overlay::~steam_overlay()
{
}