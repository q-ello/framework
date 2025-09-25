#include "MyApp.h"
#include "PostProcessManager.h"


#pragma comment(lib, "ComCtl32.lib")

WNDPROC g_OriginalPanelWndProc;

MyApp::MyApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
	, _lastMousePos {0, 0}
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

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	InitManagers();
	BuildRootSignatures();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	GeometryManager::BuildNecessaryGeometry();
	_gridManager->AddRenderItem(md3dDevice.Get(), { "grid" });
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

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 10.f, 2000.f);
	_camera.UpdateFrustum();
}

void MyApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = FrameResource::frameResources()[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		if (eventHandle)
		{
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}
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

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	if (_isWireframe)
	{
		WireframePass();
	}
	else
	{
		_lightingManager->DrawShadows(mCommandList.Get(), mCurrFrameResource, _objectsManager->Objects());
		GBufferPass();
		LightingPass();
	}

	//drawing grid
	_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &_gBuffer->DepthStencilView());
	_gridManager->Draw(mCommandList.Get(), mCurrFrameResource);

	//ImGui draw
	ID3D12DescriptorHeap* descriptorHeaps[] = {_imGuiDescriptorHeap.Get()};
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	//// Rendering
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
	// Swap the back and front buffers
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

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
	_lastMousePos.x = x;
	_lastMousePos.y = y;
	SetCapture(mhMainWnd);
	_mbDown = true;
}

void MyApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
	_mbDown = false;
}

void MyApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	ImGuiIO& io = ImGui::GetIO();
	if ((btnState & MK_LBUTTON) != 0 && !io.WantCaptureMouse)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = 0.001f * static_cast<float>(x - _lastMousePos.x);
		float dy = _cameraSpeed * static_cast<float>(y - _lastMousePos.y);

		_camera.Walk(dy);
		_camera.RotateY(dx);
	}
	else if ((btnState & MK_RBUTTON) != 0 && !io.WantCaptureMouse)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.001f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.001f * static_cast<float>(y - _lastMousePos.y);

		_camera.Pitch(dy);
		_camera.RotateY(dx);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void MyApp::OnMouseWheel(WPARAM btnState)
{
	ImGuiIO& io = ImGui::GetIO();
	if (_mbDown)
	{
		_cameraSpeed += (float)GET_WHEEL_DELTA_WPARAM(btnState)/(float)WHEEL_DELTA * 0.01f;
		_cameraSpeed = MathHelper::Clamp(_cameraSpeed, 0.f, 25.f);
	}
	else if (!io.WantCaptureMouse)
	{
		float direction = ((float)GET_WHEEL_DELTA_WPARAM(btnState) > 0) ? 1.f : -1.f;
		_camera.Walk(direction);
	}
}

void MyApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('E') & 0x8000)
	{
		//since view matrix is transponed I'm taking z-column
		_lightingManager->SetMainLightDirection(_camera.GetLook3f());
	}

	const float dt = gt.DeltaTime();

	//moving camera
	if (_mbDown && GetAsyncKeyState('W') & 0x8000)
	{
		_camera.Walk(_cameraSpeed);
	}
	if (_mbDown && GetAsyncKeyState('A') & 0x8000)
	{
		_camera.Strafe(-_cameraSpeed);
	}
	if (_mbDown && GetAsyncKeyState('S') & 0x8000)
	{
		_camera.Walk(-_cameraSpeed);
	}
	if (_mbDown && GetAsyncKeyState('D') & 0x8000)
	{
		_camera.Strafe(_cameraSpeed);
	}

	_camera.UpdateViewMatrix();
}

void MyApp::UpdateObjectCBs(const GameTimer& gt)
{
	_gridManager->UpdateObjectCBs(mCurrFrameResource);
	_objectsManager->UpdateObjectCBs(mCurrFrameResource);
}

void MyApp::UpdateMainPassCBs(const GameTimer& gt)
{
	//update pass for gbuffer
	XMMATRIX view = _camera.GetView();
	XMMATRIX proj = _camera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&_GBufferCB.ViewProj, XMMatrixTranspose(viewProj));
	_GBufferCB.DeltaTime = gt.DeltaTime();
	_GBufferCB.EyePosW = _camera.GetPosition3f();
	_GBufferCB.ScreenSize = { (float)mClientWidth, (float)mClientHeight };
	auto currGBufferCB = mCurrFrameResource->GBufferPassCB.get();
	currGBufferCB->CopyData(0, _GBufferCB);
	
	//update pass for lighting
	XMStoreFloat4x4(&_lightingCB.InvViewProj, XMMatrixTranspose(invViewProj));
	_lightingCB.EyePosW = _camera.GetPosition3f();
	XMStoreFloat4x4(&_lightingCB.ViewProj, XMMatrixTranspose(viewProj));
	_lightingCB.RTSize = { (float)mClientWidth, (float)mClientHeight };
	_lightingCB.mousePosition = {(float)_lastMousePos.x, (float)_lastMousePos.y};
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

void PostProcessManager::GodRaysPass(ID3D12GraphicsCommandList* cmdList)
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
		_gridManager->AddObjectToResource(md3dDevice, FrameResource::frameResources()[i].get());
		_objectsManager->AddObjectToResource(md3dDevice, FrameResource::frameResources()[i].get());
	}
}

void MyApp::DrawInterface()
{
	int buttonId = 0;

	ImGui::SetNextWindowPos({ 0.f, 0.f }, 0, { 0.f, 0.f });
	ImGui::SetNextWindowSize({ 250.f, (float)mClientHeight});

	ImGui::Begin("Data", 0, ImGuiWindowFlags_NoResize);
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

	if (ImGui::BeginTabItem("Other"))
	{
		ImGui::Checkbox("Wireframe", &_isWireframe);
		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
	ImGui::End();


	//object info

	if (!_selectedModels.empty())
	{
		DrawObjectInfo(buttonId);
	}

	DrawHeader();

	//debug info
	ImGui::Begin("Debug info");
	auto visObjectsCnt = _objectsManager->VisibleObjectsCount();
	auto objectsCnt = _objectsManager->ObjectsCount();
	ImGui::Text(("Objects drawn: " + std::to_string(visObjectsCnt) + "/" + std::to_string(objectsCnt)).c_str());
	auto visLights = _lightingManager->LightsInsideFrustum();
	auto lightsCnt = _lightingManager->LightsCount();
	ImGui::Text(("Lights drawn: " + std::to_string(visLights) + "/" + std::to_string(lightsCnt)).c_str());
	ImGui::End();

	DrawToasts();
}

void MyApp::DrawObjectsList(int& btnId)
{
	ImGui::Checkbox("Draw Debug", _objectsManager->drawDebug());

	if (ImGui::CollapsingHeader("Opaque Objects", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Button("Add New"))
		{
			AddModel();
		}

		// import multiple meshes modal
		DrawImportModal();

		ImGui::Spacing();

		//for shift spacing
		static int lastClicked = -1;

		for (int i = 0; i < _objectsManager->ObjectsCount(); i++)
		{
			ImGui::PushID(btnId++);

			//bool isSelected = _selectedType == PSO::Opaque && i == _selectedObject;
			bool isSelected = _selectedModels.count(i) > 0;

			ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? ImVec4(0.2f, 0.6f, 1.0f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

			std::string name = BasicUtil::trimName(_objectsManager->objectName(i), 15);
			if (ImGui::Button(name.c_str()))
			{
				//logic for multiple selecting
				if (ImGui::GetIO().KeyShift && lastClicked != -1)
				{
					_selectedModels.clear();
					int start = min(lastClicked, i);
					int end = max(lastClicked, i);
					for (int j = start; j <= end; ++j)
						_selectedModels.insert(j);
				}
				else if (ImGui::GetIO().KeyCtrl)
				{
					if (isSelected)
					{
						_selectedModels.erase(i);
					}
					else
					{
						_selectedModels.insert(i);
					}
					lastClicked = i;
				}
				else
				{
					_selectedModels.clear();
					_selectedModels.insert(i);
					lastClicked = i;
				}
			}
			ImGui::PopStyleColor();
			ImGui::PopID();

			if (ImGui::BeginPopupContextItem())
			{
				ImGui::PushID(btnId++);
				if (ImGui::Button("delete"))
				{
					if (_selectedModels.count(i) > 0)
					{
						//going from the most index to last
						for (auto it = _selectedModels.rbegin(); it != _selectedModels.rend(); it++)
						{
							if (_objectsManager->DeleteObject(*it))
							{
								ClearData();
							}
						}
					}
					else
					{
						if (_objectsManager->DeleteObject(i))
						{
							ClearData();
						}
					}
					_selectedModels.clear();
				}
				ImGui::PopID();
				ImGui::EndPopup();
			}
			
		}
	}
}

void MyApp::DrawHandSpotlight(int& btnId)
{
	auto light = _lightingManager->MainSpotlight();

	bool lightEnabled = light->active == 1;
	ImGui::PushID(btnId++);
	if (ImGui::Checkbox("Enabled", &lightEnabled))
	{
		light->active = (int)lightEnabled;
	}
	ImGui::PopID();

	ImGui::PushID(btnId++);
	ImGui::ColorEdit3("Color", &light->color.x);
	ImGui::PopID();

	ImGui::PushID(btnId++);
	ImGui::DragFloat("Intensity", &light->intensity, 0.1f, 0.0f, 10.0f);
	ImGui::PopID();

	ImGui::PushID(btnId++);
	ImGui::DragFloat("Radius", &light->radius, 0.1f, 0.0f, 100.0f);
	ImGui::PopID();

	ImGui::PushID(btnId++);
	ImGui::SliderAngle("Angle", &light->angle, 1.0f, 89.f);
	ImGui::PopID();
}

void MyApp::DrawLightData(int& btnId)
{
	if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Checkbox("Turn on", _lightingManager->IsMainLightOn());

		ImGui::Text("Direction");
		ImGui::SameLine();
		ImGui::PushID(btnId++);
		ImGui::DragFloat3("", _lightingManager->MainLightDirection(), 0.1f);
		ImGui::PopID();

		ImVec4 color;

		ImGui::Text("Color");
		ImGui::SameLine();
		ImGui::PushID(btnId++);
		ImGui::ColorEdit3("", _lightingManager->MainLightColor());
		ImGui::PopID();
	}
	if (ImGui::CollapsingHeader("Spotlight in hand", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawHandSpotlight(btnId);
	}
	if (ImGui::CollapsingHeader("Local lights"))
	{
		ImGui::Checkbox("Debug", _lightingManager->DebugEnabled());
		if (ImGui::Button("Add light"))
		{
			_lightingManager->AddLight(md3dDevice.Get());
		}
		
		for (int i = 0; i < _lightingManager->LightsCount(); i++)
		{
			DrawLocalLightData(btnId, i);
		}
	}
}

void MyApp::DrawLocalLightData(int& btnId, int lightIndex)
{
	if (ImGui::CollapsingHeader(("Light " + std::to_string(lightIndex)).c_str()))
	{
		if (ImGui::BeginPopupContextItem())
		{
			ImGui::PushID(btnId++);
			if (ImGui::Button("delete"))
			{
				_lightingManager->DeleteLight(lightIndex);
			}
			ImGui::PopID();
			ImGui::EndPopup();
			return;
		}
		auto light = _lightingManager->GetLight(lightIndex);

		ImGui::PushID(btnId++);
		if (ImGui::RadioButton("Point Light", light->LightData.type == 0) && light->LightData.type != 0)
		{
			light->LightData.type = 0;
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::RadioButton("Spotlight", light->LightData.type == 1) && light->LightData.type != 1)
		{
			light->LightData.type = 1;
			_lightingManager->UpdateWorld(lightIndex);
		}
		bool lightEnabled = light->LightData.active == 1;
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::Checkbox("Enabled", &lightEnabled))
		{
			light->LightData.active = (int)lightEnabled;
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::DragFloat3("Position", &light->LightData.position.x, 0.1f))
		{
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::ColorEdit3("Color", &light->LightData.color.x))
		{
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::DragFloat("Intensity", &light->LightData.intensity, 0.1f, 0.0f, 10.0f))
		{
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::DragFloat("Radius", &light->LightData.radius, 0.1f, 0.0f, 30.0f))
		{
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		if (light->LightData.type == 1)
		{
			ImGui::PushID(btnId++);
			if (ImGui::DragFloat3("Direction", &light->LightData.direction.x, 0.1f))
			{
				_lightingManager->UpdateWorld(lightIndex);
			}
			ImGui::PopID();
			ImGui::PushID(btnId++);
			if (ImGui::SliderAngle("Angle", &light->LightData.angle, 1.0f, 89.f))
			{
				_lightingManager->UpdateWorld(lightIndex);
			}
			ImGui::PopID();
		}
	}
}

void MyApp::DrawObjectInfo(int& btnId)
{
	ImGui::SetNextWindowPos({ static_cast<float>(mClientWidth), 0.f }, 0, { 1.f, 0.f });
	ImGui::SetNextWindowSize({ 250.f, (float)mClientHeight }, 0);

	std::string title = _selectedModels.size() == 1
		? _objectsManager->object(*_selectedModels.begin())->Name + " Info"
		: std::to_string(_selectedModels.size()) + " objects selected";

	ImGui::Begin(title.c_str(), 0, ImGuiWindowFlags_NoResize);

	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawMultiObjectTransform(btnId);
	}

	if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (_selectedModels.size() > 1)
		{
			ImGui::TextWrapped("You cannot adjust materials of multiple objects");
		}
		else
		{
			DrawMaterials(btnId);
		}
	}

	if (ImGui::CollapsingHeader("LODs"))
	{
		if (_selectedModels.size() > 1)
		{
			ImGui::TextWrapped("You cannot adjust LODS of multiple objects");
		}
		else
		{
			DrawLODs(btnId);
		}
	}

	ImGui::End();
}

void MyApp::DrawMultiObjectTransform(int& btnId)
{
	DrawTransformInput("Location: ", btnId++, 0, 0.1f);
	DrawTransformInput("Rotation: ", btnId++, 1, 1.f);

	auto& manager = _objectsManager;

	constexpr size_t scaleIndex = BasicUtil::EnumIndex(Transform::Scale);

	XMFLOAT3 firstScale = manager->object(*_selectedModels.begin())->transform[scaleIndex];
	bool firstScaleLock = manager->object(*_selectedModels.begin())->lockedScale;
	bool allSamePos = true;

	for (int idx : _selectedModels)
	{
		if (manager->object(idx)->transform[scaleIndex].x != firstScale.x ||
			manager->object(idx)->transform[scaleIndex].y != firstScale.y ||
			manager->object(idx)->transform[scaleIndex].z != firstScale.z ||
			manager->object(idx)->lockedScale != firstScaleLock)
		{
			allSamePos = false;
			break;
		}
	}

	ImGui::Text("Scale: ");
	ImGui::SameLine();

	//it is about scale I'm just lazy to change the name
	if (allSamePos)
	{
		bool scale = firstScaleLock;
		ImGui::PushID(btnId++);
		if (ImGui::Checkbox("", &scale))
		{
			for (int idx : _selectedModels)
			{
				manager->object(idx)->lockedScale = scale;
				manager->object(idx)->NumFramesDirty = gNumFrameResources;
			}
		}
		ImGui::SameLine();
		ImGui::PopID();

		XMFLOAT3 before = firstScale;
		XMFLOAT3 pos = before;

		ImGui::PushID(btnId++);

		if (ImGui::DragFloat3("", &pos.x, 0.1f))
		{
			for (int idx : _selectedModels)
			{
				if (scale)
				{
					DirectX::XMFLOAT3 after = pos;
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
					pos = after;
				}
				manager->object(idx)->transform[scaleIndex] = pos;
				manager->object(idx)->NumFramesDirty = gNumFrameResources;
			}
		}
		ImGui::PopID();
	}
	else
	{
		ImGui::TextDisabled("Multiple values");
	}
}

void MyApp::DrawObjectMaterial(int& btnId, int matIndex)
{
	Material* material = _objectsManager->object(*_selectedModels.begin())->materials[matIndex].get();
	bool isTransparent = DrawIsTransparentCheckbox();
	bool useARM = DrawUseARMTextureCheckbox(material);
	DrawMaterialProperty(material, "Base Color", BasicUtil::EnumIndex(MatProp::BaseColor), btnId, true);
	DrawMaterialProperty(material, "Emissive", BasicUtil::EnumIndex(MatProp::Emissive), btnId, true, true, "Intensity", BasicUtil::EnumIndex(MatAddInfo::Emissive));
	if (!useARM)
	{
		DrawMaterialProperty(material, "Roughness", BasicUtil::EnumIndex(MatProp::Roughness), btnId, false);
		DrawMaterialProperty(material, "Metallic", BasicUtil::EnumIndex(MatProp::Metallic), btnId, false);
	}
	if (isTransparent)
	{
		DrawMaterialProperty(material, "Opacity", BasicUtil::EnumIndex(MatProp::Opacity), btnId, false);
	}

	DrawMaterialTexture(material, "Normal", BasicUtil::EnumIndex(MatTex::Normal), btnId);

	const auto& ri = _objectsManager->object(*_selectedModels.begin());
	if (ri->isTesselated)
	{
		DrawMaterialTexture(material, "Displacement", BasicUtil::EnumIndex(MatTex::Displacement), btnId, true, "Displacement Scale", BasicUtil::EnumIndex(MatAddInfo::Displacement));
	}

	if (!useARM)
	{
		DrawMaterialTexture(material, "Ambient Occlusion", BasicUtil::EnumIndex(MatTex::AmbOcc), btnId);
	}
	else
	{
		DrawMaterialARMTexture(material, "ARM", BasicUtil::EnumIndex(MatTex::ARM), btnId);
	}
}

void MyApp::DrawMaterials(int& btnId)
{
	static int selectedMaterial = -1;
	const auto& ri = _objectsManager->object(*_selectedModels.begin());

	std::vector<int> visibleMaterialIndices;
	for (int i = 0; i < ri->materials.size(); i++)
	{
		if (ri->materials[i]->isUsed)
		{
			visibleMaterialIndices.push_back(i);
		}
	}

	int numMaterials = (int)visibleMaterialIndices.size();
	int maxVisible = 6; // maximum number of items to show without scrolling
	float lineHeight = ImGui::GetTextLineHeightWithSpacing();

	float listHeight = lineHeight * (numMaterials < maxVisible ? numMaterials : maxVisible);

	if (ImGui::BeginListBox("##materials", ImVec2(-FLT_MIN, listHeight)))
	{
		for (int i : visibleMaterialIndices)
		{
			const bool isSelected = (selectedMaterial == i);
			if (ImGui::Selectable(ri->materials[i].get()->name.c_str(), isSelected))
				selectedMaterial = i;

			if (isSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndListBox();
	}

	if (selectedMaterial != -1)
	{
		DrawObjectMaterial(btnId, selectedMaterial);
	}
}

void MyApp::DrawMaterialProperty(Material* material, const std::string& label, size_t index, int& btnId, bool isFloat3, bool hasAdditionalInfo, const std::string& additionalInfoLabel, size_t additionalInfoIndex)
{
	auto& manager = _objectsManager;
	bool textureTabOpen = material->properties[index].texture.useTexture;

	static const int propsNum = (int)material->properties.size();
	static std::vector<int> selectedTabs;
	if (selectedTabs.size() == 0)
	{
		selectedTabs.resize(propsNum, -1);
	}
	
	if (selectedTabs[index] == -1)
	{
		selectedTabs[index] = textureTabOpen;
	}

	if (ImGui::TreeNode(label.c_str()))
	{
		if (ImGui::BeginTabBar("Tab bar"))
		{
			if (ImGui::BeginTabItem("Constant"))
			{
				selectedTabs[index] = 0;

				if (isFloat3)
				{
					ImGui::PushID(btnId++);
					if (ImGui::ColorEdit3("Color", &material->properties[index].value.x))
					{
						material->numFramesDirty = gNumFrameResources;
					}
					ImGui::PopID();
				}
				else
				{
					ImGui::PushID(btnId++);
					if (ImGui::DragFloat("", &material->properties[index].value.x, 0.1f, 0.f, 1.f))
					{
						material->numFramesDirty = gNumFrameResources;
					}
					ImGui::PopID();
				}

				ImGui::EndTabItem();

			}
			if (ImGui::BeginTabItem("Texture", 0, selectedTabs[index] ? ImGuiTabItemFlags_SetSelected : 0))
			{
				selectedTabs[index] = 1;

				std::string name = BasicUtil::trimName(material->properties[index].texture.name, 15);

				TextureHandle texHandle = material->properties[index].texture;

				ImGui::PushID(btnId++);
				if (ImGui::Button(name.c_str()))
				{
					WCHAR* texturePath;
					if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
					{
						texHandle = TextureManager::LoadTexture(texturePath, material->properties[index].texture.index, (int)_selectedModels.size());
						material->properties[index].texture = texHandle;
						material->numFramesDirty = gNumFrameResources;
						CoTaskMemFree(texturePath);
					}
				}
				ImGui::PopID();
				if (ImGui::BeginPopupContextItem())
				{
					ImGui::PushID(btnId++);
					if (ImGui::Button("delete") && texHandle.useTexture == true)
					{
						const std::string texName = texHandle.name;
						TextureManager::deleteTexture(std::wstring(texName.begin(), texName.end()), (int)_selectedModels.size());
						material->properties[index].texture = TextureHandle();
						material->properties[index].texture.useTexture = true;
						material->numFramesDirty = gNumFrameResources;
					}
					ImGui::PopID();
					ImGui::EndPopup();
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		if (hasAdditionalInfo)
		{
			ImGui::PushID(btnId++);
			ImGui::Text(additionalInfoLabel.c_str());
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID(btnId++);
			ImGui::SetNextItemWidth(100.f);
			if (ImGui::DragFloat("", &material->additionalInfo[additionalInfoIndex], 0.1f, 0.0f, 10.f))
			{
				material->numFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();
		}

		if (textureTabOpen != bool(selectedTabs[index]))
		{
			material->properties[index].texture.useTexture = selectedTabs[index];
			material->numFramesDirty = gNumFrameResources;
		}

		ImGui::TreePop();
	}
}

void MyApp::DrawMaterialTexture(Material* material, const std::string& label, size_t index, int& btnId, bool hasAdditionalInfo, const std::string& additionalInfoLabel, size_t additionalInfoIndex)
{
	auto& manager = _objectsManager;

	if (ImGui::TreeNode(label.c_str()))
	{
		TextureHandle texHandle = material->textures[index];
		std::string name = BasicUtil::trimName(texHandle.name, 15);

		ImGui::PushID(btnId++);
		if (ImGui::Button(name.c_str()))
		{
			WCHAR* texturePath;
			if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
			{
				texHandle = TextureManager::LoadTexture(texturePath, material->textures[index].index, (int)_selectedModels.size());
				material->textures[index] = texHandle;
				material->numFramesDirty = gNumFrameResources;
				CoTaskMemFree(texturePath);
			}
		}
		ImGui::PopID();
		if (ImGui::BeginPopupContextItem())
		{
			ImGui::PushID(btnId++);
			if (ImGui::Button("delete") && texHandle.useTexture == true)
			{
				const std::string texName = texHandle.name;
				TextureManager::deleteTexture(std::wstring(texName.begin(), texName.end()), (int)_selectedModels.size());
				material->textures[index] = TextureHandle();
				material->numFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();
			ImGui::EndPopup();
		}

		if (hasAdditionalInfo)
		{
			ImGui::PushID(btnId++);
			ImGui::Text(additionalInfoLabel.c_str());
			ImGui::PopID();
			ImGui::SameLine();
			ImGui::PushID(btnId++);
			ImGui::SetNextItemWidth(50.f);
			if (ImGui::DragFloat("", &material->additionalInfo[additionalInfoIndex], 0.1f, 0.0f, 10.f))
			{
				material->numFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();
		}

		ImGui::TreePop();
	}
}

void MyApp::DrawMaterialARMTexture(Material* material, const std::string& label, size_t index, int& btnId)
{
	if (ImGui::TreeNode(label.c_str()))
	{
		static const char* layouts[] = { "AO-Rough-Metal", "Rough-Metal-AO", };
		int layout = (int)material->armLayout;
		if (ImGui::Combo("Layout", &layout, layouts, IM_ARRAYSIZE(layouts)))
		{
			material->armLayout = (ARMLayout)layout;
			material->numFramesDirty = gNumFrameResources;
		}

		TextureHandle texHandle = material->textures[index];
		std::string name = BasicUtil::trimName(texHandle.name, 15);

		ImGui::PushID(btnId++);
		if (ImGui::Button(name.c_str()))
		{
			WCHAR* texturePath;
			if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
			{
				texHandle = TextureManager::LoadTexture(texturePath, material->textures[index].index, (int)_selectedModels.size());
				material->textures[index] = texHandle;
				material->numFramesDirty = gNumFrameResources;
				CoTaskMemFree(texturePath);
			}
		}
		ImGui::PopID();
		if (ImGui::BeginPopupContextItem())
		{
			ImGui::PushID(btnId++);
			if (ImGui::Button("delete") && texHandle.useTexture == true)
			{
				const std::string texName = texHandle.name;
				TextureManager::deleteTexture(std::wstring(texName.begin(), texName.end()), (int)_selectedModels.size());
				material->textures[index] = TextureHandle();
				material->numFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();
			ImGui::EndPopup();
		}

		ImGui::TreePop();
	}
}

void MyApp::DrawTransformInput(const std::string& label, int btnId, int transformIndex, float speed)
{
	auto& manager = _objectsManager;

	XMFLOAT3 firstPos = manager->object(*_selectedModels.begin())->transform[transformIndex];
	bool allSamePos = true;

	for (int idx : _selectedModels)
	{
		if (manager->object(idx)->transform[transformIndex].x != firstPos.x ||
			manager->object(idx)->transform[transformIndex].y != firstPos.y ||
			manager->object(idx)->transform[transformIndex].z != firstPos.z)
		{
			allSamePos = false;
			break;
		}
	}

	ImGui::Text(label.c_str());
	ImGui::SameLine();
	if (allSamePos)
	{
		XMFLOAT3 pos = firstPos;
		ImGui::PushID(btnId);

		if (ImGui::DragFloat3("", &pos.x, speed))
		{
			for (int idx : _selectedModels)
			{
				manager->object(idx)->transform[transformIndex] = pos;
				manager->object(idx)->NumFramesDirty = gNumFrameResources;
			}
		}
		ImGui::PopID();
	}
	else
	{
		ImGui::TextDisabled("Multiple values");
	}
}

void MyApp::DrawLODs(int& btnId)
{
	const auto& ri = _objectsManager->object(*_selectedModels.begin());

	auto& lods = ri->lodsData;

	if (ImGui::BeginListBox("##lods", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * lods.size())))
	{
		for (int i = 0; i < lods.size(); i++)
		{
			const bool isSelected = ri->currentLODIdx == i;

			std::string label = "LOD " + std::to_string(i) + " (" + std::to_string(lods[i].triangleCount) + " triangles)";
			if (_fixedLOD)
			{
				if (ImGui::Selectable(label.c_str(), isSelected))
				{
					ri->currentLODIdx = i;
				}
			}
			else
			{
				ImVec4 bgColor = isSelected ? ImVec4(0.1f, 0.4f, 0.6f, 1.0f) : ImVec4(0, 0, 0, 0);
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, bgColor);
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, bgColor);
				ImGui::PushStyleColor(ImGuiCol_Header, bgColor);
				ImGui::Selectable(label.c_str(), isSelected);
				ImGui::PopStyleColor(4);
			}

			if (lods.size() > 1)
			{
				if (ImGui::BeginPopupContextItem())
				{
					ImGui::PushID(btnId++);
					if (ImGui::Button("delete"))
					{
						if (i < lods.size() - 1)
						{
							std::string toastMessage;
							if (i == lods.size() - 2)
								toastMessage = "LOD " + std::to_string(i + 1) + " was shifted up.";
							else
								toastMessage = "LODs from " + std::to_string(i + 1) + " to " + std::to_string(lods.size() - 1) + " were shifted up.";
							AddToast(toastMessage, 5.0f);
						}
						_objectsManager->DeleteLOD(ri, i);
					}
					ImGui::PopID();
					ImGui::EndPopup();
				}
			}
		}
		ImGui::EndListBox();
	}

	if (lods.size() < 6)
	{
		if (ImGui::Button("Add LOD"))
		{
			AddLOD();
		}
	}
}

void MyApp::AddToast(const std::string& msg, float lifetime)
{
	_notifications.push_back({ msg, lifetime, (float)ImGui::GetTime() });
}

void MyApp::DrawToasts()
{
	const float pad = 10.0f;
	ImVec2 screen = ImGui::GetIO().DisplaySize;

	float y = mClientHeight - pad - 40;
	for (size_t i = 0; i < _notifications.size(); )
	{
		float age = (float)ImGui::GetTime() - _notifications[i].creationTime;
		float alpha = 1.0f - (age / _notifications[i].lifetime);

		if (alpha <= 0.0f) {
			_notifications.erase(_notifications.begin() + i);
			continue;
		}

		ImGui::SetNextWindowBgAlpha(alpha); // fade background
		ImGui::SetNextWindowPos(ImVec2(screen.x - 500 - pad, y), ImGuiCond_Always);

		ImGui::Begin(("##toast" + std::to_string(i)).c_str(), nullptr,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoMove);

		ImGui::TextUnformatted(_notifications[i].message.c_str());
		ImGui::End();

		y -= 40.0f; // stack spacing
		++i;
	}
}

void MyApp::DrawHeader()
{
	ImGui::SetNextWindowPos({ mClientWidth / 2.f, 0.f }, 0, { 0.5f, 0.0f });
	ImGui::SetNextWindowSize({ 300.f, 60.f });
	ImGui::Begin("##header", nullptr, ImGuiWindowFlags_NoResize);
	ImGui::Columns(2, "MyColumns", true); // true for borders
	//fixed lod
	ImGui::Checkbox("Fixed LOD", &_fixedLOD);
	ImGui::NextColumn();
	DrawCameraSpeed();
	ImGui::End();
}

void MyApp::DrawCameraSpeed()
{
	std::string cameraSpeedStr = std::to_string(_cameraSpeed);
	ImGui::Text(("Camera speed: " + cameraSpeedStr.substr(0, cameraSpeedStr.find_last_of('.') + 3)).c_str());
}

void MyApp::DrawImportModal()
{
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Multiple meshes", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text(("This file has " + std::to_string(_modelManager->ModelCount()) + " models. How do you want to import it?").c_str());
		ImGui::Spacing();

		// Begin a scrollable child
		ImVec2 listSize = ImVec2(400, 300); // Width x Height
		ImGui::BeginChild("MeshList", listSize, true /* border */, ImGuiWindowFlags_AlwaysVerticalScrollbar);

		for (auto& name : _modelManager->MeshNames())
		{
			ImGui::Text(name.first.c_str());
			for (auto& meshName : name.second)
			{
				ImGui::BulletText(meshName.c_str());
			}
			ImGui::Spacing();
		}

		ImGui::EndChild();

		ImGui::Spacing();

		if (ImGui::Button("Import as one model"))
		{
			//merge meshes into one file
			ModelData data = std::move(GeometryManager::BuildModelGeometry(_modelManager->ParseAsOneObject().get()));
			_selectedModels.clear();
			_selectedModels.insert(_objectsManager->AddRenderItem(md3dDevice.Get(), std::move(data)));
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::Button("Import as separate objects"))
		{
			AddMultipleModels();
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

bool MyApp::DrawIsTransparentCheckbox()
{
	auto& manager = _objectsManager;

	auto object = manager->object(*_selectedModels.begin());

	if (ImGui::Checkbox("Is transparent", &object->isTransparent))
	{
		object->NumFramesDirty = gNumFrameResources;
	}

	return object->isTransparent;
}

bool MyApp::DrawUseARMTextureCheckbox(Material* material)
{
	if (ImGui::Checkbox("Use ARM Texture", &material->useARMTexture))
	{
		material->numFramesDirty = gNumFrameResources;
	}

	return material->useARMTexture;
}

void MyApp::AddModel()
{
	PWSTR pszFilePath;
	if (BasicUtil::TryToOpenFile(L"3D Object", L"*.obj;*.fbx;*.glb", pszFilePath))
	{
		int modelCount = _modelManager->ImportObject(pszFilePath);
		if (modelCount > 1 && modelCount < 20)
		{
			ImGui::OpenPopup("Multiple meshes");
		}
		else if (modelCount == 1)
		{
			auto model = _modelManager->ParseAsOneObject();
			//generating it as one mesh
			ModelData data = GeometryManager::BuildModelGeometry(model.get());
			_selectedModels.clear();
			_selectedModels.insert(_objectsManager->AddRenderItem(md3dDevice.Get(), std::move(data)));
		}
		else if (modelCount >= 20)
		{
			AddMultipleModels();
		}
		CoTaskMemFree(pszFilePath);
	}
}

void MyApp::AddMultipleModels()
{
	_selectedModels.clear();

	for (auto& model : _modelManager->ParseScene())
	{
		ModelData data = std::move(GeometryManager::BuildModelGeometry(model.get()));
		if (data.lodsData.empty())
			continue;
		_selectedModels.insert(_objectsManager->AddRenderItem(md3dDevice.Get(), std::move(data)));
	}
}

void MyApp::AddLOD()
{
	PWSTR pszFilePath;
	if (BasicUtil::TryToOpenFile(L"3D Object", L"*.obj;*.fbx;*.glb", pszFilePath))
	{
		const auto& ri = _objectsManager->object(*_selectedModels.begin());

		if (_modelManager->ImportLODObject(pszFilePath, (int)ri->lodsData.begin()->meshes.size()))
		{
			auto lod = _modelManager->ParseAsLODObject();
			LODData data = { (int)lod.indices.size() / 3, lod.meshes };
			//generating it as one mesh
			int LODIdx = _objectsManager->AddLOD(md3dDevice.Get(), data, ri);
			GeometryManager::AddLODGeometry(ri->Name, LODIdx, lod);
			AddToast("Your LOD was added as LOD" + std::to_string(LODIdx) + "!");
		}
		CoTaskMemFree(pszFilePath);
	}
}

void MyApp::ClearData()
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrier);

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	_gBuffer->ClearInfo(Colors::Transparent);

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &barrier);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();
}

void MyApp::InitManagers()
{
	_objectsManager = std::make_unique<EditableObjectManager>(md3dDevice.Get());
	_objectsManager->Init();
	_objectsManager->SetCamera(&_camera);
	_gridManager = std::make_unique<UnlitObjectManager>(md3dDevice.Get());
	_gridManager->Init();
	
	_lightingManager = std::make_unique<LightingManager>(md3dDevice.Get());
	_lightingManager->SetData(&_camera, _objectsManager->InputLayout());
	_lightingManager->Init(_gBuffer->InfoCount(false));
}

void MyApp::GBufferPass()
{
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	//deferred rendering: writing in gbuffer first
	_gBuffer->ChangeRTVsState(D3D12_RESOURCE_STATE_RENDER_TARGET);
	_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	
	_gBuffer->ClearInfo(Colors::Transparent);
	mCommandList->OMSetRenderTargets(_gBuffer->InfoCount(), _gBuffer->RTVs().data(),
		false, &_gBuffer->DepthStencilView());
	_objectsManager->Draw(mCommandList.Get(), mCurrFrameResource, mClientHeight, _isWireframe, _fixedLOD);
}

void MyApp::LightingPass()
{

	_gBuffer->ChangeRTVsState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_READ);

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &_gBuffer->DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { TextureManager::srvDescriptorHeap.Get()};
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	_lightingManager->DrawDirLight(mCommandList.Get(), mCurrFrameResource, _gBuffer->SRVGPUHandle());

	if (_lightingManager->LightsCount() > 0)
	{
		_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_READ);
		_lightingManager->DrawLocalLights(mCommandList.Get(), mCurrFrameResource);

		if (*_lightingManager->DebugEnabled())
		{
			_lightingManager->DrawDebug(mCommandList.Get(), mCurrFrameResource);
		}
	}

	_lightingManager->DrawEmissive(mCommandList.Get(), mCurrFrameResource);
}

void MyApp::WireframePass()
{
	_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	_gBuffer->ClearInfo(Colors::Transparent);

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &_gBuffer->DepthStencilView());

	_objectsManager->Draw(mCommandList.Get(), mCurrFrameResource, mClientHeight, _isWireframe);
}

//some wndproc stuff
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool MyApp::checkForImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}