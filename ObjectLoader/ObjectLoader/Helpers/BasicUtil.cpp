#include "BasicUtil.h"
#include "../Managers/TextureManager.h"
#include <shobjidl_core.h>

std::wstring BasicUtil::GetCroppedName(const WCHAR* filename)
{
	//making pretty name
	std::wstringstream ss(filename);
	std::vector<std::wstring> chunks;
	std::wstring chunk;

	while (std::getline(ss, chunk, L'\\'))
	{
		chunks.push_back(chunk);
	}

	const std::wstring name = chunks[chunks.size() - 1];
	return name.substr(0, name.size() - 4);
}

std::string BasicUtil::TrimName(const std::string& name, int border)
{
	return name.size() > border ? name.substr(0, 12) + "..." : name;
}

bool BasicUtil::TryToOpenFile(const WCHAR* extension1, const WCHAR* extension2, PWSTR& filePath)
{
	IFileOpenDialog* pFileOpen;

	// Create the FileOpenDialog object.
	ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)));

	const COMDLG_FILTERSPEC rgSpec[] = { extension1, extension2 };

	//filter only for .obj files
	ThrowIfFailed(pFileOpen->SetFileTypes(1, rgSpec));

	// Show the Open dialog box.
	const HRESULT hr = pFileOpen->Show(nullptr);
	if (FAILED(hr))
	{
		if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
		{
			// User closed the dialog manually, just return safely
			return false;
		}
		else
		{
			// Handle other errors
			ThrowIfFailed(hr);
		}
	}

	// Get the file name from the dialog box.
	IShellItem* pItem;
	ThrowIfFailed(pFileOpen->GetResult(&pItem));
	PWSTR pszFilePath;
	ThrowIfFailed(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath));

	filePath = pszFilePath;

	pItem->Release();
	pFileOpen->Release();

	return true;
}

void BasicUtil::ChangeTextureState(ID3D12GraphicsCommandList4* cmdList, RtvSrvTexture& texture,
                                   const D3D12_RESOURCE_STATES newState)
{
	if (texture.PrevState == newState)
		return;

	const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture.Resource.Get(),
			texture.PrevState,
			newState
		);
	cmdList->ResourceBarrier(1, &barrier);

	texture.PrevState = newState;
}

std::string BasicUtil::WStringToUtf8(const std::wstring& wstr)
{
	if (wstr.empty()) return {};

	int size = WideCharToMultiByte(
		CP_UTF8,
		0,
		wstr.data(),
		static_cast<int>(wstr.size()),
		nullptr,
		0,
		nullptr,
		nullptr
	);

	std::string result(size, 0);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		wstr.data(),
		static_cast<int>(wstr.size()),
		const_cast<LPSTR>(result.data()),
		size,
		nullptr,
		nullptr
	);

	return result;
}

