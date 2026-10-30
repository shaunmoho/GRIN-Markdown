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

	// Custom styling, one optional stylesheet per theme. Paths may be relative to
	// the plugin config folder and may contain %ENVIRONMENT% variables.
	bool useCustomCss = true;
	bool replaceGithubCss = false; // CustomCssMode=replace: drop github-markdown-css
	std::wstring customCssLight;
	std::wstring customCssDark;
	std::wstring codeThemeLight;   // highlight.js style name; empty = GitHub's own code colors
	std::wstring codeThemeDark;
	std::wstring mermaidThemeLight = L"default";
	std::wstring mermaidThemeDark = L"dark";

	void Load(const std::wstring& iniPath);
	void Save(const std::wstring& iniPath) const;

	bool IsMarkdownExtension(const std::wstring& path) const;

	// Full path of a CustomCss* setting, or empty when it is not set.
	std::wstring ResolvePath(const std::wstring& setting) const;

private:
	std::wstring _configDir;
};
