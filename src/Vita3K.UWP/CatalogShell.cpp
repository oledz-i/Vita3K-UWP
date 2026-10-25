// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox-first, streaming-catalog frontend. UI is not a claim of game compatibility.
#include "CatalogShell.h"
#include "MenuMusic.h"
#include "SfoReader.h"
#include "GameFormats.h"
#include "VulkanDznProbe.h"
#include "VitaGuestCpuUwpProbe.h"
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
    switch (theme_) {
    case 1: return RgbColor(16, 32, 35);
    case 2: return RgbColor(35, 24, 22);
    case 3: return RgbColor(22, 33, 46);
    case 4: return RgbColor(27, 22, 41);
    default: return RgbColor(24, 25, 30);
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
    scroll->VerticalScrollBarVisibility = ScrollBarVisibility::Hidden;
    scroll->HorizontalScrollBarVisibility = ScrollBarVisibility::Disabled;
    content_ = Vertical();
    content_->Margin = Thickness(30, 27, 48, 20);
    scroll->Content = content_;
    Grid::SetColumn(scroll, 1);
    Grid::SetRow(scroll, 0);
    root_->Children->Append(scroll);

    auto statusBar = ref new Border();
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
    auto hero = ref new Border();
    hero->Height = 262;
    hero->CornerRadius = Windows::UI::Xaml::CornerRadius(18);
    hero->Margin = Thickness(0, 0, 0, 20);
    hero->BorderBrush = Solid(Alpha(22, 255, 255, 255));
    hero->BorderThickness = Thickness(1);
    auto heroLayout = ref new Grid();
    heroLayout->Background = Gradient(Card(), Glow());
    if (hasGames && !ArtworkUri(0)->IsEmpty()) {
        auto art = ref new Image();
        art->Source = ref new BitmapImage(ref new Uri(ArtworkUri(0)));
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
    auto kicker = Text(hasGames ? L"FEATURED" : L"WELCOME TO VITA3K", 10, true);
    kicker->CharacterSpacing = 240;
    kicker->Foreground = Solid(Accent());
    copy->Children->Append(kicker);
    auto headline = Text(hasGames ? games_[0].name : L"Every game.\nA new world.", 36, false);
    headline->FontWeight = FontWeights::Light;
    headline->MaxLines = 2;
    headline->Margin = Thickness(0, 8, 0, 10);
    copy->Children->Append(headline);
    auto detail = Text(hasGames
        ? L"Picked from your library. Open it to see the details or change its artwork."
        : L"Your Vita collection, on the big screen. Add a game or a folder to begin.", 13, false, true);
    detail->MaxWidth = 360;
    detail->Margin = Thickness(0, 0, 0, 18);
    copy->Children->Append(detail);
    auto controls = ref new StackPanel();
    controls->Orientation = Orientation::Horizontal;
    auto primary = Button(hasGames ? L"View game" : L"Add a game", true);
    primary->MinWidth = 132;
    primary->Click += ref new RoutedEventHandler([this,hasGames](Platform::Object^, RoutedEventArgs^) {
        if (!hasGames) ChooseGameFile();
        else { selectedGame_ = 0; ShowPage(4); }
    });
    controls->Children->Append(primary);
    auto secondary = Button(hasGames ? L"Browse library" : L"Choose a folder");
    secondary->Click += ref new RoutedEventHandler([this,hasGames](Platform::Object^, RoutedEventArgs^) {
        if (hasGames) ShowPage(1);
        else ChooseGameFolder();
    });
    controls->Children->Append(secondary);
    copy->Children->Append(controls);
    heroLayout->Children->Append(copy);
    hero->Child = heroLayout;
    content_->Children->Append(hero);

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
    auto note = Text(L"EARLY ACCESS   \u2022   Game execution is still in development. "
        L"Library items are not playable yet.", 10, false, true);
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
    auto warning = Text(L"Game file detection doesn't mean the title can run. The Vita3K executable core is still being ported.", 10, false, true);
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
    auto note = Text(L"Added to your personal collection. This build can display metadata and artwork while the emulator core is being ported.", 13, false, true);
    note->MaxWidth = 520;
    right->Children->Append(note);

    auto actions = ActionRow(168);
    actions->Margin = Thickness(0, 18, 0, 8);
    auto inspect = Button(L"Inspect game", true);
    inspect->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        InspectLaunch(index);
    });
    actions->Children->Append(inspect);
    // Show Launch only when a UWP-built, signed Vita3K core is present.
    // No inactive button or simulated launch screen in frontend-only builds.
    std::wstring coreStatus;
    if (libretro_.Probe(coreStatus) && games_[index].file != nullptr) {
        auto launch = Button(L"Launch Vita game", true);
        launch->Click += ref new RoutedEventHandler(
            [this,index](Platform::Object^, RoutedEventArgs^) {
                AttemptBoot(index);
            });
        actions->Children->Append(launch);
    }
    auto image = Button(L"Change cover art");
    image->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        ChooseCustomArtwork(index);
    });
    actions->Children->Append(image);
    auto find = Button(L"Find artwork online");
    find->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
        FetchArtwork(index);
    });
    actions->Children->Append(find);
    right->Children->Append(actions);

    auto validation = Text(L"SOURCE CHECK:  " + games_[index].validation, 12, false, true);
    validation->MaxWidth = 560;
    validation->Margin = Thickness(0, 8, 0, 4);
    right->Children->Append(validation);
    if (games_[index].format == vita3k_uwp::GameFormat::Pkg) {
        auto license = Button(L"Select owned PKG license");
        license->Margin = Thickness(0, 10, 0, 0);
        license->Click += ref new RoutedEventHandler([this,index](Platform::Object^, RoutedEventArgs^) {
            ChooseLicenseForGame(index);
        });
        right->Children->Append(license);
    }
    layout->Children->Append(right);
    feature->Child = layout;
    content_->Children->Append(feature);
    auto status = Text(L"EARLY ACCESS  /  Vita3K CPU, kernel, package installation and game rendering are still in development.", 10, false, true);
    status->Margin = Thickness(2, 14, 0, 0);
    content_->Children->Append(status);
}
void CatalogShell::Render() {
    content_->Children->Clear();
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
    Banner(L"PERSONALIZE YOUR EXPERIENCE", L"Your Vita. Your atmosphere.",
        L"Change the entire interface, choose a menu soundtrack, manage folders and artwork.");
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
        L"Off", L"Ambient Drift", L"Neon Pulse", L"Stardust", L"Tidal Blue", L"Custom track"
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

    Section(L"Storage and paths", L"Choose internal or external storage as Xbox UWP permits.");
    auto storage = Vertical();
    folderLabel_ = Text(L"Game library: not selected", 14, false, true);
    storage->Children->Append(folderLabel_);
    auto choose = Button(L"Choose game folder", true);
    choose->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ChooseGameFolder();
    });
    storage->Children->Append(choose);
    storage->Children->Append(Text(
        L"Application data: " + ApplicationData::Current->LocalFolder->Path, 12, false, true));
    firmwareLabel_ = Text(L"Firmware file: not selected", 14, false, true);
    storage->Children->Append(firmwareLabel_);
    storage->Children->Append(Text(
        L"Choose both official Sony firmware packages. They'll be copied into "
        L"Vita3K's private system directory, so the emulator can find them later. "
        L"Selecting a package does not install it until the Vita3K core is linked.",
        13, false, true));
    auto fw = Button(L"Choose main system firmware");
    fw->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
        ChooseFirmware();
    });
    storage->Children->Append(fw);
    auto preinstalled = Button(L"Choose preinstalled firmware");
    preinstalled->Margin = Thickness(0, 11, 0, 0);
    preinstalled->Click += ref new RoutedEventHandler(
        [this](Platform::Object^, RoutedEventArgs^) {
            ChoosePreinstalledFirmware();
        });
    storage->Children->Append(preinstalled);
    content_->Children->Append(Panel(storage));
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

    Section(L"Emulation engine", L"Technical details stay out of the way.");
    auto advanced = Vertical();
    advanced->Children->Append(Text(
        L"Game execution, firmware installation and full Vita3K CPU / GPU settings "
        L"will be available as the core integration progresses.", 13, false, true));
#if !defined(VITA3K_RELEASE_BUILD)
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

void CatalogShell::SaveTheme(int theme) {
    if (theme < 0 || theme > 4) return;
    theme_ = theme;
    PutPref(L"theme", StringOf(std::to_wstring(theme)));
    root_->Background = Solid(Bg());
    navigation_->Background = Solid(RailColor());
    PlayCue(5);
    auto footer = dynamic_cast<Border^>(root_->Children->GetAt(2));
    if (footer != nullptr) footer->Background = Solid(Bg());
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
    playbackStatus_ = L"Preparing original soundtrack...";
    if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
    // Use a *real file* under LocalFolder, not a temporary RAM stream.
    // Xbox's Media Foundation decoder can then seek and loop reliably.
    auto bytes = vita3k_uwp::GenerateMenuWav(static_cast<unsigned>(track - 1));
    auto data = ref new Platform::Array<unsigned char>(
        static_cast<unsigned>(bytes.size()));
    std::memcpy(data->Data, bytes.data(), bytes.size());
    create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(
        L"MenuAudio", CreationCollisionOption::OpenIfExists))
    .then([this,track,data](StorageFolder^ folder) {
        const auto filename = L"ambient" + StringOf(std::to_wstring(track)) + L".wav";
        return create_task(folder->CreateFileAsync(
            filename, CreationCollisionOption::ReplaceExisting))
        .then([fileData = data](StorageFile^ file) {
            return create_task(FileIO::WriteBytesAsync(file, fileData))
                .then([file]() { return file; });
        });
    }).then([this,track](task<StorageFile^> work) {
        try {
            auto file = work.get();
            if (track_ != track) return;  // Music changed during file write.
            musicPlayer_->IsLooping = true;
            musicPlayer_->Source = ref new Uri(
                L"ms-appdata:///local/MenuAudio/" + file->Name);
            playbackStatus_ = L"Loading " + file->Name + L"...";
            if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
            musicPlayer_->Play(); // MediaOpened also triggers Play once ready.
        } catch (Platform::Exception^ error) {
            playbackStatus_ = L"Couldn't prepare soundtrack: " + error->Message;
            if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
            SetStatus(playbackStatus_);
        } catch (...) {
            playbackStatus_ = L"Couldn't create the local soundtrack file.";
            if (musicStatus_ != nullptr) musicStatus_->Text = playbackStatus_;
        }
    }, task_continuation_context::use_current());
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
    if (firmwareLabel_ != nullptr) {
        const bool mainReady = Pref(L"firmware.stage.main", L"0")->Equals(L"1");
        const bool preinstReady = Pref(L"firmware.stage.preinst", L"0")->Equals(L"1");
        firmwareLabel_->Text = mainReady && preinstReady
            ? L"Both firmware packages are in Vita3K's system directory (not installed)."
            : mainReady ? L"Main firmware staged; preinstalled firmware still needed."
            : preinstReady ? L"Preinstalled firmware staged; main update still needed."
            : L"Two official firmware packages required.";
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
                        firmwareLabel_->Text = L"Firmware selected: " + file->Name +
                            L" (installation pending)";
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
    ChooseFirmwareSlot(false);
}
void CatalogShell::ChoosePreinstalledFirmware() {
    ChooseFirmwareSlot(true);
}
void CatalogShell::ChooseFirmwareSlot(bool preinstalled) {
    // Vita3K libretro requires BOTH official update and preinstalled firmware.
    // A UWP FutureAccessList token does not grant CRT fopen() access. Copy
    // the selected package into the frontend's writable system directory.
    Platform::String^ token = preinstalled ? L"vita-firmware-preinst" : L"vita-firmware";
    Platform::String^ name = preinstalled ? L"PSP2UPDAT_PREINST.PUP" : L"PSP2UPDAT.PUP";
    Platform::String^ statusKey = preinstalled ? L"firmware.stage.preinst" : L"firmware.stage.main";

    auto picker = ref new FileOpenPicker();
    picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
    picker->FileTypeFilter->Append(L".PUP");
    picker->FileTypeFilter->Append(L".pup");
    SetStatus(preinstalled
        ? L"Select the official PS Vita preinstalled firmware package."
        : L"Select the official PS Vita system software update package.");
    create_task(picker->PickSingleFileAsync()).then(
        [this,token,name,statusKey](task<StorageFile^> selection) {
            try {
                auto file = selection.get();
                if (file == nullptr) return;
                PutPref(statusKey, L"0");
                StorageApplicationPermissions::FutureAccessList->AddOrReplace(token, file);
                SetStatus(L"Copying official firmware package into Vita3K system storage...");
                create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(
                    L"Vita3KSystem", CreationCollisionOption::OpenIfExists))
                .then([](StorageFolder^ system) {
                    return create_task(system->CreateFolderAsync(
                        L"vita3k", CreationCollisionOption::OpenIfExists));
                }).then([file,name](StorageFolder^ data) {
                    // Memory-safe for large firmware files, unlike
                    // loading the whole .PUP into an IBuffer.
                    return create_task(file->CopyAsync(
                        data, name, NameCollisionOption::ReplaceExisting));
                }).then([this,statusKey,name](task<StorageFile^> copied) {
                    try {
                        auto staged = copied.get();
                        if (staged == nullptr) return;
                        PutPref(statusKey, L"1");
                        SetStatus(L"Firmware file prepared as " + name +
                            L". Vita3K installation still requires the emulator core.");
                        RestorePaths();
                    } catch (Platform::Exception^ e) {
                        SetStatus(L"Couldn't stage Vita firmware: " + e->Message);
                        RestorePaths();
                    }
                }, task_continuation_context::use_current());
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

void CatalogShell::AttemptBoot(size_t index) {
    if (index >= games_.size()) return;
    auto game = games_[index].file;
    if (game == nullptr) {
        SetStatus(L"Cannot launch: choose a Vita VPK/PKG/ZIP/VCI file. "
            L"Extracted game directories are not staged yet.");
        return;
    }
    std::wstring probe;
    if (!libretro_.Probe(probe)) {
        SetStatus(StringOf(probe));
        return;
    }
    // UWP's file picker grants a StorageFile capability; Vita3K's CRT-based
    // loader requires a path inside our own app sandbox or a working VFS.
    // CopyAsync streams disk data without buffering the entire Vita package.
    SetStatus(L"Preparing game file in Vita3K's private content storage...");
    create_task(ApplicationData::Current->LocalFolder->CreateFolderAsync(
        L"Vita3KContent", CreationCollisionOption::OpenIfExists))
    .then([game,index](StorageFolder^ target) {
        auto stagedName = L"vita-" + StringOf(std::to_wstring(index)) +
            L"-" + game->Name;
        return create_task(game->CopyAsync(
            target, stagedName, NameCollisionOption::ReplaceExisting));
    }).then([this](task<StorageFile^> staged) {
        try {
            auto file = staged.get();
            if (file == nullptr) return;
            auto folder = ApplicationData::Current->LocalFolder;
            auto systemPath = folder->Path + L"\\Vita3KSystem";
            auto gamePath = file->Path;
            auto toUtf8 = [](Platform::String^ source) {
                const int count = WideCharToMultiByte(
                    CP_UTF8, 0, source->Data(), -1, nullptr, 0, nullptr, nullptr);
                if (count <= 0) return std::string();
                std::string result(static_cast<size_t>(count), '\0');
                WideCharToMultiByte(CP_UTF8, 0, source->Data(), -1,
                    &result[0], count, nullptr, nullptr);
                return result;
            };
            const std::string system = toUtf8(systemPath);
            const std::string content = toUtf8(gamePath);
            libretro_.StopRunning();
            renderer_.Shutdown();
            content_->Children->Clear();
            renderSurface_ = ref new SwapChainPanel();
            renderSurface_->Height = 544;
            renderSurface_->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
            content_->Children->Append(renderSurface_);
            auto stop = Button(L"Stop game");
            stop->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
                libretro_.StopRunning(); libretro_.UnloadGame(); ShowPage(1);
            });
            content_->Children->Append(stop);
            std::wstring displayError;
            if (!renderer_.Initialize(reinterpret_cast<IUnknown*>(renderSurface_), 960, 544, displayError)) {
                SetStatus(StringOf(displayError)); return;
            }
            libretro_.SetFrameSink([this](const uint8_t* pixels, unsigned width, unsigned height, bool rgba) {
                std::wstring error;
                if (!renderer_.PresentScaledBgraFrame(pixels, width, height, width * 4, rgba, error))
                    throw std::runtime_error("D3D12 frame presentation failed");
            });
            SetStatus(L"Vita file staged. Initializing Vita3K core...");
            create_task([this,system,content]() {
                std::wstring detail;
                if (!libretro_.Initialize(system, detail)) return detail;
                if (!libretro_.LoadGame(content, detail)) return detail;
                if (!libretro_.StartRunning(detail)) return detail;
                return detail;
            }).then([this](task<std::wstring> result) {
                try { SetStatus(StringOf(result.get())); }
                catch (...) {
                    SetStatus(L"Vita3K loader failed. See core diagnostics.");
                }
            }, task_continuation_context::use_current());
        } catch (Platform::Exception^ error) {
            SetStatus(L"Couldn't stage game file: " + error->Message);
        }
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
