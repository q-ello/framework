#include "ObjectManager.h"
using namespace DirectX;
using namespace Microsoft::WRL;

ObjectManager::ObjectManager(ID3D12Device5* device)
	: _uidCount{ 0 }
	, _device {device}
{
}

void ObjectManager::Init()
{
	BuildRootSignature();
	BuildShaders();
	BuildInputLayout();
	BuildPso();
}