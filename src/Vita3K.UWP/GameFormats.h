// SPDX-License-Identifier: GPL-2.0-or-later
// Identifies supported Vita3K *input types* and validates simple file headers.
// Format recognition is not an installer, a decryptor or an emulator.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace vita3k_uwp {
enum class GameFormat {
    Unknown, Vpk, Zip, Pkg, Vci, ExecutableBin, GameFolder
};
enum class FileStatus {
    Unsupported, NeedsInspection, ValidHeader, InvalidHeader,
    NeedsLicense, EncryptedExecutable
};
struct FormatCheck {
    GameFormat format;
    FileStatus status;
    const char* diagnostic;
};
GameFormat ClassifyGameFile(const std::wstring& filename);
const wchar_t* GameFormatLabel(GameFormat format);
FormatCheck CheckGameHeader(GameFormat format, const std::uint8_t* bytes, std::size_t length);
}
