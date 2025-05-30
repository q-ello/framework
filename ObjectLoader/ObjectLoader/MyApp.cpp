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

	UploadManager::InitUploadCmdList(md3dDevice.Get(), mCommandQueue);

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	InitManagers();
	BuildRootSignatures();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	GeometryManager::BuildNecessaryGeometry();
	_selectedType = PSO::Opaque;
	_selectedObject = _objectManagers[PSO::Opaque]->addRenderItem(md3dDevice.Get(), GeometryManager::BuildModelGeometry());
	_objectManagers[PSO::Unlit]->addRenderItem(md3dDevice.Get(), L"grid");
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
	mCurrFrameResource = FrameResource::frameResources()[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCBs(gt);
	_lightingManager->UpdateLightCBs(mCurrFrameResource);
}

void MyApp::Draw(const GameTimer& gt)
{
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

	GBufferPass();

	LightingPass();

	//drawing grid
	_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &_gBuffer->DepthStencilView());
	_objectManagers[PSO::Unlit]->Draw(mCommandList.Get(), mCurrFrameResource);

	//ImGui draw
	ID3D12DescriptorHeap* descriptorHeaps[] = {_imGuiDescriptorHeap.Get()};
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	//// Rendering
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
	if (GetAsyncKeyState('E') & 0x8000)
	{
		//since view matrix is transponed I'm taking z-column
		auto lightDir = _lightingManager->mainLightDirection();
		lightDir[0] = mView._13;
		lightDir[1] = mView._23;
		lightDir[2] = mView._33;
	}
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


void MyApp::UpdateObjectCBs(const GameTimer& gt)
{
	for (int i = 0; i < (int)PSO::Count; i++)
	{
		PSO type = (PSO)i;
		_objectManagers[type]->UpdateObjectCBs(mCurrFrameResource);
	}
}

void MyApp::UpdateMainPassCBs(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&_GBufferCB.ViewProj, XMMatrixTranspose(viewProj));
	_GBufferCB.DeltaTime = gt.DeltaTime();
	auto currGBufferCB = mCurrFrameResource->GBufferPassCB.get();
	currGBufferCB->CopyData(0, _GBufferCB);

	XMStoreFloat4x4(&_lightingCB.InvViewProj, XMMatrixTranspose(invViewProj));
	_lightingCB.EyePosW = mEyePos;
	XMStoreFloat4x4(&_lightingCB.ViewProj, XMMatrixTranspose(viewProj));
	_lightingCB.RTSize = { (float)mClientWidth, (float)mClientHeight };

	auto currLightingCB = mCurrFrameResource->LightingPassCB.get();
	currLightingCB->CopyData(0, _lightingCB);

	_lightingManager->UpdateDirectionalLightCB(mCurrFrameResource);
}

void MyApp::BuildRootSignatures()
{
	
}

void MyApp::BuildDescriptorHeaps()
{
	//imgui heap
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1; // Adjust based on the number of UI textures
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_imGuiDescriptorHeap)));
}

void MyApp::BuildShadersAndInputLayout()
{
}

void MyApp::BuildPSOs()
{
	
}

void MyApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		FrameResource::frameResources().push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1));
		for (int j = 0; j < (int)PSO::Count; j++)
		{
			PSO type = (PSO)j;
			_objectManagers[type]->AddObjectToResource(md3dDevice, FrameResource::frameResources()[i].get());
		}
	}
}

void MyApp::DrawInterface()
{
	int* buttonId = new int(0);

	ImGui::SetNextWindowPos({ 0.f, 0.f }, ImGuiCond_Once, { 0.f, 0.f });
	ImGui::SetNextWindowSize({ 250.f, 500.f }, ImGuiCond_Once);

	ImGui::Begin("Data");
	ImGui::BeginTabBar("#data");
	
	if (ImGui::BeginTabItem("Objects"))
	{
		DrawObjectsList(buttonId);
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Lights"))
	{
		DrawLightData(buttonId);
		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
	ImGui::End();


	//object info

	if (_selectedObject != -1)
	{
		DrawObjectInfo(buttonId);
	}
	delete buttonId;
}

void MyApp::DrawObjectsList(int* btnId)
{
	if (ImGui::CollapsingHeader("Opaque Objects", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Button("Add New"))
		{
			PWSTR pszFilePath;
			if (BasicUtil::TryToOpenFile(L"3D Object", L"*.obj;*.fbx", pszFilePath))
			{
				_selectedType = PSO::Opaque;
				_selectedObject = _objectManagers[PSO::Opaque]->addRenderItem(md3dDevice.Get(), GeometryManager::BuildModelGeometry(pszFilePath));
				CoTaskMemFree(pszFilePath);
			}
		}

		ImGui::Spacing();

		for (int i = 0; i < _objectManagers[PSO::Opaque]->objectsCount(); i++)
		{
			ImGui::PushID((*btnId)++);

			std::string name = BasicUtil::trimName(_objectManagers[PSO::Opaque]->objectName(i), 15);
			if (ImGui::Button(name.c_str()))
			{
				_selectedObject = i;
				_selectedType = PSO::Opaque;
			}
			ImGui::PopID();

			if (ImGui::BeginPopupContextItem())
			{
				ImGui::PushID((*btnId)++);
				if (ImGui::Button("delete"))
				{
					_selectedObject = i;
					_selectedType = PSO::Opaque;
					if (_objectManagers[PSO::Opaque]->deleteObject(_selectedObject))
					{
						ClearData();
					}
					_selectedObject = -1;
				}
				ImGui::PopID();
				ImGui::EndPopup();
			}
			
		}
	}
}

void MyApp::DrawLightData(int* btnId)
{
	if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Turn on", _lightingManager->isMainLightOn());

		ImGui::Text("Direction");
		ImGui::SameLine();
		ImGui::PushID((*btnId)++);
		ImGui::DragFloat3("", _lightingManager->mainLightDirection(), 0.1f);
		ImGui::PopID();

		ImVec4 color;

		ImGui::Text("ColorEdit");
		ImGui::SameLine();
		ImGui::PushID((*btnId)++);
		ImGui::ColorEdit3("", _lightingManager->mainLightColor());
		ImGui::PopID();
	}
	if (ImGui::CollapsingHeader("Local lights"))
	{
		ImGui::Checkbox("Debug", _lightingManager->debugEnabled());
		if (ImGui::Button("Add light"))
		{
			_lightingManager->addLight(md3dDevice.Get());
		}
		
		for (int i = 0; i < _lightingManager->lightsCount(); i++)
		{
			DrawLocalLightData(btnId, i);
		}
	}
}

void MyApp::DrawLocalLightData(int* btnId, int lightIndex)
{
	if (ImGui::CollapsingHeader(("Light " + std::to_string(lightIndex)).c_str()))
	{
		if (ImGui::BeginPopupContextItem())
		{
			ImGui::PushID((*btnId)++);
			if (ImGui::Button("delete"))
			{
				_lightingManager->deleteLight(lightIndex);
			}
			ImGui::PopID();
			ImGui::EndPopup();
			return;
		}
		auto light = _lightingManager->light(lightIndex);

		ImGui::PushID((*btnId)++);
		if (ImGui::RadioButton("Point Light", light->LightData.type == 0) && light->LightData.type != 0)
		{
			light->LightData.type = 0;
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		ImGui::PushID((*btnId)++);
		if (ImGui::RadioButton("Spotlight", light->LightData.type == 1) && light->LightData.type != 1)
		{
			light->LightData.type = 1;
			_lightingManager->UpdateWorld(lightIndex);
		}
		bool lightEnabled = light->LightData.active == 1;
		ImGui::PopID();
		ImGui::PushID((*btnId)++);
		if (ImGui::Checkbox("Enabled", &lightEnabled))
		{
			light->LightData.active = (int)lightEnabled;
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID((*btnId)++);
		if (ImGui::DragFloat3("Position", &light->LightData.position.x, 0.1f))
		{
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		ImGui::PushID((*btnId)++);
		if (ImGui::ColorEdit3("Color", &light->LightData.color.x))
		{
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID((*btnId)++);
		if (ImGui::DragFloat("Intensity", &light->LightData.intensity, 0.1f, 0.0f, 10.0f))
		{
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID((*btnId)++);
		if (ImGui::DragFloat("Radius", &light->LightData.radius, 0.1f, 0.0f, 10.0f))
		{
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		if (light->LightData.type == 1)
		{
			ImGui::PushID((*btnId)++);
			if (ImGui::DragFloat3("Direction", &light->LightData.direction.x, 0.1f))
			{
				_lightingManager->UpdateWorld(lightIndex);
			}
			ImGui::PopID();
			ImGui::PushID((*btnId)++);
			if (ImGui::SliderAngle("Angle", &light->LightData.angle, 1.0f, 90.0f))
			{
				_lightingManager->UpdateWorld(lightIndex);
			}
			ImGui::PopID();
		}
	}
}

void MyApp::DrawObjectInfo(int* btnId)
{

	ImGui::SetNextWindowPos({ static_cast<float>(mClientWidth) - 300.f, 50.f }, ImGuiCond_Once, { 0.f, 0.f });
	ImGui::SetNextWindowSize({ 250.f, 350.f }, ImGuiCond_Once);

	RenderItem* selectedObject = _objectManagers[_selectedType]->object(_selectedObject);

	ImGui::Begin((selectedObject->Name + " Info").c_str());
	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawObjectTransform(selectedObject, btnId);
	}

	//textures
	if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawObjectTextures(selectedObject, btnId);
	}

	ImGui::End();
}

void MyApp::DrawObjectTransform(RenderItem* selectedObject, int* btnId)
{
	DrawTransformInput("Location: ", (*btnId)++, 0, selectedObject, 0.1f);
	DrawTransformInput("Rotation: ", (*btnId)++, 1, selectedObject, 1.f);

	ImGui::Text("Scale:");
	ImGui::SameLine();
	ImGui::PushID((*btnId)++);
	ImGui::Checkbox("", &selectedObject->lockedScale);
	ImGui::SameLine();
	ImGui::PopID();

	DirectX::XMFLOAT3 before = selectedObject->transform[2];

	ImGui::PushID((*btnId)++);
	if (ImGui::DragFloat3("", &selectedObject->transform[2].x, 0.1f))
	{
		if (selectedObject->lockedScale)
		{
			DirectX::XMFLOAT3 after = selectedObject->transform[2];
			float difference = 1.f;

			float ratio = 1.0f;
			int changedAxis = -1;
			if (after.x != before.x && after.x != 0) { ratio = after.x / before.x; changedAxis = 0; }
			else if (after.y != before.y && after.y != 0) { ratio = after.y / before.y; changedAxis = 1; }
			else if (after.z != before.z && after.z != 0) { ratio = after.z / before.z; changedAxis = 2; }

			if (changedAxis != -1)
			{
				after.x = before.x * ratio;
				after.y = before.y * ratio;
				after.z = before.z * ratio;
			}
			else
			{
				after.x = before.x;
				after.y = before.y;
				after.z = before.z;
			}
			selectedObject->transform[2] = after;
		}
		selectedObject->NumFramesDirty = gNumFrameResources;
	}
	ImGui::PopID();
}

void MyApp::DrawObjectTextures(RenderItem* selectedObject, int* btnId)
{
	//diffuse button
	DrawTextureButton("Diffuse", btnId, selectedObject->diffuseHandle);
	if (DrawTextureButton("Normal", btnId, selectedObject->normalHandle))
	{
		selectedObject->NumFramesDirty = gNumFrameResources;
	}
}

bool MyApp::DrawTextureButton(const std::string& label, int* btnId, TextureHandle& texHandle)
{
	ImGui::Text(label.c_str());
	ImGui::SameLine();
	ImGui::PushID((*btnId)++);
	std::string name = BasicUtil::trimName(texHandle.name, 15);
	if (ImGui::Button(name.c_str()))
	{
		WCHAR* texturePath;
		if (BasicUtil::TryToOpenFile(L"DDS Textures", L"*.dds", texturePath))
		{
			texHandle
				= TextureManager::LoadTexture(texturePath, texHandle.index);
			CoTaskMemFree(texturePath);
			ImGui::PopID();
			return true;
		}
	}
	ImGui::PopID();
	if (ImGui::BeginPopupContextItem())
	{
		ImGui::PushID((*btnId)++);
		if (ImGui::Button("delete") && texHandle.isRelevant == true)
		{
			const std::string texName = texHandle.name;
			TextureManager::deleteTexture(std::wstring(texName.begin(), texName.end()));
			texHandle = TextureHandle();
			ImGui::PopID();
			ImGui::EndPopup();
			return true;
		}
		ImGui::PopID();
		ImGui::EndPopup();
	}
	return false;
}

void MyApp::DrawTransformInput(const std::string& label, int btnId, int transformIndex, RenderItem* object, float speed)
{
	ImGui::Text(label.c_str());
	ImGui::SameLine();

	ImGui::PushID(btnId);
	if (ImGui::DragFloat3("", &object->transform[transformIndex].x, speed))
	{
		object->NumFramesDirty = gNumFrameResources;
	}
	ImGui::PopID();
}

void MyApp::ClearData()
{

	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	_gBuffer->ClearInfo(Colors::Transparent);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();
}

void MyApp::InitManagers()
{
	TextureManager::Init(md3dDevice.Get());
	_objectManagers[PSO::Opaque] = std::make_unique<OpaqueObjectManager>();
	_objectManagers[PSO::Opaque]->Init();
	_objectManagers[PSO::Unlit] = std::make_unique<UnlitObjectManager>();
	_objectManagers[PSO::Unlit]->Init();
	
	_lightingManager = std::make_unique<LightingManager>();
	_lightingManager->Init(_gBuffer->InfoCount(false), md3dDevice.Get(), _gBuffer->lightingHandle());
}

void MyApp::GBufferPass()
{
	//deferred rendering: writing in gbuffer first
	_gBuffer->ChangeRTVsState(D3D12_RESOURCE_STATE_RENDER_TARGET);
	_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	
	_gBuffer->ClearInfo(Colors::Transparent);
	mCommandList->OMSetRenderTargets(_gBuffer->InfoCount(), _gBuffer->RTVs().data(),
		false, &_gBuffer->DepthStencilView());
	_objectManagers[PSO::Opaque]->Draw(mCommandList.Get(), mCurrFrameResource);
}

void MyApp::LightingPass()
{
	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	_gBuffer->ChangeRTVsState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_READ);

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &_gBuffer->DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { _gBuffer->SRVHeap() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	_lightingManager->DrawDirLight(mCommandList.Get(), mCurrFrameResource, _gBuffer->SRVHeap()->GetGPUDescriptorHandleForHeapStart());

	if (_lightingManager->lightsCount() > 0)
	{
		_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_READ);
		_lightingManager->DrawLocalLights(mCommandList.Get(), mCurrFrameResource);

		if (*_lightingManager->debugEnabled())
		{
			_lightingManager->DrawDebug(mCommandList.Get(), mCurrFrameResource);
		}
	}

	
}

//some wndproc stuff
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool MyApp::checkForImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}