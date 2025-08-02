#pragma once
#include "Windows.h"
#include <string>
#include <vector>
#include <sstream>
#include <ShObjIdl.h>
#include "../../Common/d3dUtil.h"

class BasicUtil
{
public:
	//helper with strings
	static std::wstring getCroppedName(const WCHAR* filename);
	static std::string trimName(const std::string& name, int border);
	static bool TryToOpenFile(WCHAR* extension1, WCHAR* extension2, PWSTR& filePath);

	//helper with enums
	template<typename E>
	constexpr static size_t EnumIndex(E e) { return static_cast<size_t>(e); }
};