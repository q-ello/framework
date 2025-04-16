#include "MyApp.h"
#include "Model.h"
#include <shobjidl.h> 
#include <sstream>
#include <commctrl.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_dx12.h"
#include "imgui/backends/imgui_impl_win32.h"

#pragma comment(lib, "ComCtl32.lib")

WNDPROC g_OriginalPanelWndProc;

MyApp::MyApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

MyApp::~MyApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

bool MyApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	//initializing upload stuff
	OutputDebugString(L"Starting to initialize upload data...\n");
	md3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_uploadCmdAlloc));
	md3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _uploadCmdAlloc.Get(), nullptr, IID_PPV_ARGS(&_uploadCmdList));
	md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_uploadFence));
	_uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	ThrowIfFailed(_uploadCmdList->Close());
	OutputDebugString(L"Upload data initialized.\n");

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	buildGridGeometry();
	BuildMaterials();
	BuildModelGeometry();
	buildGrid();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

	ImGui::StyleColorsDark();

	// Setup Platform/Renderer backends
	ImGui_ImplDX12_InitInfo info;
	info.CommandQueue = mCommandQueue.Get();
	info.Device = md3dDevice.Get();
	info.NumFramesInFlight = gNumFrameResources;
	info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	info.SrvDescriptorHeap = _imGuiDescriptorHeap.Get();
	info.LegacySingleSrvCpuDescriptor = _imGuiDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart();
	info.LegacySingleSrvGpuDescriptor = _imGuiDescriptorHeap.Get()->GetGPUDescriptorHandleForHeapStart();
	
	ImGui_ImplDX12_Init(&info);
	ImGui_ImplWin32_Init(mhMainWnd);

	RAWINPUTDEVICE rid;
	rid.usUsagePage = 1;
	rid.usUsage = 6; // Keyboard
	rid.dwFlags = RIDEV_INPUTSINK;
	rid.hwndTarget = mhMainWnd;
	RegisterRawInputDevices(&rid, 1, sizeof(rid));

	return true;
}

void MyApp::OnResize()
{
	//resizing the render window
	OnResizing();
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void MyApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void MyApp::Draw(const GameTimer& gt)
{
	// (Your code process and dispatch Win32 messages)
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	DrawInterface();

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	for (int i = 0; i < static_cast<int>(PSO::Count); i++)
	{
		PSO type = static_cast<PSO>(i);
		mCommandList->SetPipelineState(_psos[type].Get());
		DrawRenderItems(mCommandList.Get(), _renderItems[type]);
	}

	descriptorHeaps[0] = _imGuiDescriptorHeap.Get();
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// Rendering
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void MyApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;
	SetCapture(mhMainWnd);
}

void MyApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void MyApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	ImGuiIO& io = ImGui::GetIO();
	if ((btnState & MK_LBUTTON) != 0 && !io.WantCaptureMouse)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0 && !io.WantCaptureMouse)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void MyApp::OnKeyboardInput(const GameTimer& gt)
{
}

void MyApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void MyApp::AnimateMaterials(const GameTimer& gt)
{

}

void MyApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto& currObjectsCB = mCurrFrameResource->ObjectsCB;
	for (int i = 0; i < _allRenderItems.size(); i++)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		auto& ri = _allRenderItems[i];
		if (ri->NumFramesDirty > 0)
		{
			XMMATRIX scale = XMMatrixScaling(ri->transform[2][0], ri->transform[2][1], ri->transform[2][2]);
			XMMATRIX rotation = XMMatrixRotationRollPitchYaw(ri->transform[1][0] * XM_PI / 180.,
				ri->transform[1][1] * XM_PI / 180.,
				ri->transform[1][2] * XM_PI / 180.);
			XMMATRIX translation = XMMatrixTranslation(ri->transform[0][0], ri->transform[0][1], ri->transform[0][2]);

			XMMATRIX world = scale * rotation * translation;
			XMStoreFloat4x4(&ri->World, world);
			XMMATRIX texTransform = XMLoadFloat4x4(&ri->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectsCB[ri->uid].get()->CopyData(ri->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			ri->NumFramesDirty--;
		}
	}
}

void MyApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void MyApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void MyApp::LoadTextures()
{
	auto defaultTex = std::make_unique<Texture>();
	defaultTex->SetName(L"defaultTex");
	//defaultTex->Name = L"defaultTex";
	defaultTex->Filename = L"../../Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), defaultTex->Filename.c_str(),
		defaultTex->Resource, defaultTex->UploadHeap));

	mTextures[defaultTex->Name] = std::move(defaultTex);
}

TextureHandle MyApp::LoadTexture(WCHAR* filename, int prevIndex)
{
	std::wstring croppedName = getCroppedName(filename);

	if (mTextures.find(croppedName) != mTextures.end())
	{
		if (_texIndices[croppedName] != prevIndex)
		{
			_texUsed[croppedName]++;
		}
		return { std::string(croppedName.begin(), croppedName.end()), _texIndices[croppedName], true};
	}

	auto tex = std::make_unique<Texture>();
	tex->Name = croppedName;
	tex->Filename = filename;

	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		_uploadCmdList.Get(), tex->Filename.c_str(),
		tex->Resource, tex->UploadHeap));

	UINT index = _srvHeapAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = _srvHeapAllocator.get()->GetCpuHandle(index);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = tex.get()->Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = tex.get()->Resource.Get()->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	md3dDevice->CreateShaderResourceView(tex.get()->Resource.Get(), &srvDesc, srvHandle);

	ExecuteUploadCommandList();

	mTextures[croppedName] = std::move(tex);
	_texIndices[croppedName] = index;
	_texUsed[croppedName] = 1;

	return { std::string(croppedName.begin(), croppedName.end()), index, true };
}

void MyApp::deleteTexture(std::wstring name)
{
	_texUsed[name]--;
	if (_texUsed[name] == 0)
	{
		FlushCommandQueue();
		mTextures[name].release();
		mTextures.erase(name);
		_texUsed.erase(name);
		_srvHeapAllocator->Free(_texIndices[name]);
		_texIndices.erase(name);
	}
}

void MyApp::BuildRootSignature()
{
	//3rd exercise
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	//texTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void MyApp::BuildDescriptorHeaps()
{
	 //Create the SRV heap.
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 100;

	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	_srvHeapAllocator = std::make_unique<DescriptorHeapAllocator>(mSrvDescriptorHeap.Get(), mCbvSrvUavDescriptorSize, srvHeapDesc.NumDescriptors);

	//add default texture
	UINT index = _srvHeapAllocator.get()->Allocate();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = _srvHeapAllocator.get()->GetCpuHandle(index);
	auto defaultTex = mTextures[L"defaultTex"]->Resource;
	_texIndices[L"defaultTex"] = index;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = defaultTex.Get()->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = defaultTex.Get()->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	md3dDevice->CreateShaderResourceView(defaultTex.Get(), &srvDesc, srvHandle);

	//imgui heap
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1; // Adjust based on the number of UI textures
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_imGuiDescriptorHeap)));
}

void MyApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");
	mShaders["gridPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PSGrid", "ps_5_0");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void MyApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = 0;
	boxSubmesh.BaseVertexLocation = 0;


	std::vector<Vertex> vertices(box.Vertices.size());

	for (size_t i = 0; i < box.Vertices.size(); ++i)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices = box.GetIndices16();

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = L"boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs[L"box"] = boxSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyApp::buildGridGeometry()
{
	ThrowIfFailed(_uploadCmdList->Reset(_uploadCmdAlloc.Get(), nullptr));

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.f, 20.f, 0.1f);

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = 0;
	gridSubmesh.BaseVertexLocation = 0;


	std::vector<Vertex> vertices(grid.Vertices.size());

	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		vertices[i].Pos = grid.Vertices[i].Position;
		vertices[i].Color = grid.Vertices[i].Color;
	}

	std::vector<std::uint16_t> indices = grid.GetIndices16();

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = L"gridGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	OutputDebugString(L"Creating grid vertex buffer...\n");
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		_uploadCmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
	OutputDebugString(L"Grid vertex buffer created.\n");

	OutputDebugString(L"Creating grid index buffer...\n");
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		_uploadCmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);
	OutputDebugString(L"Grid index buffer created.\n");

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs[L"grid"] = gridSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyApp::BuildModelGeometry(WCHAR* filename)
{
	std::wstring croppedName = getCroppedName(filename);

	//check if geometry already exists
	if (mGeometries.find(croppedName) != mGeometries.end())
	{
		addRenderItem(croppedName);
		return;
	}

	//load new model
	std::unique_ptr<Model> model = std::make_unique<Model>(filename);

	//veryfying that model does actually have some data
	if (model->vertices().empty() || model->indices().empty())
	{
		OutputDebugString(L"[ERROR] Model data is empty! Aborting geometry creation.\n");
		return;
	}

	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)model->vertices().size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)model->indices().size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();

	geo->Name = croppedName;

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), model->vertices().data(), vbByteSize);


	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), model->indices().data(), ibByteSize);

	OutputDebugString(L"Creating model vertex buffer...\n");
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		_uploadCmdList.Get(), model->vertices().data(), vbByteSize, geo->VertexBufferUploader);
	OutputDebugString(L"Model vertex buffer created.\n");

	OutputDebugString(L"Creating model index buffer...\n");
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		_uploadCmdList.Get(), model->indices().data(), ibByteSize, geo->IndexBufferUploader);
	OutputDebugString(L"Model index buffer created.\n");

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)model->indices().size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs[croppedName] = submesh;

	mGeometries[geo->Name] = std::move(geo);

	addRenderItem(croppedName);

	ExecuteUploadCommandList();
}

void MyApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&_psos[PSO::Opaque])));

	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["gridPS"]->GetBufferPointer()),
		mShaders["gridPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&_psos[PSO::Grid])));

}

void MyApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)_allRenderItems.size(), (UINT)mMaterials.size()));
		for (auto& item : _allRenderItems)
		{
			mFrameResources[i]->addObjectBuffer(md3dDevice.Get(), item->uid);
		}
	}
}

void MyApp::BuildMaterials()
{
	auto woodCrate = std::make_unique<Material>();
	woodCrate->Name = "woodCrate";
	woodCrate->MatCBIndex = 0;
	woodCrate->DiffuseSrvHeapIndex = 0;
	woodCrate->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	woodCrate->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	woodCrate->Roughness = 0.2f;

	mMaterials["woodCrate"] = std::move(woodCrate);

	//3rd exercise
	auto flare = std::make_unique<Material>();
	flare->Name = "flare";
	flare->MatCBIndex = 0;
	flare->DiffuseSrvHeapIndex = 1;
	flare->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	flare->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	flare->Roughness = 0.99f;
	mMaterials["flare"] = std::move(flare);
}

void MyApp::buildGrid()
{
	auto gridRItem = std::make_unique<RenderItem>();
	gridRItem->uid = uidCount++;
	gridRItem->Name = "grid";
	gridRItem->ObjCBIndex = 0;
	gridRItem->Mat = mMaterials["woodCrate"].get();
	gridRItem->Geo = mGeometries[L"gridGeo"].get();
	gridRItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	gridRItem->IndexCount = gridRItem->Geo->DrawArgs[L"grid"].IndexCount;
	gridRItem->StartIndexLocation = gridRItem->Geo->DrawArgs[L"grid"].StartIndexLocation;
	gridRItem->BaseVertexLocation = gridRItem->Geo->DrawArgs[L"grid"].BaseVertexLocation;
	gridRItem->diffuseHandle.index = 0;
	gridRItem->type = PSO::Grid;
	_renderItems[PSO::Grid].push_back(gridRItem.get());
	_allRenderItems.push_back(std::move(gridRItem));
}

void MyApp::DrawInterface()
{
	int buttonId = 0;

	//debug window
	ImGui::Begin("Debug info");
	ImGui::Text(("african texture used:" + std::to_string(_texUsed[L"african_head_diffuse"])).c_str());
	ImGui::End();

	//objects window
	ImGui::SetNextWindowPos({ 0.f, 0.f }, ImGuiCond_Once, { 0.f, 0.f });
	ImGui::SetNextWindowSize({ 200.f, 500.f }, ImGuiCond_Once);

	ImGui::Begin("Objects");                          // Create a window called "Hello, world!" and append into it.
	if (ImGui::CollapsingHeader("Opaque Objects", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Button("Add New"))
		{
			PWSTR pszFilePath;
			if (TryToOpenFile(L"3D Object", L"*.obj;*.fbx", pszFilePath))
			{
				BuildModelGeometry(pszFilePath);
				CoTaskMemFree(pszFilePath);
			}
		}

		ImGui::Spacing();

		for (int i = 0; i < _renderItems[PSO::Opaque].size(); i++)
		{
			ImGui::PushID(buttonId++);
			std::string name = trimName(_renderItems[PSO::Opaque][i]->Name + 
				(_renderItems[PSO::Opaque][i]->nameCount == 0 ? "" : std::to_string(_renderItems[PSO::Opaque][i]->nameCount)), 15);
			if (ImGui::Button(name.c_str()))
			{
				_selectedObject = i;
				_selectedType = PSO::Opaque;
			}
			ImGui::SameLine();
			ImGui::PopID();
			ImGui::PushID(buttonId++);
			if (ImGui::Button("delete"))
			{
				_selectedObject = i;
				_selectedType = PSO::Opaque;
				const std::string diffName = _renderItems[_selectedType][_selectedObject]->diffuseHandle.name;
				deleteTexture(std::wstring(diffName.begin(), diffName.end()));
				deleteObject();
			}
			ImGui::PopID();
		}
	}
	ImGui::End();

	//object info

	if (_selectedObject != -1)
	{
		ImGui::SetNextWindowPos({ static_cast<float>(mClientWidth) - 300.f, 50.f }, ImGuiCond_Once, { 0.f, 0.f });
		ImGui::SetNextWindowSize({ 250.f, 350.f }, ImGuiCond_Once);

		ImGui::Begin((_renderItems[_selectedType][_selectedObject]->Name + " Info").c_str());                          // Create a window called "Hello, world!" and append into it.
		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Location: ");
			ImGui::SameLine();

			ImGui::PushID(buttonId++);
			if (ImGui::InputFloat3("", _renderItems[_selectedType][_selectedObject]->transform[0]))
			{
				_renderItems[_selectedType][_selectedObject]->NumFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();

			ImGui::Text("Rotation: ");
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			if (ImGui::InputFloat3("", _renderItems[_selectedType][_selectedObject]->transform[1]))
			{
				_renderItems[_selectedType][_selectedObject]->NumFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();

			ImGui::Text("Scale:");
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			ImGui::Checkbox("", &_renderItems[_selectedType][_selectedObject]->lockedScale);
			ImGui::SameLine();
			ImGui::PopID();

			float before[3] = { _renderItems[_selectedType][_selectedObject]->transform[2][0], 
				_renderItems[_selectedType][_selectedObject]->transform[2][1], 
				_renderItems[_selectedType][_selectedObject]->transform[2][2] };

			ImGui::PushID(buttonId++);
			if (ImGui::InputFloat3("", _renderItems[_selectedType][_selectedObject]->transform[2]))
			{
				if (_renderItems[_selectedType][_selectedObject]->lockedScale)
				{
					float difference = 1.f;

					for (int i = 0; i < 3; i++)
					{

						difference *= _renderItems[_selectedType][_selectedObject]->transform[2][i] / before[i];
					}

					for (int i = 0; i < 3; i++)
					{
						_renderItems[_selectedType][_selectedObject]->transform[2][i] = before[i] * difference;
					}
				}
				_renderItems[_selectedType][_selectedObject]->NumFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();

		}

		//textures
		if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Diffuse");
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			std::string name = trimName(_renderItems[_selectedType][_selectedObject]->diffuseHandle.name, 15);
			if (ImGui::Button(name.c_str()))
			{
				WCHAR* texturePath;
				if (TryToOpenFile(L"DDS Textures", L"*.dds", texturePath))
				{
					_renderItems[_selectedType][_selectedObject]->diffuseHandle 
						= LoadTexture(texturePath, _renderItems[_selectedType][_selectedObject]->diffuseHandle.index);
					CoTaskMemFree(texturePath);
				}
			}
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			if (ImGui::Button("delete") && name != "load")
			{
				const std::string diffName = _renderItems[_selectedType][_selectedObject]->diffuseHandle.name;
				deleteTexture(std::wstring(diffName.begin(), diffName.end()));
				_renderItems[_selectedType][_selectedObject]->diffuseHandle = TextureHandle();
			}
			ImGui::PopID();

			ImGui::Text("Specular");
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			ImGui::Button(_renderItems[_selectedType][_selectedObject]->specularHandle.name.c_str());
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			ImGui::Button("delete");
			ImGui::PopID();

			ImGui::Text("Normal");
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			ImGui::Button(_renderItems[_selectedType][_selectedObject]->normalHandle.name.c_str());
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID(buttonId++);
			ImGui::Button("delete");
			ImGui::PopID();
		}


		ImGui::End();
	}
}

void MyApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		auto objectCB = mCurrFrameResource->ObjectsCB[ri->uid]->Resource();

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseTex(_srvHeapAllocator.get()->GetGpuHandle(ri->diffuseHandle.index));
		cmdList->SetGraphicsRootDescriptorTable(0, diffuseTex);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void MyApp::ExecuteUploadCommandList()
{
	OutputDebugString(L"Executing upload command list");
	ThrowIfFailed(_uploadCmdList->Close());
	ID3D12CommandList* cmds[] = { _uploadCmdList.Get() };
	mCommandQueue->ExecuteCommandLists(1, cmds);

	_uploadFenceValue++;
	mCommandQueue->Signal(_uploadFence.Get(), _uploadFenceValue);
	if (_uploadFence->GetCompletedValue() < _uploadFenceValue)
	{
		_uploadFence->SetEventOnCompletion(_uploadFenceValue, _uploadFenceEvent);
		WaitForSingleObject(_uploadFenceEvent, INFINITE);
	}

	 //Reset upload allocator + list for next use
	ThrowIfFailed(_uploadCmdAlloc->Reset());
	ThrowIfFailed(_uploadCmdList->Reset(_uploadCmdAlloc.Get(), nullptr));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 8> MyApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearBorder(
		4, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		6, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC linearMirror(
		7, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressW
		0.0f,                              // mipLODBias
		8);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp, linearBorder,
		anisotropicWrap, anisotropicClamp, linearMirror };
}

std::wstring MyApp::getCroppedName(WCHAR* filename)
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

std::string MyApp::trimName(const std::string& name, int border) const
{
	return name.size() > border ? name.substr(0, 12) + "..." : name;
}

void MyApp::UnloadModel(const std::wstring& modelName)
{
	if (mGeometries.find(modelName) != mGeometries.end())
	{
		mGeometries.erase(modelName);
	}
}

void MyApp::addRenderItem(const std::wstring& itemName)
{
	std::string name(itemName.begin(), itemName.end());

	auto modelRitem = std::make_unique<RenderItem>();
	_objectLoaded[itemName]++;
	modelRitem->uid = uidCount++;
	modelRitem->Name = name;
	modelRitem->nameCount = _objectCounters[itemName]++;
	modelRitem->ObjCBIndex = 0;
	modelRitem->Mat = mMaterials["woodCrate"].get();
	modelRitem->Geo = mGeometries[itemName].get();
	modelRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	modelRitem->IndexCount = modelRitem->Geo->DrawArgs[itemName].IndexCount;
	modelRitem->StartIndexLocation = modelRitem->Geo->DrawArgs[itemName].StartIndexLocation;
	modelRitem->BaseVertexLocation = modelRitem->Geo->DrawArgs[itemName].BaseVertexLocation;

	modelRitem->diffuseHandle.index = _texIndices[L"defaultTex"];

	for (int i = 0; i < mFrameResources.size(); ++i)
	{
		mFrameResources[i]->addObjectBuffer(md3dDevice.Get(), modelRitem->uid);
	}

	_renderItems[modelRitem->type].push_back(modelRitem.get());
	_allRenderItems.push_back(std::move(modelRitem));
}

void MyApp::deleteObject()
{
	std::wstring name(_renderItems[_selectedType][_selectedObject]->Name.begin(), _renderItems[_selectedType][_selectedObject]->Name.end());
	_objectLoaded[name]--;

	//need for deleting from frame resource
	std::uint32_t uid = _renderItems[_selectedType][_selectedObject]->uid;

	//finding index in the general array
	size_t index = -1;
	for (size_t i = 0; i < _allRenderItems.size(); i++)
	{
		if (_allRenderItems[i]->uid == uid)
		{
			index = i;
			break;
		}
	}

	if (index != -1)
	{
		_allRenderItems.erase(_allRenderItems.begin() + index);
	}
	_renderItems[_selectedType].erase(_renderItems[_selectedType].begin() + _selectedObject);

	FlushCommandQueue();

	if (_objectLoaded[name] == 0)
	{
		UnloadModel(name);
		_objectLoaded.erase(name);

		ThrowIfFailed(mDirectCmdListAlloc->Reset());
		ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

		mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
		mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
		FlushCommandQueue();
	}

	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources[i]->removeObjectBuffer(md3dDevice.Get(), uid);
	}

	_selectedObject = -1;
}

bool MyApp::TryToOpenFile(WCHAR* extension1, WCHAR* extension2, PWSTR& filePath)
{
	IFileOpenDialog* pFileOpen;

	// Create the FileOpenDialog object.
	ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)));

	COMDLG_FILTERSPEC rgSpec[] = { extension1, extension2 };

	//filter only for .obj files
	pFileOpen->SetFileTypes(1, rgSpec);

	// Show the Open dialog box.
	HRESULT hr = pFileOpen->Show(NULL);
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

//some wndproc stuff
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool MyApp::checkForImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}