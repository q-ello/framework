#include "MyApp.h"

#include <chrono>
#include <iostream>

#include "imgui/backends/imgui_impl_win32.h"
#include "Managers/UploadManager.h"

#pragma comment(lib, "ComCtl32.lib")

MyApp::MyApp(const HINSTANCE hInstance)
	: D3DApp(hInstance)
	  , _lastMousePos {0, 0}
{
}

MyApp::~MyApp()
{
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

	GeometryManager::BuildNecessaryGeometry();
	InitManagers();
	BuildDescriptorHeaps();
	_gridManager->AddRenderItem(_device.Get(), { "grid" });
	BuildFrameResources();

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
	info.Device = _device.Get();
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
	if (_postProcessManager != nullptr)
		_postProcessManager->OnResize(mClientWidth, mClientHeight);

	_camera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 10.f, 2000.f);

	if (_taaManager != nullptr)
	{
		_taaManager->OnResize(mClientWidth, mClientHeight);
	}
	
	if (_atmosphereManager != nullptr)
	{
		_atmosphereManager->OnResize(mClientWidth, mClientHeight);
	}

	if (_supportsRayTracing && _rayTracingManager != nullptr)
	{
		_rayTracingManager->OnResize(mClientWidth, mClientHeight);
	}
	
	_camera.UpdateFrustum();
}

void MyApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.
	_currFrameResourceIndex = (_currFrameResourceIndex + 1) % gNumFrameResources;
	_currFrameResource = FrameResource::FrameResources()[_currFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (_currFrameResource->Fence != 0 && mFence->GetCompletedValue() < _currFrameResource->Fence)
	{
		const HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(_currFrameResource->Fence, eventHandle));
		if (eventHandle)
		{
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}
	}

	UpdateObjectCBs(gt);

	UpdateMainPassCBs(gt);
	_lightingManager->UpdateLightCBs(_currFrameResource);
	_postProcessManager->UpdateSsrParameters(_currFrameResource);
	_atmosphereManager->UpdateParameters(_currFrameResource);
	_terrainManager->UpdateTerrainCb(_currFrameResource);
	
	//animation
	if (_timeSpeed != 0.f)
	{
		_timeInMinutes += gt.DeltaTime() * _timeSpeed;
		UpdateDirToSun();
	}
}

void MyApp::Draw(const GameTimer& gt)
{
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	DrawInterface();

	if (_supportsRayTracing)
	{
		_rayTracingManager->UpdateData();
	}

	const auto cmdListAlloc = _currFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

	// Indicate a state transition on the resource usage.
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
	                                                    _backBufferStates[mCurrBackBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &barrier);
	_backBufferStates[mCurrBackBuffer] = D3D12_RESOURCE_STATE_RENDER_TARGET;

	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);


	if (_isWireframe)
	{
		WireframePass();
	}
	else
	{
		const auto objects = _objectsManager->Objects();
		//cascade maps are needed only if rt is disabled
		if (!_rayTracingEnabled)
			_lightingManager->DrawShadows(mCommandList.Get(), _currFrameResource, objects);
		GBufferPass();

		if (_rayTracingEnabled)
		{
			_rayTracingManager->DispatchRays(mCommandList.Get(), _currFrameResource);
		}
		
		LightingPass();
		if (_atmosphereEnabled)
			_atmosphereManager->Draw(mCommandList.Get(), _currFrameResource);
		else
		{
			//black screen is kinda sad, so I guess that won't hurt to draw skybox
			_cubeMapManager->Draw(mCommandList.Get(), _currFrameResource);
		}
		
		if (_taaEnabled)
			_taaManager->ApplyTaa(mCommandList.Get(), _currFrameResource);
		if (_godRays)
			_postProcessManager->DrawGodRaysPass(mCommandList.Get(), _currFrameResource);
		if (_ssr)
			_postProcessManager->DrawSsr(mCommandList.Get(), _currFrameResource);
		if (_chromaticAberration)
			_postProcessManager->DrawChromaticAberration(mCommandList.Get(), _currFrameResource);
		if (_vignetting)
			_postProcessManager->DrawVignetting(mCommandList.Get(), _currFrameResource);
		FinalPass();
	}

	////don't wanna draw grid anymore
	//_gBuffer->ChangeDSVState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	//mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &_gBuffer->DepthStencilView());
	//_gridManager->Draw(mCommandList.Get(), mCurrFrameResource);

	//ImGui draw
	ID3D12DescriptorHeap* descriptorHeaps[] = {_imGuiDescriptorHeap.Get()};
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	//// Rendering
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
	// Swap the back and front buffers
	barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
	                                               _backBufferStates[mCurrBackBuffer], D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &barrier);
	_backBufferStates[mCurrBackBuffer] = D3D12_RESOURCE_STATE_PRESENT;

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	GBuffer::ChangeDepthTexture();

	// Advance the fence value to mark commands up to this fence point.
	_currFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));
}

void MyApp::OnMouseDown(WPARAM btnState, const int x, const int y)
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

void MyApp::OnMouseMove(const WPARAM btnState, const int x, const int y)
{
	const ImGuiIO& io = ImGui::GetIO();
	if ((btnState & MK_LBUTTON) != 0 && !io.WantCaptureMouse)
	{
		// Make each pixel correspond to a quarter of a degree.
		const float dx = 0.001f * static_cast<float>(x - _lastMousePos.x);
		const float dy = _cameraSpeed * static_cast<float>(y - _lastMousePos.y);

		_camera.Walk(dy);
		_camera.RotateY(dx);
	}
	else if ((btnState & MK_RBUTTON) != 0 && !io.WantCaptureMouse)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		const float dx = 0.001f * static_cast<float>(x - _lastMousePos.x);
		const float dy = 0.001f * static_cast<float>(y - _lastMousePos.y);

		_camera.Pitch(dy);
		_camera.RotateY(dx);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void MyApp::OnMouseWheel(const WPARAM btnState)
{
	const ImGuiIO& io = ImGui::GetIO();
	if (_mbDown)
	{
		_cameraSpeed += static_cast<float>(GET_WHEEL_DELTA_WPARAM(btnState))/static_cast<float>(WHEEL_DELTA) * 0.01f;
		_cameraSpeed = MathHelper::Clamp(_cameraSpeed, 0.f, 25.f);
	}
	else if (!io.WantCaptureMouse)
	{
		const float direction = (static_cast<float>(GET_WHEEL_DELTA_WPARAM(btnState)) > 0) ? 1.f : -1.f;
		_camera.Walk(direction);
	}
}

void MyApp::OnKeyboardInput(const GameTimer& gt)
{
	//made a sun dependent on time of day so bye for now
	// if (GetAsyncKeyState('E') & 0x8000)
	// {
	// 	//since view matrix is transponed I'm taking z-column
	// 	_lightingManager->SetMainLightDirection(_camera.GetLook3F());
	// }

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

void MyApp::UpdateObjectCBs(const GameTimer& gt) const
{
	_gridManager->UpdateObjectCBs(_currFrameResource);
	_objectsManager->UpdateObjectCBs(_currFrameResource);
}

void MyApp::UpdateMainPassCBs(const GameTimer& gt)
{
	//update pass for gbuffer
	const XMMATRIX view = _camera.GetView();
	const XMMATRIX proj = _camera.GetProj();

	const XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	const XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);
	
	const XMMATRIX prevView = _camera.GetPrevView();
	const XMMATRIX prevProj = _camera.GetPrevProj();
	
	const XMMATRIX prevViewProj = XMMatrixMultiply(prevView, prevProj);

	XMStoreFloat4x4(&_gBufferCb.PrevViewProj, XMMatrixTranspose(prevViewProj));
	XMStoreFloat4x4(&_gBufferCb.ViewProj, XMMatrixTranspose(viewProj));
	_gBufferCb.DeltaTime = gt.DeltaTime();
	_gBufferCb.EyePosW = _camera.GetPosition3F();
	_gBufferCb.ScreenSize = { static_cast<float>(mClientWidth), static_cast<float>(mClientHeight) };
	_gBufferCb.FrameIndex++;
	_gBufferCb.TaaEnabled = static_cast<int>(_taaEnabled);
	const auto currGBufferCb = _currFrameResource->GBufferPassCb.get();
	currGBufferCb->CopyData(0, _gBufferCb);
	
	//update pass for lighting
	XMStoreFloat4x4(&_lightingCb.InvViewProj, XMMatrixTranspose(invViewProj));
	_lightingCb.EyePosW = _camera.GetPosition3F();

	//store for taa

	XMStoreFloat4x4(&_lightingCb.ViewProj, XMMatrixTranspose(viewProj));
	_lightingCb.RtSize = { static_cast<float>(mClientWidth), static_cast<float>(mClientHeight) };
	_lightingCb.MousePosition = {static_cast<float>(_lastMousePos.x), static_cast<float>(_lastMousePos.y)};
	const auto currLightingCb = _currFrameResource->LightingPassCb.get();
	currLightingCb->CopyData(0, _lightingCb);

	_lightingManager->UpdateDirectionalLightCb(_currFrameResource);

	const auto rayTracingCb = _currFrameResource->RayTracingCb.get();
	RayTracingConstants rayTracCb;
	float* lightDirection = _lightingManager->MainLightDirection();
	rayTracCb.SunDirection = {lightDirection[0], lightDirection[1], lightDirection[2]};
	rayTracingCb->CopyData(0, rayTracCb);
}

void MyApp::BuildDescriptorHeaps()
{
	//imgui heap
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 32; // Adjust based on the number of UI textures
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_imGuiDescriptorHeap)));
}

void MyApp::BuildFrameResources() const
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		FrameResource::FrameResources().push_back(std::make_unique<FrameResource>(_device.Get(),
			1));
		_gridManager->AddObjectToResource(_device, FrameResource::FrameResources()[i].get());
		_objectsManager->AddObjectToResource(_device, FrameResource::FrameResources()[i].get());
		_cubeMapManager->AddObjectToResource(FrameResource::FrameResources()[i].get());
	}
	_postProcessManager->UpdateGodRaysParameters();
}

void MyApp::DrawInterface()
{
	int buttonId = 0;

	ImGui::SetNextWindowPos({ 0.f, 0.f }, 0, { 0.f, 0.f });
	ImGui::SetNextWindowSize({ 250.f, static_cast<float>(mClientHeight)});

	ImGui::Begin("Data", nullptr, ImGuiWindowFlags_NoResize);
	ImGui::BeginTabBar("#data");
	
	if (ImGui::BeginTabItem("Main Data"))
	{
		DrawObjectsList(buttonId);
		DrawShadowMasksList(buttonId);
		DrawTerrain(buttonId);
		DrawAtmosphere(buttonId);
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
		DrawPostProcesses();
		ImGui::Checkbox("TAA Enabled", &_taaEnabled);
		ImGui::BeginDisabled(!_supportsRayTracing);
		ImGui::Checkbox("Ray Tracing Enabled", &_rayTracingEnabled);
		if (!_supportsRayTracing)
			ImGui::SetItemTooltip("Ray tracing is not supported in your device.");
		ImGui::EndDisabled();
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
	const auto visObjectsCnt = _objectsManager->VisibleObjectsCount();
	const auto objectsCnt = _objectsManager->ObjectsCount();
	ImGui::Text(("Objects drawn: " + std::to_string(visObjectsCnt) + "/" + std::to_string(objectsCnt)).c_str());
	const auto visLights = _lightingManager->LightsInsideFrustum();
	const auto lightsCnt = _lightingManager->LightsCount();
	ImGui::Text(("Lights drawn: " + std::to_string(visLights) + "/" + std::to_string(lightsCnt)).c_str());
	const auto visGrids = _terrainManager->VisibleGrids();
	ImGui::Text(("Grids instances drawn: " + std::to_string(visGrids)).c_str());
	ImGui::End();

	DrawToasts();
}

void MyApp::DrawObjectsList(int& btnId)
{
	ImGui::Checkbox("Draw Debug", _objectsManager->DrawDebug());

	if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Button("Add New"))
		{
			AddModel();
		}

		// import multiple meshes modal
		DrawImportModal();

		ImGui::Spacing();

		// Begin a scrollable child

		const int objectsCount = _objectsManager->ObjectsCount();
		constexpr int maxVisible = 10;

		const auto listSize = ImVec2(
			-FLT_MIN,
			ImGui::GetTextLineHeightWithSpacing() * 1.5f * static_cast<float>((maxVisible < objectsCount ? maxVisible : objectsCount) + 1));
		// Width x Height
		
		ImGui::BeginChild("ObjectList", listSize, false /* border */, 0);

		//for shift spacing
		static int lastClicked = -1;

		for (int i = 0; i < _objectsManager->ObjectsCount(); i++)
		{
			ImGui::PushID(btnId++);

			//bool isSelected = _selectedType == PSO::Opaque && i == _selectedObject;
			const bool isSelected = _selectedModels.count(i) > 0;

			ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? ImVec4(0.2f, 0.6f, 1.0f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

			std::string name = BasicUtil::TrimName(_objectsManager->ObjectName(i), 15);
			if (ImGui::Button(name.c_str()))
			{
				//logic for multiple selecting
				if (ImGui::GetIO().KeyShift && lastClicked != -1)
				{
					_selectedModels.clear();
					const int start = min(lastClicked, i);
					const int end = max(lastClicked, i);
					for (int j = start; j <= end; ++j)
						_selectedModels.insert(j);
				}
				else if (ImGui::GetIO().KeyCtrl)
				{
					if (isSelected)  // NOLINT(bugprone-branch-clone)
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
						for (auto it = _selectedModels.rbegin(); it != _selectedModels.rend(); ++it)
						{
							_objectsManager->DeleteObject(*it);
						}
					}
					else
					{
						_objectsManager->DeleteObject(i);
					}
					_selectedModels.clear();
				}
				ImGui::PopID();
				ImGui::EndPopup();
			}
			
		}

		ImGui::EndChild();
	}
}

void MyApp::DrawShadowMasksList(int& btnId) const
{
	if (ImGui::CollapsingHeader("Shadow masks"))
	{
		ImGui::PushID(btnId++);
		if (ImGui::Button("Add New"))
		{
			AddShadowMask();
		}
		ImGui::PopID();

		ImGui::Spacing();

		ImGui::DragFloat("UV Scale", &_lightingManager->ShadowMaskUvScale, 0.05f, 0.1f, 1.0f);

		const size_t selectedShadowMask = _lightingManager->SelectedShadowMask();

		for (size_t i = 0; i < _lightingManager->ShadowMaskCount(); i++)
		{
			ImGui::PushID(btnId++);

			const bool isSelected = i == selectedShadowMask;

			ImGui::PushStyleColor(ImGuiCol_Button, isSelected ? ImVec4(0.2f, 0.6f, 1.0f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

			std::string name = BasicUtil::TrimName(_lightingManager->ShadowMaskName(i), 15);
			if (ImGui::Button(name.c_str()))
			{
				_lightingManager->SetSelectedShadowMask(i);
			}
			ImGui::PopStyleColor();
			ImGui::PopID();

			if (ImGui::BeginPopupContextItem())
			{
				ImGui::PushID(btnId++);
				if (ImGui::Button("delete"))
				{
					_lightingManager->DeleteShadowMask(i);
				}
				ImGui::PopID();
				ImGui::EndPopup();
			}

		}


	}
}

void MyApp::DrawTerrain(int& btnId) const
{
	if (ImGui::CollapsingHeader("Terrain"))
	{
		{
			TextureHandle& heightTexHandle = _terrainManager->HeightmapTexture();

			ImGui::PushID(btnId++);
			ImGui::Text("Heightmap Texture:");
			ImGui::PopID();
			ImGui::PushID(btnId++);
			if (ImGui::Button(heightTexHandle.Name.c_str()))
			{
				WCHAR* texturePath;
				if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
				{
					TextureManager::LoadTexture(texturePath, heightTexHandle);
					_terrainManager->InitTerrain();
					_terrainManager->SetDirty();
					CoTaskMemFree(texturePath);
				}
			}
			ImGui::PopID();
		}

		{
			ImGui::Text("Max height");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragFloat("##terrain-height", &_terrainManager->MaxTerrainHeight, 1.0f, 1.0f, 50.0f))
			{
				_terrainManager->SetDirty();
			}
		}
		
		{
			for (int i = 0; i < static_cast<int>(TerrainTexture::Count); i++)
			{
				const std::string labels[] = {"Low", "Slope", "High"};
				DrawTerrainTexture(btnId, i, labels[i].c_str());
			}
		}
		
		{
			ImGui::Text("Height threshold");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragFloat("##terrain-height-threshold", &_terrainManager->HeightThreshold, 0.01f, 0.0f, 1.0f))
			{
				_terrainManager->SetDirty();
			}
		}
		
		{
			ImGui::Text("Slope Threshold");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragFloat("##terrain-slope-threshold", &_terrainManager->SlopeThreshold, 0.01f, 0.0f, 1.0f))
			{
				_terrainManager->SetDirty();
			}
		}
	}
}

void MyApp::DrawTerrainTexture(int& btnId, const int index, const char* label) const
{
	MaterialProperty* property = _terrainManager->TerrainTexture(index);
	const bool textureTabOpen = property->texture.UseTexture;
	
	// ReSharper disable once CppVariableCanBeMadeConstexpr
	static const int propsNum = BasicUtil::EnumIndex(TerrainTexture::Count);
	static std::vector<int> selectedTabs;
	if (selectedTabs.size() == 0)
	{
		selectedTabs.resize(propsNum, -1);
	}
	
	if (selectedTabs[index] == -1)
	{
		selectedTabs[index] = textureTabOpen;
	}

	if (ImGui::TreeNode(label))
	{
		if (ImGui::BeginTabBar("Tab bar"))
		{
			if (ImGui::BeginTabItem("Constant"))
			{
				selectedTabs[index] = 0;

				if (ImGui::ColorEdit3("Color", &property->value.x))
				{
					property->texture.UseTexture = false;
					_terrainManager->SetDirty();
				}

				ImGui::EndTabItem();

			}
			if (ImGui::BeginTabItem("Texture", nullptr, selectedTabs[index] ? ImGuiTabItemFlags_SetSelected : 0))
			{
				selectedTabs[index] = 1;
				
				const std::string name = BasicUtil::TrimName(property->texture.Name, 15);

				TextureHandle texHandle = property->texture;

				ImGui::PushID(btnId++);
				if (ImGui::Button(name.c_str()))
				{
					WCHAR* texturePath;
					if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
					{
						TextureManager::LoadTexture(texturePath, texHandle);
						property->texture = texHandle;
						_terrainManager->SetDirty();
						CoTaskMemFree(texturePath);
					}
				}
				ImGui::PopID();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		
		ImGui::TreePop();
	}
	
	if (textureTabOpen != static_cast<bool>(selectedTabs[index]))
	{
		property->texture.UseTexture = selectedTabs[index];
		_terrainManager->SetDirty();
	}
}

void MyApp::DrawAtmosphere(int& btnId)
{
	if (ImGui::CollapsingHeader("Atmosphere"))
	{
		ImGui::Checkbox("Enabled##atmosphere", &_atmosphereEnabled);
		ImGui::BeginDisabled(!_atmosphereEnabled);
		{
			ImGui::Text("Time Speed");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			ImGui::DragFloat("##atmosphere-time-speed", &_timeSpeed, 0.1f, -100.0f, 100.0f);
		}
		{
			ImGui::Text("Time");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragInt("##atmosphere-time-in-hours", &_timeInHours, 1, -1, 24))
			{
				UpdateDirToSun();
			}
			ImGui::SameLine();
			ImGui::Text("h");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			//so that looks still fine and not 1.387
			int timeInMinutesInt = static_cast<int>(_timeInMinutes);
			if (ImGui::DragInt("##atmosphere-time-in-minutes", &timeInMinutesInt, 1, -1, 60))
			{
				_timeInMinutes = static_cast<float>(timeInMinutesInt);
				UpdateDirToSun();
			}
			ImGui::SameLine();
			ImGui::Text("min");
		}
		{
			ImGui::Text("Atmosphere Radius");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragFloat("##atmosphere-radius", &_atmosphereManager->Parameters.AtmosphereRadius, 10.0f, 1.0f, 10000.0f))
			{
				_atmosphereManager->SetDirty();
			}
		}
		{
			ImGui::Text("Planet Center");
			ImGui::SameLine();
			if (ImGui::DragFloat3("##atmosphere-planet-center", &_atmosphereManager->Parameters.PlanetCenter.x, 1.0f, 0.0f, 50.0f))
			{
				_atmosphereManager->SetDirty();
			}
		}
		{
			ImGui::Text("Planet Radius");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragFloat("##atmosphere-planet-radius", &_atmosphereManager->Parameters.PlanetRadius, 10.0f, 1.0f, 10000.0f))
			{
				_atmosphereManager->SetDirty();
			}
		}
		{
			ImGui::Text("Num Scattering Points");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragInt("##atmosphere-scattering", &_atmosphereManager->Parameters.NumInScatteringPoints, 1, 1, 50))
			{
				_atmosphereManager->SetDirty();
			}
		}
		{
			ImGui::Text("Num Optical Depth Points");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragInt("##atmosphere-optical-depth", &_atmosphereManager->Parameters.NumOpticalDepthPoints, 1, 1, 50))
			{
				_atmosphereManager->SetDirty();
			}
		}
		{
			ImGui::Text("Density Falloff");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(50.0f);
			if (ImGui::DragFloat("##atmosphere-density-falloff", &_atmosphereManager->Parameters.DensityFalloff, 1.0f, 1.0f, 50.0f))
			{
				_atmosphereManager->SetDirty();
			}
		}
		{
			ImGui::Text("Wavelengths");
			ImGui::SameLine();
			if (ImGui::DragFloat3("##atmosphere-wavelengths", &_atmosphereManager->Parameters.Wavelengths.x, 1.0f, 0.0f, 1000.0f))
			{
				_atmosphereManager->SetDirty();
			}
		}
		ImGui::EndDisabled();
	}
}

void MyApp::DrawHandSpotlight(int& btnId) const
{
	const auto light = _lightingManager->MainSpotlight();

	bool lightEnabled = light->Active == 1;
	ImGui::PushID(btnId++);
	if (ImGui::Checkbox("Enabled", &lightEnabled))
	{
		light->Active = static_cast<int>(lightEnabled);
	}
	ImGui::PopID();

	ImGui::BeginDisabled(!lightEnabled);

	ImGui::PushID(btnId++);
	ImGui::ColorEdit3("Color", &light->Color.x);
	ImGui::PopID();

	ImGui::PushID(btnId++);
	ImGui::DragFloat("Intensity", &light->Intensity, 0.1f, 0.0f, 10.0f);
	ImGui::PopID();

	ImGui::PushID(btnId++);
	ImGui::DragFloat("Radius", &light->Radius, 0.1f, 0.0f, 100.0f);
	ImGui::PopID();

	ImGui::PushID(btnId++);
	ImGui::SliderAngle("Angle", &light->Angle, 1.0f, 89.f);
	ImGui::PopID();
	
	ImGui::EndDisabled();
}

void MyApp::DrawLightData(int& btnId) const
{
	ImGui::BeginDisabled(_atmosphereEnabled);
	if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
	 {
	 	ImGui::Checkbox("Turn on", _lightingManager->IsMainLightOn());
	 	
	 	ImGui::BeginDisabled(!(*_lightingManager->IsMainLightOn()));
	
	 	ImGui::Text("Direction");
	 	ImGui::SameLine();
	 	ImGui::PushID(btnId++);
	 	if (ImGui::DragFloat3("", _lightingManager->MainLightDirection(), 0.1f))
	 	{
	 		const float* dir = _lightingManager->MainLightDirection();
	 		XMVECTOR dirVec = XMVectorSet(-dir[0], -dir[1], -dir[2], 1.0f);
	 		dirVec = XMVector3Normalize(dirVec);
	 		XMStoreFloat3(&_atmosphereManager->Parameters.DirToSun, dirVec);
	 		_atmosphereManager->SetDirty();
	 	}
	 	ImGui::PopID();
	
	 	ImGui::Text("Color");
	 	ImGui::SameLine();
	 	ImGui::PushID(btnId++);
	 	ImGui::ColorEdit3("", _lightingManager->MainLightColor());
	 	ImGui::PopID();
	 	
	 	ImGui::EndDisabled();
	 }
	ImGui::EndDisabled();
	if (_atmosphereEnabled)
	{
		ImGui::SetItemTooltip("Cannot configure that when atmosphere is enabled");
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
			_lightingManager->AddLight(_device.Get());
		}
		
		for (int i = 0; i < _lightingManager->LightsCount(); i++)
		{
			DrawLocalLightData(btnId, i);
		}
	}
}

void MyApp::DrawLocalLightData(int& btnId, const int lightIndex) const
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
		const auto light = _lightingManager->GetLight(lightIndex);

		bool lightEnabled = light->LightData.Active == 1;
		ImGui::PushID(btnId++);
		if (ImGui::Checkbox("Enabled", &lightEnabled))
		{
			light->LightData.Active = static_cast<int>(lightEnabled);
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();

		ImGui::BeginDisabled(!lightEnabled);

		ImGui::PushID(btnId++);
		if (ImGui::RadioButton("Point Light", light->LightData.Type == 0) && light->LightData.Type != 0)
		{
			light->LightData.Type = 0;
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::RadioButton("Spotlight", light->LightData.Type == 1) && light->LightData.Type != 1)
		{
			light->LightData.Type = 1;
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		
		ImGui::PushID(btnId++);
		if (ImGui::DragFloat3("Position", &light->LightData.Position.x, 0.1f))
		{
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::ColorEdit3("Color", &light->LightData.Color.x))
		{
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::DragFloat("Intensity", &light->LightData.Intensity, 0.1f, 0.0f, 10.0f))
		{
			light->NumFramesDirty = gNumFrameResources;
		}
		ImGui::PopID();
		ImGui::PushID(btnId++);
		if (ImGui::DragFloat("Radius", &light->LightData.Radius, 0.1f, 0.0f, 30.0f))
		{
			_lightingManager->UpdateWorld(lightIndex);
		}
		ImGui::PopID();
		if (light->LightData.Type == 1)
		{
			ImGui::PushID(btnId++);
			if (ImGui::DragFloat3("Direction", &light->LightData.Direction.x, 0.1f))
			{
				_lightingManager->UpdateWorld(lightIndex);
			}
			ImGui::PopID();
			ImGui::PushID(btnId++);
			if (ImGui::SliderAngle("Angle", &light->LightData.Angle, 1.0f, 89.f))
			{
				_lightingManager->UpdateWorld(lightIndex);
			}
			ImGui::PopID();
		}
		ImGui::EndDisabled();
	}
}

void MyApp::DrawObjectInfo(int& btnId)
{
	ImGui::SetNextWindowPos({ static_cast<float>(mClientWidth), 0.f }, 0, { 1.f, 0.f });
	ImGui::SetNextWindowSize({ 250.f, static_cast<float>(mClientHeight) }, 0);

	const std::string title = _selectedModels.size() == 1
		                    ? _objectsManager->Object(*_selectedModels.begin())->Name + " Info"
		                    : std::to_string(_selectedModels.size()) + " objects selected";

	ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoResize);

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
			DrawLoDs(btnId);
		}
	}

	ImGui::End();
}

void MyApp::DrawMultiObjectTransform(int& btnId) const
{
	DrawTransformInput("Location: ", btnId++, 0, 0.1f);
	DrawTransformInput("Rotation: ", btnId++, 1, 1.f);

	const auto& manager = _objectsManager;

	constexpr size_t scaleIndex = BasicUtil::EnumIndex(Transform::Scale);

	const XMFLOAT3 firstScale = manager->Object(*_selectedModels.begin())->Transform[scaleIndex];
	const bool firstScaleLock = manager->Object(*_selectedModels.begin())->LockedScale;
	bool allSamePos = true;

	for (const int idx : _selectedModels)
	{
		if (manager->Object(idx)->Transform[scaleIndex].x != firstScale.x ||
			manager->Object(idx)->Transform[scaleIndex].y != firstScale.y ||
			manager->Object(idx)->Transform[scaleIndex].z != firstScale.z ||
			manager->Object(idx)->LockedScale != firstScaleLock)
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
			for (const int idx : _selectedModels)
			{
				manager->Object(idx)->LockedScale = scale;
				manager->Object(idx)->RayTracingDirty = true;
				manager->Object(idx)->NumFramesDirty = gNumFrameResources;
			}
		}
		ImGui::SameLine();
		ImGui::PopID();

		const XMFLOAT3 before = firstScale;
		XMFLOAT3 pos = before;

		ImGui::PushID(btnId++);

		if (ImGui::DragFloat3("", &pos.x, 0.1f))
		{
			for (const int idx : _selectedModels)
			{
				if (scale)
				{
					DirectX::XMFLOAT3 after = pos;

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
				manager->Object(idx)->Transform[scaleIndex] = pos;
				manager->Object(idx)->RayTracingDirty = true;
				manager->Object(idx)->NumFramesDirty = gNumFrameResources;
			}
		}
		ImGui::PopID();
	}
	else
	{
		ImGui::TextDisabled("Multiple values");
	}
}

void MyApp::DrawTransformInput(const std::string& label, const int btnId, const int transformIndex, const float speed) const
{
	auto& manager = _objectsManager;

	const XMFLOAT3 firstPos = manager->Object(*_selectedModels.begin())->Transform[transformIndex];
	bool allSamePos = true;

	for (const int idx : _selectedModels)
	{
		if (manager->Object(idx)->Transform[transformIndex].x != firstPos.x ||
			manager->Object(idx)->Transform[transformIndex].y != firstPos.y ||
			manager->Object(idx)->Transform[transformIndex].z != firstPos.z)
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
			for (const int idx : _selectedModels)
			{
				manager->Object(idx)->Transform[transformIndex] = pos;
				manager->Object(idx)->RayTracingDirty = true;
				manager->Object(idx)->NumFramesDirty = gNumFrameResources;
			}
		}
		ImGui::PopID();
	}
	else
	{
		ImGui::TextDisabled("Multiple values");
	}
}

void MyApp::DrawObjectMaterial(int& btnId, const int matIndex) const
{
	Material* material = _objectsManager->Object(*_selectedModels.begin())->Materials[matIndex].get();
	const bool isTransparent = DrawIsTransparentCheckbox();
	const bool useArm = DrawUseArmTextureCheckbox(material);
	DrawMaterialProperty(material, "Base Color", BasicUtil::EnumIndex(MatProp::BaseColor), btnId, true);
	DrawMaterialProperty(material, "Emissive", BasicUtil::EnumIndex(MatProp::Emissive), btnId, true, true, "Intensity",
	                     BasicUtil::EnumIndex(MatAddInfo::Emissive));
	if (!useArm)
	{
		DrawMaterialProperty(material, "Roughness", BasicUtil::EnumIndex(MatProp::Roughness), btnId, false);
		DrawMaterialProperty(material, "Metallic", BasicUtil::EnumIndex(MatProp::Metallic), btnId, false);
	}
	if (isTransparent)
	{
		DrawMaterialProperty(material, "Opacity", BasicUtil::EnumIndex(MatProp::Opacity), btnId, false);
	}

	DrawMaterialTexture(material, "Normal", BasicUtil::EnumIndex(MatTex::Normal), btnId);

	const auto& ri = _objectsManager->Object(*_selectedModels.begin());
	if (ri->IsTesselated)
	{
		DrawMaterialTexture(material, "Displacement", BasicUtil::EnumIndex(MatTex::Displacement), btnId, true,
		                    "Displacement Scale", BasicUtil::EnumIndex(MatAddInfo::Displacement));
	}

	if (!useArm)
	{
		DrawMaterialTexture(material, "Ambient Occlusion", BasicUtil::EnumIndex(MatTex::AmbOcc), btnId);
	}
	else
	{
		DrawMaterialArmTexture(material, "ARM", BasicUtil::EnumIndex(MatTex::ARM), btnId);
	}
}

bool MyApp::DrawIsTransparentCheckbox() const
{
	auto& manager = _objectsManager;

	const auto object = manager->Object(*_selectedModels.begin());

	if (ImGui::Checkbox("Is transparent", &object->IsTransparent))
	{
		object->NumFramesDirty = gNumFrameResources;
	}

	return object->IsTransparent;
}

bool MyApp::DrawUseArmTextureCheckbox(Material* material)
{
	if (ImGui::Checkbox("Use ARM Texture", &material->useARMTexture))
	{
		material->numFramesDirty = gNumFrameResources;
	}

	return material->useARMTexture;
}

void MyApp::DrawMaterials(int& btnId) const
{
	static int selectedMaterial = -1;
	const auto& ri = _objectsManager->Object(*_selectedModels.begin());

	std::vector<int> visibleMaterialIndices;
	for (int i = 0; i < ri->Materials.size(); i++)
	{
		if (ri->Materials[i]->isUsed)
		{
			visibleMaterialIndices.push_back(i);
		}
	}

	const int numMaterials = static_cast<int>(visibleMaterialIndices.size());
	constexpr int maxVisible = 6; // maximum number of items to show without scrolling
	const float lineHeight = ImGui::GetTextLineHeightWithSpacing();

	const float listHeight = lineHeight * static_cast<float>((numMaterials < maxVisible ? numMaterials : maxVisible));

	if (ImGui::BeginListBox("##materials", ImVec2(-FLT_MIN, listHeight)))
	{
		for (const int i : visibleMaterialIndices)
		{
			const bool isSelected = (selectedMaterial == i);
			if (ImGui::Selectable(ri->Materials[i].get()->name.c_str(), isSelected))
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

void MyApp::DrawMaterialProperty(Material* material, const std::string& label, const size_t index, int& btnId,
                                 const bool isFloat3, const bool hasAdditionalInfo, const std::string& additionalInfoLabel,
                                 const size_t additionalInfoIndex) const
{
	const bool textureTabOpen = material->properties[index].texture.UseTexture;

	// ReSharper disable once CppVariableCanBeMadeConstexpr
	static const int propsNum = static_cast<int>(material->properties.size());
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
			if (ImGui::BeginTabItem("Texture", nullptr, selectedTabs[index] ? ImGuiTabItemFlags_SetSelected : 0))
			{
				selectedTabs[index] = 1;

				const std::string name = BasicUtil::TrimName(material->properties[index].texture.Name, 15);

				TextureHandle texHandle = material->properties[index].texture;

				ImGui::PushID(btnId++);
				if (ImGui::Button(name.c_str()))
				{
					WCHAR* texturePath;
					if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
					{
						texHandle = TextureManager::LoadTexture(texturePath, material->properties[index].texture.Index, static_cast<int>(_selectedModels.size()));
						material->properties[index].texture = texHandle;
						material->numFramesDirty = gNumFrameResources;
						CoTaskMemFree(texturePath);
					}
				}
				ImGui::PopID();
				if (ImGui::BeginPopupContextItem())
				{
					ImGui::PushID(btnId++);
					if (ImGui::Button("delete") && texHandle.UseTexture == true)
					{
						const std::string texName = texHandle.Name;
						TextureManager::DeleteTexture(std::wstring(texName.begin(), texName.end()), static_cast<int>(_selectedModels.size()));
						material->properties[index].texture = TextureHandle();
						material->properties[index].texture.UseTexture = true;
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

		if (textureTabOpen != static_cast<bool>(selectedTabs[index]))
		{
			material->properties[index].texture.UseTexture = selectedTabs[index];
			material->numFramesDirty = gNumFrameResources;
		}

		ImGui::TreePop();
	}
}

void MyApp::DrawMaterialTexture(Material* material, const std::string& label, const size_t index, int& btnId,
                                const bool hasAdditionalInfo, const std::string& additionalInfoLabel,
                                const size_t additionalInfoIndex) const
{

	if (ImGui::TreeNode(label.c_str()))
	{
		TextureHandle texHandle = material->textures[index];
		const std::string name = BasicUtil::TrimName(texHandle.Name, 15);

		ImGui::PushID(btnId++);
		if (ImGui::Button(name.c_str()))
		{
			WCHAR* texturePath;
			if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
			{
				texHandle = TextureManager::LoadTexture(texturePath, material->textures[index].Index, static_cast<int>(_selectedModels.size()));
				material->textures[index] = texHandle;
				material->numFramesDirty = gNumFrameResources;
				CoTaskMemFree(texturePath);
			}
		}
		ImGui::PopID();
		if (ImGui::BeginPopupContextItem())
		{
			ImGui::PushID(btnId++);
			if (ImGui::Button("delete") && texHandle.UseTexture == true)
			{
				const std::string texName = texHandle.Name;
				TextureManager::DeleteTexture(std::wstring(texName.begin(), texName.end()), static_cast<int>(_selectedModels.size()));
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

void MyApp::DrawMaterialArmTexture(Material* material, const std::string& label, const size_t index, int& btnId) const
{
	if (ImGui::TreeNode(label.c_str()))
	{
		static const char* layouts[] = { "AO-Rough-Metal", "Rough-Metal-AO", };
		int layout = static_cast<int>(material->armLayout);
		if (ImGui::Combo("Layout", &layout, layouts, IM_ARRAYSIZE(layouts)))
		{
			material->armLayout = static_cast<ARMLayout>(layout);
			material->numFramesDirty = gNumFrameResources;
		}

		TextureHandle texHandle = material->textures[index];
		const std::string name = BasicUtil::TrimName(texHandle.Name, 15);

		ImGui::PushID(btnId++);
		if (ImGui::Button(name.c_str()))
		{
			WCHAR* texturePath;
			if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
			{
				texHandle = TextureManager::LoadTexture(texturePath, material->textures[index].Index, static_cast<int>(_selectedModels.size()));
				material->textures[index] = texHandle;
				material->numFramesDirty = gNumFrameResources;
				CoTaskMemFree(texturePath);
			}
		}
		ImGui::PopID();
		if (ImGui::BeginPopupContextItem())
		{
			ImGui::PushID(btnId++);
			if (ImGui::Button("delete") && texHandle.UseTexture == true)
			{
				const std::string texName = texHandle.Name;
				TextureManager::DeleteTexture(std::wstring(texName.begin(), texName.end()), static_cast<int>(_selectedModels.size()));
				material->textures[index] = TextureHandle();
				material->numFramesDirty = gNumFrameResources;
			}
			ImGui::PopID();
			ImGui::EndPopup();
		}

		ImGui::TreePop();
	}
}

void MyApp::DrawLoDs(int& btnId)
{
	const auto& ri = _objectsManager->Object(*_selectedModels.begin());

	const auto& lods = ri->LodsData;

	if (ImGui::BeginListBox("##lods", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * lods.size())))
	{
		for (int i = 0; i < lods.size(); i++)
		{
			const bool isSelected = ri->CurrentLodIdx == i;

			std::string label = "LOD " + std::to_string(i) + " (" + std::to_string(lods[i].TriangleCount) + " triangles)";
			if (_fixedLod)
			{
				if (ImGui::Selectable(label.c_str(), isSelected))
				{
					ri->CurrentLodIdx = i;
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
						_objectsManager->DeleteLod(ri, i);
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
			AddLod();
		}
	}
}

void MyApp::DrawPostProcesses()
{
	if (ImGui::CollapsingHeader("God Rays"))
	{
		bool isDirty = false;
		isDirty = ImGui::Checkbox("Enabled##godrays", &_godRays) || isDirty;
		ImGui::BeginDisabled(!_godRays);
		isDirty = ImGui::DragInt("Samples##godrays", &_postProcessManager->GodRaysParameters.SamplesCount, 5, 10, 100) || isDirty;
		isDirty = ImGui::DragFloat("Decay##godrays", &_postProcessManager->GodRaysParameters.Decay, 0.05f, 0.1f, 1.0f) || isDirty;
		isDirty = ImGui::DragFloat("Exposure##godrays", &_postProcessManager->GodRaysParameters.Exposure, 0.05f, 0.1f, 1.0f) || isDirty;
		isDirty = ImGui::DragFloat("Density##godrays", &_postProcessManager->GodRaysParameters.Density, 0.05f, 0.1f, 1.0f) || isDirty;
		isDirty = ImGui::DragFloat("Weight##godrays", &_postProcessManager->GodRaysParameters.Weight, 0.05f, 0.1f, 1.0f) || isDirty;
		ImGui::EndDisabled();
		if (isDirty)
			_postProcessManager->UpdateGodRaysParameters();
	}

	if (ImGui::CollapsingHeader("Screen Space Reflection"))
	{
		//no need for dirty flag 'cause we update it every frame
		ImGui::Checkbox("Enabled##ssr", &_ssr);
		ImGui::BeginDisabled(!_ssr);
		ImGui::DragInt("Step Size##ssr", &_postProcessManager->SsrParameters.StepScale, 1, 1, 20);
		ImGui::DragInt("Max Screen Distance##ssr", &_postProcessManager->SsrParameters.MaxScreenDistance, 5, 100, 1000);
		ImGui::DragInt("Max Steps##ssr", &_postProcessManager->SsrParameters.MaxSteps, 5, 100, 500);
		ImGui::EndDisabled();
	}

	if (ImGui::CollapsingHeader("Chromatic Aberration"))
	{
		//no need for dirty flag 'cause we just have one root constant
		ImGui::Checkbox("Enabled##chromaticaberration", &_chromaticAberration);
		ImGui::BeginDisabled(!_chromaticAberration);
		ImGui::DragFloat("Strength##chromaticaberration", &_postProcessManager->ChromaticAberrationStrength, 0.01f, 0.0f, 0.1f);
		ImGui::EndDisabled();
	}

	if (ImGui::CollapsingHeader("Vignetting"))
	{
		ImGui::Checkbox("Enabled##vignetting", &_vignetting);
		ImGui::BeginDisabled(!_vignetting);
		ImGui::DragFloat("Vignetting Power##vignetting", &_postProcessManager->VignettingPower, 0.1f, 1.f, 2.0f);
		ImGui::EndDisabled();
	}
}

void MyApp::AddToast(const std::string& msg, const float lifetime)
{
	_notifications.push_back({ msg, lifetime, static_cast<float>(ImGui::GetTime()) });
}

void MyApp::DrawToasts()
{
	constexpr float pad = 10.0f;
	const ImVec2 screen = ImGui::GetIO().DisplaySize;

	float y = static_cast<float>(mClientHeight) - pad - 40.f;
	for (size_t i = 0; i < _notifications.size(); )
	{
		const float age = static_cast<float>(ImGui::GetTime()) - _notifications[i].CreationTime;
		const float alpha = 1.0f - (age / _notifications[i].Lifetime);

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

		ImGui::TextUnformatted(_notifications[i].Message.c_str());
		ImGui::End();

		y -= 40.0f; // stack spacing
		++i;
	}
}

void MyApp::DrawHeader()
{
	ImGui::SetNextWindowPos({ static_cast<float>(mClientWidth) / 2.f, 0.f }, 0, { 0.5f, 0.0f });
	ImGui::SetNextWindowSize({ 300.f, 60.f });
	ImGui::Begin("##header", nullptr, ImGuiWindowFlags_NoResize);
	ImGui::Columns(2, "MyColumns", true); // true for borders
	//fixed lod
	ImGui::Checkbox("Fixed LOD", &_fixedLod);
	ImGui::NextColumn();
	DrawCameraSpeed();
	ImGui::End();
}

void MyApp::DrawCameraSpeed() const
{
	const std::string cameraSpeedStr = std::to_string(_cameraSpeed);
	ImGui::Text(("Camera speed: " + cameraSpeedStr.substr(0, cameraSpeedStr.find_last_of('.') + 3)).c_str());
}

void MyApp::AddRenderItem(ModelData data)
{
	if (_supportsRayTracing)
	{
		for (auto& lod : GeometryManager::Geometries()[data.CroppedName])
		{
			GeometryManager::BuildBlasForMesh(*lod.get());
		}
		UploadManager::ExecuteUploadCommandList();
	}
	_selectedModels.insert(_objectsManager->AddRenderItem(_device.Get(), std::move(data)));
}

void MyApp::DrawImportModal()
{
	const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Multiple meshes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text(("This file has " + std::to_string(_modelManager->ModelCount()) + " models. How do you want to import it?").c_str());
		ImGui::Spacing();

		// Begin a scrollable child
		constexpr auto listSize = ImVec2(400, 300); // Width x Height
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
			AddRenderItem(std::move(data));

			
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

void MyApp::AddModel()
{
	PWSTR pszFilePath;
	if (BasicUtil::TryToOpenFile(L"3D Object", L"*.obj;*.fbx;*.glb", pszFilePath))
	{
		const int modelCount = _modelManager->ImportObject(pszFilePath);
		if (modelCount > 1 && modelCount < 20)
		{
			ImGui::OpenPopup("Multiple meshes");
		}
		else if (modelCount == 1)
		{
			const auto model = _modelManager->ParseAsOneObject();
			//generating it as one mesh
			ModelData data = GeometryManager::BuildModelGeometry(model.get());
			_selectedModels.clear();
			AddRenderItem(std::move(data));
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
		if (data.LodsData.empty())
			continue;
		AddRenderItem(std::move(data));
	}
}

void MyApp::AddLod()
{
	PWSTR pszFilePath;
	if (BasicUtil::TryToOpenFile(L"3D Object", L"*.obj;*.fbx;*.glb", pszFilePath))
	{
		const auto& ri = _objectsManager->Object(*_selectedModels.begin());

		if (_modelManager->ImportLodObject(pszFilePath, static_cast<int>(ri->LodsData.begin()->Meshes.size())))
		{
			const auto lod = _modelManager->ParseAsLodObject();
			const LodData data = { static_cast<int>(lod.Indices.size()) / 3, lod.Meshes };
			//generating it as one mesh
			const int lodIdx = _objectsManager->AddLod(_device.Get(), data, ri);
			GeometryManager::AddLodGeometry(ri->Name, lodIdx, lod);
			if (_supportsRayTracing)
			{
				GeometryManager::BuildBlasForMesh(*(GeometryManager::Geometries()[ri->Name].end() - 1)->get());
				UploadManager::ExecuteUploadCommandList();
			}
			AddToast("Your LOD was added as LOD" + std::to_string(lodIdx) + "!");
		}
		CoTaskMemFree(pszFilePath);
	}
}

void MyApp::AddShadowMask() const
{
	WCHAR* texturePath;
	if (BasicUtil::TryToOpenFile(L"Image Files", L"*.dds;*.png;*.jpg;*.jpeg;*.tga;*.bmp", texturePath))
	{
		const TextureHandle texHandle = TextureManager::LoadTexture(texturePath, -1, 1);
		_lightingManager->AddShadowMask(texHandle);
		CoTaskMemFree(texturePath);
	}
}

void MyApp::UpdateDirToSun()
{
	//I like modulo better, but I guess this way is faster.
	{
		if (_timeInMinutes > 59.f)
		{
			_timeInHours++;
			_timeInMinutes = 0.f;
		}
		else if (_timeInMinutes < 0.f)
		{
			_timeInHours--;
			_timeInMinutes = 59.f;
		}
	
		if (_timeInHours > 23)
			_timeInHours = 0;
		else if (_timeInHours < 0)
			_timeInHours = 23;
	}
	
	const float timeTotal = static_cast<float>(_timeInHours) * 60.f + _timeInMinutes;
		
	constexpr float minutesInDay = 12.f * 60.f;
	
	constexpr float phi = 80.0f / 180.0f * XM_PI;
	const float thetaPart = static_cast<float>(timeTotal) / minutesInDay;
	const float theta = thetaPart * XM_PI;
	XMVECTOR dirToSun = XMVectorSet(sin(theta)*cos(phi), -cos(theta), sin(theta)*sin(phi), 0.0f);
	dirToSun = XMVector3Normalize(dirToSun);
	const XMVECTOR negativeDirToSun = XMVectorNegate(dirToSun);
	XMStoreFloat3(&_atmosphereManager->Parameters.DirToSun, dirToSun);
	_atmosphereManager->SetDirty();
	XMFLOAT3 negativeDirToSunF;
	XMStoreFloat3(&negativeDirToSunF, negativeDirToSun);
	_lightingManager->SetMainLightDirection(negativeDirToSunF);
}

void MyApp::InitManagers()
{
	_rayTracingManager = std::make_unique<RayTracingManager>(_device.Get(), mClientWidth, mClientHeight);
	if (_supportsRayTracing)
	{
		_rayTracingManager->Init();
		_rayTracingManager->BindToOtherData(_gBuffer.get());
	}
	
	_objectsManager = std::make_unique<EditableObjectManager>(_device.Get());
	_objectsManager->Init();
	_objectsManager->BindToOtherData(&_camera, _rayTracingManager.get());
	
	_gridManager = std::make_unique<UnlitObjectManager>(_device.Get());
	_gridManager->Init();

	_cubeMapManager = std::make_unique<CubeMapManager>(_device.Get());
	_cubeMapManager->Init();

	_lightingManager = std::make_unique<LightingManager>(_device.Get(), mClientWidth, mClientHeight, _supportsRayTracing);
	_lightingManager->BindToOtherData(_gBuffer.get(), _cubeMapManager.get(), &_camera, _objectsManager->InputLayout(), _rayTracingManager.get());
	_lightingManager->Init();

	_postProcessManager = std::make_unique<PostProcessManager>(_device.Get());
	_postProcessManager->BindToManagers(_gBuffer.get(), _lightingManager.get(), &_camera);
	_postProcessManager->Init(mClientWidth, mClientHeight);

	_terrainManager = std::make_unique<TerrainManager>(_device.Get());
	_terrainManager->BindToOtherData(&_camera);
	_terrainManager->Init();

	_taaManager = std::make_unique<TaaManager>(_device.Get());
	_taaManager->BindToManagers(_lightingManager.get(), _gBuffer.get(), &_camera);
	_taaManager->Init(mClientWidth, mClientHeight);
	
	_atmosphereManager = std::make_unique<AtmosphereManager>(_device.Get());
	_atmosphereManager->BindToManagers(_lightingManager.get(), _gBuffer.get());
	_atmosphereManager->Init(mClientWidth, mClientHeight);
	
	//time of day for atmosphereManager so it'll also be here
	{
		struct tm newtime;
		__time32_t time32;

		_time32( &time32 );
		// Convert to local time.
		_localtime32_s(&newtime, &time32);
		_timeInHours = newtime.tm_hour;
		_timeInMinutes = newtime.tm_min;

		UpdateDirToSun();
	}
}

void MyApp::GBufferPass() const
{
	ID3D12DescriptorHeap* descriptorHeaps[] = { TextureManager::SrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	//deferred rendering: writing in gbuffer first
	_gBuffer->ChangeRtvsState(D3D12_RESOURCE_STATE_RENDER_TARGET);
	_gBuffer->ChangeDsvState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	
	_gBuffer->ClearInfo(Colors::Transparent);
	const auto dsv = _gBuffer->DepthStencilView();
	mCommandList->OMSetRenderTargets(_gBuffer->InfoCount(), _gBuffer->Rtvs().data(),
	                                 false, &dsv);

	_objectsManager->Draw(mCommandList.Get(), _currFrameResource, static_cast<float>(mClientHeight), _isWireframe, _fixedLod);

	_terrainManager->Draw(mCommandList.Get(), _currFrameResource);
}

void MyApp::LightingPass() const
{
	_gBuffer->ChangeRtvsState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	_gBuffer->ChangeDsvState(D3D12_RESOURCE_STATE_DEPTH_READ);

	_lightingManager->DrawDirLight(mCommandList.Get(), _currFrameResource, _rayTracingEnabled);

	if (_lightingManager->LightsCount() > 0)
	{
		_gBuffer->ChangeDsvState(D3D12_RESOURCE_STATE_DEPTH_READ);
		_lightingManager->DrawLocalLights(mCommandList.Get(), _currFrameResource, _rayTracingEnabled);

		if (*_lightingManager->DebugEnabled())
		{
			_lightingManager->DrawDebug(mCommandList.Get(), _currFrameResource);
		}
	}

	//_lightingManager->DrawEmissive(mCommandList.Get(), _currFrameResource);
}

void MyApp::WireframePass() const
{
	_gBuffer->ChangeDsvState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
	_gBuffer->ClearInfo(Colors::Transparent);
	const auto currentBackBuffer = CurrentBackBufferView();
	const auto dsv = _gBuffer->DepthStencilView();
	mCommandList->OMSetRenderTargets(1, &currentBackBuffer, true, &dsv);

	_objectsManager->Draw(mCommandList.Get(), _currFrameResource, static_cast<float>(mClientHeight), _isWireframe);
}

void MyApp::FinalPass() const
{
	const auto currentBackBuffer = CurrentBackBufferView();
	mCommandList->OMSetRenderTargets(1, &currentBackBuffer, true, nullptr);
	_lightingManager->DrawIntoBackBuffer(mCommandList.Get(), _currFrameResource);
}

//some wndproc stuff
// ReSharper disable once CppInconsistentNaming
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool MyApp::checkForImGui(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}