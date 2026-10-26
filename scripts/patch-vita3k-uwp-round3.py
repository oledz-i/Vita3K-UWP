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

# ── Firmware for the frontend: what a PUP holds, and installing one now ─────
# The Xbox frontend installs firmware the moment the user picks it, and tells
# the parts apart by what is inside the package (not by its file name, which
# Sony's downloads all share). These are plain C exports it finds with
# GetProcAddress; nothing here runs unless the frontend calls it.
replace(pup, '''std::string install_pup(const fs::path &vita_fs_path, const fs::path &pup_path, const std::function<void(uint32_t)> &progress_callback) {''',
'''bool read_pup_contents(const fs::path &pup, uint32_t &firmware_version, uint32_t &partitions) {
    firmware_version = 0;
    partitions = 0;
    std::ifstream in(pup, std::ios::binary);
    if (!in)
        return false;
    char header[0x80];
    in.read(header, sizeof(header));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header)) || strncmp(header, "SCEUF", 5) != 0)
        return false;
    uint32_t count = 0;
    memcpy(&count, &header[0x18], 4);
    memcpy(&firmware_version, &header[0x10], 4);
    if (count > 4096)
        return false;
    std::vector<unsigned char> hdr(HEADER_LENGTH);
    for (uint32_t x = 0; x < count; x++) {
        char rec[0x20];
        in.clear();
        in.seekg(0x80 + static_cast<std::streamoff>(x) * 0x20, std::ios::beg);
        in.read(rec, sizeof(rec));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(rec)))
            return false;
        uint64_t filetype = 0, offset = 0;
        memcpy(&filetype, &rec[0], 8);
        memcpy(&offset, &rec[8], 8);
        if (PUP_TYPES.contains(filetype))
            continue;
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        in.read(reinterpret_cast<char *>(hdr.data()), HEADER_LENGTH);
        if (in.gcount() != HEADER_LENGTH)
            continue;
        uint32_t magic = 0, version = 0, flags = 0;
        uint64_t metaoffs = 0;
        memcpy(&magic, &hdr[0], 4);
        memcpy(&version, &hdr[4], 4);
        memcpy(&flags, &hdr[8], 4);
        memcpy(&metaoffs, &hdr[16], 8);
        // The same test make_filename uses to name the package
        if (magic != SCE_MAGIC || version != 3 || flags != 0x30040 || metaoffs + 4 >= HEADER_LENGTH)
            continue;
        switch (hdr[metaoffs + 4]) {
        case 0x01: partitions |= 1; break; // os0
        case 0x0A: partitions |= 2; break; // vs0
        case 0x18: partitions |= 4; break; // pd0
        case 0x17: partitions |= 8; break; // sa0
        default: break;
        }
    }
    return true;
}

std::string install_pup(const fs::path &vita_fs_path, const fs::path &pup_path, const std::function<void(uint32_t)> &progress_callback) {''')
replace('vita3k/packages/include/packages/functions.h',
        'std::string install_pup(const fs::path &vita_fs_path, const fs::path &pup_path, const std::function<void(uint32_t)> &progress_callback = nullptr);',
        '''std::string install_pup(const fs::path &vita_fs_path, const fs::path &pup_path, const std::function<void(uint32_t)> &progress_callback = nullptr);
// Which partitions a PUP carries (bit 0 os0, 1 vs0, 2 pd0, 3 sa0) and its firmware version, read from its headers only.
bool read_pup_contents(const fs::path &pup, uint32_t &firmware_version, uint32_t &partitions);''')

replace(inst, '#endif\n\nenum class PupKind', '''#endif

// Progress of the install in flight, for a frontend that shows it; and the
// version the last successful install reported.
static std::function<void(uint32_t)> g_install_progress;
static std::string g_last_fw_version;

enum class PupKind''') if read(inst).count('#endif\n\nenum class PupKind') == 1 else None
text = read(inst)
if 'static std::function<void(uint32_t)> g_install_progress;' not in text:
    # Place it after the fault-guard block added above.
    marker = '#endif\n'
    first_guard_end = text.index(marker, text.index('static bool run_fault_guarded')) + len(marker)
    text = text[:first_guard_end] + '''
// Progress of the install in flight, for a frontend that shows it; and the
// version the last successful install reported.
static std::function<void(uint32_t)> g_install_progress;
static std::string g_last_fw_version;
''' + text[first_guard_end:]
    write(inst, text)
replace(inst, '''                    lr_log(RETRO_LOG_INFO, "%s install progress: %u%%\\n", job_label, progress);
                });''', '''                    lr_log(RETRO_LOG_INFO, "%s install progress: %u%%\\n", job_label, progress);
                    if (g_install_progress)
                        g_install_progress(progress);
                });''')
replace(inst, '''        fw_version = install_pup(pref_path, pup_path, [label](uint32_t progress) {
            lr_log(RETRO_LOG_INFO, "%s install progress: %u%%\\n", label, progress);
        });''', '''        fw_version = install_pup(pref_path, pup_path, [label](uint32_t progress) {
            lr_log(RETRO_LOG_INFO, "%s install progress: %u%%\\n", label, progress);
            if (g_install_progress)
                g_install_progress(progress);
        });''')
replace(inst, '''        lr_log(RETRO_LOG_INFO, "%s version %s installed successfully!\\n", label, fw_version.c_str());
    }

    return true;
}''', '''        lr_log(RETRO_LOG_INFO, "%s version %s installed successfully!\\n", label, fw_version.c_str());
    }
    g_last_fw_version = fw_version;

    return true;
}

#if defined(VITA3K_UWP)
extern "C" __declspec(dllexport) int vita3k_uwp_pup_info(const char *pup_utf8, uint32_t *firmware_version, uint32_t *partitions) {
    uint32_t version = 0, mask = 0;
    bool ok = false;
    try {
        ok = pup_utf8 && read_pup_contents(fs_utils::utf8_to_path(pup_utf8), version, mask);
    } catch (...) {
        ok = false;
    }
    if (firmware_version)
        *firmware_version = version;
    if (partitions)
        *partitions = mask;
    return ok ? 1 : 0;
}

extern "C" __declspec(dllexport) int vita3k_uwp_install_pup(const char *pref_utf8, const char *pup_utf8,
    void (*progress)(uint32_t, void *), void *user, char *version, uint32_t version_size) {
    if (!pref_utf8 || !pup_utf8)
        return 0;
    const fs::path pref_path = fs_utils::utf8_to_path(pref_utf8);
    const fs::path pup_path = fs_utils::utf8_to_path(pup_utf8);
    std::error_code ec;
    fs::create_directories(pref_path, ec);
    uint32_t fw = 0, parts = 0;
    try {
        read_pup_contents(pup_path, fw, parts);
    } catch (...) {
    }
    g_last_fw_version.clear();
    if (progress)
        g_install_progress = [progress, user](uint32_t value) { progress(value, user); };
    const bool ok = install_pup_file(pref_path, pup_path, "Firmware package");
    g_install_progress = nullptr;
    if (!ok) {
        // A half-written partition would read as installed.
        const char *names[] = { "os0", "vs0", "pd0", "sa0" };
        for (int bit = 0; bit < 4; ++bit)
            if (parts & (1u << bit))
                fs::remove_all(pref_path / names[bit], ec);
    }
    if (version && version_size)
        snprintf(version, version_size, "%s", g_last_fw_version.c_str());
    check_firmware_status(pref_path);
    return ok ? 1 : 0;
}
#endif''')

# ── NoNpDrm decryption: guarded, logged, with progress ─────────────────────
# The second console run stopped inside psvpfstools' execute() with nothing
# in the log: it reports only on std::cout, and nothing caught a failure.
replace(inst, 'static bool install_pup_file(', '''// psvpfstools reports on std::cout, which an app without a console never
// shows: each line goes to the libretro log instead.
class LineLogBuf : public std::streambuf {
public:
    ~LineLogBuf() override { flush_line(); }

protected:
    int overflow(int c) override {
        if (c == traits_type::eof())
            return 0;
        if (c == '\\n')
            flush_line();
        else if (line_.size() < 2000)
            line_.push_back(static_cast<char>(c));
        return c;
    }
    int sync() override {
        flush_line();
        return 0;
    }

private:
    void flush_line() {
        if (!line_.empty())
            lr_log(RETRO_LOG_INFO, "PFS: %s\\n", line_.c_str());
        line_.clear();
    }
    std::string line_;
};

// The algorithms psvpfstools fetches from OpenSSL; a missing one would make it
// use a null context.
static bool pfs_crypto_available() {
    bool ok = true;
    for (const char *name : { "AES-128-CBC", "AES-128-CTR", "AES-128-ECB" }) {
        EVP_CIPHER *cipher = EVP_CIPHER_fetch(nullptr, name, nullptr);
        if (!cipher) {
            lr_log(RETRO_LOG_ERROR, "OpenSSL has no %s\\n", name);
            ok = false;
        }
        EVP_CIPHER_free(cipher);
    }
    for (const char *name : { "SHA1", "SHA256" }) {
        EVP_MD *md = EVP_MD_fetch(nullptr, name, nullptr);
        if (!md) {
            lr_log(RETRO_LOG_ERROR, "OpenSSL has no %s\\n", name);
            ok = false;
        }
        EVP_MD_free(md);
    }
    for (const char *name : { "CMAC", "HMAC" }) {
        EVP_MAC *mac = EVP_MAC_fetch(nullptr, name, nullptr);
        if (!mac) {
            lr_log(RETRO_LOG_ERROR, "OpenSSL has no %s\\n", name);
            ok = false;
        }
        EVP_MAC_free(mac);
    }
    return ok;
}

static int run_pfs_decryption(std::string &zrif, std::string &src, std::string &dst) {
    if (!pfs_crypto_available()) {
        lr_log(RETRO_LOG_ERROR, "PFS decryption needs the OpenSSL algorithms above; not started\\n");
        return -1;
    }
    LineLogBuf log_buf;
    std::streambuf *previous = std::cout.rdbuf(&log_buf);
    int last_decile = -1;
    const auto started_at = std::chrono::steady_clock::now();
    PfsProgressCallback progress = [&last_decile, started_at](std::uint64_t processed, std::uint64_t total, const std::string &) {
        if (!total)
            return;
        // Every 2%: the frontend's loading screen shows it. The seconds and
        // the speed show in the log whether the time goes to the drive or to
        // the decryption itself.
        const int step = static_cast<int>(std::min<std::uint64_t>(processed * 50 / total, 50));
        if (step != last_decile) {
            last_decile = step;
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
            lr_log(RETRO_LOG_INFO, "PFS decryption: %d%% (%.1f s, %.1f MB/s)\\n", step * 2, secs, secs > 0.01 ? processed / 1048576.0 / secs : 0.0);
        }
    };
    int result = -1;
    std::string error;
    F00DEncryptorTypes type = F00DEncryptorTypes::native;
    std::string f00d_arg;
#if defined(_WIN32)
    struct Job {
        std::string *zrif, *src, *dst, *f00d_arg, *error;
        F00DEncryptorTypes type;
        PfsProgressCallback *progress;
        int *result;
    } job{ &zrif, &src, &dst, &f00d_arg, &error, type, &progress, &result };
    const bool completed = run_fault_guarded([](void *raw) {
        auto *j = static_cast<Job *>(raw);
        try {
            *j->result = execute(*j->zrif, *j->src, *j->dst, j->type, *j->f00d_arg, *j->progress);
        } catch (const std::exception &e) {
            *j->error = e.what();
            *j->result = -1;
        } catch (...) {
            *j->error = "unknown exception";
            *j->result = -1;
        }
    }, &job);
    if (!completed) {
        error = fmt::format("crashed (Windows exception 0x{:08X})", g_install_fault);
        result = -1;
    }
#else
    try {
        result = execute(zrif, src, dst, type, f00d_arg, progress);
    } catch (const std::exception &e) {
        error = e.what();
        result = -1;
    }
#endif
    std::cout.flush();
    std::cout.rdbuf(previous);
    lr_log(RETRO_LOG_INFO, "PFS decryption took %.1f s\\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count());
    if (!error.empty())
        lr_log(RETRO_LOG_ERROR, "PFS decryption %s\\n", error.c_str());
    return result;
}

static bool install_pup_file(''', 1)
replace(inst, '    int dec_result = execute(app_zRIF, src_str, dst_str, f00d_enc_type, f00d_arg);',
        '    (void)f00d_enc_type;\n    int dec_result = run_pfs_decryption(app_zRIF, src_str, dst_str);')
replace(inst, '#if defined(_WIN32)\n#include <excpt.h>', '#include <chrono>\n#include <iostream>\n#include <streambuf>\n#include <openssl/evp.h>\n\n#if defined(_WIN32)\n#include <excpt.h>', 1)
print('Added firmware exports for the frontend and guarded, logged NoNpDrm decryption.')

# ── Paths without canonicalisation ──────────────────────────────────────────
# The third console run got through firmware and the game copy, then
# psvpfstools stopped with "weakly_canonical: Access is denied" on the
# installed game's sce_sys. std::filesystem::relative() canonicalises both
# paths, which walks the parent folders of the app's storage; a UWP app may
# not open those. Every path compared here is built from the same root, so
# the lexical form gives the same answer without touching the disk.
pfs_utils = 'external/psvpfstools/psvpfsparser/Utils.cpp'
replace(pfs_utils, '''   psvpfs::path dest_path = dest_root / psvpfs::relative(source_path, source_root);''',
'''   psvpfs::path rel = psvpfs::path(source_path.generic_string()).lexically_normal().lexically_relative(
      psvpfs::path(source_root.generic_string()).lexically_normal());
   if (rel.empty())
      rel = psvpfs::relative(source_path, source_root);
   psvpfs::path dest_path = dest_root / rel;''')
replace('vita3k/util/src/fs_utils.cpp', '            const auto relative_path = fs::relative(src.path(), src_path);',
        '            const auto relative_path = src.path().lexically_normal().lexically_relative(src_path.lexically_normal());')
replace('vita3k/packages/src/sce_utils.cpp', '            const auto output_file_path = output_path / fs::relative(entry.path(), input_path);',
        '            const auto output_file_path = output_path / entry.path().lexically_normal().lexically_relative(input_path.lexically_normal());')
replace('vita3k/packages/src/sce_utils.cpp', '            const auto out_rel = fs::relative(output_file_path, cache_path);',
        '            const auto out_rel = output_file_path.lexically_normal().lexically_relative(cache_path.lexically_normal());')
replace('vita3k/libretro/libretro_lazy_pkg.cpp', '            e.name = fs::relative(it->path(), dir, ec).generic_string();',
        '            e.name = it->path().lexically_normal().lexically_relative(dir.lexically_normal()).generic_string();')
print('Replaced canonicalising path helpers with lexical ones.')

# ── Encrypted game folders: decrypt straight from where they are ───────────
# A NoNpDrm folder was first copied whole into ux0/app, then decrypted into
# a second folder that replaced the copy: twice the game's size on a console
# whose internal partition can be 5 GB, and two full passes over the data.
# The decrypted output replaces the copy entirely, so for a game (category gd)
# decrypt from the source folder itself; updates and DLC keep the old path.
replace(inst, '''static bool decrypt_nonpdrm_app(const fs::path &install_path, const fs::path &pref_path, const GameInstallResult &result,
    std::vector<uint8_t> &work_bin_data, std::string &app_zRIF, bool keep_license = true) {
    const auto work_bin = install_path / "sce_sys" / "package" / "work.bin";''',
'''static bool decrypt_nonpdrm_app(const fs::path &install_path, const fs::path &pref_path, const GameInstallResult &result,
    std::vector<uint8_t> &work_bin_data, std::string &app_zRIF, bool keep_license = true, const fs::path &source = {}) {
    // The encrypted files come from source when given (a game folder read
    // where it lies), else from the copy at install_path.
    const fs::path &encrypted = source.empty() ? install_path : source;
    const auto work_bin = encrypted / "sce_sys" / "package" / "work.bin";''')
replace(inst, '''    std::string src_str = install_path.generic_string();
    std::string dst_str = install_decrypt_folder(pref_path, install_path).generic_string();''',
'''    std::string src_str = encrypted.generic_string();
    std::string dst_str = install_decrypt_folder(pref_path, install_path).generic_string();''')
replace(inst, '''    lr_log(RETRO_LOG_INFO, "PFS decryption succeeded, replacing encrypted files...\\n");
    try {
        fs::remove_all(install_path);
        fs::rename(fs::path(dst_str), install_path);''', '''    lr_log(RETRO_LOG_INFO, "PFS decryption succeeded, replacing encrypted files...\\n");
    try {
        fs::remove_all(install_path);
        fs::create_directories(install_path.parent_path());
        fs::rename(fs::path(dst_str), install_path);''')
replace(inst, '''    // An encrypted one twice over for a while: decrypting writes a second copy
    if (!has_room(pref_path, folder_size(content_dir) * (fs::is_directory(content_dir / "sce_pfs") ? 2 : 1)))
        return result;
    try {
        fs::create_directories(install_path);
        fs::copy(content_dir, install_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        lr_log(RETRO_LOG_INFO, "App folder copy completed.\\n");
    } catch (const fs::filesystem_error &e) {
        lr_log(RETRO_LOG_ERROR, "Failed to copy game folder: %s\\n", e.what());
        lr_msg("Failed to copy the game's directory!", 300);
        return result;
    }

    // A NoNpDrm copy is decrypted in place: a game with its own license, kept; an update
    // or DLC with its own or the game's. An update stays in ux0/patch and is laid over the
    // game when it starts, as one from a PKG or VPK/ZIP (sco8487), never merged into it.
    if (fs::is_directory(install_path / "sce_pfs")) {''', '''    // An encrypted game is decrypted straight from its folder into ux0, so it
    // needs its size once; an encrypted update or DLC is still copied first
    // and so needs it twice for a while.
    const bool encrypted_source = fs::is_directory(content_dir / "sce_pfs");
    const bool decrypt_from_source = encrypted_source && app_info.app_category.find("gd") != std::string::npos
        && fs::exists(content_dir / "sce_sys" / "package" / "work.bin");
    if (!has_room(pref_path, folder_size(content_dir) * (encrypted_source && !decrypt_from_source ? 2 : 1)))
        return result;
    if (decrypt_from_source) {
        lr_log(RETRO_LOG_INFO, "Encrypted game folder: decrypting it straight into ux0, without copying it first\\n");
        std::vector<uint8_t> work_bin_data;
        std::string app_zRIF;
        if (!decrypt_nonpdrm_app(install_path, pref_path, result, work_bin_data, app_zRIF, true, content_dir)) {
            fs::remove_all(install_path);
            return result;
        }
    } else {
    try {
        fs::create_directories(install_path);
        fs::copy(content_dir, install_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        lr_log(RETRO_LOG_INFO, "App folder copy completed.\\n");
    } catch (const fs::filesystem_error &e) {
        lr_log(RETRO_LOG_ERROR, "Failed to copy game folder: %s\\n", e.what());
        lr_msg("Failed to copy the game's directory!", 300);
        return result;
    }
    }

    // A NoNpDrm copy is decrypted in place: a game with its own license, kept; an update
    // or DLC with its own or the game's. An update stays in ux0/patch and is laid over the
    // game when it starts, as one from a PKG or VPK/ZIP (sco8487), never merged into it.
    if (!decrypt_from_source && fs::is_directory(install_path / "sce_pfs")) {''')
print('Encrypted game folders are decrypted from where they are.')

# ── Free space queries a game makes must not throw ──────────────────────────
# Games ask for a device's free space (sceIoDevctl, sceAppMgrGetDevInfo); the
# throwing fs::space() would end the app if the query is refused. Report a
# generous size instead.
for relative in ['vita3k/modules/SceLibKernel/SceLibKernel.cpp', 'vita3k/modules/SceDriverUser/SceAppMgrUser.cpp']:
    replace(relative, '    fs::space_info space = fs::space(path);',
            '''    std::error_code space_ec;
    fs::space_info space = fs::space(path, space_ec);
    if (space_ec)
        space = fs::space_info{ 16ULL << 30, 8ULL << 30, 8ULL << 30 };''')
    text = read(relative)
    if '#include <system_error>' not in text:
        write(relative, '#include <system_error>\n' + text)
print('Free space queries no longer throw.')

# ── FAT images from memory ──────────────────────────────────────────────────
# libfat16 walks a file's cluster chain from its start for every 64 KB chunk,
# with a seek and a two-byte read per step: thousands of small disk reads per
# file. The partition images are at most a few hundred MB, so they are read
# into memory once and the read and seek hooks work on that.
replace(sce, '''    LOG_INFO("FAT: extracting {} to {}", partition, fs_utils::path_to_utf8(vita_fs_path / partition.substr(0, 3)));
    Fat16::Image img(
        f,
        // Read hook
        [](void *userdata, void *buffer, std::uint32_t size) -> std::uint32_t {
            return static_cast<std::uint32_t>(fread(buffer, 1, size, (FILE *)userdata));
        },
        // Seek hook
        [](void *userdata, std::uint32_t offset, int mode) -> std::uint32_t {
            fseek((FILE *)userdata, offset, (mode == Fat16::IMAGE_SEEK_MODE_BEG ? SEEK_SET : (mode == Fat16::IMAGE_SEEK_MODE_CUR ? SEEK_CUR : SEEK_END)));

            return ftell((FILE *)userdata);
        });''', '''    LOG_INFO("FAT: extracting {} to {}", partition, fs_utils::path_to_utf8(vita_fs_path / partition.substr(0, 3)));
    struct MemoryImage {
        std::vector<std::uint8_t> data;
        std::size_t position = 0;
    } memory;
    fseek(f, 0, SEEK_END);
    const long image_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (image_size > 0) {
        memory.data.resize(static_cast<std::size_t>(image_size));
        if (fread(memory.data.data(), 1, memory.data.size(), f) != memory.data.size()) {
            LOG_ERROR("FAT: could not read {}", partition);
            fclose(f);
            return;
        }
    }
    Fat16::Image img(
        &memory,
        // Read hook
        [](void *userdata, void *buffer, std::uint32_t size) -> std::uint32_t {
            auto *m = static_cast<MemoryImage *>(userdata);
            const std::size_t left = m->position < m->data.size() ? m->data.size() - m->position : 0;
            const std::size_t take = std::min<std::size_t>(size, left);
            if (take)
                std::memcpy(buffer, m->data.data() + m->position, take);
            m->position += take;
            return static_cast<std::uint32_t>(take);
        },
        // Seek hook
        [](void *userdata, std::uint32_t offset, int mode) -> std::uint32_t {
            auto *m = static_cast<MemoryImage *>(userdata);
            const std::size_t base = mode == Fat16::IMAGE_SEEK_MODE_BEG ? 0
                : mode == Fat16::IMAGE_SEEK_MODE_CUR ? m->position : m->data.size();
            m->position = std::min<std::size_t>(base + offset, m->data.size());
            return static_cast<std::uint32_t>(m->position);
        });''')
text = read(sce)
for header in ['#include <algorithm>', '#include <cstring>']:
    if header not in text:
        text = header + '\n' + text
write(sce, text)
print('FAT images are unpacked from memory.')

# ── Log level ──────────────────────────────────────────────────────────────
# At trace level the shader recompiler logs every instruction it translates;
# on the console each shader took over a second, mostly spent logging.
replace('vita3k/libretro/libretro_log.cpp', '    logger->set_level(spdlog::level::trace);',
        '    // Info, not trace: per-instruction recompiler logging slowed every shader.\n    logger->set_level(spdlog::level::info);')
replace('vita3k/libretro/libretro_log.cpp', '    spdlog::set_level(spdlog::level::trace);', '    spdlog::set_level(spdlog::level::info);')
print('Core logging at info level.')
# The emulator also applies its config's log level when it starts a game;
# its default is trace too.
replace('vita3k/config/include/config/config.h', 'code(int, "log-level", 0 /*SPDLOG_LEVEL_TRACE*/, log_level)',
        'code(int, "log-level", 2 /*SPDLOG_LEVEL_INFO*/, log_level)')
# A config.yml written by an earlier build may still say trace: clamp it.
replace('vita3k/interface.cpp', 'logging::set_level(static_cast<spdlog::level::level_enum>(emuenv.cfg.log_level));',
        'logging::set_level(static_cast<spdlog::level::level_enum>(std::max(emuenv.cfg.log_level, 2)));')
replace('vita3k/config/src/config.cpp', 'logging::set_level(static_cast<spdlog::level::level_enum>(cfg.log_level));',
        'logging::set_level(static_cast<spdlog::level::level_enum>(std::max(cfg.log_level, 2)));')
print('Log level clamped to info.')
text = read('vita3k/interface.cpp')
if '#include <algorithm>' not in text:
    write('vita3k/interface.cpp', '#include <algorithm>\n' + text)

# ── Where does app::init spend its time? ────────────────────────────────────
# One console run took about 29 s between "VitaFS path" and "app::init
# succeeded" with nothing logged in between. The three steps that sit there
# now log how long they took.
app_init = 'vita3k/app/src/app_init.cpp'
text = read(app_init)
if '#include <chrono>' not in text:
    write(app_init, '#include <chrono>\n' + text)
replace(app_init, '''    set_current_config(state, state.cfg.run_app_path.has_value() ? *state.cfg.run_app_path : "");

    // Here any path''', '''    const auto init_started = std::chrono::steady_clock::now();
    const auto init_elapsed = [&init_started]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - init_started).count(); };
    set_current_config(state, state.cfg.run_app_path.has_value() ? *state.cfg.run_app_path : "");
    LOG_INFO("app::init: config read in {:.1f} s", init_elapsed());

    // Here any path''')
replace(app_init, '''        LOG_ERROR("Failed to initialize file system for the emulator!");
        return false;
    }
''', '''        LOG_ERROR("Failed to initialize file system for the emulator!");
        return false;
    }
    LOG_INFO("app::init: folders created at {:.1f} s", init_elapsed());
''')
replace(app_init, '''    state.motion.init();

    return true;
}

void shutdown_app_runtime''', '''    state.motion.init();
    LOG_INFO("app::init: motion ready at {:.1f} s", init_elapsed());

    return true;
}

void shutdown_app_runtime''')
print('app::init timing added.')

# ── Black screen on Dozen (Vulkan on Direct3D 12) ───────────────────────────
# On Xbox the game's frames came out black while Vita3K's own overlay, drawn
# through a plain Vulkan pipeline, showed. Three things the game's rendering
# uses and the overlay does not, each of which D3D12 has no direct equivalent
# for and Dozen has to emulate, are replaced with what upstream already does
# elsewhere.
#
# 1. Negative viewport heights. GXM's y axis points the other way, so the
#    Vulkan viewport took a negative height (VK_KHR_maintenance1). D3D12 has
#    none; Dozen flips y in a shader variant. The viewport is now positive and
#    the vertex shader negates y, the way the OpenGL backend flips with
#    viewport_flip: y_f = (-yScale) * (-y_ndc) + yOffset, the same pixels,
#    winding and gl_FragCoord as before.
sync = 'vita3k/renderer/src/vulkan/sync_state.cpp'
replace(sync, '''    context.viewport = vk::Viewport{
        .x = x * res_multiplier,
        .y = y * res_multiplier,
        .width = w * res_multiplier,
        .height = h * res_multiplier,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };''', '''    context.viewport = vk::Viewport{
        .x = x * res_multiplier,
        .y = y * res_multiplier,
        .width = w * res_multiplier,
        .height = h * res_multiplier,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
#ifdef VITA3K_UWP
    // a positive height, the y flip done in the vertex shader instead
    context.viewport_y_flip = 1.0f;
    if (h < 0) {
        context.viewport.y = (y + h) * res_multiplier;
        context.viewport.height = -h * res_multiplier;
        context.viewport_y_flip = -1.0f;
    }
#endif''')
replace(sync, '''void sync_viewport_flat(VKContext &context) {
    context.viewport = vk::Viewport{''', '''void sync_viewport_flat(VKContext &context) {
#ifdef VITA3K_UWP
    context.viewport_y_flip = 1.0f;
#endif
    context.viewport = vk::Viewport{''')
replace('vita3k/renderer/include/renderer/vulkan/types.h', '    vk::Viewport viewport;\n',
        '    vk::Viewport viewport;\n#ifdef VITA3K_UWP\n    // -1 when the vertex shader flips y (the viewport is kept positive)\n    float viewport_y_flip = 1.0f;\n#endif\n')
replace('vita3k/renderer/src/vulkan/scene.cpp', '    vert_ublock.viewport_flip = context.record.viewport_flip;\n',
        '''#ifdef VITA3K_UWP
    vert_ublock.viewport_flip = { 1.0f, context.viewport_y_flip, 1.0f, 1.0f };
#else
    vert_ublock.viewport_flip = context.record.viewport_flip;
#endif
''')
replace('vita3k/shader/src/spirv_recompiler.cpp', '''                // Apply the viewport flip if opengl
                if (!translation_state.is_vulkan && translation_state.render_info_id != spv::NoResult) {''', '''                // Apply the viewport flip: OpenGL's, and on Xbox Vulkan's too
                // (its viewport is kept positive there)
#ifdef VITA3K_UWP
                if (translation_state.render_info_id != spv::NoResult) {
#else
                if (!translation_state.is_vulkan && translation_state.render_info_id != spv::NoResult) {
#endif''')
# Shaders on disk were made without the flip: a new cache version.
replace('vita3k/shader/include/shader/spirv_recompiler.h', 'static constexpr uint32_t CURRENT_VERSION = 13;',
        '#ifdef VITA3K_UWP\nstatic constexpr uint32_t CURRENT_VERSION = 9013; // Xbox: viewport flip in the shader\n#else\nstatic constexpr uint32_t CURRENT_VERSION = 13;\n#endif')

# 2. The colour attachment as an input attachment of its own subpass, in
#    every render pass, for framebuffer fetch. D3D12 cannot read a render
#    target in the pass that writes it; Dozen emulates it. Only shaders that
#    read the previous colour (programmable blending) need it, so on Xbox it
#    is used with High Accuracy Rendering only. Without it those shaders read
#    nothing, as on GPUs without any framebuffer fetch.
# 3. Texture viewport: a std430 uniform block with a vec2 array (stride 8),
#    which D3D12 constant buffers do not lay out that way. Read wrong, every
#    texture lookup lands on one corner texel: a transparent one, in most
#    sprite sheets. Upstream's path without it copies the texture instead.
rend = 'vita3k/renderer/src/vulkan/renderer.cpp'
replace(rend, '''    } else {
        // We use subpass input to get something similar to direct fragcolor access (there is no difference for the shader)
        features.direct_fragcolor = true;
        features.support_shader_interlock = false;
    }

    // texture viewport is faster but not entirely accurate
    if (support_standard_layout && !use_high_accuracy) {
        LOG_INFO("The Vulkan renderer is using texture viewport for better performance");
        features.use_texture_viewport = true;
    }''', '''    } else {
        // We use subpass input to get something similar to direct fragcolor access (there is no difference for the shader)
        features.direct_fragcolor = true;
        features.support_shader_interlock = false;
    }
#ifdef VITA3K_UWP
    if (!use_high_accuracy) {
        features.direct_fragcolor = false;
        LOG_INFO("Xbox: framebuffer fetch off (High Accuracy Rendering turns it on)");
    }
    LOG_INFO("Xbox: viewport y flip done in the vertex shader; texture viewport off");
#else

    // texture viewport is faster but not entirely accurate
    if (support_standard_layout && !use_high_accuracy) {
        LOG_INFO("The Vulkan renderer is using texture viewport for better performance");
        features.use_texture_viewport = true;
    }
#endif''')
replace('vita3k/renderer/src/vulkan/pipeline_cache.cpp', '''        subpass.setColorAttachments(color_ref);
        subpass.setInputAttachments(color_ref);''', '''        subpass.setColorAttachments(color_ref);
#ifdef VITA3K_UWP
        if (state.features.direct_fragcolor)
#endif
        subpass.setInputAttachments(color_ref);''')
print('Dozen rendering: positive viewports, framebuffer fetch only with high accuracy, no texture viewport.')
