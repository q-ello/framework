#pragma once
#include "Windows.h"
#include <string>
#include <vector>
#include <sstream>

class BasicUtil
{
public:
	//helper with strings
	static std::wstring getCroppedName(WCHAR* filename);
	static std::string trimName(const std::string& name, int border);
};