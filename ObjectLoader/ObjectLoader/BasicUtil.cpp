#include "BasicUtil.h"

std::wstring BasicUtil::getCroppedName(WCHAR* filename)
{
	//making pretty name
	std::wstringstream ss(filename);
	std::vector<std::wstring> chunks;
	std::wstring chunk;

	while (std::getline(ss, chunk, L'\\'))
	{
		chunks.push_back(chunk);
	}

	std::wstring name = chunks[chunks.size() - 1];
	return name.substr(0, name.size() - 4);
}

std::string BasicUtil::trimName(const std::string& name, int border)
{
	return name.size() > border ? name.substr(0, 12) + "..." : name;
}
