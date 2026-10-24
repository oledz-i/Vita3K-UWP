// SPDX-License-Identifier: GPL-2.0-or-later
#include "D3D12Host.h"
#include <windows.ui.xaml.media.dxinterop.h>

using Microsoft::WRL::ComPtr;

D3D12Host::~D3D12Host() { Shutdown(); }

bool D3D12Host::Fail(const wchar_t* stage, HRESULT code, std::wstring& result) {
    result = std::wstring(stage) + L" failed (HRESULT " +
        std::to_wstring(static_cast<unsigned long>(code)) + L")";
    Shutdown();
    return false;
}

void D3D12Host::Shutdown() {
    if (fenceEvent_ != nullptr) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    fence_.Reset();
    for (auto& buffer : buffers_) buffer.Reset();
    rtvHeap_.Reset();
    commands_.Reset();
    allocator_.Reset();
    swapChain_.Reset();
    queue_.Reset();
    device_.Reset();
    rtvIncrement_ = 0;
    fenceValue_ = 0;
}

bool D3D12Host::Initialize(IUnknown* panel, unsigned width, unsigned height, std::wstring& result) {
    if (Ready()) {
        result = L"D3D12 surface already initialized";
        return true;
    }
    Shutdown();
    if (panel == nullptr || width == 0 || height == 0) {
        result = L"Invalid native SwapChainPanel or dimensions";
        return false;
    }

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(device_.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"D3D12CreateDevice", hr, result);

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue_.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"CreateCommandQueue", hr, result);

    ComPtr<IDXGIFactory4> factory;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"CreateDXGIFactory2", hr, result);

    ComPtr<ISwapChainPanelNative> panelNative;
    hr = panel->QueryInterface(IID_PPV_ARGS(panelNative.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"ISwapChainPanelNative", hr, result);

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> initialSwapChain;
    hr = factory->CreateSwapChainForComposition(queue_.Get(), &desc, nullptr,
        initialSwapChain.GetAddressOf());
    if (FAILED(hr)) return Fail(L"CreateSwapChainForComposition", hr, result);

    hr = panelNative->SetSwapChain(initialSwapChain.Get());
    if (FAILED(hr)) return Fail(L"SetSwapChain", hr, result);

    hr = initialSwapChain.As(&swapChain_);
    if (FAILED(hr)) return Fail(L"IDXGISwapChain3", hr, result);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = kBufferCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"Create RTV heap", hr, result);
    rtvIncrement_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < kBufferCount; ++i) {
        hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(buffers_[i].GetAddressOf()));
        if (FAILED(hr)) return Fail(L"GetBuffer", hr, result);
        device_->CreateRenderTargetView(buffers_[i].Get(), nullptr, handle);
        handle.ptr += rtvIncrement_;
    }

    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(allocator_.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"CreateCommandAllocator", hr, result);
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(),
        nullptr, IID_PPV_ARGS(commands_.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"CreateCommandList", hr, result);
    hr = commands_->Close();
    if (FAILED(hr)) return Fail(L"Close initial commands", hr, result);

    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf()));
    if (FAILED(hr)) return Fail(L"CreateFence", hr, result);
    fenceEvent_ = CreateEventExW(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    if (fenceEvent_ == nullptr) {
        return Fail(L"CreateEventExW", HRESULT_FROM_WIN32(GetLastError()), result);
    }

    result = L"D3D12 device, command queue, 2-buffer flip swap chain and RTVs initialized";
    return true;
}

bool D3D12Host::WaitForGpu(std::wstring& result) {
    const UINT64 value = ++fenceValue_;
    HRESULT hr = queue_->Signal(fence_.Get(), value);
    if (FAILED(hr)) return Fail(L"Queue::Signal", hr, result);
    if (fence_->GetCompletedValue() < value) {
        hr = fence_->SetEventOnCompletion(value, fenceEvent_);
        if (FAILED(hr)) return Fail(L"SetEventOnCompletion", hr, result);
        const DWORD wait = WaitForSingleObjectEx(fenceEvent_, 5000, FALSE);
        if (wait != WAIT_OBJECT_0) {
            result = L"GPU synchronization timed out or failed";
            Shutdown();
            return false;
        }
    }
    return true;
}

bool D3D12Host::DrawTestFrame(std::wstring& result) {
    if (!Ready()) {
        result = L"D3D12 has not initialized";
        return false;
    }
    HRESULT hr = allocator_->Reset();
    if (FAILED(hr)) return Fail(L"Allocator reset", hr, result);
    hr = commands_->Reset(allocator_.Get(), nullptr);
    if (FAILED(hr)) return Fail(L"Command list reset", hr, result);

    const UINT frame = swapChain_->GetCurrentBackBufferIndex();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffers_[frame].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commands_->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += frame * rtvIncrement_;
    const float clearColor[4] = {0.055f, 0.105f, 0.22f, 1.0f};
    commands_->ClearRenderTargetView(handle, clearColor, 0, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commands_->ResourceBarrier(1, &barrier);
    hr = commands_->Close();
    if (FAILED(hr)) return Fail(L"Command list close", hr, result);
    ID3D12CommandList* lists[] = { commands_.Get() };
    queue_->ExecuteCommandLists(1, lists);

    hr = swapChain_->Present(1, 0);
    if (FAILED(hr)) return Fail(L"SwapChain::Present", hr, result);
    if (!WaitForGpu(result)) return false;

    result = L"D3D12 clear and swap-chain present succeeded (graphics bootstrap; no Vita draws)";
    return true;
}
