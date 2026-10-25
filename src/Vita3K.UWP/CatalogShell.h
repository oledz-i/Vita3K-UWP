// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "D3D12Host.h"
#include "GameFormats.h"
#include "VitaLibretroHost.h"
#include <collection.h>
#include <ppltasks.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Vita3KUWP {
struct LibraryGame {
    Platform::String^ name;
    Platform::String^ key;
    Platform::String^ sourceType;
    Windows::Storage::StorageFolder^ directory;
    Windows::Storage::StorageFile^ file;
    Windows::Storage::StorageFile^ icon;
    vita3k_uwp::GameFormat format = vita3k_uwp::GameFormat::Unknown;
    Platform::String^ validation = L"Awaiting source validation";
};

public ref class CatalogShell sealed : Windows::UI::Xaml::Controls::UserControl {
public:
    CatalogShell();

private:
    std::vector<LibraryGame> games_;
    Windows::UI::Xaml::Controls::Grid^ root_;
    Windows::UI::Xaml::Controls::Border^ navigation_;
    Windows::UI::Xaml::Controls::StackPanel^ content_;
    Windows::UI::Xaml::Controls::MediaElement^ musicPlayer_;
    Windows::Storage::Streams::IRandomAccessStream^ customMusicStream_ = nullptr;
    Windows::UI::Xaml::Controls::TextBlock^ musicStatus_ = nullptr;
    Platform::String^ playbackStatus_ = L"Not started";
    Windows::UI::Xaml::Controls::MediaElement^ cuePlayers_[4] = {};
    Windows::Storage::Streams::InMemoryRandomAccessStream^ cueStreams_[4] = {};
    bool cueReady_[4] = {};
    bool uiSounds_ = true;
    double uiSoundVolume_ = 0.40;
    Windows::UI::Xaml::Controls::TextBlock^ status_;
    Windows::UI::Xaml::Controls::TextBlock^ folderLabel_;
    Windows::UI::Xaml::Controls::TextBlock^ firmwareLabel_;
    Windows::UI::Xaml::Controls::TextBlock^ rendererResult_;
    Windows::UI::Xaml::Controls::TextBlock^ coreResult_;
    Windows::UI::Xaml::Controls::TextBlock^ libretroResult_;
    Windows::UI::Xaml::Controls::TextBlock^ memoryResult_;
    Windows::UI::Xaml::Controls::SwapChainPanel^ renderSurface_;
    Windows::UI::Xaml::Controls::TextBox^ searchBox_;
    Windows::UI::Xaml::Controls::StackPanel^ libraryResults_;
    void RefreshLibraryItems();
    D3D12Host renderer_;
    VitaLibretroHost libretro_;
    int page_ = 0;
    int selectedGame_ = -1;
    int theme_ = 0;
    int track_ = 1;
    double musicVolume_ = 0.22;
    bool artworkScanBusy_ = false;
    bool bootstrapped_ = false;
    unsigned libraryRevision_ = 0;
    Platform::String^ search_ = L"";

    Windows::UI::Color Bg() const;
    Windows::UI::Color Card() const;
    Windows::UI::Color Accent() const;
    Windows::UI::Color Glow() const;
    Windows::UI::Color Muted() const;
    Windows::UI::Xaml::Media::Brush^ Solid(Windows::UI::Color value);
    Windows::UI::Xaml::Media::Brush^ Gradient(Windows::UI::Color a, Windows::UI::Color b);
    Windows::UI::Xaml::Controls::TextBlock^ Text(Platform::String^ value, double size,
                                                  bool bold = false, bool muted = false);
    Windows::UI::Xaml::Controls::Button^ Button(Platform::String^ label, bool highlighted = false);
    Windows::UI::Xaml::Controls::Border^ Panel(Windows::UI::Xaml::UIElement^ child,
                                                double pad = 24);
    Windows::UI::Xaml::Controls::StackPanel^ Vertical(double spacing = 0);
    void Banner(Platform::String^ eyebrow, Platform::String^ headline,
                Platform::String^ description);
    void Section(Platform::String^ label, Platform::String^ note = L"");
    void BuildNavigation();
    void ShowPage(int nextPage);
    void Render();
    void Home();
    void Library();
    void GameDetails();
    void Settings();
    void Diagnostics();
    void MakeShelf(Platform::String^ heading, const std::vector<size_t>& items,
        Windows::UI::Xaml::Controls::StackPanel^ target = nullptr);
    Windows::UI::Xaml::Controls::Button^ MakeTile(size_t index);
    Platform::String^ ArtworkUri(size_t index);
    void ConfigureCover(Windows::UI::Xaml::Controls::Border^ border, size_t index);
    void ChooseGameFolder();
    void ChooseGameFile();
    void RestorePinnedGameFiles(unsigned revision);
    void CheckGameFile(size_t index, unsigned revision);
    void CheckGameFolder(size_t index, unsigned revision);
    void InspectLaunch(size_t index);
    void ChooseLicenseForGame(size_t index);
    void ChooseFirmware();
    void RestorePaths();
    void ScanGames();
    void ReadSfo(size_t index, Windows::Storage::StorageFolder^ folder);
    void ChooseCustomArtwork(size_t index);
    void SaveTheme(int theme);
    void SetMusic(int track);
    void PlayBundledMusic(int track);
    void PrepareCues();
    void PlayCue(unsigned kind);
    void SaveCueSettings();
    void PlayCustomMusic();
    void PickCustomMusic();
    void SetVolume(double volume);
    void SaveApiKey(Platform::String^ key);
    Platform::String^ ApiKey();
    void FetchArtwork(size_t index, bool continueBatch = false);
    void FetchMissingArtwork(size_t index = 0);
    void ProbeMemory();
    void ProbeCore();
    void ProbeLibretro(bool initialize);
    void BootRenderer();
    void PresentTestFrame();
    void SetStatus(Platform::String^ message);
};
}
