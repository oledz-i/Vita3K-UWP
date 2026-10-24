// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox-first, streaming-catalog frontend. UI is not a claim of game compatibility.
#include "CatalogShell.h"
#include "MenuMusic.h"
#include "SfoReader.h"
#include "GameFormats.h"
#include "VulkanDznProbe.h"
#include "UwpJitProbe.h"
#include <mem/allocator.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <cwctype>
#include <string>
#include <vector>
#include <windows.foundation.h>
#include <windows.ui.xaml.media.imaging.h>

using namespace concurrency;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::AccessCache;
using namespace Windows::Storage::Pickers;
using namespace Windows::Storage::Streams;
using namespace Windows::UI;
using namespace Windows::UI::Text;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Media::Imaging;

namespace Vita3KUWP {
namespace {
Color RgbColor(unsigned r, unsigned g, unsigned b) {
    return ColorHelper::FromArgb(255, static_cast<unsigned char>(r),
        static_cast<unsigned char>(g), static_cast<unsigned char>(b));
}
Platform::String^ Pref(Platform::String^ key, Platform::String^ fallback = L"") {
    auto values = ApplicationData::Current->LocalSettings->Values;
    if (!values->HasKey(key)) return fallback;
    return safe_cast<Platform::String^>(values->Lookup(key));
}
void PutPref(Platform::String^ key, Platform::String^ value) {
    ApplicationData::Current->LocalSettings->Values->Insert(key, value);
}
Platform::String^ StringOf(const std::wstring& value) {
    return ref new Platform::String(value.c_str());
}
bool ContainsIgnoreCase(Platform::String^ source, Platform::String^ search) {
    if (search == nullptr || search->IsEmpty()) return true;
    std::wstring a(source->Data()), b(search->Data());
    std::transform(a.begin(), a.end(), a.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    std::transform(b.begin(), b.end(), b.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(towlower(c));
    });
    return a.find(b) != std::wstring::npos;
}
Platform::String^ StableKey(Platform::String^ path) {
    // Stable FNV-1a over UTF-16 code units; no user paths sent to artwork services.
    std::uint64_t hash = 14695981039346656037ULL;
    for (const wchar_t* p = path->Data(); *p; ++p) {
        hash ^= static_cast<unsigned short>(*p);
        hash *= 1099511628211ULL;
    }
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%016llx", static_cast<unsigned long long>(hash));
    return ref new Platform::String(buffer);
}
bool LooksLikeGameFile(StorageFile^ file) {
    return vita3k_uwp::ClassifyGameFile(std::wstring(file->Name->Data())) !=
           vita3k_uwp::GameFormat::Unknown;
}
Platform::String^ PrettyFileName(Platform::String^ value) {
    std::wstring result(value->Data());
    const auto dot = result.find_last_of(L'.');
    if (dot != std::wstring::npos) result.erase(dot);
    std::replace(result.begin(), result.end(), L'_', L' ');
    return StringOf(result);
}
}
Color CatalogShell::Bg() const {
    switch(theme_) {
    case 1: return RgbColor(5, 24, 35);       // Aurora
    case 2: return RgbColor(28, 12, 19);      // Ember
    case 3: return RgbColor(233, 241, 251);   // Glacier / light
    case 4: return RgbColor(17, 14, 39);      // Orbit
    default: return RgbColor(9, 13, 24);      // Onyx
    }
}
Color CatalogShell::Card() const {
    switch(theme_) {
    case 1: return RgbColor(17, 49, 60);
    case 2: return RgbColor(61, 31, 41);
    case 3: return RgbColor(255, 255, 255);
    case 4: return RgbColor(37, 30, 72);
    default: return RgbColor(28, 35, 52);
    }
}
Color CatalogShell::Accent() const {
    switch(theme_) {
    case 1: return RgbColor(58, 225, 199);
    case 2: return RgbColor(255, 147, 101);
    case 3: return RgbColor(28, 106, 209);
    case 4: return RgbColor(183, 138, 255);
    default: return RgbColor(130, 111, 255);
    }
}
Color CatalogShell::Glow() const {
    switch(theme_) {
    case 1: return RgbColor(10, 72, 85);
    case 2: return RgbColor(112, 37, 48);
    case 3: return RgbColor(201, 225, 246);
    case 4: return RgbColor(72, 44, 120);
    default: return RgbColor(53, 39, 93);
    }
}
Color CatalogShell::Muted() const {
    return theme_ == 3 ? RgbColor(65, 82, 114) : RgbColor(170, 186, 210);
}
Brush^ CatalogShell::Solid(Color value) {
    return ref new SolidColorBrush(value);
}
Brush^ CatalogShell::Gradient(Color a, Color b) {
    auto brush = ref new LinearGradientBrush();
    brush->StartPoint = Point(0, 0);
    brush->EndPoint = Point(1, 1);
    auto x = ref new GradientStop();
    x->Color = a;
    x->Offset = 0;
    auto y = ref new GradientStop();
    y->Color = b;
    y->Offset = 1;
    brush->GradientStops->Append(x);
    brush->GradientStops->Append(y);
    return brush;
}
TextBlock^ CatalogShell::Text(Platform::String^ value, double size, bool bold, bool muted) {
    auto t = ref new TextBlock();
    t->Text = value;
    t->FontSize = size;
    t->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Segoe UI");
    t->FontWeight = bold ? FontWeights::SemiBold : FontWeights::Normal;
    t->Foreground = Solid(muted ? Muted() : (theme_ == 3 ? RgbColor(23, 39, 65) : Colors::White));
    t->TextWrapping = TextWrapping::Wrap;
    t->Margin = Thickness(0, 3, 0, 3);
    return t;
}
Button^ CatalogShell::Button(Platform::String^ label, bool highlighted) {
    auto button = ref new Windows::UI::Xaml::Controls::Button();
    button->Content = label;
    button->Padding = Thickness(20, 13, 20, 13);
    button->Margin = Thickness(0, 4, 12, 4);
    button->MinHeight = 48;
    button->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Segoe UI");
    button->FontWeight = FontWeights::SemiBold;
    button->FontSize = 16;
    button->BorderThickness = Thickness(1);
    button->BorderBrush = Solid(highlighted ? Accent() : Muted());
    button->Background = highlighted ? Solid(Accent()) : Solid(Card());
    button->Foreground = Solid(highlighted && theme_ != 3 ? Bg() :
                              (theme_ == 3 ? RgbColor(26, 48, 72) : Colors::White));
    button->GotFocus += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        PlayCue(0);
    });
    button->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        PlayCue(1);
    });
    return button;
}
StackPanel^ CatalogShell::Vertical(double spacing) {
    auto panel = ref new StackPanel();
    panel->Orientation = Orientation::Vertical;
    panel->Margin = Thickness(0, spacing, 0, spacing);
    return panel;
}
Border^ CatalogShell::Panel(UIElement^ child, double pad) {
    auto border = ref new Border();
    border->CornerRadius = Windows::UI::Xaml::CornerRadius(14);
    border->Padding = Thickness(pad);
    border->Margin = Thickness(0, 9, 0, 16);
    border->Background = Solid(Card());
    border->Child = child;
    return border;
}
void CatalogShell::SetStatus(Platform::String^ message) {
    if (status_ != nullptr) status_->Text = message;
}
CatalogShell::CatalogShell() {
    const auto saved = Pref(L"theme", L"0");
    theme_ = saved->Equals(L"1") ? 1 : saved->Equals(L"2") ? 2 :
             saved->Equals(L"3") ? 3 : saved->Equals(L"4") ? 4 : 0;
    const auto savedTrack = Pref(L"music.track", L"1");
    track_ = savedTrack->Equals(L"0") ? 0 : savedTrack->Equals(L"2") ? 2 :
             savedTrack->Equals(L"3") ? 3 : savedTrack->Equals(L"4") ? 4 :
             savedTrack->Equals(L"5") ? 5 : 1;
    try {
        musicVolume_ = std::stod(Pref(L"music.volume", L"0.22")->Data());
    } catch (...) { musicVolume_ = 0.22; }
    musicVolume_ = std::max(0.0, std::min(1.0, musicVolume_));
    uiSounds_ = !Pref(L"ui.sounds", L"1")->Equals(L"0");
    try { uiSoundVolume_ = std::stod(Pref(L"ui.sound.volume", L"0.40")->Data()); }
    catch (...) { uiSoundVolume_ = 0.40; }
    uiSoundVolume_ = std::max(0.0, std::min(1.0, uiSoundVolume_));

    root_ = ref new Grid();
    root_->Background = Gradient(Bg(), Glow());
    auto navRow = ref new RowDefinition();
    navRow->Height = GridLength(92);
    auto pageRow = ref new RowDefinition();
    pageRow->Height = GridLength(1, GridUnitType::Star);
    auto footerRow = ref new RowDefinition();
    footerRow->Height = GridLength(42);
    root_->RowDefinitions->Append(navRow);
    root_->RowDefinitions->Append(pageRow);
    root_->RowDefinitions->Append(footerRow);

    navigation_ = ref new Border();
    navigation_->Background = Solid(Card());
    Grid::SetRow(navigation_, 0);
    root_->Children->Append(navigation_);

    auto scroll = ref new ScrollViewer();
    scroll->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
    scroll->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
    content_ = Vertical();
    content_->Margin = Thickness(54, 24, 54, 44);
    scroll->Content = content_;
    Grid::SetRow(scroll, 1);
    root_->Children->Append(scroll);

    auto statusBar = ref new Border();
    statusBar->Background = Solid(Card());
    statusBar->Padding = Thickness(48, 8, 48, 8);
    status_ = Text(L"PRE-ALPHA  |  Upstream CPU and Vita GPU integration pending  |  Games cannot launch yet", 13, false, true);
    statusBar->Child = status_;
    Grid::SetRow(statusBar, 2);
    root_->Children->Append(statusBar);

    musicPlayer_ = ref new MediaElement();
    musicPlayer_->AutoPlay = false;
    musicPlayer_->IsLooping = true;
    musicPlayer_->Volume = musicVolume_;
    musicPlayer_->Width = 0;
    musicPlayer_->Height = 0;
    musicPlayer_->IsHitTestVisible = false;
    Grid::SetRow(musicPlayer_, 2);
    root_->Children->Append(musicPlayer_);
    for (unsigned cue = 0; cue < 4; ++cue) {
        cuePlayers_[cue] = ref new MediaElement();
        cuePlayers_[cue]->AutoPlay = false;
        cuePlayers_[cue]->IsLooping = false;
        cuePlayers_[cue]->Volume = uiSoundVolume_;
        cuePlayers_[cue]->Width = 0;
        cuePlayers_[cue]->Height = 0;
        cuePlayers_[cue]->IsHitTestVisible = false;
        Grid::SetRow(cuePlayers_[cue], 2);
        root_->Children->Append(cuePlayers_[cue]);
    }

    Content = root_;
    Window::Current->CoreWindow->KeyDown +=
        ref new TypedEventHandler<Windows::UI::Core::CoreWindow^, Windows::UI::Core::KeyEventArgs^>(
            [this](Windows::UI::Core::CoreWindow^, Windows::UI::Core::KeyEventArgs^ e) {
                if (page_ == 0) return;
                const auto code = e->VirtualKey;
                if (code == Windows::System::VirtualKey::GamepadB ||
                    code == Windows::System::VirtualKey::Escape) {
                    PlayCue(2);
                    ShowPage(page_ == 4 ? 1 : 0);
                }
            });
    BuildNavigation();
    Render();
    RestorePaths();
    Loaded += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        if (bootstrapped_) return;
        bootstrapped_ = true;
        PrepareCues();
        if (track_ > 0) {
            if (track_ == 5) PlayCustomMusic();
            else PlayBundledMusic(track_);
        }
        ScanGames();
    });
}
void CatalogShell::BuildNavigation() {
    auto navigation = ref new StackPanel();
    navigation->Orientation = Orientation::Horizontal;
    navigation->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    navigation->Margin = Thickness(46, 0, 36, 0);

    auto wordmark = Text(L"VITA3K", 27, true);
    wordmark->Foreground = Solid(Accent());
    wordmark->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    wordmark->Margin = Thickness(0, 0, 7, 0);
    navigation->Children->Append(wordmark);

    auto sub = Text(L"UWP  /  XBOX", 12, true, true);
    sub->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    sub->Margin = Thickness(0, 4, 48, 0);
    navigation->Children->Append(sub);

    const wchar_t* labels[] = {L"DISCOVER", L"LIBRARY", L"SETTINGS", L"DEVELOPER"};
    for (int i = 0; i < 4; ++i) {
        auto nav = Button(ref new Platform::String(labels[i]), page_ == i);
        nav->MinHeight = 43;
        nav->Margin = Thickness(2, 2, 8, 2);
        nav->Padding = Thickness(16, 10, 16, 10);
        nav->Click += ref new RoutedEventHandler([this, i](Platform::Object^, RoutedEventArgs^) {
            ShowPage(i);
        });
        navigation->Children->Append(nav);
    }

    searchBox_ = ref new TextBox();
    searchBox_->PlaceholderText = L"Search your library...";
    searchBox_->Text = search_;
    searchBox_->Width = 260;
    searchBox_->Height = 44;
    searchBox_->Margin = Thickness(20, 2, 0, 0);
    searchBox_->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    searchBox_->Background = Solid(Bg());
    searchBox_->Foreground = Solid(theme_ == 3 ? RgbColor(20, 40, 70) : Colors::White);
    searchBox_->TextChanged += ref new TextChangedEventHandler([this](Platform::Object^, TextChangedEventArgs^) {
        search_ = searchBox_->Text;
        if (page_ == 0 || page_ == 1) Render();
    });
    navigation->Children->Append(searchBox_);
    navigation_->Child = navigation;
}
void CatalogShell::ShowPage(int nextPage) {
    if (nextPage < 0 || nextPage > 4) nextPage = 0;
    if (page_ == 3 && nextPage != 3) renderer_.Shutdown();
    page_ = nextPage;
    BuildNavigation();
    Render();
}
void CatalogShell::Banner(Platform::String^ eyebrow, Platform::String^ headline,
                          Platform::String^ description) {
    auto body = Vertical();
    auto tag = Text(eyebrow, 14, true);
    tag->Foreground = Solid(Accent());
    body->Children->Append(tag);
    auto title = Text(headline, 48, true);
    title->MaxWidth = 920;
    body->Children->Append(title);
    auto note = Text(description, 18, false, true);
    note->MaxWidth = 920;
    body->Children->Append(note);
    auto hero = Panel(body, 34);
    hero->Background = Gradient(Card(), Glow());
    hero->MinHeight = 198;
    content_->Children->Append(hero);
}
void CatalogShell::Section(Platform::String^ label, Platform::String^ note) {
    auto row = Vertical();
    row->Margin = Thickness(0, 20, 0, 7);
    row->Children->Append(Text(label, 25, true));
    if (note != nullptr && !note->IsEmpty())
        row->Children->Append(Text(note, 15, false, true));
    content_->Children->Append(row);
}
Platform::String^ CatalogShell::ArtworkUri(size_t index) {
    if (index >= games_.size()) return L"";
    const auto key = games_[index].key;
    const auto file = Pref(L"art.file." + key);
    if (!file->IsEmpty())
        return L"ms-appdata:///local/Artwork/" + file;
    return Pref(L"art.remote." + key);
}
void CatalogShell::ConfigureCover(Border^ cover, size_t index) {
    cover->Background = Gradient(Card(), Glow());
    const auto url = ArtworkUri(index);
    if (!url->IsEmpty()) {
        try {
            auto brush = ref new ImageBrush();
            brush->ImageSource = ref new BitmapImage(ref new Uri(url));
            brush->Stretch = Stretch::UniformToFill;
            cover->Background = brush;
        } catch (Platform::Exception^) {
            cover->Background = Gradient(Card(), Glow());
        }
    } else if (index < games_.size() && games_[index].icon != nullptr) {
        // Local icon0.png is a fallback only. It is not mislabeled as box art.
        auto bitmap = ref new BitmapImage();
        auto brush = ref new ImageBrush();
        brush->ImageSource = bitmap;
        brush->Stretch = Stretch::Uniform;
        cover->Background = brush;
        auto icon = games_[index].icon;
        create_task(icon->OpenAsync(FileAccessMode::Read)).then(
            [bitmap](task<IRandomAccessStream^> opened) {
                try { bitmap->SetSource(opened.get()); }
                catch (Platform::Exception^) {}
            }, task_continuation_context::use_current());
    }
}
Button^ CatalogShell::MakeTile(size_t index) {
    auto tile = ref new Windows::UI::Xaml::Controls::Button();
    tile->Width = 207;
    tile->Height = 294;
    tile->Margin = Thickness(0, 6, 17, 12);
    tile->Padding = Thickness(0);
    tile->BorderThickness = Thickness(0);
    tile->Background = Solid(Colors::Transparent);

    auto imageBox = ref new Border();
    imageBox->CornerRadius = Windows::UI::Xaml::CornerRadius(12);
    imageBox->Width = 203;
    imageBox->Height = 290;
    ConfigureCover(imageBox, index);

    auto inside = ref new Grid();
    auto top = Text(L"PS VITA", 12, true);
    top->Foreground = Solid(Colors::White);
    top->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    top->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Top;
    top->Margin = Thickness(14);
    inside->Children->Append(top);
    auto lower = ref new Border();
    lower->Background = ref new SolidColorBrush(ColorHelper::FromArgb(224, 9, 13, 24));
    lower->Padding = Thickness(12);
    lower->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Bottom;
    auto lowerText = Vertical();
    auto label = Text(games_[index].name, 16, true);
    label->Foreground = Solid(Colors::White);
    label->MaxHeight = 46;
    lowerText->Children->Append(label);
    auto kind = Text(games_[index].sourceType + L"  /  NOT PLAYABLE YET", 11, false);
    kind->Foreground = Solid(RgbColor(198, 205, 225));
    lowerText->Children->Append(kind);
    lower->Child = lowerText;
    inside->Children->Append(lower);
    imageBox->Child = inside;
    tile->Content = imageBox;
    tile->GotFocus += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        PlayCue(0);
    });
    tile->Click += ref new RoutedEventHandler([this, index](Platform::Object^, RoutedEventArgs^) {
        PlayCue(1);
        selectedGame_ = static_cast<int>(index);
        ShowPage(4);
    });
    return tile;
}
void CatalogShell::MakeShelf(Platform::String^ heading, const std::vector<size_t>& items) {
    if (items.empty()) return;
    Section(heading);
    auto shelf = ref new StackPanel();
    shelf->Orientation = Orientation::Horizontal;
    for (auto index : items) shelf->Children->Append(MakeTile(index));
    auto scroller = ref new ScrollViewer();
    scroller->HorizontalScrollBarVisibility = ScrollBarVisibility::Auto;
    scroller->HorizontalScrollMode = ScrollMode::Enabled;
    scroller->VerticalScrollBarVisibility = ScrollBarVisibility::Disabled;
    scroller->Content = shelf;
    auto lastCell = std::make_shared<int>(0);
    scroller->ViewChanged += ref new EventHandler<ScrollViewerViewChangedEventArgs^>(
        [this, lastCell](Platform::Object^ sender, ScrollViewerViewChangedEventArgs^ event) {
            if (event->IsIntermediate) return;
            auto view = dynamic_cast<ScrollViewer^>(sender);
            if (view == nullptr) return;
            const int cell = static_cast<int>(view->HorizontalOffset / 150.0);
            if (cell != *lastCell) {
                *lastCell = cell;
                PlayCue(3);
            }
        });
    content_->Children->Append(scroller);
}
void CatalogShell::Home() {
    Banner(L"YOUR PLAYSTATION VITA COLLECTION  /  XBOX SERIES",
        L"A new way to explore Vita.",
        L"Your games. Your artwork. Your soundtrack. A cinematic home for the handheld library.");
    auto shortcuts = ref new StackPanel();
    shortcuts->Orientation = Orientation::Horizontal;
    auto add = Button(L"Choose game folder", true);
    add->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ChooseGameFolder();
    });
    shortcuts->Children->Append(add);
    auto chooseFile = Button(L"Add Vita file");
    chooseFile->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ChooseGameFile();
    });
    shortcuts->Children->Append(chooseFile);
    auto browse = Button(L"Browse library");
    browse->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ShowPage(1);
    });
    shortcuts->Children->Append(browse);
    auto customize = Button(L"Customize appearance");
    customize->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ShowPage(2);
    });
    shortcuts->Children->Append(customize);
    content_->Children->Append(shortcuts);

    std::vector<size_t> matches, packaged, folders;
    for (size_t i = 0; i < games_.size(); ++i) {
        if (!ContainsIgnoreCase(games_[i].name, search_)) continue;
        matches.push_back(i);
        if (games_[i].sourceType->Equals(L"GAME FOLDER")) folders.push_back(i);
        else packaged.push_back(i);
    }
    if (!matches.empty()) {
        MakeShelf(L"Your collection", matches);
        MakeShelf(L"Game folders", folders);
        MakeShelf(L"Packages and archives", packaged);
    } else {
        Section(L"Your next discovery", L"Artwork will appear here after a folder scan. No demo games are being passed off as installed titles.");
        auto cards = ref new StackPanel();
        cards->Orientation = Orientation::Horizontal;
        const wchar_t* heads[] = {L"01   LIBRARY", L"02   ARTWORK", L"03   SOUNDTRACK"};
        const wchar_t* descriptions[] = {
            L"Find your Vita titles", L"Add beautiful cover art", L"Set your menu atmosphere"
        };
        for (int i = 0; i < 3; ++i) {
            auto body = Vertical();
            body->Children->Append(Text(ref new Platform::String(heads[i]), 15, true));
            body->Children->Append(Text(ref new Platform::String(descriptions[i]), 19, true));
            auto panel = Panel(body, 20);
            panel->Width = 270;
            panel->Height = 180;
            panel->Margin = Thickness(0, 4, 20, 4);
            panel->Background = Gradient(Card(), Glow());
            cards->Children->Append(panel);
        }
        content_->Children->Append(cards);
    }
    Section(L"Emulator development status");
    auto status = Vertical();
    status->Children->Append(Text(L"Native Direct3D 12 presentation and a genuine Vita3K memory component are included.", 18));
    status->Children->Append(Text(L"Full CPU, Vita GPU (GXM), firmware installation, and game launch remain under development.", 16, false, true));
    auto tools = Button(L"Open developer tools");
    tools->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ShowPage(3); });
    status->Children->Append(tools);
    content_->Children->Append(Panel(status));
}
void CatalogShell::Library() {
    Banner(L"MY COLLECTION", L"All your games.",
        L"Detected PS Vita folders and installable archives. Game detection is local; nothing is uploaded.");
    auto actions = ref new StackPanel();
    actions->Orientation = Orientation::Horizontal;
    auto choose = Button(L"Change library folder", true);
    choose->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseGameFolder(); });
    actions->Children->Append(choose);
    auto importFile = Button(L"Add VPK / ZIP / PKG / VCI");
    importFile->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ChooseGameFile();
    });
    actions->Children->Append(importFile);
    auto refresh = Button(L"Rescan games");
    refresh->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ScanGames(); });
    actions->Children->Append(refresh);
    auto artwork = Button(L"Find missing artwork online");
    artwork->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        FetchMissingArtwork();
    });
    actions->Children->Append(artwork);
    content_->Children->Append(actions);
    if (games_.empty()) {
        Section(L"No games discovered yet",
            L"Select the folder containing your .vpk / .pkg files or extracted Vita game directories.");
        return;
    }
    std::vector<size_t> matches;
    for (size_t i = 0; i < games_.size(); ++i) {
        if (ContainsIgnoreCase(games_[i].name, search_)) matches.push_back(i);
    }
    MakeShelf(L"All detected items", matches);
    if (matches.empty())
        content_->Children->Append(Text(L"No titles match your search.", 18, false, true));
    content_->Children->Append(Text(
        L"Detection does not mean a title is installed or bootable. The Vita3K execution core has not been linked.", 14, false, true));
}
void CatalogShell::GameDetails() {
    if (selectedGame_ < 0 || static_cast<size_t>(selectedGame_) >= games_.size()) {
        ShowPage(1); return;
    }
    const size_t index = static_cast<size_t>(selectedGame_);
    Banner(L"GAME DETAILS", games_[index].name, games_[index].sourceType +
        L"  /  Artwork, metadata and library customization");
    auto horizontal = ref new StackPanel();
    horizontal->Orientation = Orientation::Horizontal;
    horizontal->Children->Append(MakeTile(index));
    auto actions = Vertical(4);
    actions->Width = 560;
    actions->Margin = Thickness(24, 0, 0, 0);
    actions->Children->Append(Text(L"Source format: " + games_[index].sourceType, 20, true));
    actions->Children->Append(Text(games_[index].validation, 16, false, true));
    actions->Children->Append(Text(L"Ready for your collection", 24, true));
    actions->Children->Append(Text(L"Game launching is unavailable until the full Vita3K CPU, kernel, firmware and GXM renderer have been integrated.", 17, false, true));
    auto preflight = Button(L"Inspect game for launch readiness");
    preflight->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        InspectLaunch(index);
    });
    actions->Children->Append(preflight);
    if (games_[index].format == vita3k_uwp::GameFormat::Pkg) {
        auto licenseToken = L"vita-license-" + games_[index].key;
        const bool hasLicense = StorageApplicationPermissions::FutureAccessList->ContainsItem(licenseToken);
        actions->Children->Append(Text(hasLicense ?
            L"PKG license file selected. Decryption not available until the core is linked." :
            L"PKG content usually requires its matching license from a legitimately owned Vita game.",
            15, false, true));
        auto license = Button(L"Choose matching owned PKG license file");
        license->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
            ChooseLicenseForGame(index);
        });
        actions->Children->Append(license);
    }
    auto custom = Button(L"Choose custom box art", true);
    custom->Click += ref new RoutedEventHandler([this, index](Platform::Object^, RoutedEventArgs^) {
        ChooseCustomArtwork(index);
    });
    actions->Children->Append(custom);
    auto online = Button(L"Find matching SteamGridDB cover");
    online->Click += ref new RoutedEventHandler([this, index](Platform::Object^, RoutedEventArgs^) {
        FetchArtwork(index);
    });
    actions->Children->Append(online);
    auto back = Button(L"Back to collection");
    back->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ShowPage(1);
    });
    actions->Children->Append(back);
    horizontal->Children->Append(actions);
    content_->Children->Append(Panel(horizontal));
}
void CatalogShell::Render() {
    content_->Children->Clear();
    switch(page_) {
    case 1: Library(); break;
    case 2: Settings(); break;
    case 3: Diagnostics(); break;
    case 4: GameDetails(); break;
    default: Home(); break;
    }
}

void CatalogShell::Settings() {
    Banner(L"PERSONALIZE YOUR EXPERIENCE", L"Your Vita. Your atmosphere.",
        L"Change the entire interface, choose a menu soundtrack, manage folders and artwork.");
    Section(L"Appearance", L"Five visual palettes with distinct accent colors.");
    const wchar_t* styles[] = {
        L"ONYX / Violet night", L"AURORA / Teal haze", L"EMBER / Warm cinema",
        L"GLACIER / Arctic daylight", L"ORBIT / Cosmic purple"
    };
    auto colors = ref new StackPanel();
    colors->Orientation = Orientation::Horizontal;
    for (int i = 0; i < 5; ++i) {
        auto b = Button(ref new Platform::String(styles[i]), theme_ == i);
        b->MinHeight = 74;
        b->Click += ref new RoutedEventHandler([this,i](Platform::Object^, RoutedEventArgs^) { SaveTheme(i); });
        colors->Children->Append(b);
    }
    content_->Children->Append(colors);
    Section(L"Home menu soundtrack", L"Original loopable music, your own audio, or silence.");
    const wchar_t* names[] = {
        L"Off", L"Ambient Drift", L"Neon Pulse", L"Stardust", L"Tidal Blue", L"Custom track"
    };
    auto music = ref new StackPanel();
    music->Orientation = Orientation::Horizontal;
    for (int i = 0; i < 6; ++i) {
        auto b = Button(ref new Platform::String(names[i]), track_ == i);
        b->Click += ref new RoutedEventHandler([this,i](Platform::Object^, RoutedEventArgs^) {
            if (i == 5) PickCustomMusic();
            else SetMusic(i);
        });
        music->Children->Append(b);
    }
    content_->Children->Append(music);
    content_->Children->Append(Text(L"MUSIC VOLUME", 14, true, true));
    auto volume = ref new Slider();
    volume->Minimum = 0;
    volume->Maximum = 100;
    volume->Value = musicVolume_ * 100;
    volume->Width = 420;
    volume->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    volume->ValueChanged += ref new RangeBaseValueChangedEventHandler(
        [this](Platform::Object^, RangeBaseValueChangedEventArgs^ e) {
            SetVolume(e->NewValue / 100.0);
        });
    content_->Children->Append(volume);

    Section(L"Interface sounds", L"Soft focus, confirmation, back and game-shelf swipe cues.");
    auto soundRow = ref new StackPanel();
    soundRow->Orientation = Orientation::Horizontal;
    auto enabled = Button(uiSounds_ ? L"UI sounds: ON" : L"UI sounds: OFF", uiSounds_);
    enabled->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        uiSounds_ = !uiSounds_;
        SaveCueSettings();
        Render();
    });
    soundRow->Children->Append(enabled);
    auto preview = Button(L"Preview selection sound");
    preview->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        PlayCue(1);
    });
    soundRow->Children->Append(preview);
    content_->Children->Append(soundRow);
    auto cueVolume = ref new Slider();
    cueVolume->Minimum = 0;
    cueVolume->Maximum = 100;
    cueVolume->Value = uiSoundVolume_ * 100.0;
    cueVolume->Width = 420;
    cueVolume->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    cueVolume->ValueChanged += ref new RangeBaseValueChangedEventHandler(
        [this](Platform::Object^, RangeBaseValueChangedEventArgs^ e) {
            uiSoundVolume_ = std::max(0.0, std::min(1.0, e->NewValue / 100.0));
            for (auto player : cuePlayers_) if (player != nullptr) player->Volume = uiSoundVolume_;
            SaveCueSettings();
        });
    content_->Children->Append(Text(L"INTERFACE VOLUME", 14, true, true));
    content_->Children->Append(cueVolume);

    Section(L"Storage and paths", L"Choose internal or external storage as Xbox UWP permits.");
    auto storage = Vertical();
    folderLabel_ = Text(L"Game library: not selected", 17, false, true);
    storage->Children->Append(folderLabel_);
    auto choose = Button(L"Choose game folder", true);
    choose->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ChooseGameFolder();
    });
    storage->Children->Append(choose);
    storage->Children->Append(Text(
        L"Application data: " + ApplicationData::Current->LocalFolder->Path, 14, false, true));
    firmwareLabel_ = Text(L"Firmware file: not selected", 17, false, true);
    storage->Children->Append(firmwareLabel_);
    storage->Children->Append(Text(
        L"Obtain an official PS Vita update .PUP from Sony using another device, transfer "
        L"it to accessible storage, and select it here. This saves your file selection "
        L"but does not install firmware: the full emulator core is not integrated.", 15, false, true));
    auto fw = Button(L"Choose official firmware .PUP");
    fw->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ChooseFirmware();
    });
    storage->Children->Append(fw);
    content_->Children->Append(Panel(storage));
    Section(L"Game artwork", L"Use original cover images or bring in your own.");
    auto art = Vertical();
    art->Children->Append(Text(
        L"SteamGridDB cover matching requires a personal API key. The search uses each "
        L"game's title and only accepts an exact title match, to avoid mismatched artwork. "
        L"If no match is available, your game keeps its themed cover.", 16, false, true));
    auto password = ref new PasswordBox();
    password->PlaceholderText = L"SteamGridDB API key (saved in Windows PasswordVault)";
    password->Height = 48;
    password->Width = 540;
    password->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    art->Children->Append(password);
    auto buttons = ref new StackPanel();
    buttons->Orientation = Orientation::Horizontal;
    auto save = Button(L"Save key", true);
    save->Click += ref new RoutedEventHandler([this,password](Platform::Object^, RoutedEventArgs^) {
        SaveApiKey(password->Password);
        password->Password = L"";
    });
    buttons->Children->Append(save);
    auto fetch = Button(L"Get missing covers");
    fetch->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        FetchMissingArtwork();
    });
    buttons->Children->Append(fetch);
    art->Children->Append(buttons);
    art->Children->Append(Text(
        ApiKey()->IsEmpty() ? L"No API key saved yet." : L"SteamGridDB API key saved.",
        14, false, true));
    art->Children->Append(Text(
        L"Select a game card to import a custom PNG or JPEG. Custom artwork takes priority.",
        14, false, true));
    content_->Children->Append(Panel(art));

    Section(L"Vita3K core settings", L"Full settings parity is planned, not simulated.");
    auto advanced = Vertical();
    advanced->Children->Append(Text(
        L"CPU/Dynarmic, GPU backend, shader cache, filtering, render scale, anisotropy, "
        L"VSync, language, controller remapping, rear touch, audio emulation, "
        L"logging and game overrides require the Vita3K core.", 16, false, true));
    advanced->Children->Append(Text(
        L"These will become editable settings once they can actually affect game execution. "
        L"The current active GPU path is D3D12 presentation only, not Vita GXM.", 15, false, true));
    content_->Children->Append(Panel(advanced));
    RestorePaths();
}
void CatalogShell::Diagnostics() {
    Banner(L"UNDER THE HOOD", L"Developer laboratory.",
        L"Real Vita3K upstream code, libretro core ABI and D3D12 diagnostics.");
    Section(L"Experimental real Vita3K libretro core");
    auto libretroBox = Vertical();
    libretroBox->Children->Append(Text(
        L"This probes a genuine Vita3K libretro DLL packaged inside the APPX. "
        L"Unlike a memory or loading-screen test, it checks the exported core API. "
        L"The desktop DLL cannot automatically run as UWP. Game execution needs a "
        L"successfully ported core and working Vulkan graphics (possibly Mesa Dozen).",
        16, false, true));
    auto probeCore = Button(L"Check for actual Vita3K core", true);
    probeCore->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ProbeLibretro(false);
    });
    libretroBox->Children->Append(probeCore);
    auto initCore = Button(L"Attempt genuine core initialization (experimental)");
    initCore->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ProbeLibretro(true);
    });
    libretroBox->Children->Append(initCore);
    libretroResult_ = Text(L"Libretro core not probed. No UWP-compatible core DLL bundled yet.", 17, true);
    libretroBox->Children->Append(libretroResult_);
    content_->Children->Append(Panel(libretroBox));

    Section(L"Vulkan over Direct3D 12 / Mesa Dozen");
    auto dzn = Vertical();
    dzn->Children->Append(Text(
        L"Mesa Dozen translates Vulkan to native D3D12 on Windows. "
        L"On Xbox, a compatible Vulkan loader and Dozen ICD would have to "
        L"be built for UWP and bundled in the signed APPX. "
        L"A native D3D12 test frame is not evidence that Vulkan works.", 16, false, true));
    auto dznResult = Text(L"No Vulkan / Dozen probe run on this Xbox.", 17, true);
    auto dznButton = Button(L"Probe actual packaged Vulkan device");
    dznButton->Click += ref new RoutedEventHandler(
        [this,dznResult](Platform::Object^, RoutedEventArgs^) {
            dznResult->Text = StringOf(ProbePackagedVulkan());
        });
    dzn->Children->Append(dznButton);
    dzn->Children->Append(dznResult);
    content_->Children->Append(Panel(dzn));

    Section(L"Upstream Vita3K memory allocator");
    auto core = Vertical();
    core->Children->Append(Text(
        L"This runs upstream bitmap allocation, reservation and release routines. "
        L"It is not a Dynarmic emulator boot.", 16, false, true));
    auto test = Button(L"Run real upstream memory test", true);
    test->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ProbeCore();
    });
    core->Children->Append(test);
    coreResult_ = Text(L"Not tested on this Xbox.", 17, true);
    core->Children->Append(coreResult_);
    content_->Children->Append(Panel(core));

    Section(L"Virtual address space and JIT permissions");
    auto memory = Vertical();
    memory->Children->Append(Text(
        L"Check read/write pages, RW-to-RX protection and a reserved 4 GiB guest address "
        L"range. This does not execute generated ARM instructions.", 16, false, true));
    auto probe = Button(L"Probe memory compatibility");
    probe->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ProbeMemory();
    });
    memory->Children->Append(probe);
    memoryResult_ = Text(L"Not tested on this Xbox.", 17, true);
    memory->Children->Append(memoryResult_);
    auto jitDescription = Text(
        L"JIT liveness runs a tiny native x64 function generated into a writable page, "
        L"then switches it to executable-only/readable via VirtualProtectFromApp. "
        L"Only run this on the Xbox after testing the normal memory probe. "
        L"An Xbox code-integrity policy could terminate the app instead of returning an error. "
        L"Passing is not proof that the Vita CPU or Dynarmic works.", 15, false, true);
    memory->Children->Append(jitDescription);
    auto jitButton = Button(L"Execute on-demand JIT machine-code test");
    auto jitReport = Text(L"Generated machine code has not been executed.", 16, true);
    jitButton->Click += ref new RoutedEventHandler(
        [this,jitReport](Platform::Object^, RoutedEventArgs^) {
            jitReport->Text = StringOf(RunUwpJitLivenessProbe());
        });
    memory->Children->Append(jitButton);
    memory->Children->Append(jitReport);
    content_->Children->Append(Panel(memory));

    Section(L"Direct3D 12 renderer");
    auto graphics = Vertical();
    graphics->Children->Append(Text(
        L"This display uses a D3D12 device, command queue, swap chain and GPU upload. "
        L"It cannot render PS Vita games until the GXM command and shader pipeline exists.",
        16, false, true));
    renderSurface_ = ref new SwapChainPanel();
    renderSurface_->Width = 960;
    renderSurface_->Height = 544;
    renderSurface_->Margin = Thickness(0, 10, 0, 10);
    graphics->Children->Append(renderSurface_);
    rendererResult_ = Text(L"Renderer not initialized.", 16, true);
    graphics->Children->Append(rendererResult_);
    auto actions = ref new StackPanel();
    actions->Orientation = Orientation::Horizontal;
    auto initialize = Button(L"Initialize D3D12", true);
    initialize->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { BootRenderer(); });
    actions->Children->Append(initialize);
    auto upload = Button(L"Present generated framebuffer");
    upload->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { PresentTestFrame(); });
    actions->Children->Append(upload);
    graphics->Children->Append(actions);
    content_->Children->Append(Panel(graphics));
    renderSurface_->Loaded += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        BootRenderer();
    });
}

void CatalogShell::SaveTheme(int theme) {
    if (theme < 0 || theme > 4) return;
    theme_ = theme;
    PutPref(L"theme", StringOf(std::to_wstring(theme)));
    root_->Background = Gradient(Bg(), Glow());
    navigation_->Background = Solid(Card());
    auto footer = dynamic_cast<Border^>(root_->Children->GetAt(2));
    if (footer != nullptr) footer->Background = Solid(Card());
    if (status_ != nullptr) status_->Foreground = Solid(Muted());
    BuildNavigation();
    Render();
    SetStatus(L"Theme saved. Your colors will be restored on the next launch.");
}
void CatalogShell::SetVolume(double value) {
    musicVolume_ = std::max(0.0, std::min(1.0, value));
    musicPlayer_->Volume = musicVolume_;
    PutPref(L"music.volume", StringOf(std::to_wstring(musicVolume_)));
}
void CatalogShell::SetMusic(int track) {
    if (track < 0 || track > 5) return;
    track_ = track;
    PutPref(L"music.track", StringOf(std::to_wstring(track)));
    musicPlayer_->Stop();
    if (track > 0 && track < 5) PlayBundledMusic(track);
    if (track == 5) PlayCustomMusic();
    Render();
    SetStatus(track == 0 ? L"Menu music turned off." : L"Menu soundtrack saved.");
}
void CatalogShell::PlayBundledMusic(int track) {
    auto bytes = vita3k_uwp::GenerateMenuWav(static_cast<unsigned>(track - 1));
    auto data = ref new Platform::Array<unsigned char>(static_cast<unsigned>(bytes.size()));
    std::memcpy(data->Data, bytes.data(), bytes.size());
    auto stream = ref new InMemoryRandomAccessStream();
    auto writer = ref new DataWriter(stream->GetOutputStreamAt(0));
    writer->WriteBytes(data);
    create_task(writer->StoreAsync()).then(
        [this,writer,stream,track](task<unsigned int> work) {
            try {
                work.get();
                if (track_ != track) return;
                stream->Seek(0);
                musicPlayer_->SetSource(stream, L"audio/wav");
                musicPlayer_->Volume = musicVolume_;
                musicPlayer_->Play();
            } catch (Platform::Exception^ e) {
                SetStatus(L"Menu audio error: " + e->Message);
            }
        }, task_continuation_context::use_current());
}
void CatalogShell::PlayCustomMusic() {
    auto future = StorageApplicationPermissions::FutureAccessList;
    if (!future->ContainsItem(L"menu-track")) {
        SetStatus(L"Select a custom audio file under Settings first.");
        return;
    }
    create_task(future->GetFileAsync(L"menu-track")).then(
        [this](StorageFile^ file) {
            return create_task(file->OpenAsync(FileAccessMode::Read)).then(
                [this,file](IRandomAccessStream^ stream) {
                    if (track_ != 5) return;
                    musicPlayer_->SetSource(stream, file->ContentType);
                    musicPlayer_->Play();
                    SetStatus(L"Playing custom menu audio: " + file->Name);
                });
        }).then([this](task<void> work) {
            try { work.get(); }
            catch (Platform::Exception^ e) { SetStatus(L"Custom audio failed: " + e->Message); }
        }, task_continuation_context::use_current());
}
void CatalogShell::PickCustomMusic() {
    auto picker = ref new FileOpenPicker();
    picker->SuggestedStartLocation = PickerLocationId::MusicLibrary;
    picker->FileTypeFilter->Append(L".mp3");
    picker->FileTypeFilter->Append(L".wav");
    picker->FileTypeFilter->Append(L".m4a");
    create_task(picker->PickSingleFileAsync()).then(
        [this](task<StorageFile^> work) {
            try {
                auto file = work.get();
                if (file == nullptr) return;
                StorageApplicationPermissions::FutureAccessList->AddOrReplace(L"menu-track", file);
                SetMusic(5);
            } catch (Platform::Exception^ e) { SetStatus(L"Music picker error: " + e->Message); }
        }, task_continuation_context::use_current());
}
void CatalogShell::RestorePaths() {
    auto future = StorageApplicationPermissions::FutureAccessList;
    if (folderLabel_ != nullptr) {
        folderLabel_->Text = future->ContainsItem(L"vita-games")
            ? L"Game folder access saved." : L"No game folder chosen yet.";
    }
    if (firmwareLabel_ != nullptr) {
        firmwareLabel_->Text = future->ContainsItem(L"vita-firmware")
            ? L"Official firmware selection saved (not installed)."
            : L"No firmware .PUP selected.";
    }
    if (future->ContainsItem(L"vita-games")) {
        create_task(future->GetFolderAsync(L"vita-games")).then(
            [this](task<StorageFolder^> work) {
                try {
                    auto folder = work.get();
                    if (folderLabel_ != nullptr)
                        folderLabel_->Text = L"Game folder: " + folder->Path;
                } catch (Platform::Exception^) {
                    if (folderLabel_ != nullptr)
                        folderLabel_->Text = L"Game folder permission expired. Select again.";
                }
            }, task_continuation_context::use_current());
    }
    if (future->ContainsItem(L"vita-firmware")) {
        create_task(future->GetFileAsync(L"vita-firmware")).then(
            [this](task<StorageFile^> work) {
                try {
                    auto file = work.get();
                    if (firmwareLabel_ != nullptr)
                        firmwareLabel_->Text = L"Selected firmware: " + file->Name + L" (not installed)";
                } catch (Platform::Exception^) {
                    if (firmwareLabel_ != nullptr)
                        firmwareLabel_->Text = L"Firmware permission expired. Select again.";
                }
            }, task_continuation_context::use_current());
    }
}
void CatalogShell::ChooseGameFolder() {
    auto picker = ref new FolderPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L"*");
    SetStatus(L"Select a directory containing legitimate Vita game data.");
    create_task(picker->PickSingleFolderAsync()).then(
        [this](task<StorageFolder^> work) {
            try {
                auto folder = work.get();
                if (folder == nullptr) return;
                StorageApplicationPermissions::FutureAccessList->AddOrReplace(L"vita-games", folder);
                SetStatus(L"Library folder access saved. Scanning selected storage...");
                RestorePaths();
                ScanGames();
            } catch (Platform::Exception^ e) {
                SetStatus(L"Game folder picker failed: " + e->Message);
            }
        }, task_continuation_context::use_current());
}
void CatalogShell::ChooseFirmware() {
    auto picker = ref new FileOpenPicker();
    picker->SuggestedStartLocation = PickerLocationId::Downloads;
    picker->FileTypeFilter->Append(L".PUP");
    picker->FileTypeFilter->Append(L".pup");
    SetStatus(L"Choose your official PS Vita firmware update .PUP file.");
    create_task(picker->PickSingleFileAsync()).then(
        [this](task<StorageFile^> work) {
            try {
                auto file = work.get();
                if (file == nullptr) return;
                StorageApplicationPermissions::FutureAccessList->AddOrReplace(L"vita-firmware", file);
                RestorePaths();
                SetStatus(L"Firmware file selected. Installation is blocked until core integration.");
            } catch (Platform::Exception^ e) {
                SetStatus(L"Firmware picker failed: " + e->Message);
            }
        }, task_continuation_context::use_current());
}
void CatalogShell::ScanGames() {
    auto access = StorageApplicationPermissions::FutureAccessList;
    if (!access->ContainsItem(L"vita-games")) {
        const auto revision = ++libraryRevision_;
        games_.clear();
        RestorePinnedGameFiles(revision);
        Render();
        SetStatus(L"Choose a folder or add individual Vita VPK, ZIP, PKG, VCI or eboot.bin files.");
        return;
    }
    SetStatus(L"Scanning up to 300 entries and checking supported Vita source formats...");
    create_task(access->GetFolderAsync(L"vita-games")).then(
        [](StorageFolder^ folder) {
            return create_task(folder->GetItemsAsync(0, 300));
        }).then([this](task<Windows::Foundation::Collections::IVectorView<IStorageItem^>^> work) {
            try {
                auto items = work.get();
                ++libraryRevision_;
                const unsigned revision = libraryRevision_;
                games_.clear();
                for (auto item : items) {
                    const bool directory = item->IsOfType(StorageItemTypes::Folder);
                    auto file = directory ? nullptr : safe_cast<StorageFile^>(item);
                    const auto format = directory ? vita3k_uwp::GameFormat::GameFolder :
                        vita3k_uwp::ClassifyGameFile(std::wstring(item->Name->Data()));
                    if (format == vita3k_uwp::GameFormat::Unknown) continue;
                    LibraryGame game;
                    game.name = directory ? item->Name : PrettyFileName(item->Name);
                    game.key = StableKey(item->Path);
                    game.sourceType = ref new Platform::String(vita3k_uwp::GameFormatLabel(format));
                    game.format = format;
                    game.validation = directory ? L"Checking for eboot.bin in game folder..." :
                        L"Checking file signature...";
                    game.directory = directory ? safe_cast<StorageFolder^>(item) : nullptr;
                    game.file = file;
                    game.icon = nullptr;
                    games_.push_back(game);
                }
                SetStatus(StringOf(std::to_wstring(games_.size())) +
                    L" Vita-format candidates found. Checking game headers...");
                Render();
                for (size_t i = 0; i < games_.size(); ++i) {
                    if (games_[i].directory != nullptr) {
                        CheckGameFolder(i, revision);
                        ReadSfo(i, games_[i].directory);
                    } else {
                        CheckGameFile(i, revision);
                    }
                }
                RestorePinnedGameFiles(revision);
            } catch (Platform::Exception^ e) {
                SetStatus(L"Unable to scan Vita library: " + e->Message);
            }
        }, task_continuation_context::use_current());
}
void CatalogShell::CheckGameFolder(size_t index, unsigned revision) {
    if (index >= games_.size() || revision != libraryRevision_) return;
    auto folder = games_[index].directory;
    if (folder == nullptr) return;
    create_task(folder->GetFileAsync(L"eboot.bin")).then(
        [this,index,revision](task<StorageFile^> work) {
            if (revision != libraryRevision_ || index >= games_.size()) return;
            try {
                games_[index].file = work.get();
                games_[index].validation = L"EBOOT.BIN present. Checking executable format...";
                CheckGameFile(index, revision);
            } catch (Platform::Exception^) {
                games_[index].validation =
                    L"Not an extracted Vita game root: eboot.bin is missing.";
                Render();
            }
        }, task_continuation_context::use_current());
}
void CatalogShell::CheckGameFile(size_t index, unsigned revision) {
    if (revision != libraryRevision_ || index >= games_.size()) return;
    auto file = games_[index].file;
    if (file == nullptr) return;
    const auto format = games_[index].format == vita3k_uwp::GameFormat::GameFolder ?
        vita3k_uwp::GameFormat::ExecutableBin : games_[index].format;
    // Read only a small header; never load an entire multi-gigabyte commercial PKG.
    create_task(file->OpenAsync(FileAccessMode::Read)).then(
        [this,index,revision,format](IRandomAccessStream^ stream) {
            const auto bytes = static_cast<unsigned>(
                std::min<std::uint64_t>(stream->Size, 64));
            auto reader = ref new DataReader(stream->GetInputStreamAt(0));
            return create_task(reader->LoadAsync(bytes)).then(
                [this,index,revision,format,reader](unsigned loaded) {
                    if (revision != libraryRevision_ || index >= games_.size()) return;
                    auto buffer = ref new Platform::Array<unsigned char>(loaded);
                    if (loaded > 0) reader->ReadBytes(buffer);
                    const auto check = vita3k_uwp::CheckGameHeader(
                        format, buffer->Data, static_cast<size_t>(loaded));
                    std::wstring diagnostic;
                    for (const char* p = check.diagnostic; *p; ++p)
                        diagnostic.push_back(static_cast<wchar_t>(*p));
                    games_[index].validation = StringOf(diagnostic);
                    Render();
                });
        }).then([this,index,revision](task<void> work) {
            try { work.get(); }
            catch (Platform::Exception^ error) {
                if (revision != libraryRevision_ || index >= games_.size()) return;
                games_[index].validation = L"Unable to inspect game header: " + error->Message;
                Render();
            }
        }, task_continuation_context::use_current());
}

void CatalogShell::ChooseLicenseForGame(size_t index) {
    if (index >= games_.size() || games_[index].format != vita3k_uwp::GameFormat::Pkg) return;
    const auto title = games_[index].name;
    const auto token = L"vita-license-" + games_[index].key;
    auto picker = ref new FileOpenPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L".rif");
    picker->FileTypeFilter->Append(L".zrif");
    picker->FileTypeFilter->Append(L".bin");
    create_task(picker->PickSingleFileAsync()).then(
        [this,token,title](task<StorageFile^> work) {
            try {
                auto file = work.get();
                if (file == nullptr) return;
                StorageApplicationPermissions::FutureAccessList->AddOrReplace(token, file);
                SetStatus(L"License file access saved for " + title +
                    L". PKG installation and decryption are not implemented.");
                Render();
            } catch (Platform::Exception^ error) {
                SetStatus(L"Could not select PKG license: " + error->Message);
            }
        }, task_continuation_context::use_current());
}
void CatalogShell::InspectLaunch(size_t index) {
    if (index >= games_.size()) return;
    PlayCue(1);
    // This is a real source-file preflight, not an emulated boot or game process.
    if (games_[index].file == nullptr) {
        SetStatus(L"Boot preflight: game executable is missing or unreadable.");
        return;
    }
    if (games_[index].format == vita3k_uwp::GameFormat::Pkg &&
        !StorageApplicationPermissions::FutureAccessList->ContainsItem(
            L"vita-license-" + games_[index].key)) {
        SetStatus(L"PKG needs the title's matching legitimate license. "
            L"The Vita3K CPU, installer, kernel and GXM renderer are still missing.");
        return;
    }
    SetStatus(L"Input preflight: " + games_[index].validation +
        L". Game boot is blocked: Dynarmic CPU, Vita kernel, executable loader "
        L"and GXM renderer are not integrated in this UWP build.");
}

void CatalogShell::ChooseGameFile() {
    auto picker = ref new FileOpenPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L".vpk");
    picker->FileTypeFilter->Append(L".zip");
    picker->FileTypeFilter->Append(L".pkg");
    picker->FileTypeFilter->Append(L".vci");
    picker->FileTypeFilter->Append(L".bin");
    create_task(picker->PickSingleFileAsync()).then(
        [this](task<StorageFile^> result) {
            try {
                auto file = result.get();
                if (file == nullptr) return;
                const auto type = vita3k_uwp::ClassifyGameFile(std::wstring(file->Name->Data()));
                if (type == vita3k_uwp::GameFormat::Unknown) {
                    SetStatus(L"Unsupported filename. Choose VPK, ZIP, PKG, VCI or eboot.bin.");
                    return;
                }
                const auto countString = Pref(L"vita.file.count", L"0");
                unsigned count = 0;
                try { count = static_cast<unsigned>(std::stoul(countString->Data())); }
                catch (...) { count = 0; }
                if (count >= 64) {
                    SetStatus(L"64 individually selected file slots used. Choose a containing folder.");
                    return;
                }
                const auto token = L"vita-file-" + StringOf(std::to_wstring(count));
                StorageApplicationPermissions::FutureAccessList->AddOrReplace(token, file);
                PutPref(L"vita.file.count", StringOf(std::to_wstring(count + 1)));
                SetStatus(L"Saved access to " + file->Name + L". Scanning the library...");
                ScanGames();
            } catch (Platform::Exception^ error) {
                SetStatus(L"Failed to add game file: " + error->Message);
            }
        }, task_continuation_context::use_current());
}
void CatalogShell::RestorePinnedGameFiles(unsigned revision) {
    unsigned count = 0;
    try { count = static_cast<unsigned>(std::stoul(Pref(L"vita.file.count", L"0")->Data())); }
    catch (...) { count = 0; }
    const auto access = StorageApplicationPermissions::FutureAccessList;
    for (unsigned n = 0; n < std::min(count, 64u); ++n) {
        const auto token = L"vita-file-" + StringOf(std::to_wstring(n));
        if (!access->ContainsItem(token)) continue;
        create_task(access->GetFileAsync(token)).then(
            [this,revision](task<StorageFile^> work) {
                if (revision != libraryRevision_) return;
                try {
                    auto file = work.get();
                    if (file == nullptr) return;
                    const auto format = vita3k_uwp::ClassifyGameFile(std::wstring(file->Name->Data()));
                    if (format == vita3k_uwp::GameFormat::Unknown) return;
                    const auto id = StableKey(file->Path);
                    for (const auto& existing : games_) {
                        if (existing.key->Equals(id)) return;
                    }
                    LibraryGame game;
                    game.name = PrettyFileName(file->Name);
                    game.key = id;
                    game.sourceType = ref new Platform::String(vita3k_uwp::GameFormatLabel(format));
                    game.format = format;
                    game.validation = L"Checking selected file header...";
                    game.directory = nullptr;
                    game.file = file;
                    game.icon = nullptr;
                    games_.push_back(game);
                    const size_t index = games_.size() - 1;
                    CheckGameFile(index, revision);
                    Render();
                } catch (Platform::Exception^) {
                    // A pinned file may be on a disconnected external drive.
                }
            }, task_continuation_context::use_current());
    }
}
void CatalogShell::ReadSfo(size_t index, StorageFolder^ gameFolder) {
    const unsigned revision = libraryRevision_;
    create_task(gameFolder->GetFolderAsync(L"sce_sys")).then(
        [this,index,revision](StorageFolder^ sys) {
            create_task(sys->GetFileAsync(L"icon0.png")).then(
                [this,index,revision](task<StorageFile^> icon) {
                    try {
                        if (index >= games_.size() || revision != libraryRevision_) return;
                        games_[index].icon = icon.get();
                        Render();
                    } catch (Platform::Exception^) {}
                }, task_continuation_context::use_current());
            return create_task(sys->GetFileAsync(L"param.sfo"));
        }
    ).then([](StorageFile^ file) { return create_task(FileIO::ReadBufferAsync(file)); }
    ).then([this,index,revision](task<IBuffer^> work) {
        try {
            if (revision != libraryRevision_) return;
            auto buffer = work.get();
            auto reader = DataReader::FromBuffer(buffer);
            auto bytes = ref new Platform::Array<unsigned char>(buffer->Length);
            reader->ReadBytes(bytes);
            std::vector<std::uint8_t> data(bytes->Data, bytes->Data + bytes->Length);
            auto metadata = vita3k_uwp::ReadSfo(data);
            if (index >= games_.size()) return;
            if (!metadata.title.empty()) {
                const int wideChars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    metadata.title.data(), static_cast<int>(metadata.title.size()),
                    nullptr, 0);
                if (wideChars > 0 && wideChars < 2000) {
                    std::wstring wide(static_cast<size_t>(wideChars), L'\0');
                    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        metadata.title.data(), static_cast<int>(metadata.title.size()),
                        &wide[0], wideChars);
                    games_[index].name = StringOf(wide);
                }
            }
            Render();
        } catch (Platform::Exception^) {
            // A folder without a valid param.sfo is only a library candidate.
        }
    }, task_continuation_context::use_current());
}
void CatalogShell::ChooseCustomArtwork(size_t index) {
    if (index >= games_.size()) return;
    auto picker = ref new FileOpenPicker();
    picker->SuggestedStartLocation = PickerLocationId::PicturesLibrary;
    picker->FileTypeFilter->Append(L".png");
    picker->FileTypeFilter->Append(L".jpg");
    picker->FileTypeFilter->Append(L".jpeg");
    const auto key = games_[index].key;
    create_task(picker->PickSingleFileAsync()).then(
        [this,key](task<StorageFile^> work) {
            try {
                auto file = work.get();
                if (file == nullptr) return;
                auto folderTask = create_task(ApplicationData::Current->LocalFolder->
                    CreateFolderAsync(L"Artwork", CreationCollisionOption::OpenIfExists));
                folderTask.then([this,file,key](StorageFolder^ dir) {
                    auto filename = key + file->FileType;
                    return create_task(file->CopyAsync(dir, filename, NameCollisionOption::ReplaceExisting));
                }).then([this,key](task<StorageFile^> copied) {
                    try {
                        auto file = copied.get();
                        PutPref(L"art.file." + key, file->Name);
                        SetStatus(L"Custom artwork saved for " + file->Name);
                        Render();
                    } catch (Platform::Exception^ e) {
                        SetStatus(L"Artwork copy failed: " + e->Message);
                    }
                }, task_continuation_context::use_current());
            } catch (Platform::Exception^ e) {
                SetStatus(L"Artwork picker failed: " + e->Message);
            }
        }, task_continuation_context::use_current());
}

void CatalogShell::SaveApiKey(Platform::String^ value) {
    try {
        auto vault = ref new Windows::Security::Credentials::PasswordVault();
        try {
            auto old = vault->Retrieve(L"Vita3KUWP.SteamGridDB", L"apikey");
            vault->Remove(old);
        } catch (Platform::Exception^) {}
        if (!value->IsEmpty()) {
            vault->Add(ref new Windows::Security::Credentials::PasswordCredential(
                L"Vita3KUWP.SteamGridDB", L"apikey", value));
            SetStatus(L"SteamGridDB API key saved securely in Windows PasswordVault.");
        } else {
            SetStatus(L"SteamGridDB API key cleared.");
        }
        Render();
    } catch (Platform::Exception^ e) {
        SetStatus(L"Could not save artwork API key: " + e->Message);
    }
}
Platform::String^ CatalogShell::ApiKey() {
    try {
        auto vault = ref new Windows::Security::Credentials::PasswordVault();
        auto cred = vault->Retrieve(L"Vita3KUWP.SteamGridDB", L"apikey");
        cred->RetrievePassword();
        return cred->Password;
    } catch (Platform::Exception^) {
        return L"";
    }
}
void CatalogShell::FetchMissingArtwork(size_t index) {
    if (index == 0) {
        if (artworkScanBusy_) {
            SetStatus(L"Artwork lookup is already running.");
            return;
        }
        if (ApiKey()->IsEmpty()) {
            SetStatus(L"Save your SteamGridDB API key in Settings to search covers.");
            return;
        }
        artworkScanBusy_ = true;
        SetStatus(L"Looking for matching game artwork...");
    }
    while (index < games_.size() && !ArtworkUri(index)->IsEmpty()) ++index;
    if (index == games_.size()) {
        artworkScanBusy_ = false;
        SetStatus(L"Artwork scan finished. Missing/ambiguous covers use themed placeholders.");
        Render();
        return;
    }
    FetchArtwork(index, true);
}
void CatalogShell::FetchArtwork(size_t index, bool continueBatch) {
    if (index >= games_.size()) {
        if (continueBatch) { artworkScanBusy_ = false; }
        return;
    }
    const auto key = ApiKey();
    if (key->IsEmpty()) {
        SetStatus(L"Enter a SteamGridDB API key under Settings first.");
        if (continueBatch) artworkScanBusy_ = false;
        return;
    }
    auto client = ref new Windows::Web::Http::HttpClient();
    client->DefaultRequestHeaders->TryAppendWithoutValidation(L"Authorization", L"Bearer " + key);
    const auto title = games_[index].name;
    const auto gameKey = games_[index].key;
    auto searchUri = ref new Uri(
        L"https://www.steamgriddb.com/api/v2/search/autocomplete/" + Uri::EscapeComponent(title));
    SetStatus(L"Searching SteamGridDB for: " + title);

    create_task(client->GetStringAsync(searchUri)).then(
        [this,client,index,gameKey,title,continueBatch](task<Platform::String^> search) {
            try {
                auto response = Windows::Data::Json::JsonObject::Parse(search.get());
                const auto list = response->GetNamedArray(L"data");
                unsigned gameId = 0;
                // Do not associate loosely matched art with a completely different title.
                std::wstring desired(title->Data());
                std::transform(desired.begin(), desired.end(), desired.begin(),
                    [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
                for (unsigned i = 0; i < list->Size; ++i) {
                    auto item = list->GetAt(i)->GetObject();
                    std::wstring candidate(item->GetNamedString(L"name")->Data());
                    std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
                    if (candidate == desired) {
                        gameId = static_cast<unsigned>(item->GetNamedNumber(L"id"));
                        break;
                    }
                }
                if (gameId == 0) {
                    SetStatus(L"No exact SteamGridDB match found for: " + title);
                    if (continueBatch) FetchMissingArtwork(index + 1);
                    return;
                }
                auto imageUri = ref new Uri(
                    L"https://www.steamgriddb.com/api/v2/grids/game/" +
                    StringOf(std::to_wstring(gameId)) + L"?dimensions=600x900&types=static");
                create_task(client->GetStringAsync(imageUri)).then(
                    [this,index,gameKey,title,continueBatch,client](task<Platform::String^> art) {
                        try {
                            auto response = Windows::Data::Json::JsonObject::Parse(art.get());
                            auto list = response->GetNamedArray(L"data");
                            if (list->Size == 0) {
                                SetStatus(L"No portrait covers found for: " + title);
                            } else {
                                auto item = list->GetAt(0)->GetObject();
                                const auto uri = item->GetNamedString(L"url");
                                std::wstring raw(uri->Data());
                                if (raw.find(L"https://") == 0 &&
                                    raw.find(L"steamgriddb.com") != std::wstring::npos) {
                                    PutPref(L"art.remote." + gameKey, uri);
                                    SetStatus(L"SteamGridDB art saved for: " + title);
                                    Render();
                                } else {
                                    SetStatus(L"Artwork provider returned an unsupported image URL.");
                                }
                            }
                        } catch (Platform::Exception^ e) {
                            SetStatus(L"Art lookup failed for " + title + L": " + e->Message);
                        }
                        if (continueBatch) FetchMissingArtwork(index + 1);
                    }, task_continuation_context::use_current());
            } catch (Platform::Exception^ e) {
                SetStatus(L"Artwork search failed for " + title + L": " + e->Message);
                if (continueBatch) FetchMissingArtwork(index + 1);
            }
        }, task_continuation_context::use_current());
}

void CatalogShell::ProbeLibretro(bool initialize) {
    if (libretroResult_ == nullptr) return;
    std::wstring report;
    if (initialize) {
        // The Vita3K libretro core needs a writable private filesystem.
        // Never try to write into the read-only APPX install directory.
        const auto folder = ApplicationData::Current->LocalFolder;
        create_task(folder->CreateFolderAsync(L"Vita3KSystem",
            CreationCollisionOption::OpenIfExists)).then(
            [this](task<StorageFolder^> work) {
                if (libretroResult_ == nullptr) return;
                try {
                    auto system = work.get();
                    const auto path = system->Path;
                    const int bytes = WideCharToMultiByte(CP_UTF8, 0, path->Data(), -1,
                        nullptr, 0, nullptr, nullptr);
                    if (bytes <= 0) {
                        libretroResult_->Text = L"Cannot encode the UWP Vita3K system path.";
                        return;
                    }
                    std::string narrow(static_cast<size_t>(bytes), '\\0');
                    WideCharToMultiByte(CP_UTF8, 0, path->Data(), -1,
                        &narrow[0], bytes, nullptr, nullptr);
                    std::wstring detail;
                    const bool loaded = libretro_.Initialize(narrow, detail);
                    libretroResult_->Text = StringOf(detail);
                    SetStatus(loaded ? L"Real Vita3K libretro initialization returned. "
                        L"No game execution or Vulkan context is available." :
                        L"Real Vita3K core initialization blocked. See developer report.");
                } catch (Platform::Exception^ e) {
                    libretroResult_->Text = L"Cannot create private Vita3K system directory: " + e->Message;
                }
            }, task_continuation_context::use_current());
    } else {
        const bool loaded = libretro_.Probe(report);
        libretroResult_->Text = StringOf(report);
        SetStatus(loaded ? L"Actual Vita3K core C ABI loaded; GPU and games untested." :
            L"Packaged native Vita3K core not present or not UWP compatible.");
    }
}
void CatalogShell::ProbeCore() {
    BitmapAllocator bitmap(64);
    std::uint32_t count = 4;
    const int first = bitmap.allocate_from(0, count);
    const int left = bitmap.free_slot_count(0, 64);
    const int reserved = bitmap.allocate_at(8, 2);
    bitmap.free(0, 4);
    const int afterFree = bitmap.free_slot_count(0, 64);
    const bool okay = first == 0 && count == 4 && left == 60 &&
        reserved == 0 && afterFree == 62;
    if (coreResult_ != nullptr)
        coreResult_->Text = okay ?
            L"PASS  /  Original Vita3K memory allocator executed." :
            L"FAIL  /  Upstream Vita3K allocator returned unexpected results.";
}
void CatalogShell::ProbeMemory() {
    const SIZE_T length = 4096;
    void* page = VirtualAllocFromApp(nullptr, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!page) {
        if (memoryResult_ != nullptr)
            memoryResult_->Text = L"RW allocation failed. Windows error: " +
                StringOf(std::to_wstring(GetLastError()));
        return;
    }
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(page);
    bytes[0] = 0x5a;
    const bool writable = bytes[0] == 0x5a;
    DWORD oldProtect = 0;
    const BOOL executable = VirtualProtectFromApp(page, length, PAGE_EXECUTE_READ, &oldProtect);
    const DWORD protectError = executable ? 0 : GetLastError();
    if (executable) {
        DWORD ignored = 0;
        VirtualProtectFromApp(page, length, PAGE_READWRITE, &ignored);
    }
    VirtualFree(page, 0, MEM_RELEASE);
    constexpr SIZE_T guestSize = static_cast<SIZE_T>(1ULL << 32);
    void* guest = VirtualAllocFromApp(nullptr, guestSize, MEM_RESERVE, PAGE_NOACCESS);
    const DWORD reserveError = guest ? 0 : GetLastError();
    if (guest) VirtualFree(guest, 0, MEM_RELEASE);
    std::wstring result = L"RW page: ";
    result += writable ? L"PASS" : L"FAIL";
    result += L"  /  RW-to-RX: ";
    result += executable ? L"PASS" : L"FAIL";
    if (!executable) result += L" (error " + std::to_wstring(protectError) + L")";
    result += L"  /  4 GiB virtual reserve: ";
    result += guest ? L"PASS" : L"FAIL";
    if (!guest) result += L" (error " + std::to_wstring(reserveError) + L")";
    result += L". This is not Dynarmic/JIT compatibility proof.";
    if (memoryResult_ != nullptr) memoryResult_->Text = StringOf(result);
}
void CatalogShell::BootRenderer() {
    if (renderSurface_ == nullptr || rendererResult_ == nullptr) return;
    std::wstring output;
    const bool initialized = renderer_.Initialize(
        reinterpret_cast<IUnknown*>(renderSurface_), 960, 544, output);
    if (initialized) renderer_.DrawTestFrame(output);
    rendererResult_->Text = StringOf(output);
    SetStatus(initialized ? L"Native D3D12 device created. Check renderer status for present result." :
                            L"Native D3D12 initialization failed; see developer results.");
}
void CatalogShell::PresentTestFrame() {
    if (rendererResult_ == nullptr) return;
    std::wstring output;
    if (!renderer_.Ready()) {
        rendererResult_->Text = L"Initialize D3D12 first.";
        return;
    }
    renderer_.DrawUploadTestFrame(output);
    rendererResult_->Text = StringOf(output);
}

void CatalogShell::SaveCueSettings() {
    PutPref(L"ui.sounds", uiSounds_ ? L"1" : L"0");
    PutPref(L"ui.sound.volume", StringOf(std::to_wstring(uiSoundVolume_)));
}
void CatalogShell::PlayCue(unsigned kind) {
    if (!bootstrapped_ || !uiSounds_ || uiSoundVolume_ <= 0) return;
    kind %= 4;
    if (!cueReady_[kind] || cuePlayers_[kind] == nullptr) return;
    // Dedicated player per cue means focus and confirm don't cut one another off.
    cuePlayers_[kind]->Stop();
    cuePlayers_[kind]->Play();
}
void CatalogShell::PrepareCues() {
    for (unsigned kind = 0; kind < 4; ++kind) {
        auto bytes = vita3k_uwp::GenerateInterfaceCue(kind);
        auto buffer = ref new Platform::Array<unsigned char>(static_cast<unsigned>(bytes.size()));
        std::memcpy(buffer->Data, bytes.data(), bytes.size());
        cueStreams_[kind] = ref new InMemoryRandomAccessStream();
        auto stream = cueStreams_[kind];
        auto writer = ref new DataWriter(stream->GetOutputStreamAt(0));
        writer->WriteBytes(buffer);
        create_task(writer->StoreAsync()).then([this, writer, stream, kind](task<unsigned int> operation) {
            try {
                operation.get();
                stream->Seek(0);
                cuePlayers_[kind]->SetSource(stream, L"audio/wav");
                cuePlayers_[kind]->Volume = uiSoundVolume_;
                cueReady_[kind] = true;
            } catch (Platform::Exception^) {
                cueReady_[kind] = false;
            }
        }, task_continuation_context::use_current());
    }
}
} // namespace Vita3KUWP
