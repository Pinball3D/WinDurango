﻿#pragma once
#include <winrt/Windows.ApplicationModel.h>

#include "utils.h"
#include "CoreApplicationWrapperX.h"
#include <windows.applicationmodel.core.h>

/* This function is used to compare the class name of the classId with the classIdName. */
inline bool IsClassName(HSTRING classId, const char* classIdName)
{
	const wchar_t* classIdString = WindowsGetStringRawBuffer(classId, nullptr);
	std::wstring classIdWString(classIdString);
	const std::string classIdStringUTF8(classIdWString.begin(), classIdWString.end());

	return (classIdStringUTF8 == classIdName);
}

/* Function pointers for the DllGetForCurrentThread */
typedef HRESULT(*DllGetForCurrentThreadFunc) (ICoreWindowStatic*, CoreWindow**);
/* Function pointers for the DllGetForCurrentThread */
DllGetForCurrentThreadFunc pDllGetForCurrentThread = nullptr;
/* Function pointers for the DllGetForCurrentThread */
HRESULT(STDMETHODCALLTYPE* TrueGetForCurrentThread)(ICoreWindowStatic* staticWindow, CoreWindow** window);
/* Function pointers for the DllGetActivationFactory */
typedef HRESULT(*DllGetActivationFactoryFunc) (HSTRING, IActivationFactory**);
/* Function pointers for the DllGetActivationFactory */
DllGetActivationFactoryFunc pDllGetActivationFactory = nullptr;
/* Function pointers for the WinRT RoGetActivationFactory function. */
HRESULT(WINAPI* TrueRoGetActivationFactory)(HSTRING classId, REFIID iid, void** factory) = RoGetActivationFactory;

/* Function pointers for filesystem APIs */
HFILE(WINAPI* TrueOpenFile)(LPCSTR lpFileName, LPOFSTRUCT lpReOpenBuff, UINT uStyle) = OpenFile;
HANDLE(WINAPI* TrueCreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) = CreateFileW;

DWORD(WINAPI* TrueGetFileAttributesW)(LPCWSTR lpFileName) = GetFileAttributesW;
BOOL(WINAPI* TrueGetFileAttributesExW)(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) = GetFileAttributesExW;

HANDLE(WINAPI* TrueFindFirstFileW)(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) = FindFirstFileW;

BOOL(WINAPI* TrueDeleteFileW)(LPCWSTR lpFileName) = DeleteFileW;


// Hooks for filesystem APIs
void FixRelativePath(LPCWSTR& lpFileName)
{
	static std::wstring convert{};
	std::wstring_view fileName(lpFileName);

	if (fileName.size() == 0)
		return;

	if (fileName[1] != ':')
	{
		convert = std::filesystem::current_path().c_str();
		convert.append(L"\\");
		convert.append(fileName);

		lpFileName = convert.data();
	}
}

HFILE WINAPI OpenFile_Hook(LPCSTR lpFileName, LPOFSTRUCT lpReOpenBuff, UINT uStyle)
{
	//FixRelativePath(lpFileName);

	return TrueOpenFile(lpFileName, lpReOpenBuff, uStyle);
}

HANDLE WINAPI CreateFileW_Hook(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	FixRelativePath(lpFileName);

	return TrueCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

DWORD WINAPI GetFileAttributesW_Hook(LPCWSTR lpFileName)
{
	FixRelativePath(lpFileName);

	return TrueGetFileAttributesW(lpFileName);
}

BOOL WINAPI GetFileAttributesExW_Hook(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
	FixRelativePath(lpFileName);

	return TrueGetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}


HANDLE WINAPI FindFirstFileW_Hook(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
	FixRelativePath(lpFileName);

	return TrueFindFirstFileW(lpFileName, lpFindFileData);
}

BOOL WINAPI DeleteFileW_Hook(LPCWSTR lpFileName)
{
	FixRelativePath(lpFileName);

	return TrueDeleteFileW(lpFileName);
}

// The hook function for GetForCurrentThread
HRESULT STDMETHODCALLTYPE GetForCurrentThread_Hook(ICoreWindowStatic* pThis, CoreWindow** ppWindow)
{
	HRESULT hr = TrueGetForCurrentThread(pThis, ppWindow);
	if (FAILED(hr))
	{
		return hr;
	}
	
	if (*ppWindow == NULL)
		return hr;



	if (IsXboxCallee())
		*reinterpret_cast<ICoreWindowX**>(ppWindow) = new CoreWindowWrapperX(*ppWindow);


	return hr;
}


/* Hook for the WinRT RoGetActivationFactory function. */
inline HRESULT WINAPI RoGetActivationFactory_Hook(HSTRING classId, REFIID iid, void** factory)
{
	// Get the raw buffer from the HSTRING
	const wchar_t* rawString = WindowsGetStringRawBuffer(classId, nullptr);

	if (rawString) {
		// Print the string using wprintf
		wprintf(L"%ls\n", rawString);
	}
	auto hr = 0;
	
	if (IsClassName(classId, "Windows.ApplicationModel.Core.CoreApplication"))
	{
		ComPtr<IActivationFactory> realFactory;

		hr = TrueRoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(), IID_PPV_ARGS(&realFactory));

		if (FAILED(hr))
			return hr;

		ComPtr<CoreApplicationWrapperX> wrappedFactory = Make<CoreApplicationWrapperX>(realFactory);
		
		return wrappedFactory.CopyTo(iid, factory);
	}
	else if (IsClassName(classId, "Windows.UI.Core.CoreWindow"))
	{
		//
		// for now we just hook GetForCurrentThread to get the CoreWindow but i'll change it later to
		// wrap ICoreWindowStatic or as zombie said another thing that works is by hooking IFrameworkView::SetWindow
		// but for now this *should* work just fine -AleBlbl
		//
		ComPtr<ICoreWindowStatic> coreWindowStatic;
		hr = TrueRoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Core_CoreWindow).Get(), IID_PPV_ARGS(&coreWindowStatic));
		if (FAILED(hr)) {
			return hr;
		}

		if (!TrueGetForCurrentThread)
		{
			*reinterpret_cast<void**>(&TrueGetForCurrentThread) = (*reinterpret_cast<void***>(coreWindowStatic.Get()))[6];

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
			DetourAttach(&TrueGetForCurrentThread, GetForCurrentThread_Hook);
			DetourTransactionCommit();
		}

		return coreWindowStatic.CopyTo(iid, factory);
	}

	// After WinDurango overrides try to load the rest

	hr = TrueRoGetActivationFactory(classId, iid, factory);

	if (FAILED(hr))
	{
		if (!pDllGetActivationFactory)
		{
			auto library = LoadPackagedLibrary(L"winrt_x.dll", 0);

			if (!library) library = LoadLibraryW(L"winrt_x.dll");

			if (!library) return hr;

			pDllGetActivationFactory = reinterpret_cast<DllGetActivationFactoryFunc>
				(GetProcAddress(library, "DllGetActivationFactory"));

			if (!pDllGetActivationFactory)
				return hr;
		}


		// fallback
		ComPtr<IActivationFactory> fallbackFactory;
		hr = pDllGetActivationFactory(classId, fallbackFactory.GetAddressOf());

		if (FAILED(hr))
			return hr;

		return fallbackFactory.CopyTo(iid, factory);
	}

	return hr;
}