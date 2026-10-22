// SPDX-License-Identifier: GPL-3.0-or-later
// GRIN-Markdown is free software under the GNU GPL v3 or later; see LICENSE.

// Minimal COM callback object for WebView2 completion/event handlers, so the
// plugin does not depend on WRL or WIL.
#pragma once

#include <windows.h>
#include <functional>
#include <utility>

template <typename TInterface, typename... TArgs>
class ComHandler final : public TInterface
{
public:
	using Fn = std::function<HRESULT(TArgs...)>;

	static TInterface* Create(Fn fn) { return new ComHandler(std::move(fn)); }

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv)
			return E_POINTER;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(TInterface))
		{
			*ppv = static_cast<TInterface*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&_refs); }

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG refs = InterlockedDecrement(&_refs);
		if (refs == 0)
			delete this;
		return refs;
	}

	HRESULT STDMETHODCALLTYPE Invoke(TArgs... args) override { return _fn(args...); }

private:
	explicit ComHandler(Fn fn) : _fn(std::move(fn)) {}

	LONG _refs = 1;
	Fn _fn;
};

// Takes ownership of the initial reference after handing the handler to an API.
template <typename TInterface, typename... TArgs>
class HandlerRef
{
public:
	explicit HandlerRef(typename ComHandler<TInterface, TArgs...>::Fn fn)
		: _p(ComHandler<TInterface, TArgs...>::Create(std::move(fn))) {}
	~HandlerRef() { _p->Release(); }
	HandlerRef(const HandlerRef&) = delete;
	HandlerRef& operator=(const HandlerRef&) = delete;
	operator TInterface*() const { return _p; }

private:
	TInterface* _p;
};
