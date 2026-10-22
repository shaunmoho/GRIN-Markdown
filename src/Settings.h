// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

#pragma once

#include <string>
#include <vector>

enum class ThemeMode { Auto = 0, Light = 1, Dark = 2 };

struct Settings
{
	bool syncScroll = true;
	bool previewAllFiles = false;
	ThemeMode theme = ThemeMode::Auto;
	int updateDelayMs = 300;
	std::vector<std::wstring> extensions; // lower case, with leading dot

	void Load(const std::wstring& iniPath);
	void Save(const std::wstring& iniPath) const;

	bool IsMarkdownExtension(const std::wstring& path) const;
};
