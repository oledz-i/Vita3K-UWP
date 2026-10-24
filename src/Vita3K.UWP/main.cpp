// SPDX-License-Identifier: GPL-2.0-or-later
// Xbox UWP host: persistent user-selected locations and firmware selection.
// Selecting firmware does NOT install it or imply the Vita3K core is present.
#include <collection.h>
#include <ppltasks.h>
#include <mem/allocator.h>
#include "D3D12Host.h"
#include <string>
#include <windows.h>
#include <string>

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
static D3D12Host nativeRenderer;
public ref class App sealed : Application {
private:
    TextBlock^ gamesPath_;
    TextBlock^ firmwarePath_;
    TextBlock^ feedback_;
    TextBlock^ libraryStatus_;
    TextBlock^ memoryStatus_;
    TextBlock^ coreStatus_;
    TextBlock^ rendererStatus_;
    StackPanel^ libraryEntries_;

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

    void CheckUpstreamMemoryCore() {
        // This executes a real, unmodified Vita3K memory allocator imported from
        // the pinned upstream revision. It does not initialize the full emulator.
        BitmapAllocator bitmap(64);
        std::uint32_t count = 4;
        const int first = bitmap.allocate_from(0, count);
        const int freeAfterFirst = bitmap.free_slot_count(0, 64);
        const int second = bitmap.allocate_at(8, 2);
        bitmap.free(0, 4);
        const int freeAfterRelease = bitmap.free_slot_count(0, 64);
        const bool passed = first == 0 && count == 4 &&
            freeAfterFirst == 60 && second == 0 && freeAfterRelease == 62;
        coreStatus_->Text = passed
            ? "Vita3K upstream memory allocator: PASS (actual upstream code executed)"
            : "Vita3K upstream memory allocator: FAIL (core integration blocked)";
    }

    void BootRenderer(SwapChainPanel^ surface) {
        if (rendererStatus_ == nullptr) return;
        std::wstring result;
        const bool created = nativeRenderer.Initialize(
            reinterpret_cast<IUnknown*>(surface), 960, 544, result);
        if (created) {
            std::wstring frameResult;
            const bool presented = nativeRenderer.DrawTestFrame(frameResult);
            rendererStatus_->Text = ref new Platform::String(
                (std::wstring(L"D3D12: ") + frameResult).c_str());
            if (!presented) {
                feedback_->Text = "Native renderer failed to present; check the result above.";
            }
        } else {
            rendererStatus_->Text = ref new Platform::String(
                (std::wstring(L"D3D12 initialization error: ") + result).c_str());
        }
    }

    void ProbeMemory() {
        // A diagnostic of UWP virtual memory APIs, not a Vita CPU/JIT implementation.
        const SIZE_T length = 4096;
        void* page = VirtualAlloc(nullptr, length, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!page) {
            memoryStatus_->Text = ref new Platform::String(
                (L"VirtualAlloc RW failed, Win32 error " + std::to_wstring(GetLastError())).c_str());
            return;
        }
        volatile unsigned char* bytes = static_cast<volatile unsigned char*>(page);
        bytes[0] = 0x5A;
        bool rwWorks = (bytes[0] == 0x5A);
        DWORD oldProtection = 0;
        BOOL rxWorks = VirtualProtect(page, length, PAGE_EXECUTE_READ, &oldProtection);
        DWORD protectError = rxWorks ? 0 : GetLastError();
        if (rxWorks) {
            DWORD ignored = 0;
            VirtualProtect(page, length, PAGE_READWRITE, &ignored);
        }
        VirtualFree(page, 0, MEM_RELEASE);
        std::wstring report = L"RW allocation/read/write: ";
        report += rwWorks ? L"PASS" : L"FAIL";
        report += L" | RW-to-RX protection: ";
        report += rxWorks ? L"PASS" : L"FAIL";
        if (!rxWorks) report += L" (error " + std::to_wstring(protectError) + L")";
        report += L". This does not establish JIT execution or Vita3K compatibility.";
        memoryStatus_->Text = ref new Platform::String(report.c_str());
    }

    void ScanLibrary() {
        auto access = StorageApplicationPermissions::FutureAccessList;
        libraryEntries_->Children->Clear();
        if (!access->ContainsItem("vita-games")) {
            libraryStatus_->Text = "Select a game folder before scanning.";
            return;
        }
        libraryStatus_->Text = "Reading accessible folder entries...";
        create_task(access->GetFolderAsync("vita-games")).then([this](StorageFolder^ folder) {
            return create_task(folder->GetItemsAsync(0, 100));
        }).then([this](task<Windows::Foundation::Collections::IVectorView<IStorageItem^>^> operation) {
            try {
                auto items = operation.get();
                unsigned int count = 0;
                for (auto item : items) {
                    // This is a directory preview, NOT Vita game detection.
                    auto name = item->Name;
                    if (item->IsOfType(StorageItemTypes::Folder)) {
                        libraryEntries_->Children->Append(MakeText("[Folder] " + name, 17));
                    } else {
                        libraryEntries_->Children->Append(MakeText("[File] " + name, 17));
                    }
                    ++count;
                }
                libraryStatus_->Text = "Directory preview complete (up to 100 entries). Game identification and launching require the emulator core.";
                if (count == 0) libraryStatus_->Text = "Folder is empty or contains no accessible items.";
            } catch (Platform::Exception^ error) {
                libraryStatus_->Text = "Could not read folder: " + error->Message;
            }
        }, task_continuation_context::use_current());
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
                    feedback_->Text = "Folder permission saved. You can preview accessible files below.";
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
        auto phase = MakeText("PRE-ALPHA  /  NATIVE CORE + D3D12 INTEGRATION", 20);
        phase->Foreground = ref new SolidColorBrush(Colors::DeepSkyBlue);
        panel->Children->Append(phase);
        panel->Children->Append(MakeText(
            "Genuine Vita3K memory code and native D3D12 presentation are now linked. "
            "The complete Vita3K CPU/kernel, Vita GXM renderer, firmware installation and game launching "
            "are not integrated. Games cannot boot in this build.", 21));

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
        auto scanButton = MakeButton("Preview game directory");
        scanButton->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
            ScanLibrary();
        });
        panel->Children->Append(scanButton);
        libraryStatus_ = MakeText("No directory scan performed.", 18);
        panel->Children->Append(libraryStatus_);
        libraryEntries_ = ref new StackPanel();
        panel->Children->Append(libraryEntries_);

        panel->Children->Append(MakeText("Core feasibility diagnostics", 30));
        panel->Children->Append(MakeText(
            "Test whether the UWP process can allocate writable memory and change it to executable/readable. "
            "No generated code is executed by this test.", 18));
        auto memoryButton = MakeButton("Run memory compatibility probe");
        memoryButton->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
            ProbeMemory();
        });
        panel->Children->Append(memoryButton);
        memoryStatus_ = MakeText("Memory probe not run.", 18);
        panel->Children->Append(memoryStatus_);

        panel->Children->Append(MakeText("Upstream core integration", 30));
        panel->Children->Append(MakeText(
            "Vita3K's original bitmap allocator is compiled into this Xbox UWP package. "
            "This test runs allocation and release using the upstream implementation. "
            "Dynarmic CPU, kernel, firmware services and full emulator initialization remain pending.", 18));
        auto coreButton = MakeButton("Run real Vita3K memory component");
        coreButton->Click += ref new RoutedEventHandler([this](Platform::Object^, RoutedEventArgs^) {
            CheckUpstreamMemoryCore();
        });
        panel->Children->Append(coreButton);
        coreStatus_ = MakeText("Core component has not been exercised.", 18);
        panel->Children->Append(coreStatus_);

        panel->Children->Append(MakeText("D3D12 graphics bootstrap", 30));
        panel->Children->Append(MakeText(
            "The preview below is an actual Direct3D 12 GPU clear, not a rendered PS Vita frame. "
            "Its job is to prove a working native presentation path before GXM integration.", 18));
        auto surface = ref new SwapChainPanel();
        surface->Width = 960;
        surface->Height = 544;
        surface->Margin = Thickness(0, 12, 0, 12);
        panel->Children->Append(surface);
        rendererStatus_ = MakeText("Waiting for D3D12 initialization...", 18);
        panel->Children->Append(rendererStatus_);
        surface->Loaded += ref new RoutedEventHandler([this, surface](Platform::Object^, RoutedEventArgs^) {
            BootRenderer(surface);
        });
        auto retry = MakeButton("Retry D3D12 presentation");
        retry->Click += ref new RoutedEventHandler([this, surface](Platform::Object^, RoutedEventArgs^) {
            BootRenderer(surface);
        });
        panel->Children->Append(retry);

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
            "Next engineering gates: Dynarmic and Vita3K kernel linking, Vita GXM graphics translation "
            "to D3D12, firmware services, then a homebrew boot test. No game compatibility is claimed.", 17));
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
