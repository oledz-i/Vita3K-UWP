// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox UWP application entry point. The frontend is in CatalogShell.cpp.
#include <collection.h>
#include "CatalogShell.h"
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;

namespace Vita3KUWP {
public ref class App sealed : Application {
public:
    App() {
        RequiresPointerMode = ApplicationRequiresPointerMode::WhenRequested;
        ElementSoundPlayer::State = ElementSoundPlayerState::Off;
    }
protected:
    virtual void OnLaunched(LaunchActivatedEventArgs^ args) override {
        if (args->PrelaunchActivated) return;
        // Draw edge to edge; the shell keeps its own title-safe margins, so
        // backgrounds reach the screen edge instead of stopping at a border.
        Windows::UI::ViewManagement::ApplicationView::GetForCurrentView()->SetDesiredBoundsMode(
            Windows::UI::ViewManagement::ApplicationViewBoundsMode::UseCoreWindow);
        Window::Current->Content = ref new CatalogShell();
        Window::Current->Activate();
    }
};
}
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^) {
    Application::Start(ref new ApplicationInitializationCallback([](ApplicationInitializationCallbackParams^) {
        ref new Vita3KUWP::App();
    }));
    return 0;
}
