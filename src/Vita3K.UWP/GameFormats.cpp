// SPDX-License-Identifier: GPL-2.0-or-later
#include "GameFormats.h"
#include <algorithm>
#include <cwctype>
namespace vita3k_uwp {
GameFormat ClassifyGameFile(const std::wstring& name) {
    std::wstring filename = name;
    std::transform(filename.begin(), filename.end(), filename.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    const auto last = filename.rfind(L'.');
    if (last == std::wstring::npos) return GameFormat::Unknown;
    const auto ext = filename.substr(last);
    if (ext == L".vpk") return GameFormat::Vpk;
    if (ext == L".zip") return GameFormat::Zip;
    if (ext == L".pkg") return GameFormat::Pkg;
    if (ext == L".vci") return GameFormat::Vci;
    // Generic .bin is not necessarily a Vita game. eboot.bin is a candidate.
    if (filename == L"eboot.bin") return GameFormat::ExecutableBin;
    return GameFormat::Unknown;
}
const wchar_t* GameFormatLabel(GameFormat format) {
    switch (format) {
    case GameFormat::Vpk: return L"VPK ARCHIVE";
    case GameFormat::Zip: return L"ZIP ARCHIVE";
    case GameFormat::Pkg: return L"PSN PKG";
    case GameFormat::Vci: return L"VCI IMAGE";
    case GameFormat::ExecutableBin: return L"VITA EXECUTABLE";
    case GameFormat::GameFolder: return L"GAME FOLDER";
    default: return L"UNSUPPORTED";
    }
}
FormatCheck CheckGameHeader(GameFormat format, const std::uint8_t* bytes, std::size_t count) {
    if (format == GameFormat::Unknown)
        return {format, FileStatus::Unsupported, "Not a recognized Vita3K input format"};
    if (format == GameFormat::GameFolder)
        return {format, FileStatus::NeedsInspection, "A game folder needs sce_sys and eboot.bin inspection"};
    if (bytes == nullptr || count < 4)
        return {format, FileStatus::InvalidHeader, "The file is too short to contain a valid header"};
    const bool zip = bytes[0] == 0x50 && bytes[1] == 0x4b &&
        ((bytes[2] == 3 && bytes[3] == 4) ||
         (bytes[2] == 5 && bytes[3] == 6) ||
         (bytes[2] == 7 && bytes[3] == 8));
    const bool pkg = bytes[0] == 0x7f && bytes[1] == 'P' &&
                     bytes[2] == 'K' && bytes[3] == 'G';
    const bool elf = bytes[0] == 0x7f && bytes[1] == 'E' &&
                     bytes[2] == 'L' && bytes[3] == 'F';
    // Sony SELF executables normally begin with SCE\0; some variants differ.
    const bool self = bytes[0] == 'S' && bytes[1] == 'C' &&
                      bytes[2] == 'E' && bytes[3] == 0;
    switch (format) {
    case GameFormat::Vpk:
    case GameFormat::Zip:
        return zip ? FormatCheck{format, FileStatus::ValidHeader,
                       "ZIP archive signature confirmed; installation requires Vita3K packages"} :
               FormatCheck{format, FileStatus::InvalidHeader,
                       "This .vpk/.zip does not contain a ZIP file signature"};
    case GameFormat::Pkg:
        return pkg ? FormatCheck{format, FileStatus::NeedsLicense,
                       "Sony PKG recognized; licensed content requires its matching legal license"} :
               FormatCheck{format, FileStatus::InvalidHeader,
                       "This .pkg does not contain a Sony PKG signature"};
    case GameFormat::Vci:
        // Do not make up a VCI magic value; the upstream VCI parser handles it.
        return {format, FileStatus::NeedsInspection,
                "VCI extension recognized; the full Vita3K cartridge parser must verify it"};
    case GameFormat::ExecutableBin:
        if (elf && count >= 20 && bytes[4] == 1 && bytes[5] == 1 &&
            bytes[18] == 40 && bytes[19] == 0) {
            return {format, FileStatus::ValidHeader,
                    "32-bit little-endian ARM ELF detected; Vita imports and kernel still needed"};
        }
        if (self) return {format, FileStatus::EncryptedExecutable,
            "Sony SELF identified; Vita module loading / decryption support is required"};
        return {format, FileStatus::InvalidHeader,
                "Not an ARM ELF or recognized Sony SELF executable"};
    default: return {format, FileStatus::Unsupported, "Unrecognized source format"};
    }
}
}
