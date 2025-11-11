#include "TerrainManager.h"

TerrainManager::TerrainManager(ID3D12Device* device)
{
	_device = device;
}

TerrainManager::~TerrainManager()
{
}

void TerrainManager::Init()
{
	BuildInputLayout();
	BuildRootSignature();
	BuildShaders();
	BuildPSO();

	//allocating indices for heightmap and diffuse textures
	const auto& allocator = TextureManager::srvHeapAllocator.get();
	_heightmapTexture.index = allocator->Allocate();
	_diffuseTexture.index = allocator->Allocate();
}

void TerrainManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	if (_heightmapTexture.useTexture == false)
		return;

	FrustumCulling(currFrameResource);
	if (_visibleGrids == 0)
		return;

	cmdList->SetPipelineState(_pso.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	D3D12_GPU_DESCRIPTOR_HANDLE heightMapTex(TextureManager::srvHeapAllocator->GetGpuHandle(_heightmapTexture.index));
	cmdList->SetGraphicsRootDescriptorTable(0, heightMapTex);
	cmdList->SetGraphicsRootConstantBufferView(1, currFrameResource->TerrainCB->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootConstantBufferView(2, currFrameResource->GBufferPassCB->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootShaderResourceView(3, currFrameResource->GridInfoCB->Resource()->GetGPUVirtualAddress());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	const auto& geo = GeometryManager::geometries()["shapeGeo"].begin()->get();

	cmdList->IASetVertexBuffers(0, 1, &geo->VertexBufferView());
	cmdList->IASetIndexBuffer(&geo->IndexBufferView());

	SubmeshGeometry gridSubmesh = geo->DrawArgs["terrainGrid"];
	cmdList->DrawIndexedInstanced(gridSubmesh.IndexCount, _visibleGrids, gridSubmesh.StartIndexLocation, gridSubmesh.BaseVertexLocation, 0);
}

void TerrainManager::BindToOtherData(Camera* camera)
{
	_camera = camera;
}

void TerrainManager::InitTerrain()
{
	const auto& texDesc = TextureManager::textures()[std::wstring(_heightmapTexture.name.begin(), _heightmapTexture.name.end())]
		->Resource->GetDesc();
	if (texDesc.Width == _heightmapTextureWidth && texDesc.Height == _heightmapTextureHeight)
		return;

	_heightmapTextureWidth = texDesc.Width;
	_heightmapTextureHeight = texDesc.Height;

	_grids.clear();

	int gridsHorizontalNum = _heightmapTextureWidth / ((_gridSize - 1) * 8);
	int gridsVerticalNum = _heightmapTextureHeight / ((_gridSize - 1) * 8);

	for (int i = 0; i < gridsHorizontalNum; i++)
	{
		for (int j = 0; j < gridsVerticalNum; j++)
		{
			int xTexelStart = i * (_gridSize - 1) * 8;
			int yTexelStart = j * (_gridSize - 1) * 8;
			_grids.push_back(std::move(CreateQuadNode(xTexelStart, yTexelStart, (float)(xTexelStart - _heightmapTextureWidth * 0.5f), 
				(float)(yTexelStart - _heightmapTextureHeight * 0.5f), 8)));
		}
	}
}

void TerrainManager::UpdateTerrainCB(FrameResource* currFrameResource)
{
	const auto& terrainCB = currFrameResource->TerrainCB;

	TerrainConstants terrainConstants;
	terrainConstants.heightmapSize = { _heightmapTextureWidth, _heightmapTextureHeight };
	terrainConstants.maxHeight = maxTerrainHeight;
	terrainConstants.gridSize = _gridSize;

	terrainCB->CopyData(0, terrainConstants);
}

void TerrainManager::BuildInputLayout()
{
	_inputLayout = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
}

void TerrainManager::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsShaderResourceView(0);
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, (UINT)TextureManager::GetLinearSamplers().size(), TextureManager::GetLinearSamplers().data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);
	ThrowIfFailed(_device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignature)));
}

void TerrainManager::BuildShaders()
{
	_vsShader = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "VS", "vs_5_1");
	_psShader = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS", "ps_5_1");
}

void TerrainManager::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	psoDesc.pRootSignature = _rootSignature.Get();
	psoDesc.VS = { reinterpret_cast<BYTE*>(_vsShader->GetBufferPointer()), _vsShader->GetBufferSize() };
	psoDesc.PS = { reinterpret_cast<BYTE*>(_psShader->GetBufferPointer()), _psShader->GetBufferSize() };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = GBuffer::InfoCount();
	for (int i = 0; i < GBuffer::InfoCount(); i++)
	{
		psoDesc.RTVFormats[i] = GBuffer::infoFormats[i];
	}

	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;
	ThrowIfFailed(_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso)));
}

std::unique_ptr<GridQuadNode> TerrainManager::CreateQuadNode(int xTexelStart, int yTexelStart, float worldOffsetX, float worldOffsetY, int stride)
{
	std::unique_ptr<GridQuadNode> node = std::make_unique<GridQuadNode>();
	DirectX::XMMATRIX scale = DirectX::XMMatrixScaling((float)stride, 1.0f, (float)stride);

	int texelXStart = xTexelStart;
	int texelYStart = yTexelStart;

	DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(worldOffsetX, 0.f, worldOffsetY);
	DirectX::XMMATRIX world = scale * translation;
	DirectX::BoundingBox aabb = BoundingBox({ 0.f, 0.f, 0.f }, { 16.f, 20.0f, 16.f });
	aabb.Transform(node->AABB, world);
	node->info.texelStride = stride;
	node->info.texelXStart = texelXStart;
	node->info.texelYStart = texelYStart;
	node->info.world = XMMatrixTranspose(world);

	if (stride != 1)
	{
		int halfStride = stride / 2;
		int offset = (_gridSize - 1) * halfStride;
		worldOffsetX -= (_gridSize * halfStride * 0.5f);
		worldOffsetY -= (_gridSize * halfStride * 0.5f);
		for (int i = 0; i < 4; i++)
		{
			int childTexelXStart = texelXStart + (i % 2) * offset;
			int childTexelYStart = texelYStart + (i / 2) * offset;
			float childWorldOffsetX = worldOffsetX + (i % 2) * offset;
			float childWorldOffsetY = worldOffsetY + (i / 2) * offset;
			node->children[i] = CreateQuadNode(childTexelXStart, childTexelYStart, childWorldOffsetX, childWorldOffsetY, halfStride);
		}
	}

	return std::move(node);
}

void TerrainManager::FrustumCulling(FrameResource* currFrameResource)
{
	_visibleGrids = 0;

	const auto& viewToLocal = _camera->GetInvView();
	BoundingFrustum localFrustum;
	_camera->CameraFrustum().Transform(localFrustum, viewToLocal);

	for (const auto& grid : _grids)
	{
		CullQuad(grid.get(), localFrustum, currFrameResource);
	}
}

void TerrainManager::CullQuad(GridQuadNode* grid, BoundingFrustum& localFrustum, FrameResource* currFrameResource)
{
	ContainmentType ct = localFrustum.Contains(grid->AABB);
	if (ct == CONTAINS)
	{
		AddQuadToVisible(grid, currFrameResource);
	}
	else if (ct == INTERSECTS)
	{
		if (grid->children[0] == nullptr)
		{
			AddQuadToVisible(grid, currFrameResource);
		}
		else
		{
			for (const auto& child : grid->children)
			{
				CullQuad(child.get(), localFrustum, currFrameResource);
			}
		}
	}
}

void TerrainManager::AddQuadToVisible(GridQuadNode* grid, FrameResource* currFrameResource)
{
	if (grid->children[0] == nullptr)
	{
		currFrameResource->GridInfoCB->CopyData(_visibleGrids++, grid->info);
		return;
	}

	float distX = grid->AABB.Center.x - _camera->GetPosition3f().x;
	float distY = grid->AABB.Center.y - _camera->GetPosition3f().y;
	float distZ = grid->AABB.Center.z - _camera->GetPosition3f().z;

	float distSquare = distX * distX + distY * distY + distZ * distZ;

	float lodDist = (float)(grid->info.texelStride * _gridSize * 2);
	lodDist *= lodDist;

	if (distSquare > lodDist)
	{
		currFrameResource->GridInfoCB->CopyData(_visibleGrids++, grid->info);
		return;
	}
	else
	{
		for (const auto& child : grid->children)
		{
			AddQuadToVisible(child.get(), currFrameResource);
		}
	}
}
