#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Scoped, checked adapters for config/codec/io failures in run 40164666388."""
import pathlib
import shutil
import sys

source = pathlib.Path(sys.argv[1]).resolve()
repo = pathlib.Path(__file__).resolve().parent.parent

def replace(relative, old, new, count=1):
    path = source / relative
    text = path.read_text()
    assert text.count(old) == count, f'{relative}: expected {count} matches for {old!r}'
    path.write_text(text.replace(old, new))

# A core uses argv supplied by its frontend; it must not inspect the process
# command line through shell32. Keep CLI11's ordinary passed-argv path on UWP.
cli = 'external/cli11/CLI11.hpp'
for suffix in [
    '/// Decode and return UTF-8 argv from GetCommandLineW.',
    'CLI11_INLINE std::vector<std::string> compute_win32_argv() {',
    '    (void)argv;\n\n    normalized_argv_ = detail::compute_win32_argv();',
]:
    replace(cli, '#ifdef _WIN32\n' + suffix,
            '#if defined(_WIN32) && !defined(VITA3K_UWP)\n' + suffix)

# Preserve guest deletion and directory semantics with the standard filesystem.
replace('vita3k/io/src/io.cpp', 'fs::detail::remove(emulated_path, &error_code)',
        'fs::remove(emulated_path, error_code)')
replace('vita3k/io/src/io.cpp', 'cur_path.filename_is_dot() || cur_path.filename_is_dot_dot()',
        'cur_path.filename() == fs::path(".") || cur_path.filename() == fs::path("..")')
replace('vita3k/io/src/io.cpp', 'fs::path(emulated_path).remove_trailing_separator().parent_path()',
        'fs::without_trailing_separator(emulated_path).parent_path()')
replace('vita3k/util/include/util/fs.h', 'using fstream = std::fstream;\n}',
'''using fstream = std::fstream;
inline path without_trailing_separator(path p) {
    while (!p.empty() && !p.has_filename() && p != p.root_path())
        p = p.parent_path();
    return p;
}
}''')

# Preserve AAC's exact consumed-byte callback. The complete FFCodec declaration
# was token-compared to n6.1.1 upstream; a version guard rejects ABI drift.
include = source / 'vita3k/codec/ffmpeg-internal/libavcodec'
include.mkdir(parents=True, exist_ok=True)
shutil.copyfile(repo / 'third_party/ffmpeg-internal/libavcodec/codec_internal.h', include / 'codec_internal.h')
p = source / 'vita3k/codec/CMakeLists.txt'
p.write_text(p.read_text() + '\nif(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")\n  target_include_directories(codec PRIVATE ffmpeg-internal)\nendif()\n')

# The original decoder pointed FFmpeg at unpadded guest memory and trusted an
# uninitialized got_frame flag. Own a padded packet and validate real output.
replace('vita3k/codec/src/aac.cpp', '#include <util/log.h>',
        '#include <util/log.h>\n#include <cstring>\n#include <limits>')
replace('vita3k/codec/src/aac.cpp', '''    AVPacket *packet = av_packet_alloc();
    packet->data = const_cast<uint8_t *>(data);
    packet->size = size;''', '''    es_size_used = 0;
    if (!data || size == 0 || size > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        return false;
    AVPacket *packet = av_packet_alloc();
    if (!packet) return false;
    if (av_new_packet(packet, static_cast<int>(size)) < 0) {
        av_packet_free(&packet);
        return false;
    }
    std::memcpy(packet->data, data, size);''')
replace('vita3k/codec/src/aac.cpp', '''    int got_frame;
    int len = ff_codec->cb.decode(context, frame, &got_frame, packet);
    assert(got_frame);''', '''    int got_frame = 0;
    if (ff_codec->cb_type != FF_CODEC_CB_TYPE_DECODE || !ff_codec->cb.decode) {
        av_packet_free(&packet);
        LOG_WARN("AAC decoder callback type does not match the pinned FFmpeg ABI");
        return false;
    }
    int len = ff_codec->cb.decode(context, frame, &got_frame, packet);''')
replace('vita3k/codec/src/aac.cpp', '    if (len < 0) {',
        '    if (len <= 0 || static_cast<uint32_t>(len) > size || !got_frame) {')
replace('vita3k/codec/src/aac.cpp', '''        LOG_WARN("Error sending Aac packet: {}.", codec_error_name(len));
        return false;''', '''        LOG_WARN("AAC produced no valid frame (consumed {}, input {}, frame {})", len, size, got_frame);
        av_frame_unref(frame);
        return false;''')
print('Applied checked UWP CLI argv, filesystem semantics and padded AAC decoder fixes.')
