// SPDX-License-Identifier: GPL-2.0-or-later
#include <collection.h>

using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;

namespace Vita3KUWP {
public ref class App sealed : Application {
public:
    App() {}
protected:
    virtual void OnLaunched(LaunchActivatedEventArgs^ args) override {
        if (args->PrelaunchActivated) return;
        auto panel = ref new StackPanel();
        panel->Margin = Thickness(64);
        panel->VerticalAlignment = VerticalAlignment::Center;
        auto title = ref new TextBlock();
        title->Text = "Vita3K UWP";
        title->FontSize = 48;
        title->Foreground = ref new SolidColorBrush(Colors::White);
        panel->Children->Append(title);
        auto status = ref new TextBlock();
        status->Text = "DEVELOPMENT SHELL / PRE-ALPHA";
        status->FontSize = 18;
        status->Margin = Thickness(0, 20, 0, 20);
        status->Foreground = ref new SolidColorBrush(Colors::DeepSkyBlue);
        panel->Children->Append(status);
        auto description = ref new TextBlock();
        description->Text = "Packaging and launch foundation for the Xbox UWP port.\n\nThe Vita3K emulation core is not integrated.\nThis build cannot install or run PS Vita games.\n\nD3D12: planned native backend.\nVulkan / OpenGL: platform feasibility research.\n\ngithub.com/oledz-i/Vita3K-UWP";
        description->FontSize = 22;
        description->TextWrapping = TextWrapping::Wrap;
        description->Foreground = ref new SolidColorBrush(Colors::White);
        panel->Children->Append(description);
        auto scroll = ref new ScrollViewer();
        scroll->Content = panel;
        scroll->Background = ref new SolidColorBrush(ColorHelper::FromArgb(255, 12, 18, 30));
        Window::Current->Content = scroll;
        Window::Current->Activate();
    }
};
}

[MTAThread]
int main(Platform::Array<Platform::String^>^) {
    Application::Start(ref new ApplicationInitializationCallback([](ApplicationInitializationCallbackParams^) {
        ref new Vita3KUWP::App();
    }));
    return 0;
}
