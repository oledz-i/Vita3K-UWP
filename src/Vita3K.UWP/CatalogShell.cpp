// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox-first, streaming-catalog frontend. UI is not a claim of game compatibility.
#include "CatalogShell.h"
#include "MenuMusic.h"
#include "SfoReader.h"
#include "GameFormats.h"
#include "VulkanDznProbe.h"
#include "VitaGuestCpuUwpProbe.h"
#include "CoreOptions.h"
#include "UwpFileRedirect.h"
#include "CrashLog.h"
#include <fileapifromapp.h>
#include <share.h>
#include <cstdio>
#include "UwpJitProbe.h"
#include <mem/allocator.h>
#include <windows.h>
#include <collection.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <cwctype>
#include <filesystem>
#include <system_error>
#include <string>
#include <stdexcept>
#include <vector>
#include <windows.foundation.h>
#include <windows.ui.xaml.media.imaging.h>
#include <windows.ui.xaml.shapes.h>

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
using namespace Windows::UI::Xaml::Media::Animation;
using namespace Windows::UI::Xaml::Markup;
using namespace Windows::UI::Xaml::Shapes;

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
// Defined with the firmware helpers further down.
std::wstring FirmwareRoot();
bool HasFiles(const std::wstring& directory);
std::string NarrowUtf8(const std::wstring& text);
TimeSpan Millis(double ms) {
    TimeSpan span;
    span.Duration = static_cast<long long>(ms * 10000.0);
    return span;
}
std::wstring HexOf(Color c) {
    wchar_t buffer[16] = {};
    swprintf_s(buffer, L"#%02X%02X%02X%02X", c.A, c.R, c.G, c.B);
    return std::wstring(buffer);
}
Color Alpha(unsigned a, unsigned r, unsigned g, unsigned b) {
    return ColorHelper::FromArgb(static_cast<unsigned char>(a), static_cast<unsigned char>(r),
        static_cast<unsigned char>(g), static_cast<unsigned char>(b));
}
VariableSizedWrapGrid^ ActionRow(double itemWidth) {
    // Wraps onto a second line instead of running off the edge of the screen.
    auto row = ref new VariableSizedWrapGrid();
    row->Orientation = Orientation::Horizontal;
    row->ItemWidth = itemWidth;
    row->ItemHeight = 46;
    row->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    return row;
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
    switch (theme_) {
    case 1: return RgbColor(7, 16, 19);       // Aurora: deep ocean
    case 2: return RgbColor(18, 12, 11);      // Ember: dark theater
    case 3: return RgbColor(10, 17, 25);      // Glacier: midnight blue
    case 4: return RgbColor(13, 10, 23);      // Orbit: deep cosmic
    default: return RgbColor(9, 10, 13);      // Onyx: charcoal
    }
}
Color CatalogShell::Card() const {
    // Glass: the moving scene shows through every card a little.
    switch (theme_) {
    case 1: return Alpha(206, 16, 32, 35);
    case 2: return Alpha(206, 35, 24, 22);
    case 3: return Alpha(206, 22, 33, 46);
    case 4: return Alpha(206, 27, 22, 41);
    default: return Alpha(206, 24, 25, 30);
    }
}
Color CatalogShell::Accent() const {
    switch (theme_) {
    case 1: return RgbColor(83, 225, 206);
    case 2: return RgbColor(246, 163, 100);
    case 3: return RgbColor(118, 179, 246);
    case 4: return RgbColor(178, 149, 255);
    default: return RgbColor(239, 69, 105);
    }
}
Color CatalogShell::Glow() const {
    switch (theme_) {
    case 1: return RgbColor(16, 67, 65);
    case 2: return RgbColor(103, 46, 32);
    case 3: return RgbColor(28, 71, 109);
    case 4: return RgbColor(75, 40, 105);
    default: return RgbColor(99, 24, 49);
    }
}
Color CatalogShell::Muted() const {
    return RgbColor(153, 157, 169);
}
Color CatalogShell::RailColor() const {
    // The rail sits a step darker than the page so the content reads as one raised surface.
    const Color page = Bg();
    return RgbColor(page.R * 7 / 10, page.G * 7 / 10, page.B * 7 / 10);
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
    t->Foreground = Solid(muted ? Muted() : Colors::White);
    t->TextWrapping = TextWrapping::Wrap;
    t->TextTrimming = TextTrimming::CharacterEllipsis;
    t->Margin = Thickness(0);
    return t;
}
ControlTemplate^ CatalogShell::ButtonTemplate(double radius, bool filled) {
    // One template for every focusable surface. Focus lifts the control with a soft
    // spring and draws an accent ring; a press squeezes it slightly. Both run as
    // transform/opacity animations, so they stay smooth on the console.
    const std::wstring ring = filled ? L"#FFFFFFFF" : HexOf(Accent());
    const std::wstring r = std::to_wstring(radius);
    std::wstring xaml =
        L"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
        L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
        L"<Grid x:Name='Root' RenderTransformOrigin='0.5,0.5' Background='Transparent'>"
        L"<Grid.RenderTransform><ScaleTransform x:Name='FocusScale'/></Grid.RenderTransform>"
        L"<Grid x:Name='Inner' RenderTransformOrigin='0.5,0.5'>"
        L"<Grid.RenderTransform><ScaleTransform x:Name='PressScale'/></Grid.RenderTransform>"
        L"<Border x:Name='Plate' CornerRadius='" + r + L"' Background='{TemplateBinding Background}' "
        L"BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='{TemplateBinding BorderThickness}'/>"
        L"<Border x:Name='Sheen' CornerRadius='" + r + L"' Background='#FFFFFFFF' Opacity='0' IsHitTestVisible='False'/>"
        L"<Border x:Name='Ring' CornerRadius='" + r + L"' BorderBrush='" + ring + L"' BorderThickness='2' "
        L"Opacity='0' IsHitTestVisible='False'/>"
        L"<ContentPresenter x:Name='Presenter' Content='{TemplateBinding Content}' "
        L"ContentTemplate='{TemplateBinding ContentTemplate}' Padding='{TemplateBinding Padding}' "
        L"Foreground='{TemplateBinding Foreground}' FontSize='{TemplateBinding FontSize}' "
        L"FontFamily='{TemplateBinding FontFamily}' FontWeight='{TemplateBinding FontWeight}' "
        L"HorizontalContentAlignment='{TemplateBinding HorizontalContentAlignment}' "
        L"VerticalContentAlignment='{TemplateBinding VerticalContentAlignment}'/>"
        L"</Grid>"
        L"<VisualStateManager.VisualStateGroups>"
        L"<VisualStateGroup x:Name='CommonStates'>"
        L"<VisualStateGroup.Transitions><VisualTransition GeneratedDuration='0:0:0.16'>"
        L"<VisualTransition.GeneratedEasingFunction><CubicEase EasingMode='EaseOut'/></VisualTransition.GeneratedEasingFunction>"
        L"</VisualTransition></VisualStateGroup.Transitions>"
        L"<VisualState x:Name='Normal'/>"
        L"<VisualState x:Name='PointerOver'><Storyboard>"
        L"<DoubleAnimation Storyboard.TargetName='Sheen' Storyboard.TargetProperty='Opacity' To='0.08' Duration='0:0:0.12'/>"
        L"</Storyboard></VisualState>"
        L"<VisualState x:Name='Pressed'><Storyboard>"
        L"<DoubleAnimation Storyboard.TargetName='Sheen' Storyboard.TargetProperty='Opacity' To='0.16' Duration='0:0:0.06'/>"
        L"<DoubleAnimation Storyboard.TargetName='PressScale' Storyboard.TargetProperty='ScaleX' To='0.965' Duration='0:0:0.08'/>"
        L"<DoubleAnimation Storyboard.TargetName='PressScale' Storyboard.TargetProperty='ScaleY' To='0.965' Duration='0:0:0.08'/>"
        L"</Storyboard></VisualState>"
        L"<VisualState x:Name='Disabled'><Storyboard>"
        L"<DoubleAnimation Storyboard.TargetName='Inner' Storyboard.TargetProperty='Opacity' To='0.38' Duration='0:0:0.01'/>"
        L"</Storyboard></VisualState>"
        L"</VisualStateGroup>"
        L"<VisualStateGroup x:Name='FocusStates'>"
        L"<VisualStateGroup.Transitions><VisualTransition GeneratedDuration='0:0:0.2'>"
        L"<VisualTransition.GeneratedEasingFunction><CubicEase EasingMode='EaseOut'/></VisualTransition.GeneratedEasingFunction>"
        L"</VisualTransition></VisualStateGroup.Transitions>"
        L"<VisualState x:Name='Focused'><Storyboard>"
        L"<DoubleAnimation Storyboard.TargetName='Ring' Storyboard.TargetProperty='Opacity' To='1' Duration='0:0:0.16'/>"
        L"<DoubleAnimation Storyboard.TargetName='FocusScale' Storyboard.TargetProperty='ScaleX' To='1.045' Duration='0:0:0.2'/>"
        L"<DoubleAnimation Storyboard.TargetName='FocusScale' Storyboard.TargetProperty='ScaleY' To='1.045' Duration='0:0:0.2'/>"
        L"</Storyboard></VisualState>"
        L"<VisualState x:Name='Unfocused'/>"
        L"<VisualState x:Name='PointerFocused'/>"
        L"</VisualStateGroup>"
        L"</VisualStateManager.VisualStateGroups>"
        L"</Grid></ControlTemplate>";
    try {
        return dynamic_cast<ControlTemplate^>(XamlReader::Load(StringOf(xaml)));
    } catch (Platform::Exception^) {
        return nullptr;   // fall back to the stock template rather than fail to launch
    }
}
void CatalogShell::Skin(Windows::UI::Xaml::Controls::Button^ button, double radius, bool filled) {
    auto custom = ButtonTemplate(radius, filled);
    if (custom != nullptr) button->Template = custom;
    button->UseSystemFocusVisuals = false;
}
void CatalogShell::Reveal(UIElement^ element, double delayMs, double rise) {
    if (element == nullptr) return;
    element->Opacity = 0;
    auto board = ref new Storyboard();
    auto ease = ref new CubicEase();
    ease->EasingMode = EasingMode::EaseOut;
    auto fade = ref new DoubleAnimation();
    fade->From = 0.0;
    fade->To = 1.0;
    fade->Duration = DurationHelper::FromTimeSpan(Millis(380));
    fade->BeginTime = Millis(delayMs);
    fade->EasingFunction = ease;
    Storyboard::SetTarget(fade, element);
    Storyboard::SetTargetProperty(fade, L"Opacity");
    board->Children->Append(fade);
    if (rise > 0 && element->RenderTransform == nullptr) {
        auto shift = ref new TranslateTransform();
        shift->Y = rise;
        element->RenderTransform = shift;
        auto lift = ref new DoubleAnimation();
        lift->From = rise;
        lift->To = 0.0;
        lift->Duration = DurationHelper::FromTimeSpan(Millis(460));
        lift->BeginTime = Millis(delayMs);
        auto liftEase = ref new CubicEase();
        liftEase->EasingMode = EasingMode::EaseOut;
        lift->EasingFunction = liftEase;
        Storyboard::SetTarget(lift, shift);
        Storyboard::SetTargetProperty(lift, L"Y");
        board->Children->Append(lift);
    }
    board->Begin();
}
Button^ CatalogShell::Button(Platform::String^ label, bool highlighted) {
    auto button = ref new Windows::UI::Xaml::Controls::Button();
    button->Content = label;
    button->Padding = Thickness(18, 8, 18, 9);
    button->Margin = Thickness(0, 0, 10, 0);
    button->MinHeight = 38;
    button->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Segoe UI");
    button->FontWeight = FontWeights::SemiBold;
    button->FontSize = 13;
    button->BorderThickness = Thickness(1);
    button->BorderBrush = Solid(highlighted ? Alpha(0, 0, 0, 0) : Alpha(34, 255, 255, 255));
    button->Background = Solid(highlighted ? Accent() : Alpha(22, 255, 255, 255));
    button->Foreground = Solid(highlighted ? RgbColor(14, 14, 17) : Colors::White);
    Skin(button, 10, highlighted);
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
    border->Margin = Thickness(0, 8, 0, 10);
    border->Background = Solid(Card());
    border->BorderBrush = Solid(Alpha(20, 255, 255, 255));
    border->BorderThickness = Thickness(1);
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
        musicVolume_ = std::stod(Pref(L"music.volume", L"0.45")->Data());
    } catch (...) { musicVolume_ = 0.45; }
    musicVolume_ = std::max(0.0, std::min(1.0, musicVolume_));
    uiSounds_ = !Pref(L"ui.sounds", L"1")->Equals(L"0");
    try { uiSoundVolume_ = std::stod(Pref(L"ui.sound.volume", L"0.40")->Data()); }
    catch (...) { uiSoundVolume_ = 0.40; }
    uiSoundVolume_ = std::max(0.0, std::min(1.0, uiSoundVolume_));

    // Paths in the app's own storage keep the fast plain file functions;
    // everything else (USB drives) goes through the FromApp ones.
    {
        std::wstring local = ApplicationData::Current->LocalFolder->Path->Data();
        const auto slash = local.find_last_of(L'\\');
        std::vector<std::wstring> roots;
        roots.push_back(slash == std::wstring::npos ? local : local.substr(0, slash));
        try { roots.push_back(Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data()); }
        catch (Platform::Exception^) {}
        vita3k_uwp::SetAppStorageRoots(roots);
    }
    // The app's own file calls (std::filesystem in Settings) reach USB drives too.
    vita3k_uwp::RedirectCoreFileAccess(vita3k_uwp::ModuleFromAddress(reinterpret_cast<const void*>(&Pref)));
    libretro_.SetLogPath(std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\vita3k-runtime.log");
    {
        // A game was running when the app last ended and nothing cleared the
        // marker: the app was closed or terminated mid-game. A crash the
        // crash log caught is above in the log; a fail-fast or the console
        // ending the app leaves only this.
        const std::wstring marker = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\vita3k-session.txt";
        FILE* file = nullptr;
        if (_wfopen_s(&file, marker.c_str(), L"rb") == 0 && file) {
            char text[400] = {};
            const size_t got = fread(text, 1, sizeof(text) - 1, file);
            fclose(file);
            text[got] = 0;
            libretro_.LogLine(std::string("Previous session ended while a game was running, without returning to the library (") + text +
                "). If no CRASH: lines precede this, the app was closed from the console or ended by a fail-fast.");
            DeleteFileW(marker.c_str());
        }
    }
    CleanUpOldCopies();

    // Cinematic television layout: permanently dark canvas, narrow icon rail,
    // full-bleed content area, and an understated 30px status line.
    root_ = ref new Grid();
    root_->Background = Solid(Bg());
    auto railColumn = ref new ColumnDefinition();
    railColumn->Width = GridLength(92);
    auto contentColumn = ref new ColumnDefinition();
    contentColumn->Width = GridLength(1, GridUnitType::Star);
    root_->ColumnDefinitions->Append(railColumn);
    root_->ColumnDefinitions->Append(contentColumn);
    auto mainRow = ref new RowDefinition();
    mainRow->Height = GridLength(1, GridUnitType::Star);
    auto footerRow = ref new RowDefinition();
    footerRow->Height = GridLength(26);
    root_->RowDefinitions->Append(mainRow);
    root_->RowDefinitions->Append(footerRow);

    navigation_ = ref new Border();
    navigation_->Background = Solid(RailColor());
    Grid::SetColumn(navigation_, 0);
    Grid::SetRowSpan(navigation_, 2);
    root_->Children->Append(navigation_);

    auto scroll = ref new ScrollViewer();
    contentScroll_ = scroll;
    scroll->VerticalScrollBarVisibility = ScrollBarVisibility::Hidden;
    scroll->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
    content_ = Vertical();
    content_->Margin = Thickness(30, 27, 48, 20);
    scroll->Content = content_;
    Grid::SetColumn(scroll, 1);
    Grid::SetRow(scroll, 0);
    root_->Children->Append(scroll);

    auto statusBar = ref new Border();
    statusBar_ = statusBar;
    statusBar->Background = Solid(Bg());
    statusBar->Padding = Thickness(32, 3, 48, 2);
    status_ = Text(L"VITA3K  /  XBOX PREVIEW", 10, false, true);
    status_->CharacterSpacing = 80;
    status_->TextTrimming = TextTrimming::CharacterEllipsis;
    status_->TextWrapping = TextWrapping::NoWrap;
    statusBar->Child = status_;
    Grid::SetColumn(statusBar, 1);
    Grid::SetRow(statusBar, 1);
    root_->Children->Append(statusBar);

    // The living backdrop, behind everything (z-index), over the page colour.
    backdropHost_ = ref new Grid();
    backdropHost_->IsHitTestVisible = false;
    Grid::SetColumnSpan(backdropHost_, 2);
    Grid::SetRowSpan(backdropHost_, 2);
    Canvas::SetZIndex(backdropHost_, -10);
    root_->Children->Append(backdropHost_);
    backdrop_.Attach(backdropHost_);
    backdrop_.SetTheme(theme_, Bg(), Accent(), Glow());
    ApplySurfaceColors();
    clockTimer_ = ref new DispatcherTimer();
    clockTimer_->Interval = Millis(15000);
    clockTimer_->Tick += ref new EventHandler<Platform::Object^>([this](Platform::Object^, Platform::Object^) { UpdateClock(); });
    clockTimer_->Start();

    musicPlayer_ = ref new MediaElement();
    musicPlayer_->AutoPlay = false;
    musicPlayer_->IsLooping = true;
    musicPlayer_->Volume = musicVolume_;
    // Xbox Media Foundation is more reliable with a loaded media element.
    // A 1x1, nearly transparent element keeps the audio pipeline initialized.
    musicPlayer_->Width = 1;
    musicPlayer_->Height = 1;
    musicPlayer_->Opacity = 0.01;
    musicPlayer_->IsHitTestVisible = false;
    Grid::SetRow(musicPlayer_, 1);
    Grid::SetColumn(musicPlayer_, 1);
    root_->Children->Append(musicPlayer_);
    musicPlayer_->MediaOpened += ref new RoutedEventHandler(
        [this](Platform::Object^, RoutedEventArgs^) {
            if (track_ == 0) return;
            musicPlayer_->Volume = musicVolume_;
            musicPlayer_->Play();
            libretro_.LogLine("Menu music opened: " + NarrowUtf8(musicPlayer_->Source ? musicPlayer_->Source->AbsoluteUri->Data() : L"custom track"));
            playbackStatus_ = L"Track opened. Starting playback...";
            if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
        });
    musicPlayer_->CurrentStateChanged += ref new RoutedEventHandler(
        [this](Platform::Object^, RoutedEventArgs^) {
            if (musicPlayer_->CurrentState == MediaElementState::Playing) {
                playbackStatus_ = L"Playing";
                if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
            }
        });
    musicPlayer_->MediaFailed += ref new ExceptionRoutedEventHandler(
        [this](Platform::Object^, ExceptionRoutedEventArgs^ e) {
            playbackStatus_ = L"Audio playback failed: " + e->ErrorMessage;
            if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
            SetStatus(playbackStatus_);
            libretro_.LogLine("Menu music failed: " + NarrowUtf8(e->ErrorMessage ? e->ErrorMessage->Data() : L"") +
                " (" + NarrowUtf8(musicPlayer_->Source ? musicPlayer_->Source->AbsoluteUri->Data() : L"no source") + ")");
        });
    for (unsigned cue = 0; cue < vita3k_uwp::kInterfaceCueCount; ++cue) {
        cuePlayers_[cue] = ref new MediaElement();
        cuePlayers_[cue]->AutoPlay = false;
        cuePlayers_[cue]->IsLooping = false;
        cuePlayers_[cue]->Volume = uiSoundVolume_;
        cuePlayers_[cue]->Width = 1;
        cuePlayers_[cue]->Height = 1;
        cuePlayers_[cue]->Opacity = 0.01;
        cuePlayers_[cue]->IsHitTestVisible = false;
        Grid::SetRow(cuePlayers_[cue], 1);
        Grid::SetColumn(cuePlayers_[cue], 1);
        root_->Children->Append(cuePlayers_[cue]);
        const unsigned cueIndex = cue;
        cuePlayers_[cue]->MediaOpened += ref new RoutedEventHandler(
            [this,cueIndex](Platform::Object^, RoutedEventArgs^) {
                cueReady_[cueIndex] = true;
            });
        cuePlayers_[cue]->MediaFailed += ref new ExceptionRoutedEventHandler(
            [this,cueIndex](Platform::Object^, ExceptionRoutedEventArgs^) {
                cueReady_[cueIndex] = false;
            });
    }

    Application::Current->Suspending += ref new SuspendingEventHandler(
        [this](Platform::Object^, SuspendingEventArgs^ args) {
            auto deferral = args->SuspendingOperation->GetDeferral();
            create_task([this,deferral] {
                libretro_.StopRunning();
                deferral->Complete();
            });
        });
    Content = root_;
    Window::Current->CoreWindow->KeyDown +=
        ref new TypedEventHandler<Windows::UI::Core::CoreWindow^, Windows::UI::Core::KeyEventArgs^>(
            [this](Windows::UI::Core::CoreWindow^, Windows::UI::Core::KeyEventArgs^ e) {
                if (inGame_) {
                    if (quickMenuOpen_) {
                        // The menu's buttons take the controller; B (or Escape)
                        // goes back to the game.
                        if (e->VirtualKey == Windows::System::VirtualKey::GamepadB ||
                            e->VirtualKey == Windows::System::VirtualKey::Escape) {
                            HideQuickMenu();
                            e->Handled = true;
                        }
                        return;
                    }
                    // The controller belongs to the game. View + Menu (read by
                    // the host) opens the quick menu; Escape does on a keyboard.
                    if (e->VirtualKey == Windows::System::VirtualKey::Escape && gameStarted_ && !loadFailed_) {
                        libretro_.SetPaused(true);
                        ShowQuickMenu();
                    }
                    e->Handled = true;
                    return;
                }
                if (libretro_.Running()) {
                    // B belongs to the game, not frontend navigation. Escape
                    // is an explicit desktop stop action while testing a core.
                    if (e->VirtualKey == Windows::System::VirtualKey::Escape) {
                        libretro_.StopRunning();
                        SetStatus(L"Game execution stopped.");
                    }
                    return;
                }
                if (page_ == 0) return;
                const auto code = e->VirtualKey;
                if (code == Windows::System::VirtualKey::GamepadB ||
                    code == Windows::System::VirtualKey::Escape) {
                    PlayCue(2);
                    ShowPage(page_ == 4 ? 1 : 0);
                }
            });
    // B on Xbox also raises a system back request; one left unhandled closes
    // the app (B at Home, or B in a game, dropped you to the dashboard). The
    // key handler above does all the going back, so every request is taken.
    Windows::UI::Core::SystemNavigationManager::GetForCurrentView()->BackRequested +=
        ref new EventHandler<Windows::UI::Core::BackRequestedEventArgs^>(
            [](Platform::Object^, Windows::UI::Core::BackRequestedEventArgs^ e) { e->Handled = true; });
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
    auto rail = ref new StackPanel();
    rail->Orientation = Orientation::Vertical;
    rail->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    rail->Margin = Thickness(0, 28, 0, 0);
    // The packaged Vita artwork doubles as the app mark.
    auto appLogo = ref new Image();
    appLogo->Width = 40;
    appLogo->Height = 40;
    appLogo->Stretch = Stretch::Uniform;
    appLogo->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    appLogo->Source = ref new BitmapImage(ref new Uri(L"ms-appx:///Assets/Square150x150Logo.png"));
    rail->Children->Append(appLogo);
    auto spacer = ref new Border();
    spacer->Height = 34;
    rail->Children->Append(spacer);

    const wchar_t* glyphs[] = {L"\uE80F", L"\uE8B7", L"\uE713", L"\uE90F"};
    const wchar_t* labels[] = {L"Home", L"Library", L"Settings", L"Tools"};
    // Tools holds developer diagnostics. Release builds (VITA3K_RELEASE_BUILD)
    // leave it out; development builds keep it. See docs/RELEASING.md.
#if defined(VITA3K_RELEASE_BUILD)
    constexpr int visibleTabs = 3;
#else
    constexpr int visibleTabs = 4;
#endif
    for (int i = 0; i < visibleTabs; ++i) {
        const bool selected = page_ == i || (i == 1 && page_ == 4);
        auto nav = ref new Windows::UI::Xaml::Controls::Button();
        nav->Margin = Thickness(0, 0, 0, 10);
        nav->Width = 52;
        nav->Height = 48;
        nav->Padding = Thickness(0);
        nav->BorderThickness = Thickness(0);
        nav->Background = Solid(selected ? Alpha(40, Accent().R, Accent().G, Accent().B)
                                         : Colors::Transparent);
        Skin(nav, 12);
        Windows::UI::Xaml::Automation::AutomationProperties::SetName(
            nav, ref new Platform::String(labels[i]));
        auto inside = ref new Grid();
        if (selected) {
            auto pill = ref new Border();
            pill->Width = 3;
            pill->Height = 20;
            pill->CornerRadius = Windows::UI::Xaml::CornerRadius(2);
            pill->Background = Solid(Accent());
            pill->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
            pill->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
            inside->Children->Append(pill);
        }
        auto symbol = Text(ref new Platform::String(glyphs[i]), 19, false);
        symbol->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets");
        symbol->Foreground = Solid(selected ? Accent() : RgbColor(170, 174, 184));
        symbol->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
        symbol->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        inside->Children->Append(symbol);
        nav->Content = inside;
        nav->GotFocus += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
            PlayCue(0);
        });
        nav->Click += ref new RoutedEventHandler([this,i](Platform::Object^, RoutedEventArgs^) {
            if (page_ == i) return;
            PlayCue(1);
            ShowPage(i);
        });
        rail->Children->Append(nav);
    }
    navigation_->Child = rail;
}
void CatalogShell::ShowPage(int nextPage) {
#if defined(VITA3K_RELEASE_BUILD)
    if (nextPage == 3) nextPage = 0;
#endif
    if (nextPage < 0 || nextPage > 4) nextPage = 0;
    if (page_ == 3 && nextPage != 3) renderer_.Shutdown();
    if (nextPage != page_) PlayCue(4);
    page_ = nextPage;
    BuildNavigation();
    Render();
}
void CatalogShell::Banner(Platform::String^ eyebrow, Platform::String^ headline,
                          Platform::String^ description) {
    auto body = Vertical();
    body->Margin = Thickness(2, 0, 0, 16);
    auto tag = Text(eyebrow, 10, true);
    tag->CharacterSpacing = 220;
    tag->Foreground = Solid(Accent());
    body->Children->Append(tag);
    auto title = Text(headline, 32, false);
    title->FontWeight = FontWeights::Light;
    title->Margin = Thickness(0, 6, 0, 4);
    body->Children->Append(title);
    auto note = Text(description, 13, false, true);
    note->MaxWidth = 640;
    body->Children->Append(note);
    content_->Children->Append(body);
}
void CatalogShell::Section(Platform::String^ label, Platform::String^ note) {
    auto row = Vertical();
    row->Margin = Thickness(2, 18, 0, 12);
    auto heading = Text(label, 17, true);
    row->Children->Append(heading);
    if (note != nullptr && !note->IsEmpty()) {
        auto secondary = Text(note, 12, false, true);
        secondary->Margin = Thickness(0, 3, 0, 0);
        row->Children->Append(secondary);
    }
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
    // Give every title a film-poster-like palette even when offline or when
    // SteamGridDB has no match. Never display made-up third-party game art.
    const Color posterTops[] = {
        RgbColor(99, 34, 63), RgbColor(31, 78, 112),
        RgbColor(40, 92, 91), RgbColor(109, 71, 41),
        RgbColor(70, 50, 113)
    };
    const Color posterBottoms[] = {
        RgbColor(23, 15, 30), RgbColor(14, 22, 35),
        RgbColor(13, 30, 32), RgbColor(33, 19, 16),
        RgbColor(18, 17, 36)
    };
    const auto palette = static_cast<unsigned>(index % 5);
    cover->Background = Gradient(posterTops[palette], posterBottoms[palette]);
    const auto url = ArtworkUri(index);
    if (!url->IsEmpty()) {
        try {
            auto brush = ref new ImageBrush();
            brush->ImageSource = ref new BitmapImage(ref new Uri(url));
            brush->Stretch = Stretch::UniformToFill;
            cover->Background = brush;
        } catch (Platform::Exception^) {
            cover->Background = Gradient(posterTops[palette], posterBottoms[palette]);
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
    tile->Width = 146;
    tile->Height = 236;
    tile->Margin = Thickness(0, 4, 10, 8);
    tile->Padding = Thickness(8);
    tile->BorderThickness = Thickness(0);
    tile->Background = Solid(Colors::Transparent);
    tile->HorizontalContentAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    tile->VerticalContentAlignment = Windows::UI::Xaml::VerticalAlignment::Top;
    Skin(tile, 14);

    auto card = Vertical();
    card->Width = 130;
    auto cover = ref new Border();
    cover->Width = 130;
    cover->Height = 174;
    cover->CornerRadius = Windows::UI::Xaml::CornerRadius(10);
    ConfigureCover(cover, index);

    auto art = ref new Grid();
    auto systemMark = Text(L"PS VITA", 9, true);
    systemMark->CharacterSpacing = 130;
    systemMark->Foreground = Solid(Alpha(210, 255, 255, 255));
    systemMark->Margin = Thickness(10, 10, 0, 0);
    systemMark->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Top;
    art->Children->Append(systemMark);
    if (ArtworkUri(index)->IsEmpty() && games_[index].icon == nullptr) {
        auto v = Text(L"V", 88, true);
        v->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Bahnschrift");
        v->Foreground = Solid(Colors::White);
        v->Opacity = 0.14;
        v->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
        v->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        art->Children->Append(v);
        auto coverCaption = Text(games_[index].name, 12, true);
        coverCaption->Foreground = Solid(Colors::White);
        coverCaption->MaxHeight = 44;
        coverCaption->Margin = Thickness(10, 0, 10, 12);
        coverCaption->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Bottom;
        art->Children->Append(coverCaption);
    }
    cover->Child = art;
    card->Children->Append(cover);
    auto title = Text(games_[index].name, 12, true);
    title->MaxHeight = 34;
    title->Margin = Thickness(2, 9, 0, 1);
    card->Children->Append(title);
    auto kind = Text(games_[index].sourceType, 10, false, true);
    kind->CharacterSpacing = 70;
    kind->Margin = Thickness(2, 1, 0, 0);
    kind->MaxLines = 1;
    kind->TextWrapping = TextWrapping::NoWrap;
    card->Children->Append(kind);
    tile->Content = card;
    // Scale, ring and press feedback come from the shared button template.
    tile->GotFocus += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        PlayCue(0);
    });
    tile->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        PlayCue(1);
        selectedGame_ = static_cast<int>(index);
        ShowPage(4);
    });
    return tile;
}
void CatalogShell::MakeShelf(Platform::String^ heading,
    const std::vector<size_t>& items, StackPanel^ target) {
    if (items.empty()) return;
    auto destination = target != nullptr ? target : content_;
    auto label = Text(heading, 17, true);
    label->Margin = Thickness(2, 10, 0, 8);
    destination->Children->Append(label);
    auto shelf = ref new StackPanel();
    shelf->Orientation = Orientation::Horizontal;
    unsigned order = 0;
    for (const auto index : items) {
        auto tile = MakeTile(index);
        shelf->Children->Append(tile);
        Reveal(tile, 140.0 + 38.0 * std::min(order++, 9u), 16);
    }
    auto scroller = ref new ScrollViewer();
    scroller->Height = 256;
    scroller->HorizontalScrollBarVisibility = ScrollBarVisibility::Hidden;
    scroller->HorizontalScrollMode = ScrollMode::Enabled;
    scroller->VerticalScrollBarVisibility = ScrollBarVisibility::Disabled;
    scroller->VerticalScrollMode = ScrollMode::Disabled;
    scroller->Content = shelf;
    auto lastCell = std::make_shared<int>(0);
    scroller->ViewChanged += ref new EventHandler<ScrollViewerViewChangedEventArgs^>(
        [this,lastCell](Platform::Object^ sender, ScrollViewerViewChangedEventArgs^ event) {
            if (event->IsIntermediate) return;
            auto view = dynamic_cast<ScrollViewer^>(sender);
            if (view == nullptr) return;
            const int cell = static_cast<int>(view->HorizontalOffset / 156.0);
            if (cell != *lastCell) {
                *lastCell = cell;
                PlayCue(3);
            }
        });
    destination->Children->Append(scroller);
}
void CatalogShell::Home() {
    const bool hasGames = !games_.empty();
    // The hero features the game played last, so Play is one press away.
    size_t featured = 0;
    bool playedBefore = false;
    if (hasGames) {
        const auto last = Pref(L"lastPlayed", L"");
        for (size_t i = 0; i < games_.size(); ++i) {
            if (!last->IsEmpty() && games_[i].name == last) { featured = i; playedBefore = true; break; }
        }
    }
    // A greeting and the time, over the scene: the console's own front page.
    {
        auto header = ref new Grid();
        header->Margin = Thickness(2, 0, 4, 14);
        auto left = Vertical();
        auto mood = Text(theme_ == 1 ? L"AURORA  \u2022  ELECTRIC TEAL" : theme_ == 2 ? L"EMBER  \u2022  WARM CINEMA" :
                         theme_ == 3 ? L"GLACIER  \u2022  ARCTIC BLUE" : theme_ == 4 ? L"ORBIT  \u2022  COSMIC VIOLET" :
                         L"ONYX  \u2022  MIDNIGHT & ROSE", 10, true);
        mood->CharacterSpacing = 220;
        mood->Foreground = Solid(Accent());
        left->Children->Append(mood);
        greeting_ = Text(L"", 26, false);
        greeting_->FontWeight = FontWeights::Light;
        greeting_->Margin = Thickness(0, 4, 0, 0);
        left->Children->Append(greeting_);
        header->Children->Append(left);
        clock_ = Text(L"", 26, false);
        clock_->FontWeight = FontWeights::Light;
        clock_->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Right;
        clock_->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Bottom;
        clock_->TextWrapping = TextWrapping::NoWrap;
        header->Children->Append(clock_);
        content_->Children->Append(header);
        UpdateClock();
    }
    auto hero = ref new Border();
    hero->Height = 262;
    hero->CornerRadius = Windows::UI::Xaml::CornerRadius(18);
    hero->Margin = Thickness(0, 0, 0, 20);
    hero->BorderBrush = Solid(Alpha(22, 255, 255, 255));
    hero->BorderThickness = Thickness(1);
    auto heroLayout = ref new Grid();
    heroLayout->Background = Gradient(Card(), Alpha(190, Glow().R, Glow().G, Glow().B));
    if (hasGames && !ArtworkUri(featured)->IsEmpty()) {
        auto art = ref new Image();
        art->Source = ref new BitmapImage(ref new Uri(ArtworkUri(featured)));
        art->Stretch = Stretch::UniformToFill;
        art->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Right;
        art->Width = 520;
        art->Opacity = 0.82;
        heroLayout->Children->Append(art);
    } else {
        // No artwork yet: a large, faint mark keeps the right side from feeling empty.
        auto mark = ref new Image();
        mark->Source = ref new BitmapImage(ref new Uri(L"ms-appx:///Assets/Square150x150Logo.png"));
        mark->Width = 230;
        mark->Height = 230;
        mark->Stretch = Stretch::Uniform;
        mark->Opacity = 0.16;
        mark->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Right;
        mark->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        mark->Margin = Thickness(0, 0, 54, 0);
        heroLayout->Children->Append(mark);
    }
    auto fade = ref new LinearGradientBrush();
    fade->StartPoint = Point(0, 0.5);
    fade->EndPoint = Point(1, 0.5);
    auto stop = ref new GradientStop();
    stop->Color = Alpha(235, Bg().R, Bg().G, Bg().B);
    stop->Offset = 0;
    fade->GradientStops->Append(stop);
    stop = ref new GradientStop();
    stop->Color = Alpha(150, Bg().R, Bg().G, Bg().B);
    stop->Offset = 0.38;
    fade->GradientStops->Append(stop);
    stop = ref new GradientStop();
    stop->Color = Alpha(0, Bg().R, Bg().G, Bg().B);
    stop->Offset = 1;
    fade->GradientStops->Append(stop);
    auto veil = ref new Border();
    veil->Background = fade;
    heroLayout->Children->Append(veil);

    auto copy = Vertical();
    copy->Margin = Thickness(34, 0, 20, 0);
    copy->MaxWidth = 430;
    copy->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    copy->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    auto kicker = Text(!hasGames ? L"WELCOME TO VITA3K" : (playedBefore ? L"CONTINUE PLAYING" : L"FEATURED"), 10, true);
    kicker->CharacterSpacing = 240;
    kicker->Foreground = Solid(Accent());
    copy->Children->Append(kicker);
    auto headline = Text(hasGames ? games_[featured].name : L"Every game.\nA new world.", 36, false);
    headline->FontWeight = FontWeights::Light;
    headline->MaxLines = 2;
    headline->Margin = Thickness(0, 8, 0, 10);
    copy->Children->Append(headline);
    auto detail = Text(hasGames
        ? L"Press Play to start it, or open its page for details and cover art."
        : L"Your Vita collection, on the big screen. Add a game or a folder to begin.", 13, false, true);
    detail->MaxWidth = 360;
    detail->Margin = Thickness(0, 0, 0, 18);
    copy->Children->Append(detail);
    auto controls = ref new StackPanel();
    controls->Orientation = Orientation::Horizontal;
    auto primary = Button(hasGames ? L"\u25B6  Play" : L"Add a game", true);
    primary->MinWidth = 132;
    primary->Click += ref new RoutedEventHandler([this,hasGames,featured](Platform::Object^, RoutedEventArgs^) {
        if (!hasGames) ChooseGameFile();
        else AttemptBoot(featured);
    });
    controls->Children->Append(primary);
    auto secondary = Button(hasGames ? L"Details" : L"Choose a folder");
    secondary->Click += ref new RoutedEventHandler([this,hasGames,featured](Platform::Object^, RoutedEventArgs^) {
        if (hasGames) { selectedGame_ = static_cast<int>(featured); ShowPage(4); }
        else ChooseGameFolder();
    });
    controls->Children->Append(secondary);
    copy->Children->Append(controls);
    heroLayout->Children->Append(copy);
    hero->Child = heroLayout;
    content_->Children->Append(hero);

    SetupCard();

    if (hasGames) {
        std::vector<size_t> all, folders, packages;
        for (size_t i = 0; i < games_.size(); ++i) {
            all.push_back(i);
            if (games_[i].format == vita3k_uwp::GameFormat::GameFolder) folders.push_back(i);
            else packages.push_back(i);
        }
        MakeShelf(L"Your collection", all);
        MakeShelf(L"Game folders", folders);
        MakeShelf(L"Archives and packages", packages);
    } else {
        auto heading = Text(L"Make it yours", 17, true);
        heading->Margin = Thickness(2, 2, 0, 12);
        content_->Children->Append(heading);
        auto promos = ref new StackPanel();
        promos->Orientation = Orientation::Horizontal;
        const wchar_t* headlines[] = {
            L"BUILD YOUR LIBRARY", L"MAKE IT PERSONAL", L"SET THE MOOD"
        };
        const wchar_t* descriptions[] = {
            L"Import your Vita games", L"Covers, themes and art", L"Original menu soundtracks"
        };
        const wchar_t* symbols[] = {L"\uE8B7", L"\uE713", L"\uE8D6"};
        for (int i = 0; i < 3; ++i) {
            auto promo = ref new Windows::UI::Xaml::Controls::Button();
            promo->Width = 226;
            promo->Height = 104;
            promo->Margin = Thickness(0, 4, 12, 8);
            promo->BorderThickness = Thickness(0);
            promo->Padding = Thickness(0);
            promo->HorizontalContentAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
            promo->VerticalContentAlignment = Windows::UI::Xaml::VerticalAlignment::Stretch;
            Skin(promo, 14);
            auto surface = ref new Border();
            surface->CornerRadius = Windows::UI::Xaml::CornerRadius(14);
            surface->Background = Gradient(Card(), i == 0 ? Glow() : RgbColor(36, 35, 44));
            surface->BorderBrush = Solid(Alpha(20, 255, 255, 255));
            surface->BorderThickness = Thickness(1);
            auto body = ref new Grid();
            auto symbol = Text(ref new Platform::String(symbols[i]), 54, false);
            symbol->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Segoe MDL2 Assets");
            symbol->Foreground = Solid(Accent());
            symbol->Opacity = 0.16;
            symbol->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Right;
            symbol->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
            symbol->Margin = Thickness(0, 0, 18, 0);
            body->Children->Append(symbol);
            auto caption = Vertical();
            caption->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Bottom;
            caption->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
            caption->Margin = Thickness(16, 0, 12, 14);
            auto tag = Text(ref new Platform::String(headlines[i]), 10, true);
            tag->CharacterSpacing = 140;
            caption->Children->Append(tag);
            auto desc = Text(ref new Platform::String(descriptions[i]), 12, false, true);
            desc->Margin = Thickness(0, 3, 0, 0);
            caption->Children->Append(desc);
            body->Children->Append(caption);
            surface->Child = body;
            promo->Content = surface;
            promo->GotFocus += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
                PlayCue(0);
            });
            promo->Click += ref new RoutedEventHandler([this,i](Platform::Object^, RoutedEventArgs^) {
                PlayCue(1);
                if (i == 0) ChooseGameFile();
                else ShowPage(2);
            });
            promos->Children->Append(promo);
            Reveal(promo, 160.0 + 70.0 * i, 14);
        }
        auto promoScroll = ref new ScrollViewer();
        promoScroll->HorizontalScrollBarVisibility = ScrollBarVisibility::Hidden;
        promoScroll->HorizontalScrollMode = ScrollMode::Enabled;
        promoScroll->VerticalScrollBarVisibility = ScrollBarVisibility::Disabled;
        promoScroll->VerticalScrollMode = ScrollMode::Disabled;
        promoScroll->Content = promos;
        content_->Children->Append(promoScroll);
    }
    auto note = Text(L"PRE-ALPHA   \u2022   Some games don't run yet. If one fails, Settings has a button to save the log.", 10, false, true);
    note->CharacterSpacing = 40;
    note->Margin = Thickness(2, 16, 0, 6);
    content_->Children->Append(note);
}
void CatalogShell::Library() {
    Banner(L"COLLECTION  /  PLAYSTATION VITA",
        L"Your library.", L"Everything you've added, organized in one place.");
    auto actions = ActionRow(146);
    actions->Margin = Thickness(0, 0, 0, 12);
    auto add = Button(L"Add a game", true);
    add->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseGameFile(); });
    actions->Children->Append(add);
    auto folder = Button(L"Choose folder");
    folder->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseGameFolder(); });
    actions->Children->Append(folder);
    auto refresh = Button(L"Rescan");
    refresh->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ScanGames(); });
    actions->Children->Append(refresh);
    auto covers = Button(L"Find cover art");
    covers->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { FetchMissingArtwork(); });
    actions->Children->Append(covers);
    content_->Children->Append(actions);

    searchBox_ = ref new TextBox();
    searchBox_->PlaceholderText = L"Search your Vita games";
    searchBox_->Text = search_;
    searchBox_->Width = 420;
    searchBox_->Height = 42;
    searchBox_->FontSize = 14;
    searchBox_->Padding = Thickness(14, 9, 10, 9);
    searchBox_->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    searchBox_->Background = Solid(Card());
    searchBox_->Foreground = Solid(Colors::White);
    searchBox_->BorderThickness = Thickness(1);
    searchBox_->BorderBrush = Solid(Alpha(40, 255, 255, 255));
    searchBox_->TextChanged += ref new TextChangedEventHandler(
        [this](Platform::Object^, TextChangedEventArgs^) {
            search_ = searchBox_->Text;
            RefreshLibraryItems(); // Only replace results; keep typing focus.
        });
    content_->Children->Append(searchBox_);
    libraryResults_ = Vertical();
    libraryResults_->Margin = Thickness(0, 14, 0, 0);
    content_->Children->Append(libraryResults_);
    RefreshLibraryItems();
}
void CatalogShell::RefreshLibraryItems() {
    if (libraryResults_ == nullptr) return;
    libraryResults_->Children->Clear();
    if (games_.empty()) {
        auto message = Text(L"No titles yet. Add a VPK, ZIP, PKG, VCI or an extracted game folder.", 14, false, true);
        message->Margin = Thickness(2, 16, 0, 0);
        libraryResults_->Children->Append(message);
        return;
    }
    std::vector<size_t> matches;
    for (size_t i = 0; i < games_.size(); ++i) {
        if (ContainsIgnoreCase(games_[i].name, search_)) matches.push_back(i);
    }
    MakeShelf(L"All titles", matches, libraryResults_);
    if (matches.empty()) {
        libraryResults_->Children->Append(Text(L"No games match that search.", 13, false, true));
    }
    auto warning = Text(L"Pre-alpha: compatibility varies from game to game.", 10, false, true);
    warning->Margin = Thickness(2, 10, 0, 0);
    libraryResults_->Children->Append(warning);
}
void CatalogShell::GameDetails() {
    if (selectedGame_ < 0 || static_cast<size_t>(selectedGame_) >= games_.size()) {
        ShowPage(1);
        return;
    }
    const size_t index = static_cast<size_t>(selectedGame_);
    auto back = Button(L"\u2039  All games");
    back->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ShowPage(1); });
    back->Margin = Thickness(0, 0, 0, 14);
    content_->Children->Append(back);

    auto feature = ref new Border();
    feature->CornerRadius = Windows::UI::Xaml::CornerRadius(18);
    feature->MinHeight = 300;
    feature->Padding = Thickness(30, 24, 28, 24);
    feature->Background = Gradient(Card(), Glow());
    feature->BorderBrush = Solid(Alpha(22, 255, 255, 255));
    feature->BorderThickness = Thickness(1);

    auto layout = ref new Grid();
    auto artColumn = ref new ColumnDefinition();
    artColumn->Width = GridLength(214);
    auto infoColumn = ref new ColumnDefinition();
    infoColumn->Width = GridLength(1, GridUnitType::Star);
    layout->ColumnDefinitions->Append(artColumn);
    layout->ColumnDefinitions->Append(infoColumn);

    auto cover = ref new Border();
    cover->Width = 190;
    cover->Height = 254;
    cover->CornerRadius = Windows::UI::Xaml::CornerRadius(12);
    cover->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
    cover->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    ConfigureCover(cover, index);
    if (ArtworkUri(index)->IsEmpty() && games_[index].icon == nullptr) {
        auto fallback = Vertical();
        fallback->Margin = Thickness(16, 14, 16, 20);
        fallback->Children->Append(Text(L"PS VITA", 10, true));
        auto emblem = Text(L"V", 120, true);
        emblem->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Bahnschrift");
        emblem->Opacity = 0.21;
        emblem->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
        fallback->Children->Append(emblem);
        fallback->Children->Append(Text(games_[index].name, 14, true));
        cover->Child = fallback;
    }
    layout->Children->Append(cover);

    auto right = Vertical();
    right->Margin = Thickness(22, 8, 6, 4);
    Grid::SetColumn(right, 1);
    auto category = Text(L"PLAYSTATION VITA  /  " + games_[index].sourceType, 10, true);
    category->CharacterSpacing = 160;
    category->Foreground = Solid(Accent());
    right->Children->Append(category);
    auto title = Text(games_[index].name, 32, true);
    title->FontWeight = FontWeights::Light;
    title->Margin = Thickness(0, 8, 0, 10);
    title->MaxWidth = 560;
    title->MaxLines = 2;
    right->Children->Append(title);
    // Whether Vita3K has installed this game already (its title ID folder in ux0/app).
    std::wstring titleId;
    if (games_[index].directory) titleId = games_[index].directory->Name->Data();
    const bool installed = titleId.size() == 9 && HasFiles(FirmwareRoot() + L"\\ux0\\app\\" + titleId);
    auto note = Text(installed
        ? L"Ready to play."
        : L"The first start installs the game into Vita3K's data folder, then it starts straight away next time.",
        13, false, true);
    note->MaxWidth = 520;
    right->Children->Append(note);

    auto actions = ref new StackPanel();
    actions->Orientation = Orientation::Horizontal;
    actions->Margin = Thickness(0, 22, 0, 8);
    auto play = Button(L"▶  Play", true);
    play->MinWidth = 168;
    play->Height = 52;
    play->FontSize = 18;
    play->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        AttemptBoot(index);
    });
    actions->Children->Append(play);
    auto image = Button(L"Change cover");
    image->Margin = Thickness(12, 0, 0, 0);
    image->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        ChooseCustomArtwork(index);
    });
    actions->Children->Append(image);
    auto find = Button(L"Find cover online");
    find->Margin = Thickness(12, 0, 0, 0);
    find->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        FetchArtwork(index);
    });
    actions->Children->Append(find);
    right->Children->Append(actions);
    if (games_[index].format == vita3k_uwp::GameFormat::Pkg) {
        auto license = Button(L"Choose the game's license file");
        license->Margin = Thickness(0, 10, 0, 0);
        license->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
            ChooseLicenseForGame(index);
        });
        right->Children->Append(license);
    }
#if !defined(VITA3K_RELEASE_BUILD)
    auto inspect = Button(L"Inspect game (developer)");
    inspect->Margin = Thickness(0, 10, 0, 0);
    inspect->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        InspectLaunch(index);
    });
    right->Children->Append(inspect);
    auto validation = Text(L"Source check: " + games_[index].validation, 11, false, true);
    validation->MaxWidth = 560;
    validation->Margin = Thickness(0, 8, 0, 4);
    validation->Opacity = 0.7;
    right->Children->Append(validation);
#endif
    layout->Children->Append(right);
    feature->Child = layout;
    content_->Children->Append(feature);
    // The controller lands on Play.
    play->Loaded += ref new RoutedEventHandler([play](Platform::Object^, RoutedEventArgs^) {
        play->Focus(Windows::UI::Xaml::FocusState::Programmatic);
    });
}
void CatalogShell::Render() {
    content_->Children->Clear();
    clock_ = nullptr;
    greeting_ = nullptr;
    musicStatus_ = nullptr;
    libraryResults_ = nullptr;
    switch(page_) {
    case 1: Library(); break;
    case 2: Settings(); break;
    case 3: Diagnostics(); break;
    case 4: GameDetails(); break;
    default: Home(); break;
    }
    // Each page settles in: its blocks rise and fade in one after another.
    // Re-renders on the same page (theme change, toggles) only fade, so the
    // layout never jumps while someone is working in it.
    const bool newPage = page_ != shownPage_;
    shownPage_ = page_;
    unsigned order = 0;
    for (auto child : content_->Children) {
        Reveal(child, 45.0 * std::min(order++, 8u), newPage ? 18 : 0);
    }
}

void CatalogShell::Settings() {
    Banner(L"SETTINGS", L"Set up once. Play anywhere.",
        L"Firmware, where your games and Vita3K data live, emulation options, and the look and sound of the app.");
    FirmwareSettings();
    StorageSettings();
    EmulationSettings();
    Section(L"Appearance", L"Five moods. One beautifully simple library.");
    const wchar_t* styles[] = {
        L"Onyx", L"Aurora", L"Ember", L"Glacier", L"Orbit"
    };
    const wchar_t* captions[] = {
        L"Midnight & rose", L"Electric teal", L"Warm cinema",
        L"Arctic blue", L"Cosmic violet"
    };
    const Color samples[] = {
        RgbColor(111, 34, 56), RgbColor(23, 95, 94),
        RgbColor(127, 65, 37), RgbColor(48, 95, 145),
        RgbColor(83, 55, 131)
    };
    auto palettes = ref new StackPanel();
    palettes->Orientation = Orientation::Horizontal;
    for (int i = 0; i < 5; ++i) {
        const bool active = theme_ == i;
        auto choice = ref new Windows::UI::Xaml::Controls::Button();
        choice->Width = 148;
        choice->Height = 84;
        choice->Margin = Thickness(0, 4, 10, 8);
        choice->Padding = Thickness(0);
        choice->BorderThickness = Thickness(active ? 2 : 1);
        choice->BorderBrush = Solid(active ? Accent() : Alpha(34, 255, 255, 255));
        choice->HorizontalContentAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
        choice->VerticalContentAlignment = Windows::UI::Xaml::VerticalAlignment::Stretch;
        Skin(choice, 14);
        auto swatch = ref new Border();
        swatch->Background = Gradient(RgbColor(23, 23, 29), samples[i]);
        swatch->Padding = Thickness(12);
        swatch->CornerRadius = Windows::UI::Xaml::CornerRadius(13);
        auto caption = Vertical();
        caption->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Bottom;
        auto name = Text(ref new Platform::String(styles[i]), 15, true);
        caption->Children->Append(name);
        auto sub = Text(ref new Platform::String(captions[i]), 11, false, true);
        sub->Foreground = Solid(RgbColor(214, 212, 225));
        sub->Margin = Thickness(0, 4, 0, 0);
        caption->Children->Append(sub);
        swatch->Child = caption;
        choice->Content = swatch;
        choice->GotFocus += ref new RoutedEventHandler(
            [this](Platform::Object^, RoutedEventArgs^) { PlayCue(0); });
        choice->Click += ref new RoutedEventHandler(
            [this,i](Platform::Object^, RoutedEventArgs^) { SaveTheme(i); });
        palettes->Children->Append(choice);
    }
    auto themeScroll = ref new ScrollViewer();
    themeScroll->HorizontalScrollBarVisibility = ScrollBarVisibility::Hidden;
    themeScroll->HorizontalScrollMode = ScrollMode::Enabled;
    themeScroll->VerticalScrollBarVisibility = ScrollBarVisibility::Disabled;
    themeScroll->Content = palettes;
    content_->Children->Append(themeScroll);

    Section(L"Home menu soundtrack", L"Original loopable music, your own audio, or silence.");
    const wchar_t* names[] = {
        L"Off", L"Afterglow", L"Neon Arcade", L"Stardust", L"Tidal", L"Your own track"
    };
    auto music = ActionRow(142);
    for (int i = 0; i < 6; ++i) {
        auto b = Button(ref new Platform::String(names[i]), track_ == i);
        b->Click += ref new RoutedEventHandler([this,i](Platform::Object^, RoutedEventArgs^) {
            if (i == 5) PickCustomMusic();
            else SetMusic(i);
        });
        music->Children->Append(b);
    }
    content_->Children->Append(music);
    musicStatus_ = Text(L"Audio: " + playbackStatus_, 12, false, true);
    musicStatus_->Margin = Thickness(3, 11, 0, 11);
    content_->Children->Append(musicStatus_);
    content_->Children->Append(Text(L"MUSIC VOLUME", 12, true, true));
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
    auto soundRow = ActionRow(200);
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
    content_->Children->Append(Text(L"INTERFACE VOLUME", 12, true, true));
    content_->Children->Append(cueVolume);

    Section(L"Game artwork", L"Use original cover images or bring in your own.");
    auto art = Vertical();
    art->Children->Append(Text(
        L"SteamGridDB cover matching requires a personal API key. The search uses each "
        L"game's title and only accepts an exact title match, to avoid mismatched artwork. "
        L"If no match is available, your game keeps its themed cover.", 13, false, true));
    auto password = ref new PasswordBox();
    password->PlaceholderText = L"SteamGridDB API key (saved in Windows PasswordVault)";
    password->Height = 40;
    password->Width = 460;
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
        12, false, true));
    art->Children->Append(Text(
        L"Select a game card to import a custom PNG or JPEG. Custom artwork takes priority.",
        12, false, true));
    content_->Children->Append(Panel(art));

    Section(L"Diagnostics", L"Where to look when something goes wrong.");
    auto advanced = Vertical();
    advanced->Children->Append(Text(
        L"When a game or install goes wrong, the log says why. Save it to your drive and open it on a PC, "
        L"or look at the end of it here.", 13, false, true));
    auto logActions = ref new StackPanel();
    logActions->Orientation = Orientation::Horizontal;
    logActions->Margin = Thickness(0, 12, 0, 0);
    auto saveLog = Button(L"Save log to your drive", true);
    saveLog->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { SaveLogToDrive(); });
    logActions->Children->Append(saveLog);
    auto showLog = Button(L"Show the recent log");
    showLog->Margin = Thickness(10, 0, 0, 0);
    logActions->Children->Append(showLog);
    advanced->Children->Append(logActions);
    auto logView = Text(L"", 10, false, true);
    logView->FontFamily = ref new Windows::UI::Xaml::Media::FontFamily(L"Consolas");
    logView->IsTextSelectionEnabled = true;
    logView->TextWrapping = TextWrapping::Wrap;
    logView->TextTrimming = TextTrimming::None;
    auto logScroll = ref new ScrollViewer();
    logScroll->MaxHeight = 320;
    logScroll->Margin = Thickness(0, 12, 0, 0);
    logScroll->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
    logScroll->Content = logView;
    advanced->Children->Append(logScroll);
    showLog->Click += ref new RoutedEventHandler([this,logView,logScroll](Platform::Object^, RoutedEventArgs^) {
        logView->Text = ref new Platform::String(RecentLogText().c_str());
        logScroll->Visibility = Windows::UI::Xaml::Visibility::Visible;
        logScroll->ChangeView(nullptr, ref new Platform::Box<double>(1e9), nullptr, true);
    });
    advanced->Children->Append(Text(
        L"It is also in the Device Portal: File Explorer, LocalAppData, this app, LocalState, vita3k-runtime.log.",
        11, false, true));
#if !defined(VITA3K_RELEASE_BUILD)
    // D3D12's debug layer checks every call the graphics driver makes, which
    // costs frame time; it is for chasing a graphics bug, so it is off unless
    // asked for here. It can only start with the app (before any device).
    {
        auto checks = Button(Pref(L"debug.d3d12")->Equals(L"1")
            ? L"Graphics debug checks: On (slower)" : L"Graphics debug checks: Off");
        checks->Margin = Thickness(0, 12, 0, 0);
        checks->Click += ref new RoutedEventHandler([this,checks](Platform::Object^, RoutedEventArgs^) {
            const bool on = !Pref(L"debug.d3d12")->Equals(L"1");
            PutPref(L"debug.d3d12", on ? L"1" : L"0");
            checks->Content = ref new Platform::String(on ? L"Graphics debug checks: On after a restart (slower)" : L"Graphics debug checks: Off after a restart");
        });
        advanced->Children->Append(checks);
    }
    // Development builds only; release builds ship without the Tools page.
    auto openTools = Button(L"Developer diagnostics");
    openTools->Margin = Thickness(0, 12, 0, 0);
    openTools->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ShowPage(3);
    });
    advanced->Children->Append(openTools);
#endif
    content_->Children->Append(Panel(advanced));
    RestorePaths();
}
void CatalogShell::Diagnostics() {
    Banner(L"UNDER THE HOOD", L"Developer laboratory.",
        L"Vita3K CPU, graphics and emulator diagnostics.");
    Section(L"Vita3K core");
    auto libretroBox = Vertical();
    libretroBox->Children->Append(Text(
        L"Check for a Vita3K core DLL inside the app package. "
        L"Verify the core API before attempting to load a game. "
        L"The desktop DLL cannot automatically run as UWP. Game execution needs a "
        L"successfully ported core and working Vulkan graphics (possibly Mesa Dozen).",
        13, false, true));
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
    libretroResult_ = Text(L"Libretro core not probed. No UWP-compatible core DLL bundled yet.", 14, true);
    libretroBox->Children->Append(libretroResult_);
    content_->Children->Append(Panel(libretroBox));

    Section(L"Vulkan over Direct3D 12 / Mesa Dozen");
    auto dzn = Vertical();
    dzn->Children->Append(Text(
        L"Mesa Dozen translates Vulkan to native D3D12 on Windows. "
        L"On Xbox, the Vulkan loader and Dozen ICD must "
        L"run inside the app package. "
        L"A native D3D12 test frame is not evidence that Vulkan works.", 13, false, true));
    auto dznResult = Text(L"No Vulkan / Dozen probe run on this Xbox.", 14, true);
    auto dznButton = Button(L"Probe actual packaged Vulkan device");
    dznButton->Click += ref new RoutedEventHandler(
        [this,dznResult](Platform::Object^, RoutedEventArgs^) {
            dznResult->Text = StringOf(ProbePackagedVulkan());
        });
    dzn->Children->Append(dznButton);
    dzn->Children->Append(dznResult);
    content_->Children->Append(Panel(dzn));

    Section(L"Vita CPU test", L"Run a short ARMv7 program through Dynarmic.");
    auto cpuPanel = Vertical();
    cpuPanel->Children->Append(Text(
        L"This test runs three ARM instructions through Vita3K's Dynarmic CPU: "
        L"MOV, ADD and SVC. A pass means Xbox executed ARM guest code and returned "
        L"R0 = 12. It does not launch a Vita game.", 13, false, true));
    auto cpuReport = Text(L"ARM CPU test not started.", 13, false, true);
    auto runCpu = Button(L"Run Vita ARM instructions", true);
    runCpu->Click += ref new RoutedEventHandler(
        [cpuReport](Platform::Object^, RoutedEventArgs^) {
            cpuReport->Text = StringOf(RunVitaGuestArmCpuProbe());
        });
    cpuPanel->Children->Append(runCpu);
    cpuPanel->Children->Append(cpuReport);
    content_->Children->Append(Panel(cpuPanel));

    Section(L"Upstream Vita3K memory allocator");
    auto core = Vertical();
    core->Children->Append(Text(
        L"This runs upstream bitmap allocation, reservation and release routines. "
        L"It is not a Dynarmic emulator boot.", 13, false, true));
    auto test = Button(L"Run real upstream memory test", true);
    test->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ProbeCore();
    });
    core->Children->Append(test);
    coreResult_ = Text(L"Not tested on this Xbox.", 14, true);
    core->Children->Append(coreResult_);
    content_->Children->Append(Panel(core));

    Section(L"Virtual address space and JIT permissions");
    auto memory = Vertical();
    memory->Children->Append(Text(
        L"Check read/write pages, RW-to-RX protection and a reserved 4 GiB guest address "
        L"range. This does not execute generated ARM instructions.", 13, false, true));
    auto probe = Button(L"Probe memory compatibility");
    probe->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ProbeMemory();
    });
    memory->Children->Append(probe);
    memoryResult_ = Text(L"Not tested on this Xbox.", 14, true);
    memory->Children->Append(memoryResult_);
    auto jitDescription = Text(
        L"JIT liveness runs a tiny native x64 function generated into a writable page, "
        L"then switches it to executable-only/readable via VirtualProtectFromApp. "
        L"Only run this on the Xbox after testing the normal memory probe. "
        L"An Xbox code-integrity policy could terminate the app instead of returning an error. "
        L"Passing is not proof that the Vita CPU or Dynarmic works.", 13, false, true);
    memory->Children->Append(jitDescription);
    auto jitButton = Button(L"Execute on-demand JIT machine-code test");
    auto jitReport = Text(L"Generated machine code has not been executed.", 13, true);
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
        13, false, true));
    renderSurface_ = ref new SwapChainPanel();
    renderSurface_->Width = 960;
    renderSurface_->Height = 544;
    renderSurface_->Margin = Thickness(0, 10, 0, 10);
    graphics->Children->Append(renderSurface_);
    rendererResult_ = Text(L"Renderer not initialized.", 13, true);
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

void CatalogShell::ApplySurfaceColors() {
    // The rail and the status line are tinted glass over the moving scene.
    root_->Background = Solid(Bg());
    const Color rail = RailColor();
    navigation_->Background = Solid(Alpha(178, rail.R, rail.G, rail.B));
    if (statusBar_ != nullptr) statusBar_->Background = Solid(Alpha(120, Bg().R, Bg().G, Bg().B));
    if (status_ != nullptr) status_->Foreground = Solid(Muted());
}
void CatalogShell::UpdateClock() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (clock_ != nullptr) {
        const int hour12 = now.wHour % 12 == 0 ? 12 : now.wHour % 12;
        wchar_t text[32] = {};
        swprintf_s(text, L"%d:%02d %s", hour12, now.wMinute, now.wHour < 12 ? L"AM" : L"PM");
        clock_->Text = ref new Platform::String(text);
    }
    if (greeting_ != nullptr) {
        const wchar_t* line = now.wHour < 5 ? L"Late-night session." : now.wHour < 12 ? L"Good morning." :
                              now.wHour < 17 ? L"Good afternoon." : now.wHour < 22 ? L"Good evening." : L"Late-night session.";
        greeting_->Text = ref new Platform::String(line);
    }
}
void CatalogShell::SaveTheme(int theme) {
    if (theme < 0 || theme > 4) return;
    theme_ = theme;
    PutPref(L"theme", StringOf(std::to_wstring(theme)));
    PlayCue(5);
    ApplySurfaceColors();
    backdrop_.SetTheme(theme_, Bg(), Accent(), Glow());
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
    musicPlayer_->Source = nullptr;
    customMusicStream_ = nullptr;
    if (track > 0 && track < 5) PlayBundledMusic(track);
    else if (track == 5) PlayCustomMusic();
    else playbackStatus_ = L"Music off";
    Render();
    if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
}
void CatalogShell::PlayBundledMusic(int track) {
    if (track < 1 || track > 4) return;
    // Original tracks shipped in the package (scripts/music/compose.py).
    static const wchar_t* files[4] = { L"afterglow", L"neon-arcade", L"stardust", L"tidal" };
    musicPlayer_->IsLooping = true;
    musicPlayer_->Source = ref new Uri(ref new Platform::String(
        (std::wstring(L"ms-appx:///Assets/Music/") + files[track - 1] + L".wav").c_str()));
    playbackStatus_ = L"Playing";
    if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
    musicPlayer_->Play(); // MediaOpened also starts it once ready.
}
void CatalogShell::PlayCustomMusic() {
    auto access = StorageApplicationPermissions::FutureAccessList;
    if (!access->ContainsItem(L"menu-track")) {
        playbackStatus_ = L"Choose a custom MP3, WAV or M4A file first.";
        if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
        return;
    }
    playbackStatus_ = L"Opening custom soundtrack...";
    create_task(access->GetFileAsync(L"menu-track"))
    .then([this](StorageFile^ file) {
        return create_task(file->OpenAsync(FileAccessMode::Read)).then(
            [this,file](IRandomAccessStream^ stream) {
                if (track_ != 5) return;
                // Retain source for the entire playback lifetime.
                customMusicStream_ = stream;
                musicPlayer_->IsLooping = true;
                musicPlayer_->SetSource(customMusicStream_, file->ContentType);
                musicPlayer_->Play();
                playbackStatus_ = L"Opening " + file->Name + L"...";
                if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
            });
    }).then([this](task<void> work) {
        try { work.get(); }
        catch (Platform::Exception^ error) {
            playbackStatus_ = L"Couldn't open custom music: " + error->Message;
            if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
        }
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
    RefreshFirmwareStatus();
    if (addonsLabel_ != nullptr) {
        auto addons = Pref(L"storage.addons", L"");
        addonsLabel_->Text = addons->IsEmpty() ? ref new Platform::String(L"No updates folder chosen yet.") : L"Updates folder: " + addons;
    }
    if (dataLabel_ != nullptr) {
        auto chosen = Pref(L"storage.dataRoot", L"");
        dataLabel_->Text = chosen->IsEmpty() ? ref new Platform::String(L"Internal storage") : chosen;
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
namespace {
// The three firmware parts, as the core installs them.
struct FirmwarePart {
    const wchar_t* title;
    const wchar_t* partition;
    bool required;
    const wchar_t* versionKey;
    const wchar_t* about;
};
const FirmwarePart kFirmwareParts[3] = {
    { L"System firmware", L"vs0", true, L"firmware.version.vs0",
      L"Sony's PS Vita system update. It downloads as PSP2UPDAT.PUP; older copies are named PSVUPDAT.PUP. They are the same thing." },
    { L"Preinstalled package", L"pd0", false, L"firmware.version.pd0",
      L"A separate, smaller Sony download with the Vita's built-in apps. Some games want it." },
    { L"Fonts", L"sa0", false, L"firmware.version.sa0",
      L"A separate Sony download with the system fonts. Without it some text is missing." },
};

std::wstring InternalDataRoot() {
    return std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\Vita3KSystem";
}
// Where Vita3K keeps firmware, installed games and saves: the app's own
// storage, or a folder the user chose (usually on a USB drive).
std::wstring DataRoot() {
    auto chosen = Pref(L"storage.dataRoot", L"");
    return chosen->IsEmpty() ? InternalDataRoot() : std::wstring(chosen->Data());
}
std::wstring FirmwareRoot() {
    return DataRoot() + L"\\vita3k";
}
bool HasFiles(const std::wstring& directory) {
    std::error_code ec;
    return std::filesystem::is_directory(directory, ec) && !std::filesystem::is_empty(directory, ec);
}
// True when the core will be able to read this path where it is.
bool CanOpenFromApp(const std::wstring& path) {
    HANDLE handle = CreateFile2FromAppW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    CloseHandle(handle);
    return true;
}
std::string NarrowUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &result[0], count, nullptr, nullptr);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}
std::wstring WidenUtf8(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (count <= 1) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], count);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}
// 0x03740000 -> "3.74"
std::wstring FirmwareVersionText(std::uint32_t version) {
    if (!version) return L"";
    wchar_t text[16] = {};
    swprintf_s(text, L"%X.%02X", (version >> 24) & 0xFF, (version >> 16) & 0xFF);
    return text;
}
const wchar_t* PupKindName(std::uint32_t partitions) {
    if (partitions & 2) return L"System firmware";
    if (partitions & 4) return L"Preinstalled package";
    if (partitions & 8) return L"Font package";
    return nullptr;
}
// The most telling line of a failure's log text: the last error, else the last line.
std::wstring ProblemLine(const std::wstring& text) {
    std::wstring best, last;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        if (line.empty()) continue;
        last = line;
        std::wstring lower = line;
        for (auto& c : lower) c = static_cast<wchar_t>(towlower(c));
        if (lower.find(L"fail") != std::wstring::npos || lower.find(L"error") != std::wstring::npos ||
            lower.find(L"could not") != std::wstring::npos || lower.find(L"crash") != std::wstring::npos ||
            lower.find(L"denied") != std::wstring::npos || lower.find(L"|e|") != std::wstring::npos)
            best = line;
    }
    std::wstring pick = best.empty() ? last : best;
    if (pick.size() > 220) pick = pick.substr(0, 220) + L"...";
    return pick.empty() ? std::wstring(L"Save the log in Settings, Diagnostics, for the details.") : pick;
}
struct FirmwareResult {
    std::wstring name;
    std::uint32_t partitions = 0;
    std::uint32_t headerVersion = 0;
    bool recognised = false;
    bool installed = false;
    std::wstring version;
    std::wstring detail;
};
}

bool CatalogShell::SystemFirmwareInstalled() {
    return HasFiles(FirmwareRoot() + L"\\vs0");
}

void CatalogShell::SetupCard() {
    const bool storageDone = !Pref(L"storage.dataRoot", L"")->IsEmpty() || Pref(L"storage.internal", L"0")->Equals(L"1");
    const bool firmwareDone = SystemFirmwareInstalled();
    const bool gamesDone = StorageApplicationPermissions::FutureAccessList->ContainsItem(L"vita-games") || !games_.empty();
    if (storageDone && firmwareDone && gamesDone) return;

    auto setup = Vertical();
    auto kick = Text(L"GET STARTED", 10, true);
    kick->CharacterSpacing = 300;
    kick->Foreground = Solid(Accent());
    setup->Children->Append(kick);
    auto heading = Text(L"Three steps and you're playing", 22, false);
    heading->FontWeight = FontWeights::Light;
    heading->Margin = Thickness(0, 6, 0, 14);
    setup->Children->Append(heading);

    auto step = [this,setup](int number, bool done, bool current, Platform::String^ title, Platform::String^ detail) {
        auto row = ref new Grid();
        row->Margin = Thickness(0, 0, 0, 14);
        auto c0 = ref new ColumnDefinition();
        c0->Width = GridLength(44);
        auto c1 = ref new ColumnDefinition();
        c1->Width = GridLength(1, GridUnitType::Star);
        row->ColumnDefinitions->Append(c0);
        row->ColumnDefinitions->Append(c1);
        auto badge = ref new Border();
        badge->Width = 30;
        badge->Height = 30;
        badge->CornerRadius = Windows::UI::Xaml::CornerRadius(15);
        badge->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Top;
        badge->Background = Solid(done ? RgbColor(52, 168, 104) : current ? Accent() : Alpha(40, 255, 255, 255));
        auto mark = Text(done ? L"✓" : ref new Platform::String(std::to_wstring(number).c_str()), 14, true);
        mark->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
        mark->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        if (current && !done) mark->Foreground = Solid(Colors::Black);
        badge->Child = mark;
        row->Children->Append(badge);
        auto copy = Vertical();
        auto name = Text(title, 15, true);
        if (done) name->Opacity = 0.6;
        copy->Children->Append(name);
        auto about = Text(detail, 12, false, true);
        about->Margin = Thickness(0, 2, 0, 0);
        copy->Children->Append(about);
        Grid::SetColumn(copy, 1);
        row->Children->Append(copy);
        setup->Children->Append(row);
        return copy;
    };

    // 1. Storage: USB drives first.
    auto storage = step(1, storageDone, !storageDone, L"Choose where Vita3K keeps its data",
        storageDone ? ref new Platform::String(Pref(L"storage.dataRoot", L"")->IsEmpty() ? L"Internal storage" : Pref(L"storage.dataRoot", L"")->Data())
                    : ref new Platform::String(L"Firmware, installed games and saves. A USB drive is best: installed games can be large."));
    if (!storageDone) {
        auto drives = ref new StackPanel();
        drives->Orientation = Orientation::Horizontal;
        drives->Margin = Thickness(0, 10, 0, 0);
        storage->Children->Append(drives);
        auto finding = Text(L"Looking for USB drives...", 12, false, true);
        drives->Children->Append(finding);
        create_task(KnownFolders::RemovableDevices->GetFoldersAsync()).then(
            [this,drives,finding](task<Windows::Foundation::Collections::IVectorView<StorageFolder^>^> found) {
                unsigned index = 0;
                if (drives->Children->IndexOf(finding, &index)) drives->Children->RemoveAt(index);
                unsigned count = 0;
                try {
                    for (auto drive : found.get()) {
                        auto path = drive->Path;
                        auto use = Button(L"Use " + drive->DisplayName, count == 0);
                        use->Margin = Thickness(0, 0, 10, 0);
                        use->Click += ref new RoutedEventHandler([this,path](Platform::Object^, RoutedEventArgs^) {
                            std::wstring root = path->Data();
                            if (!root.empty() && root.back() != L'\\') root.push_back(L'\\');
                            ChangeDataRoot(root + L"Vita3K");
                        });
                        drives->Children->Append(use);
                        ++count;
                    }
                } catch (Platform::Exception^) {}
                if (count == 0) {
                    auto pick = Button(L"Choose a folder on a drive", true);
                    pick->Margin = Thickness(0, 0, 10, 0);
                    pick->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseDataFolder(); });
                    drives->Children->Append(pick);
                }
                auto internal = Button(L"Use internal storage");
                internal->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
                    PutPref(L"storage.internal", L"1");
                    ChangeDataRoot(L"");
                    Render();
                });
                drives->Children->Append(internal);
            }, task_continuation_context::use_current());
    }

    // 2. Firmware.
    auto firmware = step(2, firmwareDone, storageDone && !firmwareDone, L"Install the PS Vita firmware",
        firmwareDone ? L"Installed" : L"Pick the .PUP files you downloaded, all at once. They install right away.");
    if (!firmwareDone) {
        auto install = Button(L"Install firmware", storageDone);
        install->Margin = Thickness(0, 10, 0, 0);
        install->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
        install->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ShowPage(2); ChooseFirmware(); });
        firmware->Children->Append(install);
    }

    // 3. Games.
    auto games = step(3, gamesDone, storageDone && firmwareDone && !gamesDone, L"Choose your games folder",
        gamesDone ? L"Done" : L"Your Vita games stay where they are, on the drive. Nothing is copied.");
    if (!gamesDone) {
        auto choose = Button(L"Choose games folder", storageDone && firmwareDone);
        choose->Margin = Thickness(0, 10, 0, 0);
        choose->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
        choose->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseGameFolder(); });
        games->Children->Append(choose);
    }

    auto tip = Text(L"Tip: in Dev Home, set this app's type to Game, so the Xbox gives it the memory games need.", 11, false, true);
    tip->Opacity = 0.75;
    setup->Children->Append(tip);
    auto card = Panel(setup, 24);
    card->Margin = Thickness(0, 0, 0, 20);
    card->Background = Gradient(Card(), Alpha(255, 30, 24, 38));
    content_->Children->Append(card);
}

std::wstring CatalogShell::RecentLogText() {
    libretro_.FlushLog();
    const std::wstring path = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\vita3k-runtime.log";
    FILE* file = _wfsopen(path.c_str(), L"rb", _SH_DENYNO);
    if (!file) return L"No log yet. It is written when Vita3K starts.";
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    const long take = std::min<long>(size, 24 * 1024);
    std::fseek(file, size - take, SEEK_SET);
    std::string tail(static_cast<size_t>(take), '\0');
    std::fread(&tail[0], 1, tail.size(), file);
    std::fclose(file);
    const auto firstLine = tail.find('\n');
    if (size > take && firstLine != std::string::npos) tail.erase(0, firstLine + 1);
    return WidenUtf8(tail);
}

void CatalogShell::SaveLogToDrive() {
    libretro_.FlushLog();
    // To the drive Vita3K's data is on, else the games folder, else ask.
    auto save = [this](StorageFolder^ target) {
        create_task(ApplicationData::Current->LocalFolder->GetFileAsync(L"vita3k-runtime.log"))
        .then([target](StorageFile^ log) {
            return create_task(log->CopyAsync(target, L"vita3k-runtime.log", NameCollisionOption::ReplaceExisting));
        }).then([this,target](task<StorageFile^> copied) {
            try {
                copied.get();
                SetStatus(L"Log saved to " + target->Path + L"\\vita3k-runtime.log");
                // The frame the game saved for diagnosis, when there is one.
                create_task(ApplicationData::Current->LocalFolder->TryGetItemAsync(L"frame-300.bmp"))
                .then([target](IStorageItem^ item) {
                    auto frame = dynamic_cast<StorageFile^>(item);
                    if (frame) frame->CopyAsync(target, L"frame-300.bmp", NameCollisionOption::ReplaceExisting);
                });
            } catch (Platform::Exception^ e) {
                SetStatus(L"Couldn't save the log: " + e->Message);
            }
        }, task_continuation_context::use_current());
    };
    auto pick = [this,save]() {
        auto picker = ref new FolderPicker();
        picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
        picker->FileTypeFilter->Append(L"*");
        create_task(picker->PickSingleFolderAsync()).then([save](task<StorageFolder^> picked) {
            try { if (auto folder = picked.get()) save(folder); } catch (Platform::Exception^) {}
        }, task_continuation_context::use_current());
    };
    auto chosen = Pref(L"storage.dataRoot", L"");
    if (!chosen->IsEmpty()) {
        create_task(StorageFolder::GetFolderFromPathAsync(chosen)).then([save,pick](task<StorageFolder^> found) {
            try { save(found.get()); } catch (Platform::Exception^) { pick(); }
        }, task_continuation_context::use_current());
        return;
    }
    auto access = StorageApplicationPermissions::FutureAccessList;
    if (access->ContainsItem(L"vita-games")) {
        create_task(access->GetFolderAsync(L"vita-games")).then([save,pick](task<StorageFolder^> found) {
            try { save(found.get()); } catch (Platform::Exception^) { pick(); }
        }, task_continuation_context::use_current());
        return;
    }
    pick();
}

void CatalogShell::CleanUpOldCopies() {
    // Earlier builds copied every game into app storage and parked firmware
    // files in the system folder. Games now play where they are and firmware
    // installs straight away, so those copies only take up space.
    const std::wstring local = ApplicationData::Current->LocalFolder->Path->Data();
    const std::wstring internalFirmware = InternalDataRoot() + L"\\vita3k";
    create_task([local,internalFirmware]() {
        std::error_code ec;
        std::filesystem::remove_all(local + L"\\Vita3KContent", ec);
        std::filesystem::remove_all(local + L"\\Vita3KTemp", ec);
        for (const wchar_t* name : { L"PSP2UPDAT.PUP", L"PSP2UPDAT_PREINST.PUP", L"PSP2UPDAT_FONT.PUP" })
            std::filesystem::remove(internalFirmware + L"\\" + name, ec);
    });
    auto values = ApplicationData::Current->LocalSettings->Values;
    for (const wchar_t* key : { L"firmware.stage.main", L"firmware.stage.preinst" })
        if (values->HasKey(ref new Platform::String(key))) values->Remove(ref new Platform::String(key));
}

void CatalogShell::ChooseFirmware() {
    // Like a BIOS folder in other emulators: pick every .PUP at once, under
    // any names. Each one is identified by what is inside it.
    auto picker = ref new FileOpenPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L".pup");
    picker->FileTypeFilter->Append(L".PUP");
    create_task(picker->PickMultipleFilesAsync()).then(
        [this](task<Windows::Foundation::Collections::IVectorView<StorageFile^>^> picked) {
            try {
                auto files = picked.get();
                std::vector<StorageFile^> list;
                if (files) for (auto file : files) list.push_back(file);
                InstallFirmwareFiles(list);
            } catch (Platform::Exception^ e) {
                SetStatus(L"Firmware picker failed: " + e->Message);
            }
        }, task_continuation_context::use_current());
}

void CatalogShell::ChooseFirmwareFolder() {
    auto picker = ref new FolderPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L"*");
    create_task(picker->PickSingleFolderAsync()).then([this](task<StorageFolder^> picked) {
        StorageFolder^ folder = nullptr;
        try { folder = picked.get(); } catch (Platform::Exception^ e) { SetStatus(L"Folder picker failed: " + e->Message); }
        if (folder == nullptr) return;
        create_task(folder->GetFilesAsync()).then(
            [this](task<Windows::Foundation::Collections::IVectorView<StorageFile^>^> listed) {
                std::vector<StorageFile^> list;
                try {
                    for (auto file : listed.get()) {
                        std::wstring name = file->Name->Data();
                        for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
                        if (name.size() > 4 && name.compare(name.size() - 4, 4, L".pup") == 0) list.push_back(file);
                    }
                } catch (Platform::Exception^ e) {
                    SetStatus(L"Couldn't read that folder: " + e->Message);
                    return;
                }
                if (list.empty()) {
                    ShowFirmwareProgress(-1, L"No .PUP files in that folder.");
                    return;
                }
                InstallFirmwareFiles(list);
            }, task_continuation_context::use_current());
    }, task_continuation_context::use_current());
}

void CatalogShell::ShowFirmwareProgress(double value, Platform::String^ message) {
    if (firmwareProgress_ != nullptr) {
        // -1 hides the bar, -2 shows it without a percentage.
        firmwareProgress_->Visibility = value == -1 ? Windows::UI::Xaml::Visibility::Collapsed
                                                    : Windows::UI::Xaml::Visibility::Visible;
        firmwareProgress_->IsIndeterminate = value == -2;
        if (value >= 0) firmwareProgress_->Value = value;
    }
    if (message != nullptr) {
        if (firmwareMessage_ != nullptr) firmwareMessage_->Text = message;
        SetStatus(message);
    }
}

void CatalogShell::InstallFirmwareFiles(std::vector<StorageFile^> files) {
    if (files.empty()) return;
    if (firmwareBusy_) {
        SetStatus(L"Firmware is already being installed.");
        return;
    }
    if (libretro_.GameLoaded() || booting_) {
        SetStatus(L"Stop the game before installing firmware.");
        return;
    }
    firmwareBusy_ = true;
    ShowFirmwareProgress(0, L"Getting the firmware files ready...");
    // Files the core can read where they are (internal storage, USB drives)
    // install from there. Anything else is copied to a temporary folder first.
    auto paths = std::make_shared<std::vector<std::pair<std::wstring, std::wstring>>>();
    auto temps = std::make_shared<std::vector<std::wstring>>();
    const std::string system = NarrowUtf8(DataRoot());
    auto dispatcher = Dispatcher;
    create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(L"Vita3KTemp", CreationCollisionOption::OpenIfExists))
    .then([files,paths,temps](StorageFolder^ temp) {
        task<void> chain = task_from_result();
        for (auto file : files) {
            chain = chain.then([file,temp,paths,temps]() -> task<void> {
                if (CanOpenFromApp(file->Path->Data())) {
                    paths->push_back({ file->Path->Data(), file->Name->Data() });
                    return task_from_result();
                }
                return create_task(file->CopyAsync(temp, file->Name, NameCollisionOption::GenerateUniqueName))
                    .then([file,paths,temps](StorageFile^ copy) {
                        paths->push_back({ copy->Path->Data(), file->Name->Data() });
                        temps->push_back(copy->Path->Data());
                    });
            });
        }
        return chain;
    }).then([this,paths,system,dispatcher]() {
        std::vector<FirmwareResult> results;
        const size_t total = paths->size();
        // With Vita3K's data on a USB drive, each package installs into the
        // app's own storage first and the finished parts are then copied to
        // the drive: the installer's temporary files and deletions stay off
        // the drive, where they go through the slower brokered file access.
        const std::wstring finalRoot = WidenUtf8(system);
        const bool staged = _wcsicmp(finalRoot.c_str(), InternalDataRoot().c_str()) != 0;
        const std::wstring stagingRoot = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\Vita3KStaging";
        const std::string installRoot = staged ? NarrowUtf8(stagingRoot) : system;
        // The system firmware first, then the preinstalled package, then the
        // fonts: the system firmware is what games need, and it is the one
        // that installed alone (or first after a restart) but failed after the
        // other two in the same batch.
        struct Pending { std::wstring path, name; bool recognised = false; std::uint32_t version = 0, partitions = 0; };
        std::vector<Pending> order;
        for (const auto& entry : *paths) {
            Pending item{ entry.first, entry.second };
            item.recognised = libretro_.PupInfo(NarrowUtf8(entry.first), item.version, item.partitions);
            order.push_back(item);
        }
        const auto rank = [](const Pending& item) {
            if (!item.recognised) return 9;
            if (item.partitions & 3u) return 0;
            if (item.partitions & 4u) return 1;
            return 2;
        };
        std::stable_sort(order.begin(), order.end(), [&](const Pending& a, const Pending& b) { return rank(a) < rank(b); });
        for (size_t i = 0; i < total; ++i) {
            FirmwareResult result;
            result.name = order[i].name;
            const std::string pup = NarrowUtf8(order[i].path);
            result.recognised = order[i].recognised;
            result.headerVersion = order[i].version;
            result.partitions = order[i].partitions;
            const wchar_t* kind = PupKindName(result.partitions);
            if (!result.recognised || !kind) {
                result.detail = L"not a PS Vita firmware package";
                results.push_back(result);
                continue;
            }
            const std::wstring heading = std::wstring(L"Installing ") + kind + L" " +
                FirmwareVersionText(result.headerVersion) + L" (" + std::to_wstring(i + 1) + L" of " +
                std::to_wstring(total) + L")...";
            std::function<void(std::uint32_t)> notify = [this,dispatcher,i,total,heading](std::uint32_t percent) {
                const double overall = (static_cast<double>(i) * 100.0 + percent) / static_cast<double>(total);
                dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                    ref new Windows::UI::Core::DispatchedHandler([this,overall,heading]() {
                        ShowFirmwareProgress(overall, ref new Platform::String(heading.c_str()));
                    }));
            };
            notify(0);
            std::string version;
            libretro_.LogLine("Firmware: installing " + NarrowUtf8(result.name) + " (" + std::to_string(i + 1) + " of " + std::to_string(total) + ")");
            result.installed = libretro_.InstallFirmware(installRoot, pup, notify, version, result.detail);
            if (!result.installed && !libretro_.GameLoaded()) {
                // It installs on its own: give the file system a moment to
                // finish the previous package's cleanup and try once more.
                libretro_.LogLine("Firmware: " + NarrowUtf8(result.name) + " failed in the batch; trying it once more");
                Sleep(1500);
                notify(0);
                version.clear();
                result.installed = libretro_.InstallFirmware(installRoot, pup, notify, version, result.detail);
            }
            libretro_.LogLine("Firmware: " + NarrowUtf8(result.name) + (result.installed ? " installed" : " failed: " + NarrowUtf8(result.detail)));
            result.version = version.empty() ? FirmwareVersionText(result.headerVersion) : WidenUtf8(version);
            if (result.installed && staged) {
                dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                    ref new Windows::UI::Core::DispatchedHandler([this]() {
                        ShowFirmwareProgress(-2, L"Copying the firmware to your drive...");
                    }));
                for (const wchar_t* part : { L"os0", L"vs0", L"pd0", L"sa0" }) {
                    const std::wstring from = stagingRoot + L"\\vita3k\\" + part;
                    if (!HasFiles(from)) continue;
                    const std::wstring to = finalRoot + L"\\vita3k\\" + part;
                    std::error_code ec;
                    std::filesystem::remove_all(to, ec);
                    ec.clear();
                    std::filesystem::create_directories(to, ec);
                    ec.clear();
                    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive |
                        std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) {
                        result.installed = false;
                        result.detail = L"Copying " + std::wstring(part) + L" to the drive failed: " + WidenUtf8(ec.message());
                        libretro_.LogLine("Firmware: copying " + NarrowUtf8(part) + " to the drive failed: " + ec.message());
                        break;
                    }
                }
                std::error_code ec;
                std::filesystem::remove_all(stagingRoot, ec);
            }
            results.push_back(result);
        }
        return results;
    }, task_continuation_context::use_arbitrary())
    .then([this,temps](task<std::vector<FirmwareResult>> done) {
        firmwareBusy_ = false;
        for (const auto& path : *temps) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
        std::vector<FirmwareResult> results;
        try {
            results = done.get();
        } catch (Platform::Exception^ e) {
            ShowFirmwareProgress(-1, L"Couldn't read the firmware files: " + e->Message);
            RefreshFirmwareStatus();
            return;
        } catch (...) {
            ShowFirmwareProgress(-1, L"Couldn't install the firmware. vita3k-runtime.log has the details.");
            RefreshFirmwareStatus();
            return;
        }
        std::wstring summary;
        int systemCount = 0;
        for (const auto& r : results) {
            if (!summary.empty()) summary += L"\n";
            const wchar_t* kind = PupKindName(r.partitions);
            if (!r.recognised || !kind) {
                summary += r.name + L": not a PS Vita firmware package, skipped.";
                continue;
            }
            if (r.partitions & 2) ++systemCount;
            if (!r.installed) {
                summary += r.name + L": " + kind + L", install failed. " + ProblemLine(r.detail);
                continue;
            }
            summary += r.name + L": " + kind + (r.version.empty() ? std::wstring() : L" " + r.version) + L", installed.";
            const std::uint32_t bits[3] = { 2u, 4u, 8u };
            for (int part = 0; part < 3; ++part)
                if (r.partitions & bits[part])
                    PutPref(ref new Platform::String(kFirmwareParts[part].versionKey),
                            ref new Platform::String(r.version.c_str()));
        }
        if (systemCount > 1)
            summary += L"\nMore than one file was the system firmware: PSVUPDAT.PUP and PSP2UPDAT.PUP are the same update under two names.";
        const std::wstring root = FirmwareRoot();
        std::wstring missing;
        for (const auto& part : kFirmwareParts) {
            if (HasFiles(root + L"\\" + part.partition)) continue;
            if (!missing.empty()) missing += L", ";
            missing += part.title;
        }
        summary += missing.empty() ? std::wstring(L"\nAll firmware parts are installed.")
                                   : L"\nStill missing: " + missing + L".";
        ShowFirmwareProgress(-1, ref new Platform::String(summary.c_str()));
        RefreshFirmwareStatus();
    }, task_continuation_context::use_current());
}
namespace {
Platform::String^ CoreOptionPrefKey(const vita3k_uwp::CoreOptionDef& option) {
    std::wstring key = L"core.";
    for (const char* c = option.key; *c; ++c) key.push_back(static_cast<wchar_t>(*c));
    return ref new Platform::String(key.c_str());
}
// The saved value of an option, or its default when none (or an unknown one) is saved.
std::string SavedCoreOption(const vita3k_uwp::CoreOptionDef& option) {
    auto saved = Pref(CoreOptionPrefKey(option), L"");
    std::string value;
    for (const wchar_t* c = saved->Data(); *c; ++c) value.push_back(static_cast<char>(*c));
    for (std::size_t v = 0; v < option.valueCount; ++v)
        if (value == option.values[v].value) return value;
    return option.defaultValue;
}
}

namespace {
// Settings a game needs to get through, applied when the player has not set
// that option themselves. Each from the game's Vita3K compatibility report.
struct TitleOverride {
    const wchar_t* titleId;
    const char* key;
    const char* value;
};
const TitleOverride kTitleOverrides[] = {
    // Danganronpa: Trigger Happy Havoc (Vita3K/compatibility#94): the
    // prologue's classroom conversation only goes on with surface sync off.
    { L"PCSE00261", "vita3k_surface_sync", "disabled" },
};
// The title ID (four letters, five digits) in a game's folder or file name.
std::wstring TitleIdIn(const std::wstring& path) {
    for (std::size_t i = 0; i + 9 <= path.size(); ++i) {
        bool match = true;
        for (std::size_t k = 0; k < 9 && match; ++k) {
            const wchar_t c = path[i + k];
            match = k < 4 ? (c >= L'A' && c <= L'Z') : (c >= L'0' && c <= L'9');
        }
        const bool edgeBefore = i == 0 || !iswalnum(path[i - 1]);
        const bool edgeAfter = i + 9 == path.size() || !iswalnum(path[i + 9]);
        if (match && edgeBefore && edgeAfter) return path.substr(i, 9);
    }
    return std::wstring();
}
}

void CatalogShell::ApplyCoreOptions(const std::wstring& contentPath) {
    const std::wstring titleId = TitleIdIn(contentPath);
    for (std::size_t i = 0; i < vita3k_uwp::kCoreOptionCount; ++i) {
        const auto& option = vita3k_uwp::kCoreOptions[i];
        std::string value = SavedCoreOption(option);
        const bool chosen = Pref(CoreOptionPrefKey(option), L"")->Length() != 0;
        if (!chosen && !titleId.empty()) {
            for (const auto& o : kTitleOverrides) {
                if (titleId == o.titleId && std::strcmp(option.key, o.key) == 0) {
                    value = o.value;
                    libretro_.LogLine(std::string("Game setting for ") + NarrowUtf8(titleId) + ": " + o.key + " = " + o.value);
                }
            }
        }
        libretro_.SetOption(option.key, value);
    }
}

void CatalogShell::FirmwareSettings() {
    Section(L"Firmware", L"Vita3K runs on Sony's PS Vita firmware. This app ships none of it.");
    auto box = Vertical();
    // One status tile per part.
    auto tiles = ref new Grid();
    for (int i = 0; i < 3; ++i) {
        auto column = ref new ColumnDefinition();
        column->Width = GridLength(1, GridUnitType::Star);
        tiles->ColumnDefinitions->Append(column);
    }
    for (int i = 0; i < 3; ++i) {
        const FirmwarePart& part = kFirmwareParts[i];
        auto tile = ref new Border();
        tile->CornerRadius = Windows::UI::Xaml::CornerRadius(14);
        tile->Background = Gradient(RgbColor(28, 29, 37), RgbColor(19, 20, 26));
        tile->BorderBrush = Solid(Alpha(26, 255, 255, 255));
        tile->BorderThickness = Thickness(1);
        tile->Padding = Thickness(16, 14, 16, 14);
        tile->Margin = Thickness(i == 0 ? 0 : 6, 0, i == 2 ? 0 : 6, 0);
        auto inner = Vertical();
        auto head = ref new StackPanel();
        head->Orientation = Orientation::Horizontal;
        firmwareDot_[i] = ref new Border();
        firmwareDot_[i]->Width = 10;
        firmwareDot_[i]->Height = 10;
        firmwareDot_[i]->CornerRadius = Windows::UI::Xaml::CornerRadius(5);
        firmwareDot_[i]->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        firmwareDot_[i]->Margin = Thickness(0, 0, 8, 0);
        head->Children->Append(firmwareDot_[i]);
        auto need = Text(part.required ? L"REQUIRED" : L"RECOMMENDED", 10, true, !part.required);
        need->CharacterSpacing = 160;
        if (part.required) need->Foreground = Solid(Accent());
        head->Children->Append(need);
        inner->Children->Append(head);
        auto title = Text(ref new Platform::String(part.title), 17, true);
        title->Margin = Thickness(0, 8, 0, 2);
        inner->Children->Append(title);
        firmwareStatus_[i] = Text(L"", 13, false, true);
        inner->Children->Append(firmwareStatus_[i]);
        auto about = Text(ref new Platform::String(part.about), 11, false, true);
        about->Margin = Thickness(0, 8, 0, 0);
        about->Opacity = 0.8;
        inner->Children->Append(about);
        tile->Child = inner;
        Grid::SetColumn(tile, i);
        tiles->Children->Append(tile);
    }
    box->Children->Append(tiles);

    auto actions = ref new StackPanel();
    actions->Orientation = Orientation::Horizontal;
    actions->Margin = Thickness(0, 16, 0, 0);
    auto install = Button(L"Install firmware files", true);
    install->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseFirmware(); });
    actions->Children->Append(install);
    auto fromFolder = Button(L"Install from a folder");
    fromFolder->Margin = Thickness(10, 0, 0, 0);
    fromFolder->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseFirmwareFolder(); });
    actions->Children->Append(fromFolder);
    auto reset = Button(L"Remove installed firmware");
    reset->Margin = Thickness(10, 0, 0, 0);
    reset->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ResetInstalledFirmware(); });
    actions->Children->Append(reset);
    box->Children->Append(actions);

    firmwareProgress_ = ref new ProgressBar();
    firmwareProgress_->Minimum = 0;
    firmwareProgress_->Maximum = 100;
    firmwareProgress_->Height = 6;
    firmwareProgress_->Margin = Thickness(0, 14, 0, 0);
    firmwareProgress_->Foreground = Solid(Accent());
    firmwareProgress_->Visibility = firmwareBusy_ ? Windows::UI::Xaml::Visibility::Visible
                                                  : Windows::UI::Xaml::Visibility::Collapsed;
    box->Children->Append(firmwareProgress_);
    firmwareMessage_ = Text(
        L"Pick the .PUP files you downloaded, all at once, under any names. Vita3K looks inside each one, "
        L"tells the system firmware, preinstalled package and fonts apart, and installs them right away.",
        12, false, true);
    firmwareMessage_->Margin = Thickness(0, 10, 0, 0);
    box->Children->Append(firmwareMessage_);
    auto where = Text(
        L"Where to get them (on a PC, then copy them to a USB drive): the system firmware from Sony's PS Vita "
        L"system software page. The preinstalled package and fonts are separate Sony downloads, linked by the "
        L"Vita3K libretro core: bit.ly/4hlePsX (preinstalled) and bit.ly/48ouDaa (fonts).",
        11, false, true);
    where->Margin = Thickness(0, 8, 0, 0);
    where->Opacity = 0.8;
    box->Children->Append(where);
    content_->Children->Append(Panel(box));
    RefreshFirmwareStatus();
}

void CatalogShell::StorageSettings() {
    Section(L"Storage", L"Games play from where they are. Vita3K's own data can live on your drive too.");
    auto box = Vertical();
    auto addRow = [this,box](Platform::String^ title, Platform::String^ detail, TextBlock^ label,
                             Windows::UI::Xaml::Controls::Button^ first, Windows::UI::Xaml::Controls::Button^ second) {
        auto grid = ref new Grid();
        grid->Margin = Thickness(0, 6, 0, 10);
        auto left = ref new ColumnDefinition();
        left->Width = GridLength(1, GridUnitType::Star);
        auto right = ref new ColumnDefinition();
        right->Width = GridLength(1, GridUnitType::Auto);
        grid->ColumnDefinitions->Append(left);
        grid->ColumnDefinitions->Append(right);
        auto copy = Vertical();
        copy->Children->Append(Text(title, 15, true));
        label->Foreground = Solid(Accent());
        label->Margin = Thickness(0, 3, 0, 3);
        copy->Children->Append(label);
        auto about = Text(detail, 11, false, true);
        about->Margin = Thickness(0, 0, 24, 0);
        copy->Children->Append(about);
        grid->Children->Append(copy);
        auto actions = ref new StackPanel();
        actions->Orientation = Orientation::Horizontal;
        actions->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        actions->Children->Append(first);
        if (second != nullptr) {
            second->Margin = Thickness(10, 0, 0, 0);
            actions->Children->Append(second);
        }
        Grid::SetColumn(actions, 1);
        grid->Children->Append(actions);
        box->Children->Append(grid);
    };
    folderLabel_ = Text(L"", 13);
    auto chooseGames = Button(L"Choose games folder", true);
    chooseGames->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseGameFolder(); });
    addRow(L"Games", L"A folder of games (game folders, .vpk or .pkg files), or one game's own folder. "
        L"A USB drive works: games are read where they are, nothing is copied.", folderLabel_, chooseGames, nullptr);
    dataLabel_ = Text(L"", 13);
    auto onDrive = Button(L"Use a folder on a drive");
    onDrive->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseDataFolder(); });
    auto internal = Button(L"Use internal storage");
    internal->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChangeDataRoot(L""); });
    addRow(L"Vita3K data", L"Firmware, installed games, saves and caches. Vita3K installs a game here the first "
        L"time it runs it, so if internal space is short, put this on your USB drive.", dataLabel_, onDrive, internal);
    addonsLabel_ = Text(L"", 13);
    auto chooseAddons = Button(L"Choose updates folder", true);
    chooseAddons->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ChooseAddonsFolder(); });
    auto installAddons = Button(L"Install new ones");
    installAddons->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { InstallAddons(); });
    addRow(L"Updates and DLC", L"A folder of game updates and DLC (folders, .vpk, .zip or .pkg). New ones are installed "
        L"when you choose it, with Install new ones, and before a game they belong to starts.", addonsLabel_, chooseAddons, installAddons);
    content_->Children->Append(Panel(box));
    RestorePaths();
}

void CatalogShell::ChooseDataFolder() {
    auto picker = ref new FolderPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L"*");
    create_task(picker->PickSingleFolderAsync()).then([this](task<StorageFolder^> picked) {
        StorageFolder^ folder = nullptr;
        try { folder = picked.get(); } catch (Platform::Exception^ e) { SetStatus(L"Folder picker failed: " + e->Message); }
        if (folder == nullptr) return;
        StorageApplicationPermissions::FutureAccessList->AddOrReplace(L"vita-data", folder);
        ChangeDataRoot(std::wstring(folder->Path->Data()) + L"\\Vita3K");
    }, task_continuation_context::use_current());
}

namespace {
// The updates and DLC in a folder: folders with a param.sfo (one level of
// grouping folders too), and .vpk/.zip/.pkg files.
std::vector<std::filesystem::path> AddonItems(const std::wstring& root) {
    namespace fs = std::filesystem;
    std::vector<fs::path> items;
    std::error_code ec;
    const auto isTitle = [](const fs::path& dir) {
        std::error_code e;
        return fs::exists(dir / L"sce_sys" / L"param.sfo", e);
    };
    const auto isPackage = [](const fs::path& file) {
        std::wstring ext = file.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return ext == L".pkg" || ext == L".vpk" || ext == L".zip";
    };
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        const fs::path path = it->path();
        if (fs::is_directory(path, e)) {
            if (isTitle(path)) { items.push_back(path); continue; }
            for (fs::directory_iterator sub(path, e), subEnd; !e && sub != subEnd; sub.increment(e)) {
                std::error_code e2;
                if (fs::is_directory(sub->path(), e2) ? isTitle(sub->path()) : isPackage(sub->path()))
                    items.push_back(sub->path());
            }
        } else if (isPackage(path)) {
            items.push_back(path);
        }
    }
    return items;
}
// Changes when the item does: its size (a file) or its param.sfo's.
std::wstring AddonSignature(const std::filesystem::path& item) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path probe = fs::is_directory(item, ec) ? item / L"sce_sys" / L"param.sfo" : item;
    const auto size = fs::file_size(probe, ec);
    const auto stamp = fs::last_write_time(probe, ec).time_since_epoch().count();
    return item.wstring() + L"|" + std::to_wstring(size) + L"|" + std::to_wstring(stamp);
}
// Installs the items not installed before (for one title, when titleId is set:
// those whose name says it). Returns a line for each one tried.
std::wstring InstallPendingAddons(VitaLibretroHost& host, const std::string& system, const std::wstring& root,
                                  const std::wstring& titleId, int& installed, int& failed) {
    std::wstring summary;
    for (const auto& item : AddonItems(root)) {
        const std::wstring name = item.filename().wstring();
        if (!titleId.empty() && item.wstring().find(titleId) == std::wstring::npos) continue;
        const auto key = L"addon.done." + StableKey(ref new Platform::String(AddonSignature(item).c_str()));
        if (!Pref(key, L"")->IsEmpty()) continue;
        std::string id, category;
        std::wstring detail;
        host.LogLine("Updates and DLC: installing " + NarrowUtf8(item.wstring()));
        if (host.InstallAddon(system, NarrowUtf8(item.wstring()), id, category, detail)) {
            PutPref(key, L"1");
            ++installed;
            const wchar_t* kind = category.find("gp") != std::string::npos ? L"update" :
                                  category.find("ac") != std::string::npos ? L"DLC" : L"content";
            summary += name + L": " + kind + L" for " + WidenUtf8(id) + L" installed.\n";
            host.LogLine("Updates and DLC: installed " + NarrowUtf8(name) + " (" + id + ", " + category + ")");
        } else {
            ++failed;
            summary += name + L": not installed. " + ProblemLine(detail) + L"\n";
            host.LogLine("Updates and DLC: " + NarrowUtf8(name) + " failed: " + NarrowUtf8(detail));
        }
    }
    return summary;
}
}

void CatalogShell::ChooseAddonsFolder() {
    auto picker = ref new FolderPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L"*");
    create_task(picker->PickSingleFolderAsync()).then([this](task<StorageFolder^> picked) {
        StorageFolder^ folder = nullptr;
        try { folder = picked.get(); } catch (Platform::Exception^ e) { SetStatus(L"Folder picker failed: " + e->Message); }
        if (folder == nullptr) return;
        StorageApplicationPermissions::FutureAccessList->AddOrReplace(L"vita-addons", folder);
        PutPref(L"storage.addons", folder->Path);
        RestorePaths();
        InstallAddons();
    }, task_continuation_context::use_current());
}

void CatalogShell::InstallAddons() {
    const std::wstring root = Pref(L"storage.addons", L"")->Data();
    if (root.empty()) {
        SetStatus(L"Choose an updates folder first.");
        return;
    }
    if (addonsBusy_ || firmwareBusy_ || booting_ || libretro_.GameLoaded()) {
        SetStatus(L"Wait until the game or the current install has finished.");
        return;
    }
    addonsBusy_ = true;
    SetStatus(L"Looking for new updates and DLC...");
    const std::string system = NarrowUtf8(DataRoot());
    create_task([this, root, system]() {
        int installed = 0, failed = 0;
        std::wstring summary = InstallPendingAddons(libretro_, system, root, std::wstring(), installed, failed);
        if (installed == 0 && failed == 0) summary = L"No new updates or DLC in the folder.";
        return summary;
    }).then([this](task<std::wstring> done) {
        addonsBusy_ = false;
        std::wstring summary;
        try { summary = done.get(); } catch (...) { summary = L"Installing updates and DLC stopped. vita3k-runtime.log has the details."; }
        while (!summary.empty() && summary.back() == L'\n') summary.pop_back();
        SetStatus(ref new Platform::String(summary.c_str()));
    }, task_continuation_context::use_current());
}

void CatalogShell::ChangeDataRoot(const std::wstring& chosen) {
    if (libretro_.GameLoaded() || booting_ || firmwareBusy_) {
        SetStatus(L"Wait until the game or the firmware install has finished, then change the data folder.");
        return;
    }
    const std::wstring from = DataRoot();
    const std::wstring to = chosen.empty() ? InternalDataRoot() : chosen;
    if (_wcsicmp(from.c_str(), to.c_str()) == 0) {
        SetStatus(L"Vita3K data is already there.");
        return;
    }
    if (!chosen.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(to, ec);
        if (!std::filesystem::is_directory(to, ec)) {
            SetStatus(ref new Platform::String((L"Can't create " + to + L". Check that the drive is connected and writable.").c_str()));
            return;
        }
    }
    auto apply = [this,chosen,to]() {
        PutPref(L"storage.dataRoot", ref new Platform::String(chosen.c_str()));
        SetStatus(ref new Platform::String((L"Vita3K data now lives in " + to + L".").c_str()));
        Render();
    };
    if (!HasFiles(from + L"\\vita3k")) {
        apply();
        return;
    }
    auto dialog = ref new ContentDialog();
    dialog->Title = L"Move your Vita3K data?";
    dialog->Content = ref new Platform::String((L"Firmware, installed games and saves are in " + from +
        L". Move them to " + to + L"? Large games take a while.").c_str());
    dialog->PrimaryButtonText = L"Move";
    dialog->SecondaryButtonText = L"Start fresh there";
    dialog->CloseButtonText = L"Cancel";
    create_task(dialog->ShowAsync()).then([this,from,to,apply](ContentDialogResult answer) {
        if (answer == ContentDialogResult::Secondary) { apply(); return; }
        if (answer != ContentDialogResult::Primary) return;
        booting_ = true; // keeps games from starting while files move
        SetStatus(L"Moving Vita3K data. Leave the app open until it finishes...");
        create_task([from,to]() -> std::wstring {
            std::error_code ec;
            std::filesystem::create_directories(to + L"\\vita3k", ec);
            std::filesystem::copy(from + L"\\vita3k", to + L"\\vita3k",
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return L"Moving failed: " + WidenUtf8(ec.message()) + L". Nothing was removed from the old place.";
            std::filesystem::remove_all(from + L"\\vita3k", ec);
            return std::wstring();
        }).then([this,apply](task<std::wstring> moved) {
            booting_ = false;
            std::wstring error;
            try { error = moved.get(); } catch (...) { error = L"Moving failed."; }
            if (!error.empty()) { SetStatus(ref new Platform::String(error.c_str())); return; }
            apply();
        }, task_continuation_context::use_current());
    }, task_continuation_context::use_current());
}

void CatalogShell::EmulationSettings() {
    Section(L"Emulation", L"Vita3K's own options. They take effect when a game starts.");
    {
        // What draws the game, and how it fills the screen.
        auto display = Vertical();
        display->Children->Append(Text(L"Display and renderer", 16, true));
        auto rendererRow = ref new Grid();
        rendererRow->Margin = Thickness(0, 14, 0, 0);
        auto left = ref new ColumnDefinition();
        left->Width = GridLength(1, GridUnitType::Star);
        auto right = ref new ColumnDefinition();
        right->Width = GridLength(1, GridUnitType::Auto);
        rendererRow->ColumnDefinitions->Append(left);
        rendererRow->ColumnDefinitions->Append(right);
        auto rendererCopy = Vertical();
        rendererCopy->Children->Append(Text(L"Renderer", 14, true));
        auto rendererInfo = Text(L"Vita3K's Vulkan renderer, running on the Xbox's Direct3D 12 through Mesa Dozen. "
            L"It is the only renderer that works on Xbox; Vita3K's OpenGL one has no driver here.", 12, false, true);
        rendererInfo->Margin = Thickness(0, 3, 24, 0);
        rendererCopy->Children->Append(rendererInfo);
        rendererRow->Children->Append(rendererCopy);
        auto rendererName = Text(L"Vulkan (Direct3D 12)", 13, true);
        rendererName->Foreground = Solid(Accent());
        rendererName->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        Grid::SetColumn(rendererName, 1);
        rendererRow->Children->Append(rendererName);
        display->Children->Append(rendererRow);

        auto screenRow = ref new Grid();
        screenRow->Margin = Thickness(0, 14, 0, 0);
        auto l2 = ref new ColumnDefinition();
        l2->Width = GridLength(1, GridUnitType::Star);
        auto r2 = ref new ColumnDefinition();
        r2->Width = GridLength(1, GridUnitType::Auto);
        screenRow->ColumnDefinitions->Append(l2);
        screenRow->ColumnDefinitions->Append(r2);
        auto screenCopy = Vertical();
        screenCopy->Children->Append(Text(L"Screen", 14, true));
        auto screenInfo = Text(L"Fit keeps the Vita's shape with thin bars; Stretch fills the whole TV.", 12, false, true);
        screenInfo->Margin = Thickness(0, 3, 24, 0);
        screenCopy->Children->Append(screenInfo);
        screenRow->Children->Append(screenCopy);
        auto screen = ref new ComboBox();
        screen->Width = 240;
        screen->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        screen->Items->Append(L"Fit (keep shape)");
        screen->Items->Append(L"Stretch to fill");
        screen->SelectedIndex = Pref(L"display.stretch", L"0")->Equals(L"1") ? 1 : 0;
        screen->SelectionChanged += ref new SelectionChangedEventHandler([this](Platform::Object^ sender, SelectionChangedEventArgs^) {
            PutPref(L"display.stretch", safe_cast<ComboBox^>(sender)->SelectedIndex == 1 ? L"1" : L"0");
            PlayCue(1);
        });
        Grid::SetColumn(screen, 1);
        screenRow->Children->Append(screen);
        display->Children->Append(screenRow);
        content_->Children->Append(Panel(display));
    }
    std::wstring current;
    StackPanel^ group = nullptr;
    for (std::size_t i = 0; i < vita3k_uwp::kCoreOptionCount; ++i) {
        const auto& option = vita3k_uwp::kCoreOptions[i];
        if (group == nullptr || current != option.category) {
            if (group != nullptr) content_->Children->Append(Panel(group));
            group = Vertical();
            current = option.category;
            group->Children->Append(Text(ref new Platform::String(option.category), 16, true));
        }
        auto row = ref new Grid();
        row->Margin = Thickness(0, 14, 0, 0);
        auto left = ref new ColumnDefinition();
        left->Width = GridLength(1, GridUnitType::Star);
        auto right = ref new ColumnDefinition();
        right->Width = GridLength(1, GridUnitType::Auto);
        row->ColumnDefinitions->Append(left);
        row->ColumnDefinitions->Append(right);
        auto copy = Vertical();
        copy->Children->Append(Text(ref new Platform::String(option.label), 14, true));
        if (option.info && *option.info) {
            auto info = Text(ref new Platform::String(option.info), 12, false, true);
            info->Margin = Thickness(0, 3, 24, 0);
            info->MaxLines = 3;
            copy->Children->Append(info);
        }
        row->Children->Append(copy);
        auto picker = ref new ComboBox();
        picker->Width = 240;
        picker->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
        const std::string saved = SavedCoreOption(option);
        for (std::size_t v = 0; v < option.valueCount; ++v) {
            picker->Items->Append(ref new Platform::String(option.values[v].label));
            if (saved == option.values[v].value) picker->SelectedIndex = static_cast<int>(v);
        }
        picker->SelectionChanged += ref new SelectionChangedEventHandler(
            [this,i](Platform::Object^ sender, SelectionChangedEventArgs^) {
                const int chosen = safe_cast<ComboBox^>(sender)->SelectedIndex;
                const auto& changed = vita3k_uwp::kCoreOptions[i];
                if (chosen < 0 || static_cast<std::size_t>(chosen) >= changed.valueCount) return;
                const char* value = changed.values[chosen].value;
                std::wstring wide;
                for (const char* c = value; *c; ++c) wide.push_back(static_cast<wchar_t>(*c));
                PutPref(CoreOptionPrefKey(changed), ref new Platform::String(wide.c_str()));
                libretro_.SetOption(changed.key, value);
                PlayCue(1);
            });
        Grid::SetColumn(picker, 1);
        row->Children->Append(picker);
        group->Children->Append(row);
    }
    if (group != nullptr) content_->Children->Append(Panel(group));
    auto reset = Button(L"Restore default emulation options");
    reset->Margin = Thickness(0, 12, 0, 0);
    reset->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        for (std::size_t i = 0; i < vita3k_uwp::kCoreOptionCount; ++i) {
            const auto& option = vita3k_uwp::kCoreOptions[i];
            ApplicationData::Current->LocalSettings->Values->Remove(CoreOptionPrefKey(option));
            libretro_.SetOption(option.key, option.defaultValue);
        }
        SetStatus(L"Emulation options restored to Vita3K's defaults.");
        Render();
    });
    content_->Children->Append(reset);
}
void CatalogShell::RefreshFirmwareStatus() {
    const std::wstring root = FirmwareRoot();
    for (int i = 0; i < 3; ++i) {
        if (firmwareStatus_[i] == nullptr) continue;
        const FirmwarePart& part = kFirmwareParts[i];
        const bool installed = HasFiles(root + L"\\" + part.partition);
        auto version = Pref(ref new Platform::String(part.versionKey), L"");
        firmwareStatus_[i]->Text = installed
            ? (version->IsEmpty() ? ref new Platform::String(L"Installed") : L"Installed  ·  " + version)
            : ref new Platform::String(L"Not installed");
        const Color color = installed ? RgbColor(111, 227, 154)
            : part.required ? Accent() : RgbColor(240, 199, 94);
        if (firmwareDot_[i] != nullptr) firmwareDot_[i]->Background = Solid(color);
    }
}
void CatalogShell::ResetInstalledFirmware() {
    if (firmwareBusy_ || libretro_.GameLoaded() || booting_) {
        SetStatus(L"Wait until the game or the firmware install has finished.");
        return;
    }
    ShowFirmwareProgress(-1, L"Removing installed firmware...");
    const std::wstring root = FirmwareRoot();
    create_task([root]() {
        for (const wchar_t* part : { L"os0", L"vs0", L"pd0", L"sa0", L"PUP_DEC" }) {
            std::error_code ec;
            std::filesystem::remove_all(root + L"\\" + part, ec);
        }
    }).then([this](task<void> done) {
        try { done.get(); } catch (...) {}
        auto values = ApplicationData::Current->LocalSettings->Values;
        for (const auto& part : kFirmwareParts) {
            auto key = ref new Platform::String(part.versionKey);
            if (values->HasKey(key)) values->Remove(key);
        }
        ShowFirmwareProgress(-1, L"Installed firmware removed. Install it again with Install firmware files.");
        RefreshFirmwareStatus();
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
            // The chosen folder may be one game rather than a folder of games
            // (its eboot.bin is right there). Then it is the only entry.
            return create_task(folder->TryGetItemAsync(L"eboot.bin")).then(
                [folder](IStorageItem^ eboot) -> task<Windows::Foundation::Collections::IVectorView<IStorageItem^>^> {
                    if (eboot != nullptr) {
                        auto single = ref new Platform::Collections::Vector<IStorageItem^>();
                        single->Append(folder);
                        return task_from_result(single->GetView());
                    }
                    return create_task(folder->GetItemsAsync(0, 300));
                });
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

namespace {
// Progress of one staging copy, shared by its tasks.
struct StageProgress {
    std::atomic<uint32_t> files{0};
    std::atomic<uint32_t> skipped{0};
    std::atomic<uint64_t> bytes{0};
};

// Copies one file unless an identical-sized copy is already there, so a game
// that was staged before starts without copying gigabytes again, and an
// interrupted copy resumes where it stopped.
task<void> StageFile(StorageFile^ source, StorageFolder^ target, std::shared_ptr<StageProgress> progress) {
    return create_task(target->TryGetItemAsync(source->Name))
    .then([source,target,progress](IStorageItem^ existing) -> task<void> {
        auto copy = [source,target,progress]() {
            return create_task(source->CopyAsync(target, source->Name, NameCollisionOption::ReplaceExisting))
                .then([progress](StorageFile^) { progress->files++; });
        };
        auto existingFile = dynamic_cast<StorageFile^>(existing);
        if (existingFile == nullptr) return copy();
        return create_task(source->GetBasicPropertiesAsync())
        .then([existingFile](FileProperties::BasicProperties^ from) {
            return create_task(existingFile->GetBasicPropertiesAsync())
                .then([from](FileProperties::BasicProperties^ to) { return from->Size == to->Size; });
        }).then([copy,progress](bool same) -> task<void> {
            if (same) { progress->skipped++; return task_from_result(); }
            return copy();
        });
    });
}

task<void> StageFolder(StorageFolder^ source, StorageFolder^ target, std::shared_ptr<StageProgress> progress,
                       std::function<void()> report) {
    return create_task(source->GetFilesAsync())
    .then([source,target,progress,report](Collections::IVectorView<StorageFile^>^ files) {
        task<void> chain = task_from_result();
        for (auto file : files) {
            chain = chain.then([file,target,progress,report]() {
                return StageFile(file, target, progress).then([report]() { report(); });
            });
        }
        return chain;
    }).then([source]() { return create_task(source->GetFoldersAsync()); })
    .then([target,progress,report](Collections::IVectorView<StorageFolder^>^ folders) {
        task<void> chain = task_from_result();
        for (auto folder : folders) {
            chain = chain.then([folder,target,progress,report]() {
                return create_task(target->CreateFolderAsync(folder->Name, CreationCollisionOption::OpenIfExists))
                    .then([folder,progress,report](StorageFolder^ child) {
                        return StageFolder(folder, child, progress, report);
                    });
            });
        }
        return chain;
    });
}

std::string Utf8Of(Platform::String^ source) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, source->Data(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return std::string();
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, source->Data(), -1, &result[0], count, nullptr, nullptr);
    result.resize(static_cast<size_t>(count - 1));
    return result;
}
}

void CatalogShell::AttemptBoot(size_t index) {
    if (index >= games_.size()) return;
    if (booting_) {
        SetStatus(L"A game is already starting. Wait for it to finish.");
        return;
    }
    PutPref(L"lastPlayed", games_[index].name);
    auto folder = games_[index].directory;
    // A folder game also carries its eboot.bin; the whole folder is what runs.
    auto file = folder != nullptr ? nullptr : games_[index].file;
    if (file == nullptr && folder == nullptr) {
        SetStatus(L"This entry has no game file or folder to start.");
        return;
    }
    if (file != nullptr && _wcsicmp(file->Name->Data(), L"eboot.bin") == 0) {
        SetStatus(L"An eboot.bin can't start on its own. Add the game's whole folder with Choose a folder.");
        return;
    }
    std::wstring probe;
    if (!libretro_.Probe(probe)) {
        SetStatus(StringOf(probe));
        return;
    }
    if (!HasFiles(FirmwareRoot() + L"\\vs0")) {
        SetStatus(L"Install the system firmware first: Settings, Firmware, Install firmware files.");
        ShowPage(2);
        return;
    }
    // Games are read where they are (internal storage or a USB drive): the
    // core reaches them through the FromApp file redirect. Only a place it
    // still cannot read falls back to copying the game into app storage.
    const std::wstring direct = folder != nullptr ? folder->Path->Data() : file->Path->Data();
    const std::wstring check = folder != nullptr ? direct + L"\\sce_sys\\param.sfo" : direct;
    if (CanOpenFromApp(check)) {
        booting_ = true;
        SetStatus(ref new Platform::String((L"Starting " + std::wstring(games_[index].name->Data()) + L"...").c_str()));
        StartCore(ref new Platform::String(direct.c_str()));
        return;
    }
    booting_ = true;
    // Vita3K reads games with ordinary file APIs, which UWP only allows inside
    // the app's own storage, so the game is copied there first. The copy keeps
    // the original names (the core recognises eboot.bin and folder layouts by
    // name) and is reused on the next start.
    auto progress = std::make_shared<StageProgress>();
    auto dispatcher = Dispatcher;
    auto lastReport = std::make_shared<std::atomic<uint32_t>>(0);
    std::function<void()> report = [this,progress,dispatcher,lastReport]() {
        const uint32_t done = progress->files + progress->skipped;
        if (done - lastReport->load() < 10) return;
        lastReport->store(done);
        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Low,
            ref new Windows::UI::Core::DispatchedHandler([this,progress]() {
                SetStatus(L"Copying the game into Vita3K storage: " +
                    StringOf(std::to_wstring(progress->files.load())) + L" copied, " +
                    StringOf(std::to_wstring(progress->skipped.load())) + L" already there...");
            }));
    };
    SetStatus(L"Vita3K can't read this game where it is, so it is being copied into app storage first...");
    create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(
        L"Vita3KContent", CreationCollisionOption::OpenIfExists))
    .then([file,folder,progress,report](StorageFolder^ content) -> task<Platform::String^> {
        if (file != nullptr) {
            return create_task(content->CreateFolderAsync(L"files", CreationCollisionOption::OpenIfExists))
            .then([file,progress](StorageFolder^ files) {
                // eboot.bin is only meaningful inside its game folder.
                return StageFile(file, files, progress).then([files,file]() {
                    return files->Path + L"\\" + file->Name;
                });
            });
        }
        return create_task(content->CreateFolderAsync(folder->Name, CreationCollisionOption::OpenIfExists))
        .then([folder,progress,report](StorageFolder^ target) {
            return StageFolder(folder, target, progress, report).then([target]() { return target->Path; });
        });
    }).then([this,progress](task<Platform::String^> staged) {
        try {
            auto path = staged.get();
            SetStatus(L"Game ready (" + StringOf(std::to_wstring(progress->files.load())) +
                L" files copied, " + StringOf(std::to_wstring(progress->skipped.load())) +
                L" reused). Starting Vita3K...");
            StartCore(path);
        } catch (Platform::Exception^ error) {
            booting_ = false;
            SetStatus(L"Couldn't copy the game into Vita3K storage: " + error->Message +
                L" Check free space on the console.");
        } catch (...) {
            booting_ = false;
            SetStatus(L"Couldn't copy the game into Vita3K storage.");
        }
    }, task_continuation_context::use_current());
}

void CatalogShell::EnterGameView(Platform::String^ title) {
    inGame_ = true;
    gameTitle_ = title;
    quickMenu_ = nullptr;
    quickMenuOpen_ = false;
    gameStarted_ = false;
    loadFailed_ = false;
    loadingHidden_ = false;
    gameRunningSince_ = 0;
    pictureSeen_ = false;
    framesShown_ = 0;
    lastLevel_ = 0;
    lastLit_ = 0;
    {
        std::lock_guard<std::mutex> guard(presentErrorMutex_);
        presentError_.clear();
    }
    // The game owns the whole screen: the menus go away (and can't take
    // focus or controller input), the menu music stops.
    navigation_->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
    if (contentScroll_) contentScroll_->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
    if (statusBar_) statusBar_->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
    musicPlayer_->Pause();
    // Nothing of the menu scene runs under a game.
    backdrop_.SetActive(false);
    if (clockTimer_) clockTimer_->Stop();

    gameLayer_ = ref new Grid();
    gameLayer_->Background = Solid(Colors::Black);
    Grid::SetColumnSpan(gameLayer_, 2);
    Grid::SetRowSpan(gameLayer_, 2);
    renderSurface_ = ref new SwapChainPanel();
    renderSurface_->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
    renderSurface_->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Stretch;
    renderSurface_->SizeChanged += ref new SizeChangedEventHandler([this](Platform::Object^, SizeChangedEventArgs^ e) {
        renderer_.SetViewport(static_cast<float>(e->NewSize.Width), static_cast<float>(e->NewSize.Height),
            Pref(L"display.stretch", L"0")->Equals(L"1"));
    });
    gameLayer_->Children->Append(renderSurface_);

    // Loading screen, over the game until it shows its first picture.
    loadingLayer_ = ref new Grid();
    loadingLayer_->Background = Gradient(Bg(), Glow());
    auto column = Vertical();
    column->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    column->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    column->MaxWidth = 560;
    auto kicker = Text(L"NOW LOADING", 10, true);
    kicker->CharacterSpacing = 300;
    kicker->Foreground = Solid(Accent());
    kicker->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    column->Children->Append(kicker);
    auto name = Text(title, 30, false);
    name->FontWeight = FontWeights::Light;
    name->TextAlignment = TextAlignment::Center;
    name->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    name->Margin = Thickness(0, 10, 0, 22);
    column->Children->Append(name);
    loadingBar_ = ref new ProgressBar();
    loadingBar_->Width = 420;
    loadingBar_->Height = 4;
    loadingBar_->Minimum = 0;
    loadingBar_->Maximum = 100;
    loadingBar_->IsIndeterminate = true;
    loadingBar_->Foreground = Solid(Accent());
    loadingBar_->Background = Solid(Alpha(40, 255, 255, 255));
    column->Children->Append(loadingBar_);
    loadingStage_ = Text(L"Getting ready", 14, false, true);
    loadingStage_->TextAlignment = TextAlignment::Center;
    loadingStage_->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    loadingStage_->Margin = Thickness(0, 14, 0, 0);
    column->Children->Append(loadingStage_);
    loadingHint_ = Text(L"Press View + Menu together for the quick menu.", 11, false, true);
    loadingHint_->Opacity = 0.7;
    loadingHint_->TextAlignment = TextAlignment::Center;
    loadingHint_->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    loadingHint_->Margin = Thickness(0, 40, 0, 0);
    column->Children->Append(loadingHint_);
    loadingLayer_->Children->Append(column);
    gameLayer_->Children->Append(loadingLayer_);
#if !defined(VITA3K_RELEASE_BUILD)
    // Development builds: what the display path sees, always in a corner.
    statsLabel_ = Text(L"", 10, false, true);
    statsLabel_->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Right;
    statsLabel_->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Bottom;
    statsLabel_->Margin = Thickness(0, 0, 24, 14);
    statsLabel_->Opacity = 0.6;
    gameLayer_->Children->Append(statsLabel_);
#endif
    root_->Children->Append(gameLayer_);

    if (!gameTimer_) {
        gameTimer_ = ref new DispatcherTimer();
        TimeSpan interval;
        interval.Duration = 2000000; // 200 ms
        gameTimer_->Interval = interval;
        gameTimer_->Tick += ref new EventHandler<Platform::Object^>([this](Platform::Object^, Platform::Object^) { GameTick(); });
    }
    gameTimer_->Start();
}

void CatalogShell::GameTick() {
    if (!inGame_ || loadFailed_) return;
    if (gameStarted_ && !libretro_.Running()) {
        // View + Menu, or the core stopped on its own.
        ExitGameView();
        return;
    }
    if (!gameStarted_) return;
    if (loadingHidden_) { UpdateGameStats(); return; }
    const auto now = GetTickCount64();
    if (!gameRunningSince_) gameRunningSince_ = now;
    if (pictureSeen_.load() || now - gameRunningSince_ > 60000) {
        // The game is drawing: fade the loading screen out.
        loadingHidden_ = true;
        auto fade = ref new DoubleAnimation();
        fade->From = 1.0;
        fade->To = 0.0;
        fade->Duration = DurationHelper::FromTimeSpan(Millis(350));
        auto story = ref new Storyboard();
        Storyboard::SetTarget(fade, loadingLayer_);
        Storyboard::SetTargetProperty(fade, L"Opacity");
        story->Children->Append(fade);
        story->Completed += ref new EventHandler<Platform::Object^>([this](Platform::Object^, Platform::Object^) {
            if (loadingLayer_) loadingLayer_->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
        });
        story->Begin();
        return;
    }
    if (now - gameRunningSince_ > 8000 && loadingStage_) {
        loadingStage_->Text = L"The game is running and getting its first picture ready. Some games stay dark for a while at start.";
    }
    UpdateGameStats();
}

void CatalogShell::UpdateGameStats() {
    std::wstring presenter;
    {
        std::lock_guard<std::mutex> guard(presentErrorMutex_);
        presenter = presentError_;
    }
    wchar_t text[256] = {};
    swprintf_s(text, L"Frames %llu  \u00B7  picture level %u (%u of 225 points lit)  \u00B7  display %s",
        framesShown_.load(), lastLevel_.load(), lastLit_.load(), presenter.empty() ? L"OK" : presenter.c_str());
    if (loadingHint_ && gameStarted_ && !loadFailed_)
        loadingHint_->Text = ref new Platform::String(text);
    if (statsLabel_) statsLabel_->Text = ref new Platform::String(text);
}

void CatalogShell::ShowLoadFailure(Platform::String^ detail) {
    loadFailed_ = true;
    if (gameTimer_) gameTimer_->Stop();
    if (!loadingLayer_) return;
    loadingLayer_->Visibility = Windows::UI::Xaml::Visibility::Visible;
    loadingLayer_->Opacity = 1.0;
    loadingBar_->IsIndeterminate = false;
    loadingBar_->Value = 0;
    loadingBar_->Visibility = Windows::UI::Xaml::Visibility::Collapsed;
    loadingStage_->Text = L"This game couldn't start.";
    std::wstring text = detail ? detail->Data() : L"";
    if (text.size() > 400) text = text.substr(0, 400) + L"...";
    loadingHint_->Text = ref new Platform::String((text + L"\nvita3k-runtime.log has the details.").c_str());
    loadingHint_->Opacity = 0.85;
    auto back = Button(L"Back to the library", true);
    back->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    back->Margin = Thickness(0, 24, 0, 0);
    back->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { ExitGameView(); });
    auto column = safe_cast<StackPanel^>(loadingLayer_->Children->GetAt(0));
    column->Children->Append(back);
    inGame_ = false; // the controller drives this button now
    back->Focus(Windows::UI::Xaml::FocusState::Programmatic);
}

void CatalogShell::ExitGameView() {
    if (gameTimer_) gameTimer_->Stop();
    inGame_ = false;
    libretro_.SetProgressSink(nullptr);
    libretro_.SetMenuSink(nullptr);
    DeleteFileW((std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\vita3k-session.txt").c_str());
    quickMenuOpen_ = false;
    quickMenu_ = nullptr;
    SetStatus(L"Closing the game...");
    // Unloading joins the core's threads; keep it off the UI thread.
    create_task([this]() {
        libretro_.StopRunning();
        libretro_.UnloadGame();
    }).then([this](task<void> done) {
        try { done.get(); } catch (...) {}
        renderer_.Shutdown();
        if (gameLayer_) {
            unsigned index = 0;
            if (root_->Children->IndexOf(gameLayer_, &index)) root_->Children->RemoveAt(index);
        }
        gameLayer_ = nullptr;
        loadingLayer_ = nullptr;
        loadingStage_ = nullptr;
        loadingHint_ = nullptr;
        loadingBar_ = nullptr;
        renderSurface_ = nullptr;
        statsLabel_ = nullptr;
        booting_ = false;
        navigation_->Visibility = Windows::UI::Xaml::Visibility::Visible;
        if (contentScroll_) contentScroll_->Visibility = Windows::UI::Xaml::Visibility::Visible;
        if (statusBar_) statusBar_->Visibility = Windows::UI::Xaml::Visibility::Visible;
        backdrop_.SetActive(true);
        if (clockTimer_) clockTimer_->Start();
        if (track_ > 0) musicPlayer_->Play();
        SetStatus(L"Back in the library.");
        ShowPage(1);
    }, task_continuation_context::use_current());
}

void CatalogShell::ShowQuickMenu() {
    if (!inGame_ || !gameLayer_ || quickMenuOpen_) return;
    if (!gameStarted_ || loadFailed_) {
        // Still loading, or failed: nothing to pause.
        libretro_.SetPaused(false);
        return;
    }
    libretro_.SetPaused(true);
    quickMenuOpen_ = true;
    PlayCue(1);

    quickMenu_ = ref new Grid();
    quickMenu_->Background = Solid(Alpha(190, 6, 6, 10));
    quickMenu_->Opacity = 0;

    auto card = ref new Border();
    card->Width = 440;
    card->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Center;
    card->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Center;
    card->CornerRadius = Windows::UI::Xaml::CornerRadius(22);
    card->Padding = Thickness(34, 30, 34, 26);
    card->Background = Gradient(Card(), Glow());
    card->BorderBrush = Solid(Alpha(34, 255, 255, 255));
    card->BorderThickness = Thickness(1);

    auto column = Vertical(0);
    auto kicker = Text(L"PAUSED", 10, true);
    kicker->CharacterSpacing = 300;
    kicker->Foreground = Solid(Accent());
    column->Children->Append(kicker);
    auto name = Text(gameTitle_ ? gameTitle_ : ref new Platform::String(L"Your game"), 26, false);
    name->FontWeight = FontWeights::Light;
    name->TextWrapping = TextWrapping::Wrap;
    name->MaxLines = 2;
    name->Margin = Thickness(0, 2, 0, 14);
    column->Children->Append(name);

    auto add = [this,column](Platform::String^ label, bool highlighted) {
        auto button = Button(label, highlighted);
        button->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
        button->HorizontalContentAlignment = Windows::UI::Xaml::HorizontalAlignment::Left;
        button->Padding = Thickness(18, 0, 18, 0);
        button->Height = 50;
        button->Margin = Thickness(0, 0, 0, 10);
        column->Children->Append(button);
        return button;
    };
    auto resume = add(L"\u25B6   Resume", true);
    resume->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) { HideQuickMenu(); });

    const bool stretch = Pref(L"display.stretch", L"0")->Equals(L"1");
    auto screen = add(ref new Platform::String(stretch ? L"\u2B1A   Screen: Stretch" : L"\u2B1A   Screen: Fit"), false);
    screen->Click += ref new RoutedEventHandler([this,screen](Platform::Object^, RoutedEventArgs^) {
        const bool next = !Pref(L"display.stretch", L"0")->Equals(L"1");
        PutPref(L"display.stretch", ref new Platform::String(next ? L"1" : L"0"));
        screen->Content = ref new Platform::String(next ? L"\u2B1A   Screen: Stretch" : L"\u2B1A   Screen: Fit");
        if (renderSurface_)
            renderer_.SetViewport(static_cast<float>(renderSurface_->ActualWidth), static_cast<float>(renderSurface_->ActualHeight), next);
    });

    auto log = add(L"\u2913   Save log to your drive", false);
    log->Click += ref new RoutedEventHandler([this,log](Platform::Object^, RoutedEventArgs^) {
        SaveLogToDrive();
        log->Content = ref new Platform::String(L"\u2713   Saving the log to your drive");
    });

    auto quit = add(L"\u2715   Quit game", false);
    quit->Margin = Thickness(0, 8, 0, 4);
    quit->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        quickMenuOpen_ = false;
        ExitGameView();
    });

    auto hint = Text(L"B  Resume     \u00B7     A  Select", 11, false, true);
    hint->Opacity = 0.7;
    hint->Margin = Thickness(2, 14, 0, 0);
    column->Children->Append(hint);
    card->Child = column;
    quickMenu_->Children->Append(card);
    gameLayer_->Children->Append(quickMenu_);

    auto fade = ref new DoubleAnimation();
    fade->From = 0.0;
    fade->To = 1.0;
    fade->Duration = DurationHelper::FromTimeSpan(Millis(160));
    auto story = ref new Storyboard();
    Storyboard::SetTarget(fade, quickMenu_);
    Storyboard::SetTargetProperty(fade, L"Opacity");
    story->Children->Append(fade);
    story->Begin();
    Reveal(card, 0, 14);
    resume->Focus(Windows::UI::Xaml::FocusState::Programmatic);
}

void CatalogShell::HideQuickMenu() {
    if (!quickMenuOpen_) return;
    quickMenuOpen_ = false;
    if (gameLayer_ && quickMenu_) {
        unsigned index = 0;
        if (gameLayer_->Children->IndexOf(quickMenu_, &index)) gameLayer_->Children->RemoveAt(index);
    }
    quickMenu_ = nullptr;
    libretro_.SetPaused(false);
}

// A 32-bit BMP, top-down; the pixels' alpha is dropped.
static bool WriteBmp(const std::wstring& path, const uint8_t* pixels, unsigned width, unsigned height, bool rgba) {
    if (!pixels || !width || !height) return false;
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file) return false;
    const uint32_t imageBytes = width * height * 4;
    uint8_t header[54] = {};
    auto put32 = [&header](size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) header[at + i] = static_cast<uint8_t>(v >> (8 * i)); };
    header[0] = 'B'; header[1] = 'M';
    put32(2, 54 + imageBytes);
    put32(10, 54);
    put32(14, 40);
    put32(18, width);
    put32(22, static_cast<uint32_t>(-static_cast<int32_t>(height))); // negative: top-down rows
    header[26] = 1;
    header[28] = 32;
    put32(34, imageBytes);
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    std::vector<uint8_t> row(static_cast<size_t>(width) * 4);
    for (unsigned y = 0; ok && y < height; ++y) {
        const uint8_t* in = pixels + static_cast<size_t>(y) * width * 4;
        for (unsigned x = 0; x < width; ++x) {
            row[x * 4 + 0] = rgba ? in[x * 4 + 2] : in[x * 4 + 0];
            row[x * 4 + 1] = in[x * 4 + 1];
            row[x * 4 + 2] = rgba ? in[x * 4 + 0] : in[x * 4 + 2];
            row[x * 4 + 3] = 255;
        }
        ok = fwrite(row.data(), 1, row.size(), file) == row.size();
    }
    fclose(file);
    return ok;
}

void CatalogShell::StartCore(Platform::String^ contentPath) {
    const std::string system = NarrowUtf8(DataRoot());
    const std::string content = Utf8Of(contentPath);
    Platform::String^ title = L"Starting your game";
    for (const auto& game : games_) {
        const std::wstring path = contentPath->Data();
        if ((game.directory && path == game.directory->Path->Data()) || (game.file && path == game.file->Path->Data())) {
            title = game.name;
            break;
        }
    }
    libretro_.StopRunning();
    renderer_.Shutdown();
    EnterGameView(title);
    {
        const std::wstring marker = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\vita3k-session.txt";
        FILE* file = nullptr;
        if (_wfopen_s(&file, marker.c_str(), L"wb") == 0 && file) {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            const std::string line = NarrowUtf8(title->Data()) + ", started " + std::to_string(now.wHour) + ":" +
                (now.wMinute < 10 ? "0" : "") + std::to_string(now.wMinute);
            fwrite(line.data(), 1, line.size(), file);
            fclose(file);
        }
    }
    std::wstring displayError;
    if (!renderer_.Initialize(reinterpret_cast<IUnknown*>(renderSurface_), 960, 544, displayError)) {
        booting_ = false;
        if (displayError.find(L"2289696800") != std::wstring::npos || displayError.find(L"2289696773") != std::wstring::npos)
            // DXGI_ERROR_DRIVER_INTERNAL_ERROR / DEVICE_REMOVED: a game before
            // this one left the console's GPU driver failed for this app.
            displayError = L"The graphics driver stopped during an earlier game. Close Vita3K completely (Menu button on its tile, then Quit) and open it again.";
        libretro_.LogLine("Display: the presenter could not start: " + NarrowUtf8(displayError));
        ShowLoadFailure(StringOf(displayError));
        return;
    }
    libretro_.LogLine("Display: presenter ready (" + NarrowUtf8(displayError) + ")");
    libretro_.LogLine(D3D12Host::DebugLayerOn()
        ? "Display: D3D12 debug layer on; its errors follow as [d3d12] lines"
        : "Display: D3D12 debug layer not available on this console (or a release build)");
    const std::wstring snapshotPath = std::wstring(ApplicationData::Current->LocalFolder->Path->Data()) + L"\\frame-300.bmp";
    libretro_.SetFrameSink([this,snapshotPath](const uint8_t* pixels, unsigned width, unsigned height, bool rgba) {
        std::wstring error;
        const bool presented = renderer_.PresentFrame(pixels, width, height, width * 4, rgba, error);
        const auto shown = ++framesShown_;
        if (shown % 3600 == 0) vita3k_uwp::ReassertCrashLog();
        if (shown <= 5 || shown % 30 == 0) {
            for (const auto& message : renderer_.TakeDebugMessages())
                libretro_.LogLine("[d3d12] " + message);
        }
        if (shown == 300) {
            // Exactly what the core handed over, next to the log: settles
            // whether a dark screen is in the frames or in the display.
            if (WriteBmp(snapshotPath, pixels, width, height, rgba))
                libretro_.LogLine("Display: frame 300 saved as frame-300.bmp next to this log");
        }
        if (!presented) {
            std::lock_guard<std::mutex> guard(presentErrorMutex_);
            presentError_ = error.empty() ? L"presenter not ready" : error;
        }
        if (!pictureSeen_.load() || shown == 60 || shown == 600 || shown % 3600 == 0) {
            // Sample a grid of the frame: is anything visible yet, and how bright is it?
            unsigned lit = 0, total = 0;
            unsigned long long sum = 0;
            for (unsigned gy = 1; gy < 16; ++gy) {
                const unsigned y = height * gy / 16;
                for (unsigned gx = 1; gx < 16; ++gx) {
                    const unsigned x = width * gx / 16;
                    const uint8_t* p = pixels + (static_cast<size_t>(y) * width + x) * 4;
                    const unsigned value = (static_cast<unsigned>(p[0]) + p[1] + p[2]) / 3;
                    sum += value;
                    ++total;
                    if (value > 20) ++lit;
                }
            }
            if (lit > 0) pictureSeen_ = true;
            lastLevel_ = static_cast<unsigned>(total ? sum / total : 0);
            lastLit_ = lit;
            if (shown == 1 || shown == 60 || shown == 600 || shown % 3600 == 0) {
                // Memory too: the console ends an app over its limit with no
                // crash to log, so the climb towards it has to show here.
                unsigned long long usedMb = 0, limitMb = 0;
                try {
                    usedMb = Windows::System::MemoryManager::AppMemoryUsage / (1024ull * 1024ull);
                    limitMb = Windows::System::MemoryManager::AppMemoryUsageLimit / (1024ull * 1024ull);
                } catch (...) {}
                char line[220];
                std::snprintf(line, sizeof(line), "Display: frame %llu, %ux%u, %u of %u samples lit, average level %llu; app memory %llu of %llu MB",
                    shown, width, height, lit, total, total ? sum / total : 0ull, usedMb, limitMb);
                libretro_.LogLine(line);
            }
        }
        if (!error.empty() && (shown <= 3 || shown % 600 == 0))
            libretro_.LogLine("Display: " + NarrowUtf8(error));
    });
    auto dispatcher = Dispatcher;
    libretro_.SetMenuSink([this,dispatcher]() {
        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::High,
            ref new Windows::UI::Core::DispatchedHandler([this]() { ShowQuickMenu(); }));
    });
    libretro_.SetProgressSink([this,dispatcher](const std::string& stage, int percent) {
        const std::wstring text = WidenUtf8(stage);
        dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
            ref new Windows::UI::Core::DispatchedHandler([this,text,percent]() {
                if (!loadingStage_ || loadFailed_) return;
                if (!text.empty() && text[0] == L'!') {
                    // A stage that ends the game (the GPU stopped): say so.
                    if (loadingLayer_) loadingLayer_->Visibility = Windows::UI::Xaml::Visibility::Visible;
                    ShowLoadFailure(ref new Platform::String(text.c_str() + 1));
                    return;
                }
                loadingStage_->Text = ref new Platform::String(text.c_str());
                loadingBar_->IsIndeterminate = percent < 0;
                if (percent >= 0) loadingBar_->Value = percent;
            }));
    });
    ApplyCoreOptions(contentPath->Data());
    // No D3D12 pipeline library: on the Xbox, Dozen's attempt to make one
    // (ONYX_PSO_CACHE) came back with the GPU device removed, at the game's
    // first pipeline, and every later game then failed to start.
    VitaVulkanBridge::SetPipelineCachePath(L"");
    const std::wstring addonsRoot = Pref(L"storage.addons", L"")->Data();
    const std::wstring titleId = TitleIdIn(contentPath->Data());
    create_task([this,system,content,addonsRoot,titleId]() {
        std::wstring detail;
        if (!libretro_.Initialize(system, detail)) return std::make_pair(false, detail);
        // Its updates and DLC dropped in the updates folder since it last ran
        if (!addonsRoot.empty() && !titleId.empty()) {
            int installed = 0, failed = 0;
            InstallPendingAddons(libretro_, system, addonsRoot, titleId, installed, failed);
        }
        if (!libretro_.LoadGame(content, detail)) return std::make_pair(false, detail);
        if (!libretro_.StartRunning(detail)) return std::make_pair(false, detail);
        return std::make_pair(true, detail);
    }).then([this](task<std::pair<bool, std::wstring>> result) {
        booting_ = false;
        std::pair<bool, std::wstring> outcome{ false, L"Vita3K stopped while loading." };
        try { outcome = result.get(); } catch (...) {}
        if (!outcome.first) {
            ShowLoadFailure(ref new Platform::String(outcome.second.c_str()));
            return;
        }
        gameStarted_ = true;
        if (loadingStage_ && !pictureSeen_.load()) loadingStage_->Text = L"Starting the game";
    }, task_continuation_context::use_current());
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
                    SetStatus(loaded ? L"Vita3K core initialization returned. "
                        L"No game execution or Vulkan context is available." :
                        L"Vita3K core initialization blocked. See diagnostic details.");
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
    kind %= vita3k_uwp::kInterfaceCueCount;
    if (!cueReady_[kind] || cuePlayers_[kind] == nullptr) return;
    // Dedicated player per cue means focus and confirm don't cut one another off.
    cuePlayers_[kind]->Stop();
    cuePlayers_[kind]->Play();
}
void CatalogShell::PrepareCues() {
    // Small UI sounds also use durable file-backed sources. MediaOpened is
    // the readiness gate; a generated byte buffer alone is not proof of audio.
    create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(
        L"MenuAudio", CreationCollisionOption::OpenIfExists))
    .then([this](task<StorageFolder^> result) {
        try {
            auto folder = result.get();
            for (unsigned i = 0; i < vita3k_uwp::kInterfaceCueCount; ++i) {
                auto bytes = vita3k_uwp::GenerateInterfaceCue(i);
                auto buffer = ref new Platform::Array<unsigned char>(
                    static_cast<unsigned>(bytes.size()));
                std::memcpy(buffer->Data, bytes.data(), bytes.size());
                const auto name = L"ui" + StringOf(std::to_wstring(i)) + L".wav";
                create_task(folder->CreateFileAsync(
                    name, CreationCollisionOption::ReplaceExisting))
                .then([buffer](StorageFile^ file) {
                    return create_task(FileIO::WriteBytesAsync(file, buffer))
                        .then([file]() { return file; });
                }).then([this,i](task<StorageFile^> completed) {
                    try {
                        const auto file = completed.get();
                        cueReady_[i] = false;
                        cuePlayers_[i]->Source = ref new Uri(
                            L"ms-appdata:///local/MenuAudio/" + file->Name);
                    } catch (Platform::Exception^) {
                        cueReady_[i] = false;
                    }
                }, task_continuation_context::use_current());
            }
        } catch (Platform::Exception^ error) {
            SetStatus(L"Could not prepare UI audio: " + error->Message);
        }
    }, task_continuation_context::use_current());
}
} // namespace Vita3KUWP
