// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

#include "PreviewPanel.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <map>

#include "WebView2.h"
#include "Docking.h"
#include "dockingResource.h"
#include "ComHandler.h"

namespace
{
	constexpr wchar_t kWindowClass[] = L"GrinMarkdownPreviewPanel";
	constexpr wchar_t kPanelTitle[] = L"Markdown Preview";
	constexpr wchar_t kModuleName[] = L"GRIN-Markdown.dll";
	constexpr wchar_t kAppHost[] = L"app.grin-markdown.local";
	constexpr wchar_t kAppUrl[] = L"https://app.grin-markdown.local/index.html";
	constexpr wchar_t kAppOrigin[] = L"https://app.grin-markdown.local/";
	constexpr wchar_t kDocOrigin[] = L"https://doc.grin-markdown.local/";
	constexpr wchar_t kDocFilter[] = L"https://doc.grin-markdown.local/*";
	constexpr UINT_PTR kUpdateTimer = 1;

	constexpr COLORREF kLightBackground = RGB(0xff, 0xff, 0xff);
	constexpr COLORREF kDarkBackground = RGB(0x0d, 0x11, 0x17);

	template <typename T>
	void SafeRelease(T*& p)
	{
		if (p)
		{
			p->Release();
			p = nullptr;
		}
	}

	std::wstring ToLower(std::wstring s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return s;
	}

	bool StartsWith(const std::wstring& s, const wchar_t* prefix)
	{
		return s.rfind(prefix, 0) == 0;
	}

	void AppendJsonString(std::wstring& out, const std::wstring& s)
	{
		out += L'"';
		for (wchar_t c : s)
		{
			switch (c)
			{
			case L'"': out += L"\\\""; break;
			case L'\\': out += L"\\\\"; break;
			case L'\n': out += L"\\n"; break;
			case L'\r': out += L"\\r"; break;
			case L'\t': out += L"\\t"; break;
			default:
				if (c < 0x20)
				{
					wchar_t buf[8];
					swprintf_s(buf, L"\\u%04x", static_cast<unsigned>(c));
					out += buf;
				}
				else
				{
					out += c;
				}
			}
		}
		out += L'"';
	}

	// Parses the flat {"key": "string" | number} objects the page sends.
	std::map<std::wstring, std::wstring> ParseFlatJson(const std::wstring& json)
	{
		std::map<std::wstring, std::wstring> result;
		size_t i = 0;
		const size_t n = json.size();
		auto skipWs = [&] { while (i < n && std::iswspace(json[i])) ++i; };
		auto readString = [&](std::wstring& out) -> bool {
			if (i >= n || json[i] != L'"')
				return false;
			++i;
			while (i < n && json[i] != L'"')
			{
				wchar_t c = json[i++];
				if (c == L'\\' && i < n)
				{
					wchar_t e = json[i++];
					switch (e)
					{
					case L'n': out += L'\n'; break;
					case L'r': out += L'\r'; break;
					case L't': out += L'\t'; break;
					case L'b': out += L'\b'; break;
					case L'f': out += L'\f'; break;
					case L'u':
						if (i + 4 <= n)
						{
							out += static_cast<wchar_t>(wcstoul(json.substr(i, 4).c_str(), nullptr, 16));
							i += 4;
						}
						break;
					default: out += e; break;
					}
				}
				else
				{
					out += c;
				}
			}
			if (i >= n)
				return false;
			++i;
			return true;
		};

		skipWs();
		if (i >= n || json[i] != L'{')
			return result;
		++i;
		for (;;)
		{
			skipWs();
			std::wstring key, value;
			if (!readString(key))
				break;
			skipWs();
			if (i >= n || json[i] != L':')
				break;
			++i;
			skipWs();
			if (i < n && json[i] == L'"')
			{
				if (!readString(value))
					break;
			}
			else
			{
				const size_t start = i;
				while (i < n && json[i] != L',' && json[i] != L'}')
					++i;
				value = json.substr(start, i - start);
				while (!value.empty() && std::iswspace(value.back()))
					value.pop_back();
			}
			result[key] = value;
			skipWs();
			if (i < n && json[i] == L',')
			{
				++i;
				continue;
			}
			break;
		}
		return result;
	}

	std::wstring PercentDecodeUtf8(const std::wstring& s)
	{
		std::string bytes;
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == L'%' && i + 2 < s.size() && std::iswxdigit(s[i + 1]) && std::iswxdigit(s[i + 2]))
			{
				bytes += static_cast<char>(wcstoul(s.substr(i + 1, 2).c_str(), nullptr, 16));
				i += 2;
			}
			else
			{
				char utf8[8];
				const int len = WideCharToMultiByte(CP_UTF8, 0, &s[i], 1, utf8, sizeof(utf8), nullptr, nullptr);
				bytes.append(utf8, len > 0 ? len : 0);
			}
		}
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
		std::wstring out(wlen, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), out.data(), wlen);
		return out;
	}

	// Files a Markdown link may point at that are better shown in Explorer than opened as text.
	bool IsBinaryDocument(const std::wstring& path)
	{
		static const wchar_t* const kBinary[] = {
			L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".ico", L".webp", L".tif", L".tiff", L".psd",
			L".pdf", L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx", L".odt", L".ods", L".odp",
			L".zip", L".7z", L".rar", L".gz", L".tar", L".bz2", L".xz",
			L".mp3", L".wav", L".ogg", L".flac", L".mp4", L".mkv", L".avi", L".mov", L".webm",
			L".exe", L".dll", L".msi", L".com", L".scr", L".lnk", L".bin", L".iso",
		};
		const std::wstring ext = ToLower(PathFindExtensionW(path.c_str()));
		return std::any_of(std::begin(kBinary), std::end(kBinary), [&](const wchar_t* e) { return ext == e; });
	}

	// "C:\\dir\\a b" -> "C:/dir/a%20b"; "\\\\server\\share" -> "unc/server/share"
	std::wstring PathToUrlPath(const std::wstring& path)
	{
		std::wstring p = path;
		std::wstring out;
		if (StartsWith(p, L"\\\\"))
		{
			out = L"unc/";
			p.erase(0, 2);
		}
		std::string utf8;
		const int len = WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()), nullptr, 0, nullptr, nullptr);
		utf8.resize(len);
		WideCharToMultiByte(CP_UTF8, 0, p.c_str(), static_cast<int>(p.size()), utf8.data(), len, nullptr, nullptr);
		for (unsigned char c : utf8)
		{
			if (c == '\\')
				out += L'/';
			else if (isalnum(c) || strchr("-._~:/", c))
				out += static_cast<wchar_t>(c);
			else
			{
				wchar_t buf[4];
				swprintf_s(buf, L"%%%02X", c);
				out += buf;
			}
		}
		return out;
	}

	// Inverse of PathToUrlPath; query and fragment are dropped.
	std::wstring UrlPathToPath(std::wstring urlPath)
	{
		urlPath = urlPath.substr(0, urlPath.find_first_of(L"?#"));
		std::wstring p = PercentDecodeUtf8(urlPath);
		std::replace(p.begin(), p.end(), L'/', L'\\');
		if (StartsWith(ToLower(p), L"unc\\"))
			p = L"\\\\" + p.substr(4);
		if (p.size() < 3 || PathIsRelativeW(p.c_str()))
			return {};
		wchar_t full[MAX_PATH * 4];
		const DWORD len = GetFullPathNameW(p.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
		if (len == 0 || len >= std::size(full))
			return {};
		return full;
	}

	const wchar_t* MediaTypeFor(const std::wstring& path)
	{
		static const std::pair<const wchar_t*, const wchar_t*> kTypes[] = {
			{ L".png", L"image/png" }, { L".jpg", L"image/jpeg" }, { L".jpeg", L"image/jpeg" },
			{ L".gif", L"image/gif" }, { L".webp", L"image/webp" }, { L".avif", L"image/avif" },
			{ L".bmp", L"image/bmp" }, { L".ico", L"image/x-icon" }, { L".svg", L"image/svg+xml" },
			{ L".mp4", L"video/mp4" }, { L".webm", L"video/webm" }, { L".ogv", L"video/ogg" },
			{ L".mp3", L"audio/mpeg" }, { L".ogg", L"audio/ogg" }, { L".wav", L"audio/wav" },
		};
		const std::wstring ext = ToLower(PathFindExtensionW(path.c_str()));
		for (const auto& [e, type] : kTypes)
			if (ext == e)
				return type;
		return nullptr;
	}
}

PreviewPanel::PreviewPanel(HINSTANCE module, const NppData& npp, Settings& settings)
	: _module(module), _npp(npp), _settings(settings)
{
}

PreviewPanel::~PreviewPanel()
{
	Shutdown();
	if (_hwnd && IsWindow(_hwnd))
		DestroyWindow(_hwnd);
	if (_bgBrush)
		DeleteObject(_bgBrush);
}

void PreviewPanel::SetPaths(std::wstring assetsDir, std::wstring userDataDir)
{
	_assetsDir = std::move(assetsDir);
	_userDataDir = std::move(userDataDir);
}

bool PreviewPanel::CreateWindowIfNeeded()
{
	if (_hwnd)
		return true;

	WNDCLASSEXW wc = { sizeof(wc) };
	if (!GetClassInfoExW(_module, kWindowClass, &wc))
	{
		wc.lpfnWndProc = WndProc;
		wc.hInstance = _module;
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.lpszClassName = kWindowClass;
		if (!RegisterClassExW(&wc))
			return false;
	}

	_hwnd = CreateWindowExW(0, kWindowClass, kPanelTitle, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		0, 0, 400, 600, _npp._nppHandle, nullptr, _module, this);
	UpdateBackground();
	return _hwnd != nullptr;
}

void PreviewPanel::Show(int dockCmdIndex, HICON tabIcon)
{
	if (_destroyed || !CreateWindowIfNeeded())
		return;

	if (!_registered)
	{
		tTbData data{};
		data.hClient = _hwnd;
		data.pszName = kPanelTitle;
		data.dlgID = dockCmdIndex;
		data.uMask = DWS_DF_CONT_RIGHT | (tabIcon ? DWS_ICONTAB : 0);
		data.hIconTab = tabIcon;
		data.pszModuleName = kModuleName;
		SendMessageW(_npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
		_registered = true;
	}

	SendMessageW(_npp._nppHandle, NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(_hwnd));
	_visible = true;

	if (_controller)
	{
		_controller->put_IsVisible(TRUE);
		ResizeWebView();
		UpdateNow();
	}
	else if (!_creatingWebView)
	{
		CreateWebView();
	}
}

void PreviewPanel::Hide()
{
	if (!_hwnd)
		return;
	SendMessageW(_npp._nppHandle, NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(_hwnd));
	_visible = false;
	KillTimer(_hwnd, kUpdateTimer);
	if (_controller)
		_controller->put_IsVisible(FALSE);
}

// Called on NPPN_SHUTDOWN, which Notepad++ sends *before* it saves the docking
// layout: only the WebView goes away here. The panel window must stay registered
// so its position and visibility are saved; Notepad++ destroys it afterwards.
void PreviewPanel::Shutdown()
{
	if (_destroyed)
		return;
	_destroyed = true;
	if (_hwnd)
		KillTimer(_hwnd, kUpdateTimer);
	if (_controller)
		_controller->Close();
	SafeRelease(_webview);
	SafeRelease(_controller);
	SafeRelease(_env);
}

// ---------------------------------------------------------------------------
// WebView2
// ---------------------------------------------------------------------------

void PreviewPanel::CreateWebView()
{
	_creatingWebView = true;
	_errorText.clear();

	// Notepad++ initialises OLE on its UI thread; this is a no-op safety net.
	const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	(void)coHr;

	HandlerRef<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*> onEnv(
		[this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
			if (_destroyed)
				return S_OK;
			if (FAILED(hr) || !env)
			{
				_creatingWebView = false;
				_errorText = L"Could not start the Microsoft Edge WebView2 runtime.\n\n"
					L"Install it from https://developer.microsoft.com/microsoft-edge/webview2/";
				InvalidateRect(_hwnd, nullptr, TRUE);
				return S_OK;
			}
			_env = env;
			_env->AddRef();

			HandlerRef<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*> onController(
				[this](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT {
					_creatingWebView = false;
					if (_destroyed)
					{
						if (controller)
							controller->Close();
						return S_OK;
					}
					if (FAILED(hr2) || !controller)
					{
						_errorText = L"Could not create the WebView2 control.";
						InvalidateRect(_hwnd, nullptr, TRUE);
						return S_OK;
					}
					OnWebViewCreated(controller);
					return S_OK;
				});
			const HRESULT hrCreate = _env->CreateCoreWebView2Controller(_hwnd, onController);
			if (FAILED(hrCreate))
			{
				_creatingWebView = false;
				_errorText = L"Could not create the WebView2 control.";
				InvalidateRect(_hwnd, nullptr, TRUE);
			}
			return S_OK;
		});

	const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, _userDataDir.c_str(), nullptr, onEnv);
	if (FAILED(hr))
	{
		_creatingWebView = false;
		_errorText = L"The Microsoft Edge WebView2 runtime is not installed.\n\n"
			L"Install it from https://developer.microsoft.com/microsoft-edge/webview2/";
		InvalidateRect(_hwnd, nullptr, TRUE);
	}
}

void PreviewPanel::OnWebViewCreated(ICoreWebView2Controller* controller)
{
	_controller = controller;
	_controller->AddRef();
	_controller->get_CoreWebView2(&_webview);
	if (!_webview)
		return;

	UpdateBackground();

	ICoreWebView2Settings* settings = nullptr;
	if (SUCCEEDED(_webview->get_Settings(&settings)) && settings)
	{
		settings->put_IsStatusBarEnabled(FALSE);
		settings->put_AreDefaultScriptDialogsEnabled(FALSE);
		settings->put_AreHostObjectsAllowed(FALSE);
		settings->put_IsWebMessageEnabled(TRUE);
		settings->put_IsZoomControlEnabled(TRUE);
#ifdef NDEBUG
		settings->put_AreDevToolsEnabled(FALSE);
#endif
		settings->Release();
	}

	ICoreWebView2_3* webview3 = nullptr;
	if (SUCCEEDED(_webview->QueryInterface(__uuidof(ICoreWebView2_3), reinterpret_cast<void**>(&webview3))) && webview3)
	{
		webview3->SetVirtualHostNameToFolderMapping(kAppHost, _assetsDir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY);
		webview3->Release();
	}

	EventRegistrationToken token{};

	HandlerRef<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*> onMessage(
		[this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
			LPWSTR source = nullptr;
			if (SUCCEEDED(args->get_Source(&source)) && source)
			{
				const bool trusted = StartsWith(source, kAppOrigin);
				CoTaskMemFree(source);
				if (!trusted)
					return S_OK;
			}
			LPWSTR json = nullptr;
			if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json)
			{
				const std::wstring message = json;
				CoTaskMemFree(json);
				OnWebMessage(message);
			}
			return S_OK;
		});
	_webview->add_WebMessageReceived(onMessage, &token);

	// The page never navigates itself; links are routed through web messages.
	HandlerRef<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*> onNavigate(
		[this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
			LPWSTR uri = nullptr;
			if (SUCCEEDED(args->get_Uri(&uri)) && uri)
			{
				if (!StartsWith(uri, kAppUrl))
					args->put_Cancel(TRUE);
				else
					_pageReady = false; // (re)load of our page; wait for its "ready" message
				CoTaskMemFree(uri);
			}
			return S_OK;
		});
	_webview->add_NavigationStarting(onNavigate, &token);

	HandlerRef<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*> onNewWindow(
		[this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
			args->put_Handled(TRUE);
			LPWSTR uri = nullptr;
			if (SUCCEEDED(args->get_Uri(&uri)) && uri)
			{
				const std::wstring target = uri;
				CoTaskMemFree(uri);
				const std::wstring lower = ToLower(target);
				if (StartsWith(lower, L"http://") || StartsWith(lower, L"https://"))
				{
					if (lower.find(L".grin-markdown.local/") == std::wstring::npos) // our own hosts
						OpenLink(target);
				}
			}
			return S_OK;
		});
	_webview->add_NewWindowRequested(onNewWindow, &token);

	HandlerRef<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*> onCompleted(
		[this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
			BOOL ok = FALSE;
			args->get_IsSuccess(&ok);
			if (!ok)
			{
				COREWEBVIEW2_WEB_ERROR_STATUS status{};
				args->get_WebErrorStatus(&status);
				if (status != COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)
				{
					_errorText = L"Could not load the preview page from\n" + _assetsDir +
						L"\n\nReinstall the plugin so that the 'web' folder sits next to GRIN-Markdown.dll.";
					_controller->put_IsVisible(FALSE);
					InvalidateRect(_hwnd, nullptr, TRUE);
				}
			}
			return S_OK;
		});
	_webview->add_NavigationCompleted(onCompleted, &token);

	_webview->AddWebResourceRequestedFilter(kDocFilter, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
	HandlerRef<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*> onResource(
		[this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
			OnWebResourceRequested(args);
			return S_OK;
		});
	_webview->add_WebResourceRequested(onResource, &token);

	ResizeWebView();
	_controller->put_IsVisible(_visible ? TRUE : FALSE);
	_webview->Navigate(kAppUrl);
}

void PreviewPanel::OnWebMessage(const std::wstring& json)
{
	const auto msg = ParseFlatJson(json);
	const auto type = msg.find(L"type");
	if (type == msg.end())
		return;

	if (type->second == L"ready")
	{
		_pageReady = true;
		_lastScrollLine = -1;
		UpdateNow();
	}
	else if (type->second == L"link")
	{
		const auto href = msg.find(L"href");
		if (href != msg.end())
			OpenLink(href->second);
	}
	else if (type->second == L"gotoLine")
	{
		const auto line = msg.find(L"line");
		if (line != msg.end())
			GotoLine(_wtoi(line->second.c_str()));
	}
}

void PreviewPanel::PostJson(const std::wstring& json)
{
	if (_webview && _pageReady)
		_webview->PostWebMessageAsJson(json.c_str());
}

// ---------------------------------------------------------------------------
// Document -> preview
// ---------------------------------------------------------------------------

HWND PreviewPanel::CurrentScintilla() const
{
	int which = 0;
	SendMessageW(_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
	return which == 1 ? _npp._scintillaSecondHandle : _npp._scintillaMainHandle;
}

int PreviewPanel::FirstVisibleDocLine() const
{
	const HWND sci = CurrentScintilla();
	const auto visible = SendMessageW(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
	return static_cast<int>(SendMessageW(sci, SCI_DOCLINEFROMVISIBLE, visible, 0));
}

std::wstring PreviewPanel::CurrentFilePath() const
{
	wchar_t path[MAX_PATH * 4] = {};
	SendMessageW(_npp._nppHandle, NPPM_GETFULLCURRENTPATH, std::size(path), reinterpret_cast<LPARAM>(path));
	return path;
}

bool PreviewPanel::ShouldPreview(const std::wstring& path) const
{
	return _settings.previewAllFiles || _settings.IsMarkdownExtension(path);
}

bool PreviewPanel::IsDarkTheme() const
{
	switch (_settings.theme)
	{
	case ThemeMode::Light: return false;
	case ThemeMode::Dark: return true;
	default: return SendMessageW(_npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
	}
}

std::wstring PreviewPanel::DocumentBaseUrl(const std::wstring& filePath) const
{
	const DWORD attrs = GetFileAttributesW(filePath.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
		return {}; // unsaved document: no folder to resolve relative URLs against
	std::wstring folder = filePath;
	PathRemoveFileSpecW(folder.data());
	folder.resize(wcslen(folder.c_str()));
	return kDocOrigin + PathToUrlPath(folder) + L"/";
}

// Serves images/media referenced by the document (https://doc.grin-markdown.local/C:/dir/img.png).
// Only media types are served, so a document cannot pull arbitrary local files into the page.
void PreviewPanel::OnWebResourceRequested(ICoreWebView2WebResourceRequestedEventArgs* args)
{
	ICoreWebView2WebResourceRequest* request = nullptr;
	if (FAILED(args->get_Request(&request)) || !request)
		return;
	LPWSTR uri = nullptr;
	request->get_Uri(&uri);
	request->Release();
	if (!uri)
		return;
	const std::wstring url = uri;
	CoTaskMemFree(uri);

	int status = 404;
	const wchar_t* reason = L"Not Found";
	std::wstring headers = L"Cache-Control: no-cache";
	IStream* stream = nullptr;

	const std::wstring path = UrlPathToPath(url.substr(wcslen(kDocOrigin)));
	const wchar_t* mime = path.empty() ? nullptr : MediaTypeFor(path);
	if (mime && SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream)))
	{
		status = 200;
		reason = L"OK";
		headers += L"\r\nContent-Type: ";
		headers += mime;
	}

	ICoreWebView2WebResourceResponse* response = nullptr;
	if (_env && SUCCEEDED(_env->CreateWebResourceResponse(stream, status, reason, headers.c_str(), &response)) && response)
	{
		args->put_Response(response);
		response->Release();
	}
	if (stream)
		stream->Release();
}

void PreviewPanel::ScheduleUpdate()
{
	if (!_visible || !_hwnd)
		return;
	SetTimer(_hwnd, kUpdateTimer, static_cast<UINT>(std::max(_settings.updateDelayMs, 10)), nullptr);
}

void PreviewPanel::UpdateNow()
{
	if (_hwnd)
		KillTimer(_hwnd, kUpdateTimer);
	if (!_visible || !_pageReady || !_webview)
		return;

	const std::wstring path = CurrentFilePath();
	const wchar_t* theme = IsDarkTheme() ? L"dark" : L"light";

	if (!ShouldPreview(path))
	{
		std::wstring json = L"{\"type\":\"placeholder\",\"theme\":\"";
		json += theme;
		json += L"\",\"text\":";
		AppendJsonString(json, L"Not a Markdown file. Enable “Preview Non-Markdown Files” in the plugin menu to preview it anyway.");
		json += L'}';
		PostJson(json);
		return;
	}

	const HWND sci = CurrentScintilla();
	const auto length = static_cast<int>(SendMessageW(sci, SCI_GETLENGTH, 0, 0));
	const char* text = reinterpret_cast<const char*>(SendMessageW(sci, SCI_GETCHARACTERPOINTER, 0, 0));
	const UINT codePage = SendMessageW(sci, SCI_GETCODEPAGE, 0, 0) == SC_CP_UTF8 ? CP_UTF8 : CP_ACP;

	std::wstring wide;
	if (text && length > 0)
	{
		const int wlen = MultiByteToWideChar(codePage, 0, text, length, nullptr, 0);
		wide.resize(wlen);
		MultiByteToWideChar(codePage, 0, text, length, wide.data(), wlen);
	}

	std::wstring json;
	json.reserve(wide.size() + wide.size() / 8 + 256);
	json += L"{\"type\":\"render\",\"theme\":\"";
	json += theme;
	json += L"\",\"docBase\":";
	AppendJsonString(json, DocumentBaseUrl(path));
	if (!_pendingAnchor.empty())
	{
		json += L",\"anchor\":";
		AppendJsonString(json, _pendingAnchor);
		_pendingAnchor.clear();
	}
	else if (_settings.syncScroll)
	{
		_lastScrollLine = FirstVisibleDocLine();
		json += L",\"line\":" + std::to_wstring(_lastScrollLine);
	}
	json += L",\"text\":";
	AppendJsonString(json, wide);
	json += L'}';
	PostJson(json);
}

void PreviewPanel::OnEditorScrolled()
{
	if (!_visible || !_pageReady || !_settings.syncScroll)
		return;
	const int line = FirstVisibleDocLine();
	if (line == _lastScrollLine)
		return;
	_lastScrollLine = line;
	PostJson(L"{\"type\":\"scroll\",\"line\":" + std::to_wstring(line) + L"}");
}

void PreviewPanel::OnThemeChanged()
{
	UpdateBackground();
	PostJson(std::wstring(L"{\"type\":\"theme\",\"theme\":\"") + (IsDarkTheme() ? L"dark" : L"light") + L"\"}");
}

void PreviewPanel::UpdateBackground()
{
	const COLORREF color = IsDarkTheme() ? kDarkBackground : kLightBackground;
	if (_bgBrush)
		DeleteObject(_bgBrush);
	_bgBrush = CreateSolidBrush(color);
	if (_hwnd)
		InvalidateRect(_hwnd, nullptr, TRUE);

	ICoreWebView2Controller2* controller2 = nullptr;
	if (_controller && SUCCEEDED(_controller->QueryInterface(__uuidof(ICoreWebView2Controller2), reinterpret_cast<void**>(&controller2))) && controller2)
	{
		controller2->put_DefaultBackgroundColor({ 255, GetRValue(color), GetGValue(color), GetBValue(color) });
		controller2->Release();
	}
}

// ---------------------------------------------------------------------------
// Preview -> Notepad++
// ---------------------------------------------------------------------------

void PreviewPanel::GotoLine(int line)
{
	const HWND sci = CurrentScintilla();
	SendMessageW(sci, SCI_ENSUREVISIBLEENFORCEPOLICY, line, 0);
	SendMessageW(sci, SCI_GOTOLINE, line, 0);
	SetFocus(sci);
}

void PreviewPanel::OpenLink(const std::wstring& href)
{
	const std::wstring lower = ToLower(href);
	if (StartsWith(lower, L"http://") || StartsWith(lower, L"https://") || StartsWith(lower, L"mailto:"))
	{
		ShellExecuteW(_npp._nppHandle, L"open", href.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	std::wstring path;
	std::wstring fragment;
	if (StartsWith(lower, L"file:"))
	{
		wchar_t buf[MAX_PATH * 4];
		DWORD len = static_cast<DWORD>(std::size(buf));
		if (FAILED(PathCreateFromUrlW(href.c_str(), buf, &len, 0)))
			return;
		path = buf;
	}
	else
	{
		// Any other scheme (javascript:, data:, ms-settings:, ...) is ignored.
		const size_t colon = href.find(L':');
		const size_t slash = href.find_first_of(L"/\\");
		if (colon != std::wstring::npos && colon > 1 && (slash == std::wstring::npos || colon < slash))
			return;

		const size_t hash = href.find(L'#');
		if (hash != std::wstring::npos)
			fragment = PercentDecodeUtf8(href.substr(hash + 1));
		std::wstring rel = href.substr(0, href.find_first_of(L"?#"));
		if (rel.empty())
			return;
		rel = PercentDecodeUtf8(rel);
		std::replace(rel.begin(), rel.end(), L'/', L'\\');

		if (PathIsRelativeW(rel.c_str()))
		{
			std::wstring base = CurrentFilePath();
			if (GetFileAttributesW(base.c_str()) == INVALID_FILE_ATTRIBUTES)
				return; // unsaved document: nothing to resolve against
			PathRemoveFileSpecW(base.data());
			base.resize(wcslen(base.c_str()));
			if (rel[0] == L'\\')
			{
				// Root-relative ("/docs/x.md"): resolve against the drive/share root.
				PathStripToRootW(base.data());
				base.resize(wcslen(base.c_str()));
				rel.erase(0, 1);
			}
			path = base + L"\\" + rel;
		}
		else
		{
			path = rel;
		}
	}

	wchar_t full[MAX_PATH * 4];
	const DWORD len = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
	if (len == 0 || len >= std::size(full))
		return;
	path = full;

	const DWORD attrs = GetFileAttributesW(path.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES)
	{
		MessageBoxW(_npp._nppHandle, (L"File not found:\n" + path).c_str(), kPanelTitle, MB_OK | MB_ICONINFORMATION);
		return;
	}
	if (attrs & FILE_ATTRIBUTE_DIRECTORY)
	{
		ShellExecuteW(_npp._nppHandle, L"explore", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	else if (IsBinaryDocument(path))
	{
		// Never run or open binaries straight from a document link; reveal them instead.
		const std::wstring args = L"/select,\"" + path + L"\"";
		ShellExecuteW(_npp._nppHandle, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	}
	else
	{
		// Opening the file activates its buffer, which triggers the render that consumes the anchor.
		_pendingAnchor = fragment;
		SendMessageW(_npp._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str()));
		_pendingAnchor.clear();
	}
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

void PreviewPanel::ResizeWebView()
{
	if (!_controller || !_hwnd)
		return;
	RECT rc;
	GetClientRect(_hwnd, &rc);
	_controller->put_Bounds(rc);
}

LRESULT CALLBACK PreviewPanel::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PreviewPanel* self;
	if (msg == WM_NCCREATE)
	{
		self = static_cast<PreviewPanel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		self->_hwnd = hwnd;
	}
	else
	{
		self = reinterpret_cast<PreviewPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}
	return self ? self->HandleMessage(hwnd, msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PreviewPanel::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_NCDESTROY:
		SetWindowLongPtrW(_hwnd, GWLP_USERDATA, 0);
		_hwnd = nullptr;
		return DefWindowProcW(hwnd, msg, wParam, lParam);

	case WM_SIZE:
		ResizeWebView();
		return 0;

	case WM_MOVE:
	case WM_WINDOWPOSCHANGED:
		if (_controller)
			_controller->NotifyParentWindowPositionChanged();
		break;

	case WM_SETFOCUS:
		if (_controller)
			_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
		return 0;

	case WM_TIMER:
		if (wParam == kUpdateTimer)
		{
			UpdateNow();
			return 0;
		}
		break;

	case WM_ERASEBKGND:
	{
		RECT rc;
		GetClientRect(_hwnd, &rc);
		FillRect(reinterpret_cast<HDC>(wParam), &rc, _bgBrush ? _bgBrush : GetSysColorBrush(COLOR_WINDOW));
		return 1;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(_hwnd, &ps);
		if (!_errorText.empty())
		{
			RECT rc;
			GetClientRect(_hwnd, &rc);
			InflateRect(&rc, -16, -16);
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, IsDarkTheme() ? RGB(0xe6, 0xed, 0xf3) : RGB(0x1f, 0x23, 0x28));
			HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
			DrawTextW(dc, _errorText.c_str(), -1, &rc, DT_WORDBREAK | DT_NOPREFIX);
			SelectObject(dc, oldFont);
		}
		EndPaint(_hwnd, &ps);
		return 0;
	}

	case WM_NOTIFY:
	{
		const auto* hdr = reinterpret_cast<const NMHDR*>(lParam);
		if (LOWORD(hdr->code) == DMN_CLOSE)
		{
			_visible = false;
			KillTimer(_hwnd, kUpdateTimer);
			if (_controller)
				_controller->put_IsVisible(FALSE);
			if (onClosed)
				onClosed();
			return TRUE;
		}
		break;
	}
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}
