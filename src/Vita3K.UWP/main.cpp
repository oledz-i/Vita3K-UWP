// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox UWP application entry point. The frontend is in CatalogShell.cpp.
#include <collection.h>
#include "CatalogShell.h"
#include "CrashLog.h"
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;

namespace Vita3KUWP {
public ref class App sealed : Application {
public:
    App() {
        // First, so the core's guest fault filter chains to it when it loads.
        vita3k_uwp::InstallCrashLog(std::wstring(Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data()) +
            L"\\vita3k-runtime.log");
        UnhandledException += ref new UnhandledExceptionEventHandler([](Platform::Object^, UnhandledExceptionEventArgs^ e) {
            std::wstring message = e->Message ? e->Message->Data() : L"";
            char line[600] = {};
            WideCharToMultiByte(CP_UTF8, 0, (L"\nCRASH: unhandled app exception 0x" + std::to_wstring(static_cast<unsigned long>(e->Exception.Value)) +
                L": " + message + L"\n").c_str(), -1, line, sizeof(line) - 1, nullptr, nullptr);
            vita3k_uwp::WriteCrashLine(line);
        });
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
