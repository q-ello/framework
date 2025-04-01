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

DescriptorHeapAllocator g_heapAlloc;

static void ShowExampleMenuFile()
{
	ImGui::MenuItem("(demo menu)", NULL, false, false);
	if (ImGui::MenuItem("New")) {}
	if (ImGui::MenuItem("Open", "Ctrl+O")) {}
	if (ImGui::BeginMenu("Open Recent"))
	{
		ImGui::MenuItem("fish_hat.c");
		ImGui::MenuItem("fish_hat.inl");
		ImGui::MenuItem("fish_hat.h");
		if (ImGui::BeginMenu("More.."))
		{
			ImGui::MenuItem("Hello");
			ImGui::MenuItem("Sailor");
			if (ImGui::BeginMenu("Recurse.."))
			{
				ShowExampleMenuFile();
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
	if (ImGui::MenuItem("Save", "Ctrl+S")) {}
	if (ImGui::MenuItem("Save As..")) {}

	ImGui::Separator();
	if (ImGui::BeginMenu("Options"))
	{
		static bool enabled = true;
		ImGui::MenuItem("Enabled", "", &enabled);
		ImGui::BeginChild("child", ImVec2(0, 60), ImGuiChildFlags_Borders);
		for (int i = 0; i < 10; i++)
			ImGui::Text("Scrolling Text %d", i);
		ImGui::EndChild();
		static float f = 0.5f;
		static int n = 0;
		ImGui::SliderFloat("Value", &f, 0.0f, 1.0f);
		ImGui::InputFloat("Input", &f, 0.1f);
		ImGui::Combo("Combo", &n, "Yes\0No\0Maybe\0\0");
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Colors"))
	{
		float sz = ImGui::GetTextLineHeight();
		for (int i = 0; i < ImGuiCol_COUNT; i++)
		{
			const char* name = ImGui::GetStyleColorName((ImGuiCol)i);
			ImVec2 p = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), ImGui::GetColorU32((ImGuiCol)i));
			ImGui::Dummy(ImVec2(sz, sz));
			ImGui::SameLine();
			ImGui::MenuItem(name);
		}
		ImGui::EndMenu();
	}

	// Here we demonstrate appending again to the "Options" menu (which we already created above)
	// Of course in this demo it is a little bit silly that this function calls BeginMenu("Options") twice.
	// In a real code-base using it would make senses to use this feature from very different code locations.
	if (ImGui::BeginMenu("Options")) // <-- Append!
	{
		static bool b = true;
		ImGui::Checkbox("SomeOption", &b);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Disabled", false)) // Disabled
	{
		IM_ASSERT(0);
	}
	if (ImGui::MenuItem("Checked", NULL, true)) {}
	ImGui::Separator();
	if (ImGui::MenuItem("Quit", "Alt+F4")) {}
}

static void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");
	if (ImGui::BeginItemTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

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
	buildGrid();
	BuildModelGeometry();
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
	info.SrvDescriptorHeap = mSrvDescriptorHeap.Get();
	info.LegacySingleSrvCpuDescriptor = mSrvDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart();
	info.LegacySingleSrvGpuDescriptor = mSrvDescriptorHeap.Get()->GetGPUDescriptorHandleForHeapStart();
	/*info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
		{ return g_heapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
	info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
		{ return g_heapAlloc.Free(cpu_handle, gpu_handle); };*/

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

//change transform of my objects
void MyApp::processText(HWND hwnd, wchar_t* text)
{
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			if ((HWND)_transformControlsCoords[i][j]->hwnd.get() == hwnd)
			{
				wchar_t* end;
				float newNumber = std::wcstof(text, &end);
				if (i == 2 && newNumber == 0.f)
				{
					newNumber = .1f;
					if (!mAllRitems[_selectedObject]->lockedScale)
					{
						mAllRitems[_selectedObject]->transform[i][j] = newNumber;
						SetWindowText((HWND)_transformControlsCoords[i][j]->hwnd.get(),
							std::to_wstring(mAllRitems[_selectedObject]->transform[i][j]).c_str());
					}
				}
				else if (i == 2 && mAllRitems[_selectedObject]->lockedScale)
				{
					float difference = newNumber / mAllRitems[_selectedObject]->transform[i][j];
					for (int k = 0; k < 3; k++)
					{
						mAllRitems[_selectedObject]->transform[i][k] = mAllRitems[_selectedObject]->transform[i][k] * difference;
						SetWindowText((HWND)_transformControlsCoords[i][k]->hwnd.get(), 
							std::to_wstring(mAllRitems[_selectedObject]->transform[i][k]).c_str());
					}
				}
				else
				{
					mAllRitems[_selectedObject]->transform[i][j] = newNumber;
				}

				mAllRitems[_selectedObject]->NumFramesDirty = gNumFrameResources;
				return;
			}
		}
	}
}

void MyApp::OnResize()
{
	//resizing the render window
	resizeRenderWindow();
	OnResizing();
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void MyApp::OnResizing()
{
	//resizing the controls
	// Invalidate and force a redraw of the entire main window
	SetWindowPos(_transformPanel, NULL, mClientWidth - 270, 20, 270, 130, SWP_NOZORDER);

	InvalidateRect(mhMainWnd, NULL, true);
	UpdateWindow(mhMainWnd);
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

	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
		ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

		static auto first_time = true;
		if (first_time)
		{
			first_time = false;
			// Clear out existing layout
			ImGui::DockBuilderRemoveNode(dockspace_id);
			// Add empty node
			ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
			// Main node should cover entire window
			ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetWindowSize());
			// get id of main dock space area
			ImGuiID dockspace_main_id = dockspace_id;
			// Create a dock node for the right docked window
			ImGuiID right = ImGui::DockBuilderSplitNode(dockspace_main_id, ImGuiDir_Right, 0.25f, nullptr, &dockspace_main_id);

			ImGui::DockBuilderDockWindow("Content One", dockspace_main_id);
			ImGui::DockBuilderDockWindow("Content Two", dockspace_main_id);
			ImGui::DockBuilderDockWindow("Side Bar", right);
			ImGui::DockBuilderFinish(dockspace_id);
		}
	}

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

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

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

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
	if ((btnState & MK_LBUTTON) != 0 && GetCapture() == mhMainWnd)
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
	else if ((btnState & MK_RBUTTON) != 0 && GetCapture() == mhMainWnd)
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
	for (int i = 0; i < mAllRitems.size(); i++)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (mAllRitems[i]->NumFramesDirty > 0)
		{
			XMMATRIX scale = XMMatrixScaling(mAllRitems[i]->transform[2][0], mAllRitems[i]->transform[2][1], mAllRitems[i]->transform[2][2]);
			XMMATRIX rotation = XMMatrixRotationRollPitchYaw(mAllRitems[i]->transform[1][0] * XM_PI / 180., 
				mAllRitems[i]->transform[1][1] * XM_PI / 180.,
				mAllRitems[i]->transform[1][2] * XM_PI / 180.);
			XMMATRIX translation = XMMatrixTranslation(mAllRitems[i]->transform[0][0], mAllRitems[i]->transform[0][1], mAllRitems[i]->transform[0][2]);

			XMMATRIX world = scale * rotation * translation;
			XMStoreFloat4x4(&mAllRitems[i]->World, world);
			XMMATRIX texTransform = XMLoadFloat4x4(&mAllRitems[i]->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.useColor = i == 0;

			currObjectsCB[i].get()->CopyData(mAllRitems[i]->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			mAllRitems[i]->NumFramesDirty--;
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
	auto woodCrateTex = std::make_unique<Texture>();
	woodCrateTex->Name = L"woodCrateTex";
	//woodCrateTex->Filename = L"../../Textures/WoodCrate01_mipmapped.dds";
	woodCrateTex->Filename = L"../../Textures/african_head_diffuse.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), woodCrateTex->Filename.c_str(),
		woodCrateTex->Resource, woodCrateTex->UploadHeap));

	mTextures[woodCrateTex->Name] = std::move(woodCrateTex);

	//3rd exercise
	auto flareTex = std::make_unique<Texture>();
	flareTex->Name = L"flareTex";
	flareTex->Filename = L"../../Textures/flare.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), flareTex->Filename.c_str(), flareTex->Resource, flareTex->UploadHeap));
	mTextures[flareTex->Name] = std::move(flareTex);
	auto flareAlphaTex = std::make_unique<Texture>();
	flareAlphaTex->Name = L"flareAlphaTex";
	flareAlphaTex->Filename = L"../../Textures/flarealpha.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(), flareAlphaTex->Filename.c_str(),
		flareAlphaTex->Resource, flareAlphaTex->UploadHeap));
	mTextures[flareAlphaTex->Name] = std::move(flareAlphaTex);
}

void MyApp::BuildRootSignature()
{
	//3rd exercise
	CD3DX12_DESCRIPTOR_RANGE texTable[2];
	texTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	texTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(2, texTable, D3D12_SHADER_VISIBILITY_PIXEL);
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
	srvHeapDesc.NumDescriptors = 1;

	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDesc(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	auto woodCrateTex = mTextures[L"woodCrateTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = woodCrateTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = woodCrateTex->GetDesc().MipLevels;

	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	md3dDevice->CreateShaderResourceView(woodCrateTex.Get(), &srvDesc, hDescriptor);
	g_heapAlloc.Alloc(&hDescriptor, &gpuDesc);
}

void MyApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");

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

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs[L"grid"] = gridSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyApp::BuildModelGeometry(WCHAR* filename)
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
	std::wstring croppedName = name.substr(0, name.size() - 4);

	//check if geometry already exists
	if (mGeometries.find(croppedName) != mGeometries.end())
	{
		addRenderItem(croppedName);
		return;
	}

	//load new model

	std::unique_ptr<Model> model = std::make_unique<Model>(filename);

	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)model->vertices().size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)model->indices().size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();

	geo->Name = croppedName;

	if (!mFrameResources.empty())
	{
		// Reset command list and begin recording
		ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	}

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), model->vertices().data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), model->indices().data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), model->vertices().data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), model->indices().data(), ibByteSize, geo->IndexBufferUploader);

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

	if (mFrameResources.empty())
	{
		return;
	}

	ThrowIfFailed(mCommandList->Close());

	// Execute command list to commit GPU resource updates
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Flush command queue to ensure all commands are completed
	FlushCommandQueue();

	
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
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));
}

void MyApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
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

void MyApp::BuildRenderItems()
{
}

void MyApp::buildGrid()
{
	auto gridRItem = std::make_unique<RenderItem>();
	gridRItem->ObjCBIndex = 0;
	gridRItem->Mat = mMaterials["woodCrate"].get();
	gridRItem->Geo = mGeometries[L"gridGeo"].get();
	gridRItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	gridRItem->IndexCount = gridRItem->Geo->DrawArgs[L"grid"].IndexCount;
	gridRItem->StartIndexLocation = gridRItem->Geo->DrawArgs[L"grid"].StartIndexLocation;
	gridRItem->BaseVertexLocation = gridRItem->Geo->DrawArgs[L"grid"].BaseVertexLocation;
	mOpaqueRitems.push_back(gridRItem.get());
	mAllRitems.push_back(std::move(gridRItem));


	for (int i = 0; i < mFrameResources.size(); ++i)
	{
		mFrameResources[i]->addObjectBuffer(md3dDevice.Get());
	}
}

void MyApp::CreateControls()
{
	// Create the controls
	_addNewBtn = std::make_shared<Control>(CreateWindow(
		L"BUTTON", L"Add New", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
		10, 50, 100, 30, mhMainWnd, (HMENU)_newId, (HINSTANCE)GetWindowLongPtr(mhMainWnd, GWLP_HINSTANCE), NULL),
		_newId, L"Add New");

	_newId++;

	//creating transform panel
	_transformPanel = CreateWindowEx(
		0, L"STATIC", L"Transform", WS_CHILD | WS_VISIBLE,
		mClientWidth - 270, 20, 270, 130, mhMainWnd, NULL, (HINSTANCE)GetWindowLongPtr(mhMainWnd, GWLP_HINSTANCE), NULL);

	// Save the original WindowProc of the static control (_transformPanel)
	g_OriginalPanelWndProc = (WNDPROC)GetWindowLongPtr(_transformPanel, GWLP_WNDPROC);

	// Subclass the window (attach custom WindowProc)
	SetWindowLongPtr(_transformPanel, GWLP_WNDPROC, (LONG_PTR)TransformPanelProc);

	std::vector<std::wstring> labels = { L"Position", L"Rotation", L"Scale" };
	std::vector<std::wstring> labelCoords = { L"x:", L"y:", L"z:" };

	for (int i = 0; i < 3; i++)
	{
		_transformControls.push_back(CreateWindowEx(
			0, L"STATIC", labels[i].c_str(), WS_CHILD | WS_VISIBLE,
			0, 30 + 30*i, 260, 20, _transformPanel, NULL, (HINSTANCE)GetWindowLongPtr(_transformPanel, GWLP_HINSTANCE), NULL));
	}

	//forcing to send message for redrawing because windows is stupid!!
	for (HWND ctrl : _transformControls)
	{
		DRAWITEMSTRUCT dis = {};
		dis.CtlID = GetDlgCtrlID(ctrl);
		dis.hwndItem = ctrl;
		dis.hDC = GetDC(ctrl);
		dis.rcItem = { 0, 0, 260, 20 }; // Adjust as necessary

		SendMessage(mhMainWnd, WM_DRAWITEM, dis.CtlID, (LPARAM)&dis);

		ReleaseDC(ctrl, dis.hDC);
	}

	_lockScaleBtn = std::make_shared<Control>(CreateWindow(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_NOTIFY,
		40, 34 + 30 * 2, 12, 12, _transformPanel, (HMENU)_newId, (HINSTANCE)GetWindowLongPtr(_transformPanel, GWLP_HINSTANCE), NULL), _newId,
		L"Lock Scale");

	_newId++;

	for (int i = 0; i < 3; i++)
	{
		_transformControlsRects.push_back({});
		_transformControlsCoords.push_back({});
		for (int j = 0; j < 3; j++)
		{
			_transformControlsRects[i].push_back(CreateWindowEx(
				0, L"STATIC", labelCoords[j].c_str(), WS_CHILD | WS_VISIBLE,
				70 + 60 * j, 30 + 30 * i, 50, 20, _transformPanel, NULL, (HINSTANCE)GetWindowLongPtr(_transformPanel, GWLP_HINSTANCE), NULL));
			_transformControlsCoords[i].push_back(std::make_shared<Control>(CreateWindow(
				L"EDIT", L"", WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
				82 + 60 * j, 30 + 30 * i, 38, 20, _transformPanel, (HMENU)_newId, (HINSTANCE)GetWindowLongPtr(_transformPanel, GWLP_HINSTANCE), NULL),
				_newId, L"Edit Field"));

			SetWindowSubclass((HWND)_transformControlsCoords[i][j]->hwnd.get(), EditProc, 0, 0);
			_newId++;
		}
	}

	ShowWindow(_transformPanel, SW_SHOW);
	UpdateWindow(_transformPanel);
}

void MyApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto matCB = mCurrFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto objectCB = mCurrFrameResource->ObjectsCB[i]->Resource();
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		//CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		//tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		//cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
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

void MyApp::resizeRenderWindow()
{
}

bool MyApp::handleControls(WPARAM wParam)
{
	int controlId = LOWORD(wParam);
	//add new object
	if (controlId == _addNewBtn->id)
	{
		IFileOpenDialog* pFileOpen;

		// Create the FileOpenDialog object.
		ThrowIfFailed(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)));

		//filter only for .obj files
		COMDLG_FILTERSPEC rgSpec[] = { L"Wavefront Object", L"*.obj" };
		pFileOpen->SetFileTypes(1, rgSpec);

		// Show the Open dialog box.
		HRESULT hr = pFileOpen->Show(NULL);
		if (FAILED(hr))
		{
			if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
			{
				// User closed the dialog manually, just return safely
				return true;
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

		BuildModelGeometry(pszFilePath);

		CoTaskMemFree(pszFilePath);
		pItem->Release();
		pFileOpen->Release();

		return true;
	}
	//delete object
	if (controlId == DELETE_ID)
	{
		deleteObject();
		return true;
	}
	//focus on object
	for (int i = 0; i < _objectBtns.size(); i++)
	{
		if (_objectBtns[i]->id == controlId)
		{
			_selectedObject = i + 1;
			showTransform(true);
			return true;
		}
	}
	//lock scale
	if (controlId == _lockScaleBtn->id && HIWORD(wParam) == BN_CLICKED)
	{
		mAllRitems[_selectedObject]->lockedScale = !mAllRitems[_selectedObject]->lockedScale;
		return true;
	}
	//transform
	return false;
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
	auto modelRitem = std::make_unique<RenderItem>();
	modelRitem->ObjCBIndex = 0;
	modelRitem->Mat = mMaterials["woodCrate"].get();
	modelRitem->Geo = mGeometries[itemName].get();
	modelRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	modelRitem->IndexCount = modelRitem->Geo->DrawArgs[itemName].IndexCount;
	modelRitem->StartIndexLocation = modelRitem->Geo->DrawArgs[itemName].StartIndexLocation;
	modelRitem->BaseVertexLocation = modelRitem->Geo->DrawArgs[itemName].BaseVertexLocation;

	mOpaqueRitems.push_back(modelRitem.get());
	mAllRitems.push_back(std::move(modelRitem));

	addObjectControl(itemName);

	for (int i = 0; i < mFrameResources.size(); ++i)
	{
		mFrameResources[i]->addObjectBuffer(md3dDevice.Get());
	}
}

void MyApp::addObjectControl(const std::wstring& name)
{
	/*std::wstring counter;
	if (_objectCounters.find(name) != _objectCounters.end())
	{
		_objectCounters[name]++;
		counter = std::to_wstring(_objectCounters[name]);
	}
	else
	{
		_objectCounters[name] = 1;
		counter = L"";
	}

	std::wstring objName = name + counter;

	std::shared_ptr<Control> newObjBtn = std::make_shared<Control>(CreateWindow(
		L"BUTTON", objName.c_str(), WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_LEFT | BS_OWNERDRAW,
		10, 100 + (int)_objectBtns.size() * 40, 100, 30, mhMainWnd, (HMENU)_newId, (HINSTANCE)GetWindowLongPtr(mhMainWnd, GWLP_HINSTANCE), NULL),
		_newId, name);

	SetFocus((HWND)newObjBtn->hwnd.get());

	_objectBtns.push_back(std::move(newObjBtn));

	if (_objectLoaded.find(name) == _objectLoaded.end())
	{
		_objectLoaded[name] = 1;
	}
	else
	{
		_objectLoaded[name]++;
	}

	_selectedObject = (int)_objectBtns.size();
	showTransform(true);


	_newId++;*/
}

void MyApp::handleRightClickControls(HWND hwnd, int x, int y)
{
	// Check if the right-click happened on a specific control
	for (int i = 0; i < _objectBtns.size(); i++)
	{
		if (hwnd == _objectBtns[i]->hwnd.get())
		{
			_selectedObject = i + 1;
			SetFocus((HWND)_objectBtns[i]->hwnd.get());
			showTransform(true);

			HMENU hPopupMenu = CreatePopupMenu();
			AppendMenu(hPopupMenu, MF_STRING, DELETE_ID, L"Delete");
			// Show the menu at the clicked position
			TrackPopupMenu(hPopupMenu, TPM_RIGHTBUTTON, x, y, 0, mhMainWnd, NULL);
			DestroyMenu(hPopupMenu);
		}
	}
}

void MyApp::drawUI(LPDRAWITEMSTRUCT lpdis)
{
	HBRUSH hBrush;

	if (lpdis->CtlType == ODT_BUTTON) // Ensure it's a button
	{

		// Check button state
		if (lpdis->itemState & ODS_SELECTED) // Button pressed
		{
			hBrush = CreateSolidBrush(RGB(50, 50, 50)); // Darker blue
		}
		else if (lpdis->itemState & ODS_FOCUS) // Button focused
		{
			hBrush = CreateSolidBrush(RGB(60, 60, 60)); // Original blue
		}
		else // Default state
		{
			hBrush = CreateSolidBrush(RGB(90, 90, 90)); // Lighter blue
		}

		FillRect(lpdis->hDC, &lpdis->rcItem, hBrush);
		DeleteObject(hBrush); // Prevent memory leaks

		// Draw button text
		SetTextColor(lpdis->hDC, RGB(255, 255, 255)); // White text
		SetBkMode(lpdis->hDC, TRANSPARENT);

		wchar_t buttonText[256];
		GetWindowText(lpdis->hwndItem, buttonText, _countof(buttonText));
		DrawText(lpdis->hDC, buttonText, -1, &lpdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

		return;
	}
}

LRESULT MyApp::colorOtherData(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if ((HWND)lParam == _transformPanel)
	{
		HDC hdcStatic = (HDC)wParam;
		SetBkColor(hdcStatic, RGB(40, 40, 40));
		SetTextColor(hdcStatic, RGB(200, 200, 200));
		return (INT_PTR)_transformBG;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void MyApp::handlePaint()
{
}

void MyApp::showTransform(bool show)
{
	if (!show)
	{
		ShowWindow(_transformPanel, SW_HIDE);
		return;
	}

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			SetWindowText((HWND)_transformControlsCoords[i][j]->hwnd.get(), std::to_wstring(mAllRitems[_selectedObject]->transform[i][j]).c_str());
		}
	}

	SendMessage((HWND)_lockScaleBtn->hwnd.get(), BM_SETCHECK, mAllRitems[_selectedObject]->lockedScale ? BST_CHECKED : BST_UNCHECKED, 0);

	ShowWindow(_transformPanel, SW_SHOW);
}

bool MyApp::onKeyDown(UINT key)
{
	if (key == VK_DELETE && _selectedObject != -1)
	{
		HWND focusedSmth = GetFocus();
		for (auto& objBtn : _objectBtns)
		{
			if ((HWND)objBtn->hwnd.get() == focusedSmth)
			{
				deleteObject();
				return true;
			}
		}
		return false;
	}

	
	return false;
}


void MyApp::deleteObject()
{
	std::wstring name = _objectBtns[_selectedObject]->name;
	_objectLoaded[name]--;
	_objectBtns.erase(_objectBtns.begin() + _selectedObject - 1);
	mAllRitems.erase(mAllRitems.begin() + _selectedObject);
	mOpaqueRitems.erase(mOpaqueRitems.begin() + _selectedObject);

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

	for (int i = 0; i < _objectBtns.size(); i++)
	{
		SetWindowPos((HWND)_objectBtns[i]->hwnd.get(), NULL, 10, 100 + i * 40, 100, 30, SWP_NOZORDER);
	}

	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources[i]->removeObjectBuffer(md3dDevice.Get(), _selectedObject);
	}

	_selectedObject = -1;
	showTransform(false);
}

LRESULT CALLBACK TransformPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_COMMAND:
		if (HIWORD(wParam) == EN_CHANGE) // If the edit control text changes
		{
			HWND hWndEdit = (HWND)lParam;
			int len = GetWindowTextLength(hWndEdit);
			if (len == 0) return 0;

			wchar_t* text = new wchar_t[len + 1];
			GetWindowText(hWndEdit, text, len + 1);

			std::wstring filteredText;
			bool hasDot = false;
			bool hasMinus = false;

			for (int i = 0; i < len; i++)
			{
				wchar_t ch = text[i];

				if (iswdigit(ch)) // Allow digits
				{
					filteredText += ch;
				}
				else if (ch == L'.' && !hasDot) // Allow only one decimal point
				{
					filteredText += ch;
					hasDot = true;
				}
				else if (ch == L'-' && i == 0 && !hasMinus) // Allow minus sign only at the beginning
				{
					filteredText += ch;
					hasMinus = true;
				}
			}

			// If the filtered text differs from input, update the edit control
			if (filteredText != text)
			{
				SetWindowText(hWndEdit, filteredText.c_str());

				// Move the caret to the end of the text
				SendMessage(hWndEdit, EM_SETSEL, filteredText.size(), filteredText.size());
			}

			delete[] text;
		}
		// Handle command messages, like button clicks
		SendMessage(GetParent(hwnd), msg, wParam, lParam);
		break;

	case WM_CTLCOLORSTATIC:
	{
		HDC hdcStatic = (HDC)wParam;
		HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50));
		SetBkColor(hdcStatic, RGB(50, 50, 50));
		SetTextColor(hdcStatic, RGB(200, 200, 200));
		return (INT_PTR)hBrush;
	}

	default:
		// Forward all other messages to the original WindowProc
		return CallWindowProc(g_OriginalPanelWndProc, hwnd, msg, wParam, lParam);
	}

	return 0;
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_RETURN)
		{
			wchar_t text[256];
			GetWindowText(hwnd, text, 256);

			MyApp::GetApp()->processText(hwnd, text);

			return 0; // Prevents beeping
		}
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(hwnd, EditProc, uIdSubclass);
		break;
	case WM_CHAR:
		if (wParam == VK_RETURN)
		{
			return 0; // Block Enter key from making the beep
		}
		break; // Let all other characters, including arrows, be processed normally
	}
	
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

//some wndproc stuff
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool MyApp::checkForImGui(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}