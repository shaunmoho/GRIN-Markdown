// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

// Dockable Notepad++ panel hosting a WebView2 that renders the current document.
#pragma once

#include <windows.h>
#include <string>

#include "PluginInterface.h"
#include "Settings.h"

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;
struct ICoreWebView2WebResourceRequestedEventArgs;

class PreviewPanel
{
public:
	PreviewPanel(HINSTANCE module, const NppData& npp, Settings& settings);
	~PreviewPanel();

	// Paths are resolved by the plugin: the folder holding index.html etc., and
	// a writable folder for the WebView2 profile.
	void SetPaths(std::wstring assetsDir, std::wstring userDataDir);

	void Show(int dockCmdIndex, HICON tabIcon);
	void Hide();
	bool IsVisible() const { return _visible; }

	// Re-render the active document after the configured delay (typing) or now (tab switch).
	void ScheduleUpdate();
	void UpdateNow();
	void OnEditorScrolled();
	void OnThemeChanged();
	void Shutdown();

	// Called when the docking container's close button is used.
	void (*onClosed)() = nullptr;

private:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool CreateWindowIfNeeded();
	void CreateWebView();
	void OnWebViewCreated(ICoreWebView2Controller* controller);
	void OnWebMessage(const std::wstring& json);
	void OpenLink(const std::wstring& href);
	void GotoLine(int line);
	void ResizeWebView();
	void UpdateBackground();

	HWND CurrentScintilla() const;
	int FirstVisibleDocLine() const;
	bool IsDarkTheme() const;
	bool ShouldPreview(const std::wstring& path) const;
	std::wstring CurrentFilePath() const;
	std::wstring DocumentBaseUrl(const std::wstring& filePath) const;
	void OnWebResourceRequested(ICoreWebView2WebResourceRequestedEventArgs* args);
	void PostJson(const std::wstring& json);

	HINSTANCE _module;
	NppData _npp;
	Settings& _settings;
	std::wstring _assetsDir;
	std::wstring _userDataDir;

	HWND _hwnd = nullptr;
	HBRUSH _bgBrush = nullptr;
	bool _registered = false;
	bool _visible = false;
	bool _creatingWebView = false;
	bool _pageReady = false;
	bool _destroyed = false;

	ICoreWebView2Environment* _env = nullptr;
	ICoreWebView2Controller* _controller = nullptr;
	ICoreWebView2* _webview = nullptr;

	int _lastScrollLine = -1;
	std::wstring _pendingAnchor; // "#fragment" of a link that opened another document
	std::wstring _errorText;
};
