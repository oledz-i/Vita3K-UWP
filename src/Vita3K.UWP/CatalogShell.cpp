// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox-first, streaming-catalog frontend. UI is not a claim of game compatibility.
#include "CatalogShell.h"
#include "MenuMusic.h"
#include "SfoReader.h"
#include <mem/allocator.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
Color RGB(unsigned r, unsigned g, unsigned b) {
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
    std::transform(a.begin(), a.end(), a.begin(), towlower);
    std::transform(b.begin(), b.end(), b.begin(), towlower);
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
    auto extension = file->FileType;
    return extension->Equals(L".vpk") || extension->Equals(L".VPK") ||
           extension->Equals(L".pkg") || extension->Equals(L".PKG") ||
           extension->Equals(L".zip") || extension->Equals(L".ZIP");
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
    case 1: return RGB(5, 24, 35);       // Aurora
    case 2: return RGB(28, 12, 19);      // Ember
    case 3: return RGB(233, 241, 251);   // Glacier / light
    case 4: return RGB(17, 14, 39);      // Orbit
    default: return RGB(9, 13, 24);      // Onyx
    }
}
Color CatalogShell::Card() const {
    switch(theme_) {
    case 1: return RGB(17, 49, 60);
    case 2: return RGB(61, 31, 41);
    case 3: return RGB(255, 255, 255);
    case 4: return RGB(37, 30, 72);
    default: return RGB(28, 35, 52);
    }
}
Color CatalogShell::Accent() const {
    switch(theme_) {
    case 1: return RGB(58, 225, 199);
    case 2: return RGB(255, 147, 101);
    case 3: return RGB(28, 106, 209);
    case 4: return RGB(183, 138, 255);
    default: return RGB(130, 111, 255);
    }
}
Color CatalogShell::Glow() const {
    switch(theme_) {
    case 1: return RGB(10, 72, 85);
    case 2: return RGB(112, 37, 48);
    case 3: return RGB(201, 225, 246);
    case 4: return RGB(72, 44, 120);
    default: return RGB(53, 39, 93);
    }
}
Color CatalogShell::Muted() const {
    return theme_ == 3 ? RGB(65, 82, 114) : RGB(170, 186, 210);
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
    t->FontFamily = ref new FontFamily(L"Segoe UI");
    t->FontWeight = bold ? FontWeights::SemiBold : FontWeights::Normal;
    t->Foreground = Solid(muted ? Muted() : (theme_ == 3 ? RGB(23, 39, 65) : Colors::White));
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
    button->FontFamily = ref new FontFamily(L"Segoe UI");
    button->FontWeight = FontWeights::SemiBold;
    button->FontSize = 16;
    button->BorderThickness = Thickness(1);
    button->BorderBrush = Solid(highlighted ? Accent() : Muted());
    button->Background = highlighted ? Solid(Accent()) : Solid(Card());
    button->Foreground = Solid(highlighted && theme_ != 3 ? Bg() :
                              (theme_ == 3 ? RGB(26, 48, 72) : Colors::White));
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
    border->CornerRadius = CornerRadius(14);
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

    Content = root_;
    BuildNavigation();
    Render();
    RestorePaths();
    if (track_ > 0) {
        if (track_ == 5) PlayCustomMusic();
        else PlayBundledMusic(track_);
    }
    ScanGames();
}
void CatalogShell::BuildNavigation() {
    auto navigation = ref new StackPanel();
    navigation->Orientation = Orientation::Horizontal;
    navigation->VerticalAlignment = VerticalAlignment::Center;
    navigation->Margin = Thickness(46, 0, 36, 0);

    auto wordmark = Text(L"VITA3K", 27, true);
    wordmark->Foreground = Solid(Accent());
    wordmark->VerticalAlignment = VerticalAlignment::Center;
    wordmark->Margin = Thickness(0, 0, 7, 0);
    navigation->Children->Append(wordmark);

    auto sub = Text(L"UWP  /  XBOX", 12, true, true);
    sub->VerticalAlignment = VerticalAlignment::Center;
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
    searchBox_->VerticalAlignment = VerticalAlignment::Center;
    searchBox_->Background = Solid(Bg());
    searchBox_->Foreground = Solid(theme_ == 3 ? RGB(20, 40, 70) : Colors::White);
    searchBox_->TextChanged += ref new TextChangedEventHandler([this](Platform::Object^, TextChangedEventArgs^) {
        search_ = searchBox_->Text;
        if (page_ == 0 || page_ == 1) Render();
    });
    navigation->Children->Append(searchBox_);
    navigation_->Child = navigation;
}
void CatalogShell::ShowPage(int nextPage) {
    if (nextPage < 0 || nextPage > 4) nextPage = 0;
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
    imageBox->CornerRadius = CornerRadius(12);
    imageBox->Width = 203;
    imageBox->Height = 290;
    ConfigureCover(imageBox, index);

    auto inside = ref new Grid();
    auto top = Text(L"PS VITA", 12, true);
    top->Foreground = Solid(Colors::White);
    top->HorizontalAlignment = HorizontalAlignment::Left;
    top->VerticalAlignment = VerticalAlignment::Top;
    top->Margin = Thickness(14);
    inside->Children->Append(top);
    auto lower = ref new Border();
    lower->Background = ref new SolidColorBrush(ColorHelper::FromArgb(224, 9, 13, 24));
    lower->Padding = Thickness(12);
    lower->VerticalAlignment = VerticalAlignment::Bottom;
    auto lowerText = Vertical();
    auto label = Text(games_[index].name, 16, true);
    label->Foreground = Solid(Colors::White);
    label->MaxHeight = 46;
    lowerText->Children->Append(label);
    auto kind = Text(games_[index].sourceType + L"  /  NOT PLAYABLE YET", 11, false);
    kind->Foreground = Solid(RGB(198, 205, 225));
    lowerText->Children->Append(kind);
    lower->Child = lowerText;
    inside->Children->Append(lower);
    imageBox->Child = inside;
    tile->Content = imageBox;
    tile->Click += ref new RoutedEventHandler([this, index](Platform::Object^, RoutedEventArgs^) {
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
    actions->Children->Append(Text(L"Ready for your collection", 24, true));
    actions->Children->Append(Text(L"Game launching is unavailable until the full Vita3K CPU, kernel, firmware and GXM renderer have been integrated.", 17, false, true));
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
