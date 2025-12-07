#pragma once
#include <string>
#include "Windows.h"
#include "../../../Common/d3dUtil.h"

class BasicUtil
{
public:
	//helper with strings
	static std::wstring GetCroppedName(const WCHAR* filename);
	static std::string TrimName(const std::string& name, int border);
	static bool TryToOpenFile(const WCHAR* extension1, const WCHAR* extension2, PWSTR& filePath);

	//helper with enums
	template<typename E>
	constexpr static size_t EnumIndex(E e) { return static_cast<size_t>(e); }
};