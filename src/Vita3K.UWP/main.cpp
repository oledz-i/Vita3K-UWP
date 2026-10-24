// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox UWP host: persistent user-selected locations and firmware selection.
// Selecting firmware does NOT install it or imply the Vita3K core is present.
#include <collection.h>
#include <ppltasks.h>

using namespace concurrency;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::Storage;
using namespace Windows::Storage::AccessCache;
using namespace Windows::Storage::Pickers;
using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Media;

namespace Vita3KUWP {
public ref class App sealed : Application {
private:
    TextBlock^ gamesPath_;
    TextBlock^ firmwarePath_;
    TextBlock^ feedback_;

    TextBlock^ MakeText(Platform::String^ text, double size) {
        auto block = ref new TextBlock();
        block->Text = text;
        block->FontSize = size;
        block->TextWrapping = TextWrapping::Wrap;
        block->Foreground = ref new SolidColorBrush(Colors::White);
        block->Margin = Thickness(0, 8, 0, 8);
        return block;
    }

    Button^ MakeButton(Platform::String^ title) {
        auto button = ref new Button();
        button->Content = title;
        button->Margin = Thickness(0, 10, 0, 10);
        button->MinHeight = 52;
        button->MinWidth = 320;
        return button;
    }

    void RestoreSelections() {
        auto access = StorageApplicationPermissions::FutureAccessList;
        gamesPath_->Text = "Game folder: not selected";
        firmwarePath_->Text = "Firmware file: not selected";
        if (access->ContainsItem("vita-games")) {
            gamesPath_->Text = "Game folder access saved (select again to change)";
            create_task(access->GetFolderAsync("vita-games")).then([this](task<StorageFolder^> operation) {
                try { gamesPath_->Text = "Game folder: " + operation.get()->Path; }
                catch (Platform::Exception^) { gamesPath_->Text = "Game folder permission expired; select again"; }
            }, task_continuation_context::use_current());
        }
        if (access->ContainsItem("vita-firmware")) {
            firmwarePath_->Text = "Firmware selection saved (select again to change)";
            create_task(access->GetFileAsync("vita-firmware")).then([this](task<StorageFile^> operation) {
                try { firmwarePath_->Text = "Firmware selected: " + operation.get()->Name; }
                catch (Platform::Exception^) { firmwarePath_->Text = "Firmware permission expired; select again"; }
            }, task_continuation_context::use_current());
        }
    }

    void ChooseGamesFolder() {
        auto picker = ref new FolderPicker();
        picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
        picker->FileTypeFilter->Append("*");
        feedback_->Text = "Choose a folder containing your legally obtained games.";
        create_task(picker->PickSingleFolderAsync()).then([this](task<StorageFolder^> operation) {
            try {
                auto folder = operation.get();
                if (folder != nullptr) {
                    StorageApplicationPermissions::FutureAccessList->AddOrReplace("vita-games", folder);
                    gamesPath_->Text = "Game folder: " + folder->Path;
                    feedback_->Text = "Folder permission saved. Game scanning requires the Vita3K core.";
                }
            } catch (Platform::Exception^ error) {
                feedback_->Text = "Folder selection failed: " + error->Message;
            }
        }, task_continuation_context::use_current());
    }

    void ChooseFirmware() {
        auto picker = ref new FileOpenPicker();
        picker->SuggestedStartLocation = PickerLocationId::Downloads;
        picker->FileTypeFilter->Append(".PUP");
        feedback_->Text = "Choose your official PS Vita firmware .PUP file.";
        create_task(picker->PickSingleFileAsync()).then([this](task<StorageFile^> operation) {
            try {
                auto file = operation.get();
                if (file != nullptr) {
                    StorageApplicationPermissions::FutureAccessList->AddOrReplace("vita-firmware", file);
                    firmwarePath_->Text = "Firmware selected: " + file->Name;
                    feedback_->Text = "Firmware file permission saved. Installation is not available until the emulator core is integrated.";
                }
            } catch (Platform::Exception^ error) {
                feedback_->Text = "Firmware selection failed: " + error->Message;
            }
        }, task_continuation_context::use_current());
    }

public:
    App() {}
protected:
    virtual void OnLaunched(LaunchActivatedEventArgs^ args) override {
        if (args->PrelaunchActivated) return;
        auto panel = ref new StackPanel();
        panel->Margin = Thickness(64);
        panel->MaxWidth = 1000;
        panel->Children->Append(MakeText("Vita3K UWP", 44));
        auto phase = MakeText("PRE-ALPHA  /  STORAGE SETUP", 20);
        phase->Foreground = ref new SolidColorBrush(Colors::DeepSkyBlue);
        panel->Children->Append(phase);
        panel->Children->Append(MakeText(
            "The emulator core and GPU renderer are not integrated yet. Games cannot boot in this build. "
            "These controls prepare persistent file access for future integration.", 21));

        panel->Children->Append(MakeText("Game library", 30));
        panel->Children->Append(MakeText(
            "Choose a folder on an available internal or external drive. Availability depends on Xbox UWP picker permissions.", 18));
        gamesPath_ = MakeText("Game folder: not selected", 18);
        panel->Children->Append(gamesPath_);
        auto gamesButton = MakeButton("Choose game folder");
        gamesButton->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
            ChooseGamesFolder();
        });
        panel->Children->Append(gamesButton);

        panel->Children->Append(MakeText("Firmware", 30));
        panel->Children->Append(MakeText(
            "Obtain the official PS Vita system firmware from Sony using a separate device, "
            "then select its .PUP file here. This step saves access only; it does not install firmware.", 18));
        firmwarePath_ = MakeText("Firmware file: not selected", 18);
        panel->Children->Append(firmwarePath_);
        auto firmwareButton = MakeButton("Choose firmware .PUP");
        firmwareButton->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
            ChooseFirmware();
        });
        panel->Children->Append(firmwareButton);

        feedback_ = MakeText("Ready for storage setup.", 18);
        panel->Children->Append(feedback_);
        panel->Children->Append(MakeText(
            "Next engineering gates: core dependency build, UWP executable-memory diagnostics, "
            "D3D12 rendering, then a homebrew boot test. No commercial-game compatibility is claimed.", 17));
        auto scroll = ref new ScrollViewer();
        scroll->Content = panel;
        scroll->Background = ref new SolidColorBrush(ColorHelper::FromArgb(255, 12, 18, 30));
        Window::Current->Content = scroll;
        Window::Current->Activate();
        RestoreSelections();
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
