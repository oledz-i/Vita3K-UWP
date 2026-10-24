// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
// A tiny native UWP just-in-time executable-code liveness test.
// On-demand only; does not interpret ARM instructions or boot Vita games.
std::wstring RunUwpJitLivenessProbe();
