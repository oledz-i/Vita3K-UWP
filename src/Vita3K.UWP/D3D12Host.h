// SPDX-License-Identifier: GPL-2.0-or-later
// Native Direct3D 12 presentation host for Xbox UWP.
// This is a working API-path experiment, not yet a PS Vita GXM renderer.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <mutex>
#include <wrl/client.h>
#include <string>
#include <cstdint>

class D3D12Host final {
public:
    D3D12Host() = default;
    ~D3D12Host();
    D3D12Host(const D3D12Host&) = delete;
    D3D12Host& operator=(const D3D12Host&) = delete;

    bool Initialize(IUnknown* panel, unsigned width, unsigned height, std::wstring& result);
    bool DrawTestFrame(std::wstring& result);
    bool PresentBgraFrame(const std::uint8_t* pixels, unsigned width, unsigned height,
                          unsigned strideBytes, std::wstring& result);
    bool DrawUploadTestFrame(std::wstring& result);
    bool PresentScaledBgraFrame(const std::uint8_t* pixels, unsigned width,
        unsigned height, unsigned strideBytes, bool sourceRgba,
        std::wstring& result);
    // A game frame: the swap chain follows its size and format (RGBA or
    // BGRA), so higher internal resolutions keep their detail and no pixel
    // conversion happens on the CPU. Safe to call from the game thread.
    bool PresentFrame(const std::uint8_t* pixels, unsigned width, unsigned height,
                      unsigned strideBytes, bool sourceRgba, std::wstring& result);
    // The panel's size in DIPs: the frame is scaled to fit it (keeping its
    // shape, or filling it when stretch is set) by the compositor, on the GPU.
    void SetViewport(float width, float height, bool stretch);
    void Shutdown();
    bool Ready() const { return swapChain_ != nullptr; }

private:
    static constexpr unsigned kBufferCount = 2;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffers_[kBufferCount];
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D12Resource> frameUpload_;
    unsigned width_ = 0;
    unsigned height_ = 0;
    HANDLE fenceEvent_ = nullptr;
    UINT64 fenceValue_ = 0;
    UINT rtvIncrement_ = 0;

    DXGI_FORMAT format_ = DXGI_FORMAT_B8G8R8A8_UNORM;
    UINT64 uploadBytes_ = 0;
    float viewWidth_ = 0, viewHeight_ = 0;
    bool stretch_ = false;
    std::recursive_mutex mutex_;
    bool Resize(unsigned width, unsigned height, DXGI_FORMAT format, std::wstring& result);
    void ApplyTransform();
    bool Fail(const wchar_t* stage, HRESULT code, std::wstring& result);
    bool WaitForGpu(std::wstring& result);
};
