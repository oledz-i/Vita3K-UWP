// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <xaudio2.h>
#include <wrl/client.h>
#include <cstdint>
#include <mutex>
#include <set>
#include <vector>
#pragma comment(lib, "xaudio2.lib")

// 48 kHz stereo S16 matches the pinned core's retro_get_system_av_info.
// Own every buffer until XAudio2 reports completion; bound queued latency.
class CoreAudio final : private IXAudio2VoiceCallback {
    using Block = std::vector<std::int16_t>;
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SourceVoice* source_ = nullptr;
    std::mutex mutex_;
    std::set<Block*> pending_;
public:
    ~CoreAudio() { Stop(); }
    bool Start() {
        if (source_) return true;
        if (FAILED(XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
        if (FAILED(engine_->CreateMasteringVoice(&master_, 2, 48000))) { Stop(); return false; }
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
        format.nSamplesPerSec = 48000; format.wBitsPerSample = 16;
        format.nBlockAlign = 4; format.nAvgBytesPerSec = 192000;
        if (FAILED(engine_->CreateSourceVoice(&source_, &format, 0, 1.0f, this)) ||
            FAILED(source_->Start())) { Stop(); return false; }
        return true;
    }
    void Stop() {
        if (source_) { source_->DestroyVoice(); source_ = nullptr; }
        if (master_) { master_->DestroyVoice(); master_ = nullptr; }
        engine_.Reset();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto block : pending_) delete block;
        pending_.clear();
    }
    std::size_t Push(const std::int16_t* data, std::size_t frames) {
        if (!source_ || !data || frames == 0 || frames > 48000) return 0;
        auto block = new Block(data, data + frames * 2);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_.size() >= 4) { delete block; return 0; }
            pending_.insert(block);
        }
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(frames * 4);
        buffer.pAudioData = reinterpret_cast<const BYTE*>(block->data());
        buffer.pContext = block;
        if (FAILED(source_->SubmitSourceBuffer(&buffer))) {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.erase(block); delete block; return 0;
        }
        return frames;
    }
private:
    void STDMETHODCALLTYPE OnBufferEnd(void* context) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto block = static_cast<Block*>(context);
        if (pending_.erase(block)) delete block;
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};
