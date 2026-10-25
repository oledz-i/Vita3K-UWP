#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Firmware installation hardening for the Xbox port.

The first console test stopped right after "install progress: 70%", which is
where install_pup unpacks the decrypted partition images. This makes that
step bounded and logged, replaces the exFAT walker (pd0, the preinstalled
package) with one that cannot recurse or allocate without bound, checks every
file it opens, installs the system firmware before the optional parts, and on
Windows turns a hardware fault during installation into a logged failure
instead of a silent crash.
"""
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(__file__).resolve().parent.parent


def read(relative):
    return (source / relative).read_text(encoding='utf-8')


def write(relative, text):
    (source / relative).write_text(text, encoding='utf-8')


def replace(relative, old, new, count=1):
    text = read(relative)
    assert text.count(old) == count, f'{relative}: expected {count} matches for {old!r}'
    write(relative, text.replace(old, new))


# ── exFAT (pd0) ─────────────────────────────────────────────────────────────
exfat = 'vita3k/packages/src/exfat.cpp'
text = read(exfat)
marker = 'static uint64_t get_cluster_offset('
assert text.count(marker) == 1, 'exfat.cpp layout changed'
head = text[:text.index(marker)]
inc = (repo / 'scripts/core-patches/exfat-traverse.inc').read_text(encoding='utf-8')
head = head.replace('#include <packages/exfat.h>',
                    '#include <packages/exfat.h>\n#include <util/fs.h>\n\n#include <algorithm>\n'
                    '#include <cstring>\n#include <fstream>\n#include <set>\n#include <system_error>\n#include <vector>', 1)
# get_exfat_file_name is unused by the new walker; keep it from warning.
head = head.replace('static fs::path get_exfat_file_name(', '[[maybe_unused]] static fs::path get_exfat_file_name(', 1)
write(exfat, head + inc)

# ── FAT16 (os0, vs0, sa0) ───────────────────────────────────────────────────
sce = 'vita3k/packages/src/sce_utils.cpp'
replace(sce, '#include <util/string_utils.h>\n', '#include <util/fs.h>\n#include <util/string_utils.h>\n\n#include <system_error>\n')
replace(sce, '''    FILE *f = FOPEN(filename.native().c_str(), "wb");

    static constexpr std::uint32_t CHUNK_SIZE = 0x10000;''', '''    FILE *f = FOPEN(filename.native().c_str(), "wb");
    if (!f) {
        LOG_ERROR("FAT: could not create {}", fs_utils::path_to_utf8(filename));
        return;
    }

    static constexpr std::uint32_t CHUNK_SIZE = 0x10000;''')
replace(sce, '''        if (img.read_from_cluster(&temp_buf[0], offset, entry.entry.starting_cluster, size_to_take) != size_to_take) {
            break;
        }''', '''        if (img.read_from_cluster(&temp_buf[0], offset, entry.entry.starting_cluster, size_to_take) != size_to_take) {
            LOG_ERROR("FAT: short read in {}", fs_utils::path_to_utf8(filename));
            break;
        }''')
replace(sce, '''static void traverse_directory(Fat16::Image &img, Fat16::Entry mee, const fs::path &dir_path) {
    fs::create_directories(dir_path);
''', '''static void traverse_directory(Fat16::Image &img, Fat16::Entry mee, const fs::path &dir_path, int depth = 0) {
    std::error_code dir_ec;
    fs::create_directories(dir_path, dir_ec);
    if (dir_ec) {
        LOG_ERROR("FAT: could not create {}: {}", fs_utils::path_to_utf8(dir_path), dir_ec.message());
        return;
    }
    if (depth > 48) {
        LOG_ERROR("FAT: {} is nested too deeply; skipped", fs_utils::path_to_utf8(dir_path));
        return;
    }
''')
replace(sce, '''                traverse_directory(img, baby, dir_path / std::wstring(dir_name.begin(), dir_name.end()) / "");''',
        '''                traverse_directory(img, baby, dir_path / std::wstring(dir_name.begin(), dir_name.end()) / "", depth + 1);''')
replace(sce, '''    FILE *f = FOPEN((partition_path / partition).native().c_str(), "rb");
    Fat16::Image img(''', '''    FILE *f = FOPEN((partition_path / partition).native().c_str(), "rb");
    if (!f) {
        LOG_ERROR("FAT: failed to open {}", partition);
        return;
    }
    LOG_INFO("FAT: extracting {} to {}", partition, fs_utils::path_to_utf8(vita_fs_path / partition.substr(0, 3)));
    Fat16::Image img(''')
replace(sce, '''    traverse_directory(img, first, vita_fs_path / partition.substr(0, 3));

    fclose(f);''', '''    traverse_directory(img, first, vita_fs_path / partition.substr(0, 3));

    fclose(f);
    LOG_INFO("FAT: {} done", partition);''')

# ── install_pup: check each step and say which partition it is on ──────────
pup = 'vita3k/packages/src/pup.cpp'
replace(pup, '#include <fstream>\n', '#include <fstream>\n#include <system_error>\n')
replace(pup, '''    EVP_CIPHER *cipher = EVP_CIPHER_fetch(nullptr, "AES-128-CTR", nullptr);
    int dec_len = 0;''', '''    EVP_CIPHER *cipher = EVP_CIPHER_fetch(nullptr, "AES-128-CTR", nullptr);
    if (!cipher_ctx || !cipher) {
        LOG_ERROR("PUP: AES-128-CTR is not available from OpenSSL; cannot decrypt {}", fs_utils::path_to_utf8(filename));
        EVP_CIPHER_CTX_free(cipher_ctx);
        EVP_CIPHER_free(cipher);
        return;
    }
    int dec_len = 0;''')
replace(pup, '''    update_progress(70);
    if (fs::file_size(pup_dec / "os0.img") > 0)
        extract_fat(pup_dec, "os0.img", vita_fs_path);
    if (fs::file_size(pup_dec / "pd0.img") > 0)
        exfat::extract_exfat(pup_dec, "pd0.img", vita_fs_path);
    if (fs::file_size(pup_dec / "sa0.img") > 0)
        extract_fat(pup_dec, "sa0.img", vita_fs_path);
    if (fs::file_size(pup_dec / "vs0.img") > 0)
        extract_fat(pup_dec, "vs0.img", vita_fs_path);
    update_progress(100);''', '''    update_progress(70);
    const auto image_size = [&](const char *name) -> uint64_t {
        std::error_code ec;
        const uint64_t size = fs::file_size(pup_dec / name, ec);
        return ec ? 0 : size;
    };
    LOG_INFO("PUP: decrypted images: os0 {} bytes, pd0 {} bytes, sa0 {} bytes, vs0 {} bytes",
        image_size("os0.img"), image_size("pd0.img"), image_size("sa0.img"), image_size("vs0.img"));
    if (image_size("os0.img") > 0)
        extract_fat(pup_dec, "os0.img", vita_fs_path);
    update_progress(75);
    if (image_size("pd0.img") > 0)
        exfat::extract_exfat(pup_dec, "pd0.img", vita_fs_path);
    update_progress(85);
    if (image_size("sa0.img") > 0)
        extract_fat(pup_dec, "sa0.img", vita_fs_path);
    update_progress(90);
    if (image_size("vs0.img") > 0)
        extract_fat(pup_dec, "vs0.img", vita_fs_path);
    update_progress(100);''')
replace(pup, '''    fs::remove_all(pup_dec_root);

    return fw_version;''', '''    std::error_code cleanup_ec;
    fs::remove_all(pup_dec_root, cleanup_ec);

    return fw_version;''')

# ── Installer: system firmware first, fault guard, no half-installed parts ──
inst = 'vita3k/libretro/libretro_installer.cpp'
replace(inst, '#include <vector>\n', '''#include <vector>

#if defined(_WIN32)
#include <excpt.h>
#include <malloc.h>
#include <stdexcept>
// A fault inside the firmware unpacker (bad data, an out-of-memory write,
// a stack overflow) would otherwise end the app with nothing in the log.
// The __try lives in a function with no C++ objects, as MSVC requires.
static unsigned long g_install_fault = 0;
static int install_fault_filter(unsigned long code) {
    g_install_fault = code;
    return EXCEPTION_EXECUTE_HANDLER;
}
static bool run_fault_guarded(void (*fn)(void *), void *ctx) {
    __try {
        fn(ctx);
        return true;
    } __except (install_fault_filter(GetExceptionCode())) {
        if (g_install_fault == 0xC00000FDu)
            _resetstkoflw();
        return false;
    }
}
#endif
''', 1)
replace(inst, '''    std::string fw_version;
    try {
        fw_version = install_pup(pref_path, pup_path, [label](uint32_t progress) {
            lr_log(RETRO_LOG_INFO, "%s install progress: %u%%\\n", label, progress);
        });
    } catch (const std::exception &e) {''', '''    std::string fw_version;
    try {
#if defined(_WIN32)
        struct Job {
            const fs::path *pref;
            const fs::path *pup;
            const char *label;
            std::string *version;
            std::string error;
        } job{ &pref_path, &pup_path, label, &fw_version, {} };
        const bool completed = run_fault_guarded([](void *raw) {
            auto *j = static_cast<Job *>(raw);
            try {
                const char *job_label = j->label;
                *j->version = install_pup(*j->pref, *j->pup, [job_label](uint32_t progress) {
                    lr_log(RETRO_LOG_INFO, "%s install progress: %u%%\\n", job_label, progress);
                });
            } catch (const std::exception &e) {
                j->error = e.what();
            } catch (...) {
                j->error = "unknown exception";
            }
        }, &job);
        if (!completed) {
            lr_log(RETRO_LOG_ERROR, "%s installation crashed (Windows exception 0x%08lX); nothing further was installed from it\\n", label, g_install_fault);
            lr_msg(fmt::format("{} installation crashed. See vita3k-runtime.log.", label).c_str(), 600);
            return false;
        }
        if (!job.error.empty())
            throw std::runtime_error(job.error);
#else
        fw_version = install_pup(pref_path, pup_path, [label](uint32_t progress) {
            lr_log(RETRO_LOG_INFO, "%s install progress: %u%%\\n", label, progress);
        });
#endif
    } catch (const std::exception &e) {''')
replace(inst, '''    const std::pair<PupKind, const char *> parts[] = {
        { PupKind::Preinst, "Preinstalled package" },
        { PupKind::Firmware, "System firmware" },
        { PupKind::Font, "Font package" },
    };''', '''    // The system firmware first: games need it, and a problem with an
    // optional part must not stop it from being installed.
    const std::pair<PupKind, const char *> parts[] = {
        { PupKind::Firmware, "System firmware" },
        { PupKind::Preinst, "Preinstalled package" },
        { PupKind::Font, "Font package" },
    };''')
replace(inst, '''        (install_pup_file(pref_path, pup, label) ? installed : failed)++;''', '''        if (install_pup_file(pref_path, pup, label)) {
            installed++;
        } else {
            failed++;
            // A half-written partition would read as installed next time.
            const char *partition = kind == PupKind::Preinst ? "pd0" : kind == PupKind::Font ? "sa0" : "vs0";
            std::error_code ec;
            fs::remove_all(pref_path / partition, ec);
            lr_log(RETRO_LOG_ERROR, "Removed the partial %s after the failed install so it is retried\\n", partition);
        }''')
print('Hardened firmware installation: exFAT walker, FAT checks, per-partition logging, fault guard.')
