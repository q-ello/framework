#include "ObjectManager.h"
using namespace DirectX;
using namespace Microsoft::WRL;

ObjectManager::ObjectManager(ID3D12Device* device)
	: _device {device}
	, uidCount{ 0 }
{
}

ObjectManager::~ObjectManager()
{
}

void ObjectManager::Init()
{
	BuildRootSignature();
	BuildShaders();
	BuildInputLayout();
	BuildPSO();
}