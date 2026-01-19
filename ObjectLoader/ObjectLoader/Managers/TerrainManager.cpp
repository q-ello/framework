#include "TerrainManager.h"
#include "../../../Common/GBuffer.h"

TerrainManager::TerrainManager(ID3D12Device* device) : _visibleGrids(0)
{
	_device = device;
}

void TerrainManager::Init()
{
	BuildInputLayout();
	BuildRootSignature();
	BuildShaders();
	BuildPso();

	//allocating indices for heightmap and diffuse textures
	const auto& allocator = TextureManager::SrvHeapAllocator.get();
	_heightmapTexture.Index = allocator->Allocate();
	
	for (auto& texture : _textures)
	{
		texture.texture.Index = allocator->Allocate();
	}
}

void TerrainManager::Draw(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrameResource)
{
	if (_heightmapTexture.UseTexture == false)
		return;

	FrustumCulling(currFrameResource);
	if (_visibleGrids == 0)
		return;

	cmdList->SetPipelineState(_pso.Get());
	cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	const D3D12_GPU_DESCRIPTOR_HANDLE heightMapTex(TextureManager::SrvHeapAllocator->GetGpuHandle(_heightmapTexture.Index));
	cmdList->SetGraphicsRootDescriptorTable(0, heightMapTex);
	cmdList->SetGraphicsRootConstantBufferView(1, currFrameResource->TerrainCb->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootConstantBufferView(2, currFrameResource->GBufferPassCb->Resource()->GetGPUVirtualAddress());
	cmdList->SetGraphicsRootShaderResourceView(3, currFrameResource->GridInfoCb->Resource()->GetGPUVirtualAddress());
	const D3D12_GPU_DESCRIPTOR_HANDLE terrainTex(TextureManager::SrvHeapAllocator->GetGpuHandle(_textures->texture.Index));
	cmdList->SetGraphicsRootDescriptorTable(4, terrainTex);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->SetGraphicsRootConstantBufferView(5, currFrameResource->TerrainTexturesCb->Resource()->GetGPUVirtualAddress());

	const auto& geo = GeometryManager::Geometries()["shapeGeo"].begin()->get();

	const auto vertexBuffer = geo->VertexBufferView();
	cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);
	const auto indexBuffer = geo->IndexBufferView();
	cmdList->IASetIndexBuffer(&indexBuffer);

	const SubmeshGeometry gridSubmesh = geo->DrawArgs["terrainGrid"];
	cmdList->DrawIndexedInstanced(gridSubmesh.IndexCount, _visibleGrids, gridSubmesh.StartIndexLocation, gridSubmesh.BaseVertexLocation, 0);
}

void TerrainManager::BindToOtherData(Camera* camera)
{
	_camera = camera;
}

void TerrainManager::InitTerrain()
{
	const auto& texDesc = TextureManager::Textures()[std::wstring(_heightmapTexture.Name.begin(), _heightmapTexture.Name.end())]
		->Resource->GetDesc();
	if (texDesc.Width == _heightmapTextureWidth && texDesc.Height == _heightmapTextureHeight)
		return;

	_heightmapTextureWidth =static_cast<int>(texDesc.Width);
	_heightmapTextureHeight = texDesc.Height;

	_grids.clear();

	const int gridsHorizontalNum = _heightmapTextureWidth / ((_gridSize - 1) * 8);
	const int gridsVerticalNum = _heightmapTextureHeight / ((_gridSize - 1) * 8);

	for (int i = 0; i < gridsHorizontalNum; i++)
	{
		for (int j = 0; j < gridsVerticalNum; j++)
		{
			const int xTexelStart = i * (_gridSize - 1) * 8;
			const int yTexelStart = j * (_gridSize - 1) * 8;
			_grids.push_back(std::move(CreateQuadNode(xTexelStart, yTexelStart, (float)(xTexelStart - _heightmapTextureWidth * 0.5f), 
				(float)(yTexelStart - _heightmapTextureHeight * 0.5f), 8)));
		}
	}
}

void TerrainManager::UpdateTerrainCb(const FrameResource* currFrameResource)
{
	if (_numFramesDirty <= 0)
		return;

	{
		const auto& terrainCb = currFrameResource->TerrainCb;

		TerrainConstants terrainConstants;
		terrainConstants.HeightmapSize = { _heightmapTextureWidth, _heightmapTextureHeight };
		terrainConstants.MaxHeight = MaxTerrainHeight;
		terrainConstants.GridSize = _gridSize;
		terrainConstants.HeightThreshold = HeightThreshold;
		terrainConstants.SlopeThreshold = SlopeThreshold;

		terrainCb->CopyData(0, terrainConstants);
	
	}
	
	{
		const auto& terrainTexturesCb = currFrameResource->TerrainTexturesCb;
		TerrainTextures terrainTexturesConstants;

		const MaterialProperty lowProperty = _textures[BasicUtil::EnumIndex(TerrainTexture::Low)];
		terrainTexturesConstants.LowColor = lowProperty.value;
		terrainTexturesConstants.UseLowTexture = lowProperty.texture.UseTexture;
		
		const MaterialProperty slopeProperty = _textures[BasicUtil::EnumIndex(TerrainTexture::Slope)];
		terrainTexturesConstants.SlopeColor = slopeProperty.value;
		terrainTexturesConstants.UseSlopeTexture = slopeProperty.texture.UseTexture;
		
		const MaterialProperty highProperty = _textures[BasicUtil::EnumIndex(TerrainTexture::High)];
		terrainTexturesConstants.HighColor = highProperty.value;
		terrainTexturesConstants.UseHighTexture = highProperty.texture.UseTexture;
		
		terrainTexturesCb->CopyData(0, terrainTexturesConstants);
	}
	
	_numFramesDirty--;
}

MaterialProperty* TerrainManager::TerrainTexture(const int index)
{
	return &_textures[index];
}

void TerrainManager::SetDirty()
{
	_numFramesDirty = gNumFrameResources;
}

void TerrainManager::BuildInputLayout()
{
	_inputLayout = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
}

void TerrainManager::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 1);
	CD3DX12_DESCRIPTOR_RANGE texturesTable;
	texturesTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, static_cast<UINT>(BasicUtil::EnumIndex(TerrainTexture::Count)), 1, 1);

	constexpr int slotRootParameterCount = 6;
	
	CD3DX12_ROOT_PARAMETER slotRootParameter[slotRootParameterCount];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsShaderResourceView(0);
	slotRootParameter[4].InitAsDescriptorTable(1, &texturesTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[5].InitAsConstantBufferView(2);
	const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(slotRootParameterCount, slotRootParameter,
	                                        static_cast<UINT>(TextureManager::GetLinearSamplers().size()),
	                                        TextureManager::GetLinearSamplers().data(),
	                                        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
	if (errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
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

void TerrainManager::BuildPso()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { _inputLayout.data(), static_cast<UINT>(_inputLayout.size()) };
	psoDesc.pRootSignature = _rootSignature.Get();
	psoDesc.VS = { static_cast<BYTE*>(_vsShader->GetBufferPointer()), _vsShader->GetBufferSize() };
	psoDesc.PS = { static_cast<BYTE*>(_psShader->GetBufferPointer()), _psShader->GetBufferSize() };
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

std::unique_ptr<GridQuadNode> TerrainManager::CreateQuadNode(const int xTexelStart, const int yTexelStart, float worldOffsetX, float worldOffsetY, int stride)
{
	auto node = std::make_unique<GridQuadNode>();
	const DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(static_cast<float>(stride), 1.0f, static_cast<float>(stride));

	const int texelXStart = xTexelStart;
	const int texelYStart = yTexelStart;

	const DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(worldOffsetX, 0.f, worldOffsetY);
	const DirectX::XMMATRIX world = scale * translation;
	constexpr auto aabb = BoundingBox({ 0.f, 0.f, 0.f }, { 16.f, 20.0f, 16.f });
	aabb.Transform(node->Aabb, world);
	node->Info.TexelStride = stride;
	node->Info.TexelXStart = texelXStart;
	node->Info.TexelYStart = texelYStart;
	node->Info.World = XMMatrixTranspose(world);

	if (stride != 1)
	{
		const int halfStride = stride / 2;
		const int offset = (_gridSize - 1) * halfStride;
		const float worldOffset = static_cast<float>(_gridSize * halfStride) * 0.5f;
		worldOffsetX -= worldOffset;
		worldOffsetY -= worldOffset;
		for (int i = 0; i < 4; i++)
		{
			const int childTexelXStart = texelXStart + (i % 2) * offset;
			const int childTexelYStart = texelYStart + (i / 2) * offset;
			const float childWorldOffsetX = worldOffsetX + static_cast<float>((i % 2) * offset);
			const float childWorldOffsetY = worldOffsetY + static_cast<float>((i / 2) * offset);
			node->Children[i] = CreateQuadNode(childTexelXStart, childTexelYStart, childWorldOffsetX, childWorldOffsetY, halfStride);
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
	const ContainmentType ct = localFrustum.Contains(grid->Aabb);
	if (ct == CONTAINS)
	{
		AddQuadToVisible(grid, currFrameResource);
	}
	else if (ct == INTERSECTS)
	{
		if (grid->Children[0] == nullptr)
		{
			AddQuadToVisible(grid, currFrameResource);
		}
		else
		{
			for (const auto& child : grid->Children)
			{
				CullQuad(child.get(), localFrustum, currFrameResource);
			}
		}
	}
}

void TerrainManager::AddQuadToVisible(GridQuadNode* grid, FrameResource* currFrameResource)
{
	if (grid->Children[0] == nullptr)
	{
		currFrameResource->GridInfoCb->CopyData(_visibleGrids++, grid->Info);
		return;
	}

	const float distX = grid->Aabb.Center.x - _camera->GetPosition3F().x;
	const float distY = grid->Aabb.Center.y - _camera->GetPosition3F().y;
	const float distZ = grid->Aabb.Center.z - _camera->GetPosition3F().z;

	const float distSquare = distX * distX + distY * distY + distZ * distZ;

	float lodDist = static_cast<float>(grid->Info.TexelStride * _gridSize * 2);
	lodDist *= lodDist;

	if (distSquare > lodDist)
	{
		currFrameResource->GridInfoCb->CopyData(_visibleGrids++, grid->Info);
		return;
	}
	else
	{
		for (const auto& child : grid->Children)
		{
			AddQuadToVisible(child.get(), currFrameResource);
		}
	}
}
