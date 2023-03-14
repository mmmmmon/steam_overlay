#include "steam_overlay.h"

namespace present
{
    struct FrameContext
    {
        ID3D12CommandAllocator* command_allocator = nullptr;
        unsigned long long fence_value = 0;
    };
    namespace context
    {
        namespace hook
        {
            uintptr_t d3d12_execute_commandlists = 0;
            void(__stdcall *orig_execute_commandLists)(ID3D12CommandQueue*, UINT, ID3D12CommandList*) = 0;
            uintptr_t steam_dxgi_presentscene = 0;
            HRESULT(__stdcall* present_orig) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) = 0;
        }
        RECT *rect = nullptr;
        steam_hook_routine_t menu = nullptr;
        steam_hook_routine_t rendering = nullptr;
        WNDPROC wnd_proc_orig = nullptr;

        std::once_flag imgui_inited;
        IDXGISwapChain3* swap_chain = nullptr;
        ID3D12CommandQueue* command_queue = nullptr;
        ID3D12Device* d3d12_device = nullptr;
        ID3D12DescriptorHeap* rtv_desc_heap = nullptr;
        ID3D12DescriptorHeap* srv_desc_heap = nullptr;
        ID3D12GraphicsCommandList* command_list = nullptr;
        ID3D12Fence* fence = nullptr;

        HANDLE swap_chain_waitable_object = nullptr;
        HANDLE fence_event = nullptr;

        FrameContext* frame_context_array = nullptr;
        ID3D12Resource** d3d12_resource_array = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE* render_target_descriptor_array = nullptr;

        unsigned int num_back_buffers = 0;
        unsigned int num_frames_in_flight = 0;

        unsigned int frame_index = 0;
        unsigned long long fence_last_signaled_value = 0;

        HWND window = 0;
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
            ScreenToClient(context::window, &position);
            io.MousePos.x = (float)position.x;
            io.MousePos.y = (float)position.y;
            ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
            return true;
        }
        return CallWindowProc(context::wnd_proc_orig, hWnd, uMsg, wParam, lParam);
    }
    void imgui_init()
    {
        if (SUCCEEDED(context::swap_chain->GetDevice(__uuidof(ID3D12Device), (void**)&context::d3d12_device)))
        {
            DXGI_SWAP_CHAIN_DESC1 desc1;
            if (!SUCCEEDED(context::swap_chain->GetDesc1(&desc1)))
            {
                return;
            }
            context::num_back_buffers = desc1.BufferCount;
            context::num_frames_in_flight = desc1.BufferCount;

            DXGI_SWAP_CHAIN_DESC desc;
            context::swap_chain->GetDesc(&desc);
            context::window = desc.OutputWindow;
        }
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = context::num_back_buffers;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (!SUCCEEDED(context::d3d12_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&context::rtv_desc_heap))))
            {
                return;
            }
        }
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 1;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            desc.NodeMask = 0;
            if (!SUCCEEDED(context::d3d12_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&context::srv_desc_heap))))
            {
                return;
            }
        }

        if (!SUCCEEDED(context::d3d12_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context::fence))))
        {
            return;
        }
        context::frame_context_array = new FrameContext[context::num_frames_in_flight];
        context::d3d12_resource_array = new ID3D12Resource * [context::num_back_buffers];
        context::render_target_descriptor_array = new D3D12_CPU_DESCRIPTOR_HANDLE[context::num_back_buffers];

        for (UINT i = 0; i < context::num_frames_in_flight; ++i)
        {
            if (!SUCCEEDED(context::d3d12_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context::frame_context_array[i].command_allocator))))
            {
                return;
            }
        }
        SIZE_T nDescriptorSize = context::d3d12_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = context::rtv_desc_heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < context::num_back_buffers; ++i)
        {
            context::render_target_descriptor_array[i] = rtvHandle;
            rtvHandle.ptr += nDescriptorSize;
        }
        if (!SUCCEEDED(context::d3d12_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context::frame_context_array[0].command_allocator, NULL, IID_PPV_ARGS(&context::command_list))) ||
            !SUCCEEDED(context::command_list->Close()))
        {
            return;
        }
        context::swap_chain_waitable_object = context::swap_chain->GetFrameLatencyWaitableObject();
        context::fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (context::fence_event == NULL)
        {
            return;
        }
        ID3D12Resource* pBackBuffer;
        for (UINT i = 0; i < context::num_back_buffers; ++i)
        {
            if (!SUCCEEDED(context::swap_chain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer))))
            {
                return;
            }
            context::d3d12_device->CreateRenderTargetView(pBackBuffer, NULL, context::render_target_descriptor_array[i]);
            context::d3d12_resource_array[i] = pBackBuffer;
        }
        ImGui_ImplDX12_CreateDeviceObjects();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        ImGui::StyleColorsLight();
        ImGui_ImplWin32_Init(context::window);
        ImGui_ImplDX12_Init(
            context::d3d12_device,
            context::num_frames_in_flight,
            DXGI_FORMAT_R8G8B8A8_UNORM, context::srv_desc_heap,
            context::srv_desc_heap->GetCPUDescriptorHandleForHeapStart(),
            context::srv_desc_heap->GetGPUDescriptorHandleForHeapStart());

        ImFontConfig Font_cfg;
        Font_cfg.FontDataOwnedByAtlas = false;
        context::bg_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyhbd.ttc", 16.2F, &Font_cfg, io.Fonts->GetGlyphRangesChineseFull());
        context::font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyhbd.ttc", 16.0F, &Font_cfg, io.Fonts->GetGlyphRangesChineseFull());
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        context::wnd_proc_orig = (WNDPROC)SetWindowLongPtr(context::window, GWLP_WNDPROC, (LONG_PTR)wnd_proc);
        ImGui_ImplWin32_Init(context::window);
        ImGui_ImplDX12_Init(
            context::d3d12_device,
            context::num_frames_in_flight,
            DXGI_FORMAT_R8G8B8A8_UNORM, context::srv_desc_heap,
            context::srv_desc_heap->GetCPUDescriptorHandleForHeapStart(),
            context::srv_desc_heap->GetGPUDescriptorHandleForHeapStart()
        );
    }
    FrameContext* WaitForNextFrameResources()
    {
        UINT nextFrameIndex = context::frame_index + 1;
        context::frame_index = nextFrameIndex;
        HANDLE waitableObjects[] = { context::swap_chain_waitable_object, NULL };
        constexpr DWORD numWaitableObjects = 1;
        FrameContext* frameCtxt = &context::frame_context_array[nextFrameIndex % context::num_frames_in_flight];
        WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);
        return frameCtxt;
    }
    HRESULT __fastcall present_scene(IDXGISwapChain3* pSwapChain, unsigned int SyncInterval, unsigned int Flags)
    {
        if (pSwapChain == nullptr || context::command_queue == nullptr)
        {
            return context::hook::present_orig(pSwapChain, SyncInterval, Flags);
        }
        context::swap_chain = pSwapChain;
        std::call_once(context::imgui_inited, [] {imgui_init(); });

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        present::context::rendering();
        ImGui::GetIO().MouseDrawCursor = context::is_show;
        if (context::is_show)
        {
            present::context::menu();
        }
        FrameContext* frameCtxt = WaitForNextFrameResources();
        UINT backBufferIdx = context::swap_chain->GetCurrentBackBufferIndex();
        {
            frameCtxt->command_allocator->Reset();
            static D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.pResource = context::d3d12_resource_array[backBufferIdx];
            context::command_list->Reset(frameCtxt->command_allocator, NULL);
            context::command_list->ResourceBarrier(1, &barrier);
            context::command_list->OMSetRenderTargets(1, &context::render_target_descriptor_array[backBufferIdx], FALSE, NULL);
            context::command_list->SetDescriptorHeaps(1, &context::srv_desc_heap);
        }
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), context::command_list);
        static D3D12_RESOURCE_BARRIER barrier = { };
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.pResource = context::d3d12_resource_array[backBufferIdx];
        context::command_list->ResourceBarrier(1, &barrier);
        context::command_list->Close();
        context::command_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&context::command_list);
        UINT64 fenceValue = context::fence_last_signaled_value + 1;
        context::command_queue->Signal(context::fence, fenceValue);
        context::fence_last_signaled_value = fenceValue;
        frameCtxt->fence_value = fenceValue;
        return context::hook::present_orig(pSwapChain, SyncInterval, Flags);
    }
    void  execute_commandlists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists)
    {
        if (present::context::command_queue == nullptr && queue != nullptr)
        {
            present::context::command_queue = (ID3D12CommandQueue*)queue;
            if (MH_DisableHook((void*)present::context::hook::d3d12_execute_commandlists) != MH_OK)
            {
                present::context::command_queue = nullptr;
            }
        }
        return present::context::hook::orig_execute_commandLists(queue, NumCommandLists, ppCommandLists);
    }
}
steam_overlay::steam_overlay()
{
    MH_DisableHook(MH_ALL_HOOKS);
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
bool hook(__int64 addr, __int64 func, __int64* orig, __int64 hook)
{
    const auto hook_addr = tools::sig_scan("GameOverlayRenderer64.dll", "48 ? ? ? ? 57 48 83 EC 30 33 C0");
    if (hook_addr == 0)
    {
        return false;
    }
    const auto hook_routine = ((__int64(__fastcall*)(__int64 addr, __int64 func, __int64* orig, __int64 smthng))(hook_addr));
    hook_routine((__int64)addr, (__int64)func, orig, (__int64)hook);
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
        MH_Initialize();
        if (GetModuleHandleA("GameOverlayRenderer64.dll") == nullptr)
        {
            break;
        }
        present::context::hook::steam_dxgi_presentscene = tools::sig_scan("GameOverlayRenderer64.dll", "48 89 6C 24 18 48 89 74 24 20 41 56 48 83 EC 20 41 8B E8");
        if (present::context::hook::steam_dxgi_presentscene == 0)
        {
            break;
        }
       if (MH_CreateHook(
           (void*)present::context::hook::steam_dxgi_presentscene,
           (void*)present::present_scene, 
           (void**)&present::context::hook::present_orig
       ) != MH_OK)
       {
           break;
       }
       present::context::hook::d3d12_execute_commandlists = tools::sig_scan("d3d12.dll", "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC ? 48 8B 99 ? ? ? ? 48 8B E9 48 81 C3");
       if (present::context::hook::d3d12_execute_commandlists == 0)
       {
           break;
       }
       if (MH_CreateHook(
           (void*)present::context::hook::d3d12_execute_commandlists, 
           (void*)present::execute_commandlists,
           (void**)&present::context::hook::orig_execute_commandLists
       ) != MH_OK)
       {
           break;
       }
        MH_EnableHook(MH_ALL_HOOKS);
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