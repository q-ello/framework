#include "d3dUtil.h"
#include <comdef.h>
#include <fstream>

using Microsoft::WRL::ComPtr;
#pragma comment(lib, "dxcompiler.lib")

DxException::DxException(const HRESULT hr, const std::wstring& functionName, const std::wstring& filename, const int lineNumber) :
    ErrorCode(hr),
    FunctionName(functionName),
    Filename(filename),
    LineNumber(lineNumber)
{
}

bool d3dUtil::IsKeyDown(const int vkeyCode)
{
    return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}

std::string d3dUtil::ToString(HRESULT hr)
{
    return std::string();
}

ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    std::ifstream fin(filename, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    const std::ifstream::pos_type size = static_cast<int>(fin.tellg());
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

    fin.read(static_cast<char*>(blob->GetBufferPointer()), size);
    fin.close();

    return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList4* cmdList,
    const void* initData,
    const UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;

    // Create the actual default buffer resource.
    const auto heapDefaultProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapDefaultProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    // In order to copy CPU memory data into our default buffer, we need to create
    // an intermediate upload heap.
    const auto heapUploadProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapUploadProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


    // Describe the data we want to copy into the default buffer.
    D3D12_SUBRESOURCE_DATA subResourceData;
    subResourceData.pData = initData;
    subResourceData.RowPitch = static_cast<LONG_PTR>(byteSize);
    subResourceData.SlicePitch = subResourceData.RowPitch;

    // Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
    // will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
    // the intermediate upload heap data will be copied to mBuffer.
    const auto transition1 = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
                                                                      D3D12_RESOURCE_STATE_COMMON,
                                                                      D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &transition1);
    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
    const auto transition2 = CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
                                                                                                   D3D12_RESOURCE_STATE_COPY_DEST,
                                                                                                   D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &transition2);

    // Note: uploadBuffer has to be kept alive after the above function calls because
    // the command list has not been executed yet that performs the actual copy.
    // The caller can Release the uploadBuffer after it knows the copy has been executed.


    return defaultBuffer;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
    const std::wstring& filename,
    const D3D_SHADER_MACRO* defines,
    const std::string& entrypoint,
    const std::string& target)
{
    UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> byteCode = nullptr;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

    if (errors != nullptr)
        OutputDebugStringA(static_cast<char*>(errors->GetBufferPointer()));

    ThrowIfFailed(hr);

    return byteCode;
}

Microsoft::WRL::ComPtr<IDxcBlob> d3dUtil::CompileDxilLibrary(const std::wstring& filename)
{
    ComPtr<IDxcUtils> utils;
    ComPtr<IDxcCompiler3> compiler;
    ComPtr<IDxcResult> result;
    ComPtr<IDxcBlob> shaderBlob;
    ComPtr<IDxcBlobUtf8> errors;

    ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)));
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));

    ComPtr<IDxcBlobEncoding> source;
    ThrowIfFailed(utils->LoadFile(filename.c_str(), nullptr, &source));

    DxcBuffer buffer;
    buffer.Ptr = source->GetBufferPointer();
    buffer.Size = source->GetBufferSize();
    buffer.Encoding = DXC_CP_UTF8;

    // DXIL library compile arguments
    std::vector<LPCWSTR> args = {
        L"-T", L"lib_6_3",      // DXIL library
        L"-Zi",                // debug info
        L"-Qembed_debug",      // embed PDB
        L"-Od"                 // disable optimization (debug)
    };

    ThrowIfFailed(compiler->Compile(
        &buffer,
        args.data(),
        static_cast<UINT>(args.size()),
        nullptr,
        IID_PPV_ARGS(&result)
    ));

    HRESULT hr;
    ThrowIfFailed(result->GetStatus(&hr));

    if (FAILED(hr))
    {
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0)
        {
            OutputDebugStringA(errors->GetStringPointer());
        }
        throw std::runtime_error("DXC shader compilation failed");
    }

    ThrowIfFailed(result->GetOutput(
        DXC_OUT_OBJECT,
        IID_PPV_ARGS(&shaderBlob),
        nullptr
    ));
    
    return shaderBlob;
}

std::wstring DxException::ToString() const
{
    // Get the string description of the error code.
    const _com_error err(ErrorCode);
    const std::wstring msg = err.ErrorMessage();

    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}
