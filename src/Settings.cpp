// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

#include "Settings.h"

#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

namespace
{
	constexpr wchar_t kSection[] = L"General";
	constexpr wchar_t kDefaultExtensions[] = L".md;.markdown;.mdown;.mkd;.mkdn;.mdwn;.mdtxt;.mdtext;.rmd";

	std::wstring ToLower(std::wstring s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return s;
	}

	std::wstring Trim(const std::wstring& s)
	{
		const auto b = s.find_first_not_of(L" \t");
		if (b == std::wstring::npos)
			return {};
		const auto e = s.find_last_not_of(L" \t");
		return s.substr(b, e - b + 1);
	}

	std::wstring ReadString(const std::wstring& ini, const wchar_t* key, const wchar_t* def)
	{
		wchar_t buf[2048];
		GetPrivateProfileStringW(kSection, key, def, buf, static_cast<DWORD>(std::size(buf)), ini.c_str());
		return buf;
	}

	void WriteString(const std::wstring& ini, const wchar_t* key, const std::wstring& value)
	{
		WritePrivateProfileStringW(kSection, key, value.c_str(), ini.c_str());
	}
}

void Settings::Load(const std::wstring& iniPath)
{
	_configDir = iniPath;
	PathRemoveFileSpecW(_configDir.data());
	_configDir.resize(wcslen(_configDir.c_str()));

	syncScroll = GetPrivateProfileIntW(kSection, L"SyncScroll", 1, iniPath.c_str()) != 0;
	previewAllFiles = GetPrivateProfileIntW(kSection, L"PreviewAllFiles", 0, iniPath.c_str()) != 0;
	updateDelayMs = std::clamp<int>(GetPrivateProfileIntW(kSection, L"UpdateDelayMs", 300, iniPath.c_str()), 0, 5000);

	const std::wstring themeName = ToLower(ReadString(iniPath, L"Theme", L"auto"));
	theme = themeName == L"light" ? ThemeMode::Light : themeName == L"dark" ? ThemeMode::Dark : ThemeMode::Auto;

	useCustomCss = GetPrivateProfileIntW(kSection, L"CustomCss", 1, iniPath.c_str()) != 0;
	replaceGithubCss = ToLower(ReadString(iniPath, L"CustomCssMode", L"layer")) == L"replace";
	customCssLight = Trim(ReadString(iniPath, L"CustomCssLight", L""));
	customCssDark = Trim(ReadString(iniPath, L"CustomCssDark", L""));
	codeThemeLight = ToLower(Trim(ReadString(iniPath, L"CodeThemeLight", L"")));
	codeThemeDark = ToLower(Trim(ReadString(iniPath, L"CodeThemeDark", L"")));
	mermaidThemeLight = ToLower(Trim(ReadString(iniPath, L"MermaidThemeLight", L"default")));
	mermaidThemeDark = ToLower(Trim(ReadString(iniPath, L"MermaidThemeDark", L"dark")));

	extensions.clear();
	const std::wstring list = ToLower(ReadString(iniPath, L"Extensions", kDefaultExtensions));
	size_t start = 0;
	while (start <= list.size())
	{
		size_t end = list.find_first_of(L";,", start);
		if (end == std::wstring::npos)
			end = list.size();
		std::wstring ext = Trim(list.substr(start, end - start));
		if (!ext.empty())
		{
			if (ext[0] != L'.')
				ext.insert(ext.begin(), L'.');
			extensions.push_back(ext);
		}
		start = end + 1;
	}
}

void Settings::Save(const std::wstring& iniPath) const
{
	WriteString(iniPath, L"SyncScroll", syncScroll ? L"1" : L"0");
	WriteString(iniPath, L"PreviewAllFiles", previewAllFiles ? L"1" : L"0");
	WriteString(iniPath, L"Theme", theme == ThemeMode::Light ? L"light" : theme == ThemeMode::Dark ? L"dark" : L"auto");
	WriteString(iniPath, L"UpdateDelayMs", std::to_wstring(updateDelayMs));

	std::wstring list;
	for (const auto& ext : extensions)
	{
		if (!list.empty())
			list += L';';
		list += ext;
	}
	WriteString(iniPath, L"Extensions", list);

	WriteString(iniPath, L"CustomCss", useCustomCss ? L"1" : L"0");
	WriteString(iniPath, L"CustomCssMode", replaceGithubCss ? L"replace" : L"layer");
	WriteString(iniPath, L"CustomCssLight", customCssLight);
	WriteString(iniPath, L"CustomCssDark", customCssDark);
	WriteString(iniPath, L"CodeThemeLight", codeThemeLight);
	WriteString(iniPath, L"CodeThemeDark", codeThemeDark);
	WriteString(iniPath, L"MermaidThemeLight", mermaidThemeLight);
	WriteString(iniPath, L"MermaidThemeDark", mermaidThemeDark);
}

bool Settings::IsMarkdownExtension(const std::wstring& path) const
{
	const wchar_t* ext = PathFindExtensionW(path.c_str());
	if (!ext || !*ext)
		return false;
	const std::wstring lower = ToLower(ext);
	return std::find(extensions.begin(), extensions.end(), lower) != extensions.end();
}

std::wstring Settings::ResolvePath(const std::wstring& setting) const
{
	if (setting.empty())
		return {};
	wchar_t expanded[MAX_PATH * 4];
	const DWORD n = ExpandEnvironmentStringsW(setting.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
	std::wstring path = n > 0 && n <= std::size(expanded) ? expanded : setting;
	if (PathIsRelativeW(path.c_str()) && !_configDir.empty())
		path = _configDir + L"\\" + path;
	wchar_t full[MAX_PATH * 4];
	const DWORD len = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
	return len > 0 && len < std::size(full) ? full : path;
}
