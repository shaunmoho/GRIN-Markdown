// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

// GRIN-Markdown (GRIN Renders In Notepad++): GitHub Flavored Markdown viewer
// with Mermaid diagrams.

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <memory>
#include <string>

#include "PluginInterface.h"
#include "PreviewPanel.h"
#include "Settings.h"
#include "resource.h"

namespace
{
	constexpr wchar_t kPluginName[] = L"GRIN-Markdown";
	constexpr wchar_t kIniName[] = L"GRIN-Markdown.ini";

	enum Command
	{
		CmdTogglePreview,
		CmdRefresh,
		CmdSep1,
		CmdSyncScroll,
		CmdPreviewAll,
		CmdSep2,
		CmdThemeAuto,
		CmdThemeLight,
		CmdThemeDark,
		CmdSep3,
		CmdEditSettings,
		CmdAbout,
		CmdCount
	};

	HINSTANCE g_module = nullptr;
	NppData g_npp;
	FuncItem g_funcs[CmdCount];
	Settings g_settings;
	std::wstring g_iniPath;
	std::unique_ptr<PreviewPanel> g_panel;
	HICON g_iconLight = nullptr;
	HICON g_iconDark = nullptr;
	HICON g_tabIcon = nullptr;
	HBITMAP g_toolbarBmp = nullptr;

	void SetChecked(int cmd, bool checked)
	{
		SendMessageW(g_npp._nppHandle, NPPM_SETMENUITEMCHECK, g_funcs[cmd]._cmdID, checked ? TRUE : FALSE);
	}

	void UpdateThemeChecks()
	{
		SetChecked(CmdThemeAuto, g_settings.theme == ThemeMode::Auto);
		SetChecked(CmdThemeLight, g_settings.theme == ThemeMode::Light);
		SetChecked(CmdThemeDark, g_settings.theme == ThemeMode::Dark);
	}

	std::wstring ModuleDirectory()
	{
		wchar_t path[MAX_PATH * 4];
		GetModuleFileNameW(g_module, path, static_cast<DWORD>(std::size(path)));
		PathRemoveFileSpecW(path);
		return path;
	}

	std::wstring WebViewDataDirectory()
	{
		std::wstring dir;
		PWSTR local = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
			dir = std::wstring(local) + L"\\GRIN-Markdown\\WebView2";
		CoTaskMemFree(local);
		if (dir.empty())
		{
			wchar_t temp[MAX_PATH];
			GetTempPathW(MAX_PATH, temp);
			dir = std::wstring(temp) + L"GRIN-Markdown\\WebView2";
		}
		SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
		return dir;
	}

	PreviewPanel& Panel()
	{
		if (!g_panel)
		{
			g_panel = std::make_unique<PreviewPanel>(g_module, g_npp, g_settings);
			g_panel->SetPaths(ModuleDirectory() + L"\\web", WebViewDataDirectory());
			g_panel->onClosed = [] { SetChecked(CmdTogglePreview, false); };
		}
		return *g_panel;
	}

	void LoadIcons()
	{
		const int size = GetSystemMetrics(SM_CXSMICON);
		g_tabIcon = static_cast<HICON>(LoadImageW(g_module, MAKEINTRESOURCEW(IDI_ICON_LIGHT), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
		g_iconLight = static_cast<HICON>(LoadImageW(g_module, MAKEINTRESOURCEW(IDI_ICON_LIGHT), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
		g_iconDark = static_cast<HICON>(LoadImageW(g_module, MAKEINTRESOURCEW(IDI_ICON_DARK), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
		g_toolbarBmp = static_cast<HBITMAP>(LoadImageW(g_module, MAKEINTRESOURCEW(IDB_TOOLBAR), IMAGE_BITMAP, 0, 0, LR_DEFAULTSIZE | LR_LOADMAP3DCOLORS));
	}

	// ---------------------------------------------------------------------
	// Commands
	// ---------------------------------------------------------------------

	void TogglePreview()
	{
		PreviewPanel& panel = Panel();
		if (panel.IsVisible())
			panel.Hide();
		else
			panel.Show(CmdTogglePreview, g_tabIcon);
		SetChecked(CmdTogglePreview, panel.IsVisible());
	}

	void Refresh()
	{
		if (g_panel)
			g_panel->UpdateNow();
	}

	void ToggleSyncScroll()
	{
		g_settings.syncScroll = !g_settings.syncScroll;
		SetChecked(CmdSyncScroll, g_settings.syncScroll);
		g_settings.Save(g_iniPath);
		Refresh();
	}

	void TogglePreviewAll()
	{
		g_settings.previewAllFiles = !g_settings.previewAllFiles;
		SetChecked(CmdPreviewAll, g_settings.previewAllFiles);
		g_settings.Save(g_iniPath);
		Refresh();
	}

	void SetTheme(ThemeMode mode)
	{
		g_settings.theme = mode;
		UpdateThemeChecks();
		g_settings.Save(g_iniPath);
		if (g_panel)
			g_panel->OnThemeChanged();
	}

	void ThemeAuto() { SetTheme(ThemeMode::Auto); }
	void ThemeLight() { SetTheme(ThemeMode::Light); }
	void ThemeDark() { SetTheme(ThemeMode::Dark); }

	void EditSettings()
	{
		g_settings.Save(g_iniPath); // make sure every key is present
		SendMessageW(g_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(g_iniPath.c_str()));
	}

	void About()
	{
		const std::wstring text =
			L"GRIN-Markdown " GRIN_VERSION_WSTR L"\n"
			L"GRIN Renders In Notepad++: GitHub Flavored Markdown\n"
			L"viewer with Mermaid diagrams.\n\n"
			L"Renders tables, task lists, footnotes, emoji, alerts,\n"
			L"syntax-highlighted code and ```mermaid diagrams.\n\n"
			L"Double-click the preview to jump to that line.\n"
			L"Edit Settings... changes which extensions are previewed.\n\n"
			L"Bundles markdown-it, highlight.js, Mermaid, DOMPurify and\n"
			L"github-markdown-css (see THIRD_PARTY_NOTICES.md).\n"
			L"Rendering by Microsoft Edge WebView2.\n\n"
			L"Free software under the\n"
			L"GNU General Public License v3 or later.";
		MessageBoxW(g_npp._nppHandle, text.c_str(), kPluginName, MB_OK | MB_ICONINFORMATION);
	}

	void InitCommands()
	{
		auto set = [](int i, const wchar_t* name, PFUNCPLUGINCMD fn, bool check = false) {
			wcsncpy_s(g_funcs[i]._itemName, name, _TRUNCATE);
			g_funcs[i]._pFunc = fn;
			g_funcs[i]._init2Check = check;
		};
		set(CmdTogglePreview, L"Markdown Preview", TogglePreview);
		set(CmdRefresh, L"Refresh Preview", Refresh);
		set(CmdSep1, L"", nullptr);
		set(CmdSyncScroll, L"Synchronize Scrolling", ToggleSyncScroll, g_settings.syncScroll);
		set(CmdPreviewAll, L"Preview Non-Markdown Files", TogglePreviewAll, g_settings.previewAllFiles);
		set(CmdSep2, L"", nullptr);
		set(CmdThemeAuto, L"Theme: Follow Notepad++", ThemeAuto, g_settings.theme == ThemeMode::Auto);
		set(CmdThemeLight, L"Theme: Light", ThemeLight, g_settings.theme == ThemeMode::Light);
		set(CmdThemeDark, L"Theme: Dark", ThemeDark, g_settings.theme == ThemeMode::Dark);
		set(CmdSep3, L"", nullptr);
		set(CmdEditSettings, L"Edit Settings...", EditSettings);
		set(CmdAbout, L"About...", About);
	}
}

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_module = module;
		DisableThreadLibraryCalls(module);
	}
	return TRUE;
}

extern "C" __declspec(dllexport) void setInfo(NppData data)
{
	g_npp = data;

	wchar_t configDir[MAX_PATH * 4] = {};
	SendMessageW(g_npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, std::size(configDir), reinterpret_cast<LPARAM>(configDir));
	g_iniPath = std::wstring(configDir) + L"\\" + kIniName;
	g_settings.Load(g_iniPath);

	InitCommands();
	LoadIcons();
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
	return kPluginName;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count)
{
	*count = CmdCount;
	return g_funcs;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notification)
{
	const auto& hdr = notification->nmhdr;

	if (hdr.hwndFrom == g_npp._nppHandle)
	{
		switch (hdr.code)
		{
		case NPPN_TBMODIFICATION:
		{
			toolbarIconsWithDarkMode icons{ g_toolbarBmp, g_iconLight, g_iconDark };
			SendMessageW(g_npp._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE, g_funcs[CmdTogglePreview]._cmdID, reinterpret_cast<LPARAM>(&icons));
			break;
		}
		case NPPN_READY:
			UpdateThemeChecks();
			break;
		case NPPN_BUFFERACTIVATED:
		case NPPN_FILERENAMED:
		case NPPN_LANGCHANGED:
			if (g_panel)
				g_panel->UpdateNow();
			break;
		case NPPN_FILESAVED:
		{
			// Saving the settings file from "Edit Settings..." applies it immediately.
			wchar_t path[MAX_PATH * 4] = {};
			SendMessageW(g_npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, notification->nmhdr.idFrom, reinterpret_cast<LPARAM>(path));
			if (lstrcmpiW(path, g_iniPath.c_str()) == 0)
			{
				g_settings.Load(g_iniPath);
				SetChecked(CmdSyncScroll, g_settings.syncScroll);
				SetChecked(CmdPreviewAll, g_settings.previewAllFiles);
				UpdateThemeChecks();
				if (g_panel)
					g_panel->OnThemeChanged();
			}
			if (g_panel)
				g_panel->UpdateNow();
			break;
		}
		case NPPN_DARKMODECHANGED:
			if (g_panel)
				g_panel->OnThemeChanged();
			break;
		case NPPN_SHUTDOWN:
			g_settings.Save(g_iniPath);
			if (g_panel)
				g_panel->Shutdown();
			break;
		}
		return;
	}

	if (!g_panel || !g_panel->IsVisible())
		return;
	if (hdr.hwndFrom != g_npp._scintillaMainHandle && hdr.hwndFrom != g_npp._scintillaSecondHandle)
		return;

	switch (hdr.code)
	{
	case SCN_MODIFIED:
		if (notification->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
			g_panel->ScheduleUpdate();
		break;
	case SCN_UPDATEUI:
		if (notification->updated & SC_UPDATE_V_SCROLL)
			g_panel->OnEditorScrolled();
		break;
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
	return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
	return TRUE;
}
