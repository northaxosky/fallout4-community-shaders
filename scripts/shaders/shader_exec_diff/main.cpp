#define NOMINMAX

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <bcrypt.h>
#include <winver.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef FO4CS_EXEC_HARNESS_SOURCE_SHA256
#define FO4CS_EXEC_HARNESS_SOURCE_SHA256 "unembedded"
#endif

using Microsoft::WRL::ComPtr;

namespace
{
enum class Fixture
{
    Adversarial,
    Native,
};

const char* FixtureName(Fixture fixture)
{
    return fixture == Fixture::Native ? "native" : "adversarial";
}

constexpr const char* FrontFaceProbeVersion = "front-face-probe-v1";
constexpr const char* FrontFaceProbeVertexSource =
    "float4 main(uint id:SV_VertexID):SV_Position{"
    "float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};"
    "return float4(p[id],0,1);}";
constexpr const char* FrontFaceProbePixelSource =
    "float main(bool front:SV_IsFrontFace):SV_Target{"
    "return front?1.0f:0.0f;}";

struct FrontFaceProbeResult
{
    bool clockwiseStateFront = false;
    bool counterClockwiseStateFront = false;
};

struct Options
{
    std::string referencePath;
    std::string candidatePath;
    UINT seeds = 8;
    UINT seedBase = 0;
    UINT width = 256;
    UINT height = 256;
    float toleranceAbsolute = 2.0e-3f;
    float toleranceRelative = 1.0e-2f;
    bool verbose = false;
    bool xegtaoAo = false;
    bool selfTest = false;
    Fixture fixture = Fixture::Adversarial;
    std::string measurementJsonPath;
    UINT minimumBucketPopulation = 64;
    std::vector<std::string> requiredBuckets;
    std::string referencePrefilterPath;
    std::string candidatePrefilterPath;
};

struct ConstantBufferBinding
{
    std::string name;
    UINT bindPoint = 0;
    UINT size = 0;
};

struct ResourceBinding
{
    std::string name;
    UINT bindPoint = 0;
    UINT bindCount = 0;
    D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;
};

struct SamplerBinding
{
    std::string name;
    UINT bindPoint = 0;
    UINT bindCount = 0;
};

struct SignatureParameter
{
    std::string semanticName;
    UINT semanticIndex = 0;
    UINT shaderRegister = 0;
    BYTE mask = 0;
    D3D_REGISTER_COMPONENT_TYPE componentType = D3D_REGISTER_COMPONENT_UNKNOWN;
    D3D_NAME systemValue = D3D_NAME_UNDEFINED;
};

struct ShaderContract
{
    std::vector<ConstantBufferBinding> constantBuffers;
    std::vector<ResourceBinding> resources;
    std::vector<SamplerBinding> samplers;
    std::vector<SignatureParameter> inputs;
    std::vector<SignatureParameter> outputs;
    UINT renderTargetCount = 0;
};

struct DisassemblyInfo
{
    std::map<UINT, UINT> constantBufferSizes;
    std::set<UINT> comparisonSamplers;
    std::set<UINT> samplers;
    std::map<UINT, D3D_SRV_DIMENSION> resourceDimensions;
};

enum class InputProfile
{
    Unshaped,
    DirectionalLighting,
    AmbientIbl,
    DeferredPrepass,
};

const char* InputProfileName(InputProfile profile)
{
    switch (profile)
    {
    case InputProfile::DirectionalLighting:
        return "directional-lighting";
    case InputProfile::AmbientIbl:
        return "ambient-ibl";
    case InputProfile::DeferredPrepass:
        return "deferred-prepass";
    default:
        return "unshaped";
    }
}

struct InputScenario
{
    UINT id = 0;
    UINT randomSeed = 0;
    bool dedicated = false;
    std::string semanticId;
    std::map<std::string, float> controls;
};

struct ResourceFormatRecord
{
    UINT bindPoint = 0;
    std::string dimension;
    std::string resourceFormat;
    std::string srvFormat;

    bool operator<(const ResourceFormatRecord& other) const
    {
        return std::tie(bindPoint, dimension, resourceFormat, srvFormat) <
            std::tie(
                other.bindPoint, other.dimension,
                other.resourceFormat, other.srvFormat);
    }
};

struct BoundConstantBuffer
{
    UINT bindPoint = 0;
    ComPtr<ID3D11Buffer> buffer;
    std::vector<float> values;
};

struct BoundResource
{
    UINT bindPoint = 0;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11ShaderResourceView> view;
    UINT width = 0;
    UINT height = 0;
    UINT arraySize = 0;
    std::vector<std::uint8_t> encodedBytes;
    std::vector<float> decodedValues;
    ResourceFormatRecord format;
};

struct BoundSampler
{
    UINT bindPoint = 0;
    ComPtr<ID3D11SamplerState> sampler;
    D3D11_SAMPLER_DESC descriptor{};
};

struct SeedResources
{
    std::vector<BoundConstantBuffer> constantBuffers;
    std::vector<BoundResource> resources;
    std::vector<BoundSampler> samplers;
    std::vector<ResourceFormatRecord> formats;
};

using Pixel = std::array<float, 4>;
using RenderTargetPixels = std::vector<Pixel>;
using RenderOutputs = std::vector<RenderTargetPixels>;

struct WorstComponent
{
    bool present = false;
    UINT seed = 0;
    UINT renderTarget = 0;
    UINT x = 0;
    UINT y = 0;
    UINT channel = 0;
    float reference = 0.0f;
    float candidate = 0.0f;
    double absoluteDifference = 0.0;
    double relativeDifference = 0.0;
};

struct DivergentPixel
{
    UINT seed = 0;
    UINT renderTarget = 0;
    UINT x = 0;
    UINT y = 0;
    Pixel reference{};
    Pixel candidate{};
    double score = 0.0;
};

struct ComparisonStats
{
    std::uint64_t totalPixels = 0;
    std::uint64_t totalChannels = 0;
    std::uint64_t divergentChannels = 0;
    std::uint64_t divergentPixels = 0;
    double maximumAbsoluteDifference = 0.0;
    double maximumRelativeDifference = 0.0;
    double absoluteDifferenceSum = 0.0;
    double relativeDifferenceSum = 0.0;
    WorstComponent worst;
    std::vector<DivergentPixel> topDivergences;
};

struct CoverageStats
{
    std::uint64_t population = 0;
    ComparisonStats comparison;
};

struct BucketMask
{
    std::uint64_t population = 0;
    std::vector<std::uint8_t> pixels;
};

[[noreturn]] void ThrowFailure(const std::string& message)
{
    throw std::runtime_error(message);
}

void CheckHRESULT(HRESULT result, const std::string& action)
{
    if (FAILED(result))
    {
        std::ostringstream message;
        message << action << " failed (HRESULT 0x" << std::hex << std::uppercase
                << static_cast<unsigned long>(result) << ")";
        ThrowFailure(message.str());
    }
}

class Sha256
{
public:
    Sha256()
    {
        CheckStatus(
            BCryptOpenAlgorithmProvider(
                &algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
            "BCryptOpenAlgorithmProvider");
        DWORD objectSize = 0;
        DWORD returned = 0;
        CheckStatus(
            BCryptGetProperty(
                algorithm_, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
                &returned, 0),
            "BCryptGetProperty");
        object_.resize(objectSize);
        CheckStatus(
            BCryptCreateHash(
                algorithm_, &hash_, object_.data(), objectSize,
                nullptr, 0, 0),
            "BCryptCreateHash");
    }

    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    ~Sha256()
    {
        if (hash_ != nullptr)
            BCryptDestroyHash(hash_);
        if (algorithm_ != nullptr)
            BCryptCloseAlgorithmProvider(algorithm_, 0);
    }

    void Add(const void* data, std::size_t size)
    {
        if (finished_)
            ThrowFailure("SHA-256 input added after finalization");
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        while (size != 0)
        {
            const ULONG chunk = static_cast<ULONG>(std::min<std::size_t>(
                size, std::numeric_limits<ULONG>::max()));
            CheckStatus(
                BCryptHashData(
                    hash_, const_cast<PUCHAR>(bytes), chunk, 0),
                "BCryptHashData");
            bytes += chunk;
            size -= chunk;
        }
    }

    template <class Value>
    void AddValue(const Value& value)
    {
        static_assert(std::is_trivially_copyable_v<Value>);
        Add(&value, sizeof(value));
    }

    void AddString(const std::string& value)
    {
        const std::uint64_t size = value.size();
        AddValue(size);
        Add(value.data(), value.size());
    }

    std::string Finish()
    {
        if (finished_)
            return digest_;
        std::array<std::uint8_t, 32> bytes{};
        CheckStatus(
            BCryptFinishHash(
                hash_, bytes.data(), static_cast<ULONG>(bytes.size()), 0),
            "BCryptFinishHash");
        std::ostringstream text;
        text << std::hex << std::setfill('0');
        for (const std::uint8_t byte : bytes)
            text << std::setw(2) << static_cast<unsigned>(byte);
        digest_ = text.str();
        finished_ = true;
        return digest_;
    }

private:
    static void CheckStatus(NTSTATUS status, const char* action)
    {
        if (status < 0)
            ThrowFailure(std::string(action) + " failed");
    }

    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<std::uint8_t> object_;
    bool finished_ = false;
    std::string digest_;
};

struct RuntimeComponent
{
    std::string name;
    std::string state = "unavailable";
    std::string version;
    std::string sha256;
    std::uint64_t size = 0;
};

std::string FileVersion(const wchar_t* path)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0)
        return "unknown";
    std::vector<std::uint8_t> bytes(size);
    if (!GetFileVersionInfoW(path, 0, size, bytes.data()))
        return "unknown";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueW(
            bytes.data(), L"\\",
            reinterpret_cast<void**>(&info), &infoSize) ||
        info == nullptr ||
        infoSize < static_cast<UINT>(sizeof(VS_FIXEDFILEINFO)))
    {
        return "unknown";
    }
    std::ostringstream version;
    version << HIWORD(info->dwFileVersionMS) << "."
            << LOWORD(info->dwFileVersionMS) << "."
            << HIWORD(info->dwFileVersionLS) << "."
            << LOWORD(info->dwFileVersionLS);
    return version.str();
}

RuntimeComponent FingerprintRuntimeComponent(
    const wchar_t* moduleName,
    const char* stableName)
{
    RuntimeComponent component;
    component.name = stableName;
    HMODULE module = LoadLibraryW(moduleName);
    if (module == nullptr)
        return component;

    std::vector<wchar_t> path(32768);
    const DWORD pathLength = GetModuleFileNameW(
        module, path.data(), static_cast<DWORD>(path.size()));
    if (pathLength == 0 ||
        pathLength >= static_cast<DWORD>(path.size()))
    {
        FreeLibrary(module);
        return component;
    }

    HANDLE file = CreateFileW(
        path.data(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        FreeLibrary(module);
        return component;
    }

    LARGE_INTEGER fileSize{};
    Sha256 hash;
    std::array<std::uint8_t, 65536> buffer{};
    bool complete = GetFileSizeEx(file, &fileSize) != FALSE;
    while (complete)
    {
        DWORD bytesRead = 0;
        if (!::ReadFile(
                file, buffer.data(), static_cast<DWORD>(buffer.size()),
                &bytesRead, nullptr))
        {
            complete = false;
            break;
        }
        if (bytesRead == 0)
            break;
        hash.Add(buffer.data(), bytesRead);
    }
    CloseHandle(file);
    if (complete && fileSize.QuadPart >= 0)
    {
        component.state = "available";
        component.version = FileVersion(path.data());
        component.sha256 = hash.Finish();
        component.size = static_cast<std::uint64_t>(fileSize.QuadPart);
    }
    FreeLibrary(module);
    return component;
}

std::vector<RuntimeComponent> RuntimeComponents()
{
    std::vector<RuntimeComponent> components{
        FingerprintRuntimeComponent(L"d3d10warp.dll", "d3d10warp.dll"),
        FingerprintRuntimeComponent(L"d3d11.dll", "d3d11.dll"),
    };
    std::sort(
        components.begin(), components.end(),
        [](const RuntimeComponent& left, const RuntimeComponent& right) {
            return left.name < right.name;
        });
    return components;
}

std::vector<std::uint8_t> ReadFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        ThrowFailure("could not open shader: " + path);
    }

    const std::streamoff length = stream.tellg();
    if (length <= 0)
    {
        ThrowFailure("shader is empty: " + path);
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!stream)
    {
        ThrowFailure("could not read shader: " + path);
    }
    return bytes;
}

ShaderContract ReflectShader(const std::vector<std::uint8_t>& bytecode)
{
    ComPtr<ID3D11ShaderReflection> reflection;
    CheckHRESULT(
        D3DReflect(bytecode.data(), bytecode.size(), IID_ID3D11ShaderReflection,
                   reinterpret_cast<void**>(reflection.GetAddressOf())),
        "D3DReflect");

    D3D11_SHADER_DESC shaderDesc{};
    CheckHRESULT(reflection->GetDesc(&shaderDesc), "ID3D11ShaderReflection::GetDesc");

    ShaderContract contract;
    std::map<std::string, D3D11_SHADER_INPUT_BIND_DESC> boundByName;
    for (UINT index = 0; index < shaderDesc.BoundResources; ++index)
    {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        CheckHRESULT(reflection->GetResourceBindingDesc(index, &binding),
                     "GetResourceBindingDesc");
        boundByName.emplace(binding.Name != nullptr ? binding.Name : "", binding);

        if (binding.Type == D3D_SIT_TEXTURE)
        {
            contract.resources.push_back({
                binding.Name != nullptr ? binding.Name : "",
                binding.BindPoint,
                binding.BindCount,
                binding.Dimension,
            });
        }
        else if (binding.Type == D3D_SIT_SAMPLER)
        {
            contract.samplers.push_back({
                binding.Name != nullptr ? binding.Name : "",
                binding.BindPoint,
                binding.BindCount,
            });
        }
    }

    for (UINT index = 0; index < shaderDesc.ConstantBuffers; ++index)
    {
        ID3D11ShaderReflectionConstantBuffer* constantBuffer =
            reflection->GetConstantBufferByIndex(index);
        if (constantBuffer == nullptr)
        {
            ThrowFailure("GetConstantBufferByIndex returned null");
        }

        D3D11_SHADER_BUFFER_DESC bufferDesc{};
        CheckHRESULT(constantBuffer->GetDesc(&bufferDesc), "constant-buffer GetDesc");
        const std::string name = bufferDesc.Name != nullptr ? bufferDesc.Name : "";
        const auto binding = boundByName.find(name);
        if (binding == boundByName.end() || binding->second.Type != D3D_SIT_CBUFFER)
        {
            ThrowFailure("constant buffer has no b-register binding: " + name);
        }
        contract.constantBuffers.push_back({name, binding->second.BindPoint, bufferDesc.Size});
    }

    for (UINT index = 0; index < shaderDesc.InputParameters; ++index)
    {
        D3D11_SIGNATURE_PARAMETER_DESC parameter{};
        CheckHRESULT(reflection->GetInputParameterDesc(index, &parameter),
                     "GetInputParameterDesc");
        contract.inputs.push_back({
            parameter.SemanticName != nullptr ? parameter.SemanticName : "",
            parameter.SemanticIndex,
            parameter.Register,
            parameter.Mask,
            parameter.ComponentType,
            parameter.SystemValueType,
        });
    }

    for (UINT index = 0; index < shaderDesc.OutputParameters; ++index)
    {
        D3D11_SIGNATURE_PARAMETER_DESC parameter{};
        CheckHRESULT(reflection->GetOutputParameterDesc(index, &parameter),
                     "GetOutputParameterDesc");
        contract.outputs.push_back({
            parameter.SemanticName != nullptr ? parameter.SemanticName : "",
            parameter.SemanticIndex,
            parameter.Register,
            parameter.Mask,
            parameter.ComponentType,
            parameter.SystemValueType,
        });
        if (parameter.SystemValueType == D3D_NAME_TARGET)
        {
            contract.renderTargetCount =
                std::max(contract.renderTargetCount, parameter.Register + 1);
        }
    }

    if (contract.renderTargetCount == 0)
    {
        ThrowFailure("pixel shader has no SV_Target outputs");
    }
    if (contract.renderTargetCount > D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT)
    {
        ThrowFailure("pixel shader declares too many render targets");
    }
    return contract;
}

D3D_SRV_DIMENSION ParseDimensionName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (name == "texture1d")
        return D3D_SRV_DIMENSION_TEXTURE1D;
    if (name == "texture1darray")
        return D3D_SRV_DIMENSION_TEXTURE1DARRAY;
    if (name == "texture2d")
        return D3D_SRV_DIMENSION_TEXTURE2D;
    if (name == "texture2darray")
        return D3D_SRV_DIMENSION_TEXTURE2DARRAY;
    if (name == "texture2dms")
        return D3D_SRV_DIMENSION_TEXTURE2DMS;
    if (name == "texture2dmsarray")
        return D3D_SRV_DIMENSION_TEXTURE2DMSARRAY;
    if (name == "texture3d")
        return D3D_SRV_DIMENSION_TEXTURE3D;
    if (name == "texturecube")
        return D3D_SRV_DIMENSION_TEXTURECUBE;
    if (name == "texturecubearray")
        return D3D_SRV_DIMENSION_TEXTURECUBEARRAY;
    if (name == "buffer")
        return D3D_SRV_DIMENSION_BUFFER;
    return D3D_SRV_DIMENSION_UNKNOWN;
}

DisassemblyInfo InspectDisassembly(const std::vector<std::uint8_t>& bytecode)
{
    ComPtr<ID3DBlob> disassembly;
    CheckHRESULT(
        D3DDisassemble(bytecode.data(), bytecode.size(), 0, nullptr, &disassembly),
        "D3DDisassemble");

    const std::string text(
        static_cast<const char*>(disassembly->GetBufferPointer()),
        disassembly->GetBufferSize());
    DisassemblyInfo info;

    const std::regex samplerPattern(
        R"(dcl_sampler\s+s(\d+)\s*,\s*(mode_[a-z]+))",
        std::regex_constants::icase);
    for (std::sregex_iterator match(text.begin(), text.end(), samplerPattern), end;
         match != end; ++match)
    {
        const UINT bindPoint = static_cast<UINT>(std::stoul((*match)[1].str()));
        info.samplers.insert(bindPoint);
        std::string mode = (*match)[2].str();
        std::transform(mode.begin(), mode.end(), mode.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                       });
        if (mode == "mode_comparison")
            info.comparisonSamplers.insert(bindPoint);
    }

    const std::regex constantBufferPattern(
        R"(dcl_constantbuffer\s+CB(\d+)\[(\d+)\])",
        std::regex_constants::icase);
    for (std::sregex_iterator match(
             text.begin(), text.end(), constantBufferPattern), end;
         match != end; ++match)
    {
        const UINT bindPoint =
            static_cast<UINT>(std::stoul((*match)[1].str()));
        const UINT vectorCount =
            static_cast<UINT>(std::stoul((*match)[2].str()));
        info.constantBufferSizes[bindPoint] = vectorCount * 16;
    }

    const std::regex resourcePattern(
        R"(dcl_resource_([a-z0-9]+)[^\r\n]*\bt(\d+)\b)",
        std::regex_constants::icase);
    for (std::sregex_iterator match(text.begin(), text.end(), resourcePattern), end;
         match != end; ++match)
    {
        const D3D_SRV_DIMENSION dimension = ParseDimensionName((*match)[1].str());
        if (dimension != D3D_SRV_DIMENSION_UNKNOWN)
        {
            info.resourceDimensions[static_cast<UINT>(std::stoul((*match)[2].str()))] =
                dimension;
        }
    }
    return info;
}

const char* DimensionName(D3D_SRV_DIMENSION dimension)
{
    switch (dimension)
    {
    case D3D_SRV_DIMENSION_BUFFER:
        return "buffer";
    case D3D_SRV_DIMENSION_TEXTURE1D:
        return "texture1d";
    case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
        return "texture1darray";
    case D3D_SRV_DIMENSION_TEXTURE2D:
        return "texture2d";
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        return "texture2darray";
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
        return "texture2dms";
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
        return "texture2dmsarray";
    case D3D_SRV_DIMENSION_TEXTURE3D:
        return "texture3d";
    case D3D_SRV_DIMENSION_TEXTURECUBE:
        return "texturecube";
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        return "texturecubearray";
    case D3D_SRV_DIMENSION_BUFFEREX:
        return "bufferex";
    default:
        return "unknown";
    }
}

void ApplyDisassemblyContract(ShaderContract& contract, const DisassemblyInfo& disassembly)
{
    // Corpus blobs can omit RDEF bindings while retaining executable declarations.
    for (const auto& [bindPoint, size] : disassembly.constantBufferSizes)
    {
        const auto found = std::find_if(
            contract.constantBuffers.begin(), contract.constantBuffers.end(),
            [bindPoint](const ConstantBufferBinding& binding) {
                return binding.bindPoint == bindPoint;
            });
        if (found == contract.constantBuffers.end())
            contract.constantBuffers.push_back(
                {"CB" + std::to_string(bindPoint), bindPoint, size});
    }

    for (const auto& [bindPoint, dimension] : disassembly.resourceDimensions)
    {
        const auto found = std::find_if(
            contract.resources.begin(), contract.resources.end(),
            [bindPoint](const ResourceBinding& binding) {
                return bindPoint >= binding.bindPoint &&
                    bindPoint < binding.bindPoint + binding.bindCount;
            });
        if (found == contract.resources.end())
            contract.resources.push_back(
                {"t" + std::to_string(bindPoint), bindPoint, 1, dimension});
    }

    for (const UINT bindPoint : disassembly.samplers)
    {
        const auto found = std::find_if(
            contract.samplers.begin(), contract.samplers.end(),
            [bindPoint](const SamplerBinding& binding) {
                return bindPoint >= binding.bindPoint &&
                    bindPoint < binding.bindPoint + binding.bindCount;
            });
        if (found == contract.samplers.end())
            contract.samplers.push_back(
                {"s" + std::to_string(bindPoint), bindPoint, 1});
    }

    for (ResourceBinding& resource : contract.resources)
    {
        for (UINT offset = 0; offset < resource.bindCount; ++offset)
        {
            const UINT bindPoint = resource.bindPoint + offset;
            const auto dimension = disassembly.resourceDimensions.find(bindPoint);
            if (dimension != disassembly.resourceDimensions.end() &&
                dimension->second != resource.dimension)
            {
                std::cerr << "WARNING: reference reflection reports t" << bindPoint
                          << " as " << DimensionName(resource.dimension)
                          << ", disassembly reports " << DimensionName(dimension->second)
                          << "; using disassembly\n";
                resource.dimension = dimension->second;
            }
        }
    }

    std::sort(
        contract.constantBuffers.begin(), contract.constantBuffers.end(),
        [](const ConstantBufferBinding& left, const ConstantBufferBinding& right) {
            return left.bindPoint < right.bindPoint;
        });
    std::sort(
        contract.resources.begin(), contract.resources.end(),
        [](const ResourceBinding& left, const ResourceBinding& right) {
            return left.bindPoint < right.bindPoint;
        });
    std::sort(
        contract.samplers.begin(), contract.samplers.end(),
        [](const SamplerBinding& left, const SamplerBinding& right) {
            return left.bindPoint < right.bindPoint;
        });
}

template <class Value>
void WarnMapDifference(
    const char* label,
    const std::map<UINT, Value>& reference,
    const std::map<UINT, Value>& candidate)
{
    if (reference == candidate)
        return;
    ThrowFailure(std::string("contract_mismatch: ") + label);
}

void WarnContractDifferences(
    const ShaderContract& reference,
    const ShaderContract& candidate,
    const DisassemblyInfo& referenceDisassembly,
    const DisassemblyInfo& candidateDisassembly)
{
    std::map<UINT, UINT> referenceConstantBuffers;
    std::map<UINT, UINT> candidateConstantBuffers;
    for (const ConstantBufferBinding& binding : reference.constantBuffers)
        referenceConstantBuffers[binding.bindPoint] = binding.size;
    for (const ConstantBufferBinding& binding : candidate.constantBuffers)
        candidateConstantBuffers[binding.bindPoint] = binding.size;
    WarnMapDifference("constant-buffer register/size", referenceConstantBuffers,
                      candidateConstantBuffers);

    std::map<UINT, D3D_SRV_DIMENSION> referenceResources;
    std::map<UINT, D3D_SRV_DIMENSION> candidateResources;
    for (const ResourceBinding& binding : reference.resources)
        for (UINT offset = 0; offset < binding.bindCount; ++offset)
            referenceResources[binding.bindPoint + offset] = binding.dimension;
    for (const ResourceBinding& binding : candidate.resources)
        for (UINT offset = 0; offset < binding.bindCount; ++offset)
            candidateResources[binding.bindPoint + offset] = binding.dimension;
    WarnMapDifference("SRV register/dimension", referenceResources, candidateResources);

    std::set<UINT> referenceSamplers;
    std::set<UINT> candidateSamplers;
    for (const SamplerBinding& binding : reference.samplers)
        for (UINT offset = 0; offset < binding.bindCount; ++offset)
            referenceSamplers.insert(binding.bindPoint + offset);
    for (const SamplerBinding& binding : candidate.samplers)
        for (UINT offset = 0; offset < binding.bindCount; ++offset)
            candidateSamplers.insert(binding.bindPoint + offset);
    if (referenceSamplers != candidateSamplers)
        ThrowFailure("contract_mismatch: sampler registers");
    if (referenceDisassembly.comparisonSamplers !=
        candidateDisassembly.comparisonSamplers)
        ThrowFailure("contract_mismatch: comparison samplers");

    const auto signatureKey = [](const SignatureParameter& parameter) {
        return std::make_tuple(
            parameter.semanticName, parameter.semanticIndex, parameter.shaderRegister,
            parameter.mask, parameter.componentType, parameter.systemValue);
    };
    std::vector<decltype(signatureKey(reference.inputs.front()))> referenceInputs;
    std::vector<decltype(signatureKey(reference.inputs.front()))> candidateInputs;
    for (const SignatureParameter& parameter : reference.inputs)
        referenceInputs.push_back(signatureKey(parameter));
    for (const SignatureParameter& parameter : candidate.inputs)
        candidateInputs.push_back(signatureKey(parameter));
    if (referenceInputs != candidateInputs)
        ThrowFailure("contract_mismatch: input signature");

    std::vector<decltype(signatureKey(reference.outputs.front()))> referenceOutputs;
    std::vector<decltype(signatureKey(reference.outputs.front()))> candidateOutputs;
    for (const SignatureParameter& parameter : reference.outputs)
        referenceOutputs.push_back(signatureKey(parameter));
    for (const SignatureParameter& parameter : candidate.outputs)
        candidateOutputs.push_back(signatureKey(parameter));
    if (referenceOutputs != candidateOutputs)
        ThrowFailure("contract_mismatch: output signature");
}

UINT ComponentCount(BYTE mask)
{
    UINT count = 0;
    for (UINT component = 0; component < 4; ++component)
    {
        if ((mask & (1u << component)) != 0)
            count = component + 1;
    }
    if (count == 0)
        ThrowFailure("input signature contains an empty component mask");
    return count;
}

std::string HlslType(D3D_REGISTER_COMPONENT_TYPE type, UINT componentCount)
{
    const char* baseType = nullptr;
    switch (type)
    {
    case D3D_REGISTER_COMPONENT_FLOAT32:
        baseType = "float";
        break;
    case D3D_REGISTER_COMPONENT_UINT32:
        baseType = "uint";
        break;
    case D3D_REGISTER_COMPONENT_SINT32:
        baseType = "int";
        break;
    default:
        ThrowFailure("unsupported input-signature component type");
    }
    return std::string(baseType) +
        (componentCount == 1 ? "" : std::to_string(componentCount));
}

std::string Semantic(const SignatureParameter& parameter)
{
    if (parameter.semanticName.rfind("SV_", 0) == 0)
        return parameter.semanticName;
    return parameter.semanticName + std::to_string(parameter.semanticIndex);
}

std::string ValueExpression(
    D3D_REGISTER_COMPONENT_TYPE type,
    UINT componentCount,
    UINT width,
    UINT height)
{
    std::vector<std::string> components;
    if (type == D3D_REGISTER_COMPONENT_FLOAT32)
    {
        components = {
            "uv.x * " + std::to_string(width) + ".0f",
            "uv.y * " + std::to_string(height) + ".0f",
            "(float)vertexId * 0.5f",
            "1.0f",
        };
    }
    else if (type == D3D_REGISTER_COMPONENT_UINT32)
    {
        components = {"vertexId", "vertexId + 1u", "vertexId + 2u", "1u"};
    }
    else
    {
        components = {
            "(int)vertexId", "(int)vertexId + 1", "(int)vertexId + 2", "1"};
    }

    if (componentCount == 1)
        return components[0];

    std::ostringstream expression;
    expression << HlslType(type, componentCount) << "(";
    for (UINT component = 0; component < componentCount; ++component)
    {
        if (component != 0)
            expression << ", ";
        expression << components[component];
    }
    expression << ")";
    return expression.str();
}

bool IsRasterizerGeneratedInput(D3D_NAME systemValue)
{
    return systemValue == D3D_NAME_IS_FRONT_FACE ||
        systemValue == D3D_NAME_PRIMITIVE_ID ||
        systemValue == D3D_NAME_SAMPLE_INDEX ||
        systemValue == D3D_NAME_COVERAGE;
}

std::string DeferredPrepassValueExpression(
    const SignatureParameter& parameter)
{
    if (parameter.semanticName != "TEXCOORD")
        return "";
    switch (parameter.semanticIndex)
    {
    case 0:
        return "float3(1.0f, 0.0f, 0.0f)";
    case 1:
        return "float3(0.0f, 1.0f, 0.0f)";
    case 2:
        return "float3(0.0f, 0.0f, -1.0f)";
    case 3:
        return "float4(clip.x, clip.y, 2.0f + (float)vertexId * 0.1f, uv.x)";
    case 4:
        return "float4(clip.x - 0.01f, clip.y + 0.01f, "
            "2.0f + (float)vertexId * 0.1f, uv.y)";
    default:
        return "";
    }
}

ComPtr<ID3D11VertexShader> CreatePassthroughVertexShader(
    ID3D11Device* device,
    const ShaderContract& contract,
    InputProfile profile,
    UINT width,
    UINT height)
{
    std::ostringstream source;
    source << "struct VSOutput {\n";

    int positionIndex = -1;
    for (std::size_t index = 0; index < contract.inputs.size(); ++index)
    {
        const SignatureParameter& parameter = contract.inputs[index];
        if (IsRasterizerGeneratedInput(parameter.systemValue))
            continue;
        const UINT componentCount = ComponentCount(parameter.mask);
        const bool integer = parameter.componentType != D3D_REGISTER_COMPONENT_FLOAT32;
        if (parameter.systemValue == D3D_NAME_POSITION)
            positionIndex = static_cast<int>(index);
        source << "  " << (integer ? "nointerpolation " : "")
               << HlslType(parameter.componentType, componentCount)
               << " value" << index << " : " << Semantic(parameter) << ";\n";
    }
    source << "};\n"
           << "VSOutput main(uint vertexId : SV_VertexID) {\n"
           << "  VSOutput output;\n"
           << "  float2 positions[3] = { float2(-1.0f, -1.0f), "
              "float2(3.0f, -1.0f), float2(-1.0f, 3.0f) };\n"
           << "  float2 clip = positions[vertexId];\n"
           << "  float2 uv = clip * 0.5f + 0.5f;\n";

    if (positionIndex < 0)
        ThrowFailure("pixel shader input signature has no SV_Position");

    for (std::size_t index = 0; index < contract.inputs.size(); ++index)
    {
        const SignatureParameter& parameter = contract.inputs[index];
        if (IsRasterizerGeneratedInput(parameter.systemValue))
            continue;
        if (parameter.systemValue == D3D_NAME_POSITION)
        {
            source << "  output.value" << index << " = float4(clip, 0.5f, 1.0f);\n";
        }
        else
        {
            const std::string prepassValue =
                profile == InputProfile::DeferredPrepass
                ? DeferredPrepassValueExpression(parameter)
                : "";
            source << "  output.value" << index << " = "
                   << (!prepassValue.empty()
                       ? prepassValue
                       : ValueExpression(
                           parameter.componentType,
                           ComponentCount(parameter.mask), width, height))
                   << ";\n";
        }
    }
    source << "  return output;\n}\n";

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const std::string text = source.str();
    const HRESULT result = D3DCompile(
        text.data(), text.size(), "generated_passthrough_vs.hlsl", nullptr, nullptr,
        "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
    if (FAILED(result))
    {
        std::string message = "generated vertex shader failed to compile";
        if (errors != nullptr)
        {
            message += ":\n";
            message.append(
                static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize());
        }
        message += "\nGenerated source:\n" + text;
        ThrowFailure(message);
    }

    ComPtr<ID3D11VertexShader> shader;
    CheckHRESULT(
        device->CreateVertexShader(
            bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader),
        "CreateVertexShader");
    return shader;
}

FrontFaceProbeResult ProbeFrontFaceStates(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11RasterizerState* clockwiseState,
    ID3D11RasterizerState* counterClockwiseState)
{
    const auto compile = [](const char* source, const char* target) {
        ComPtr<ID3DBlob> bytecode;
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(
            source, std::strlen(source), FrontFaceProbeVersion,
            nullptr, nullptr, "main", target,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
        if (FAILED(result))
        {
            std::string message = "front_face_probe: shader compilation";
            if (errors != nullptr)
            {
                message += ": ";
                message.append(
                    static_cast<const char*>(errors->GetBufferPointer()),
                    errors->GetBufferSize());
            }
            ThrowFailure(message);
        }
        return bytecode;
    };

    const ComPtr<ID3DBlob> vertexBytecode =
        compile(FrontFaceProbeVertexSource, "vs_5_0");
    const ComPtr<ID3DBlob> pixelBytecode =
        compile(FrontFaceProbePixelSource, "ps_5_0");
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    CheckHRESULT(
        device->CreateVertexShader(
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            nullptr, &vertexShader),
        "front_face_probe: CreateVertexShader");
    CheckHRESULT(
        device->CreatePixelShader(
            pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
            nullptr, &pixelShader),
        "front_face_probe: CreatePixelShader");

    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width = 1;
    targetDesc.Height = 1;
    targetDesc.MipLevels = 1;
    targetDesc.ArraySize = 1;
    targetDesc.Format = DXGI_FORMAT_R32_FLOAT;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> targetView;
    ComPtr<ID3D11Texture2D> staging;
    CheckHRESULT(
        device->CreateTexture2D(&targetDesc, nullptr, &target),
        "front_face_probe: CreateTexture2D");
    CheckHRESULT(
        device->CreateRenderTargetView(target.Get(), nullptr, &targetView),
        "front_face_probe: CreateRenderTargetView");
    CheckHRESULT(
        device->CreateTexture2D(&stagingDesc, nullptr, &staging),
        "front_face_probe: CreateTexture2D staging");

    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = FALSE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> depthState;
    CheckHRESULT(
        device->CreateDepthStencilState(&depthDesc, &depthState),
        "front_face_probe: CreateDepthStencilState");

    const auto render = [&](ID3D11RasterizerState* rasterizer) {
        context->ClearState();
        const float clear[4] = {0.25f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(targetView.Get(), clear);
        ID3D11RenderTargetView* rawTarget = targetView.Get();
        context->OMSetRenderTargets(1, &rawTarget, nullptr);
        context->OMSetDepthStencilState(depthState.Get(), 0);
        D3D11_VIEWPORT viewport{};
        viewport.Width = 1.0f;
        viewport.Height = 1.0f;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader.Get(), nullptr, 0);
        context->PSSetShader(pixelShader.Get(), nullptr, 0);
        context->Draw(3, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        CheckHRESULT(
            context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "front_face_probe: Map");
        float value = 0.0f;
        std::memcpy(&value, mapped.pData, sizeof(value));
        context->Unmap(staging.Get(), 0);
        if (value != 0.0f && value != 1.0f)
            ThrowFailure("front_face_probe: invalid readback");
        return value == 1.0f;
    };

    FrontFaceProbeResult result;
    result.clockwiseStateFront = render(clockwiseState);
    result.counterClockwiseStateFront = render(counterClockwiseState);
    if (result.clockwiseStateFront == result.counterClockwiseStateFront)
        ThrowFailure("front_face_probe: states are not complementary");
    return result;
}

float RandomConstant(std::mt19937& random)
{
    std::uniform_int_distribution<int> category(0, 9);
    const int selected = category(random);
    if (selected < 5)
        return std::uniform_real_distribution<float>(-1.0f, 1.0f)(random);
    if (selected < 8)
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(random);
    return std::uniform_real_distribution<float>(0.0f, 4.0f)(random);
}

bool IsDirectionalLightingContract(
    const ShaderContract& contract,
    const DisassemblyInfo& disassembly)
{
    const auto hasConstantBuffer = [&contract](UINT bindPoint, UINT size) {
        return std::any_of(
            contract.constantBuffers.begin(), contract.constantBuffers.end(),
            [bindPoint, size](const ConstantBufferBinding& binding) {
                return binding.bindPoint == bindPoint && binding.size >= size;
            });
    };
    const bool hasShadowArray = std::any_of(
        contract.resources.begin(), contract.resources.end(),
        [](const ResourceBinding& binding) {
            return binding.bindPoint == 5 &&
                binding.dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY;
        });
    return contract.renderTargetCount == 2 &&
        hasConstantBuffer(2, 25 * 16) &&
        hasConstantBuffer(12, 31 * 16) &&
        hasShadowArray &&
        disassembly.comparisonSamplers.count(5) != 0;
}

bool IsAmbientIblContract(
    const ShaderContract& contract,
    const DisassemblyInfo& disassembly)
{
    const auto hasConstantBuffer = [&contract](UINT bindPoint, UINT size) {
        return std::any_of(
            contract.constantBuffers.begin(), contract.constantBuffers.end(),
            [bindPoint, size](const ConstantBufferBinding& binding) {
                return binding.bindPoint == bindPoint && binding.size >= size;
            });
    };
    const auto hasResource = [&contract](
                                 UINT bindPoint,
                                 D3D_SRV_DIMENSION dimension) {
        return std::any_of(
            contract.resources.begin(), contract.resources.end(),
            [bindPoint, dimension](const ResourceBinding& binding) {
                return binding.bindPoint == bindPoint &&
                    binding.dimension == dimension;
            });
    };
    return contract.renderTargetCount == 1 &&
        hasConstantBuffer(0, 3 * 16) &&
        hasConstantBuffer(2, 6 * 16) &&
        hasConstantBuffer(12, 31 * 16) &&
        hasResource(1, D3D_SRV_DIMENSION_TEXTURE2D) &&
        hasResource(8, D3D_SRV_DIMENSION_TEXTURECUBEARRAY) &&
        hasResource(15, D3D_SRV_DIMENSION_TEXTURE2D) &&
        disassembly.comparisonSamplers.empty();
}

bool IsDeferredPrepassContract(
    const ShaderContract& contract,
    const DisassemblyInfo& disassembly)
{
    const auto hasConstantBuffer = [&contract](UINT bindPoint, UINT size) {
        return std::any_of(
            contract.constantBuffers.begin(), contract.constantBuffers.end(),
            [bindPoint, size](const ConstantBufferBinding& binding) {
                return binding.bindPoint == bindPoint && binding.size >= size;
            });
    };
    const auto hasResource = [&contract](UINT bindPoint) {
        return std::any_of(
            contract.resources.begin(), contract.resources.end(),
            [bindPoint](const ResourceBinding& binding) {
                return binding.bindPoint == bindPoint &&
                    binding.dimension == D3D_SRV_DIMENSION_TEXTURE2D;
            });
    };
    const bool hasFrontFace = std::any_of(
        contract.inputs.begin(), contract.inputs.end(),
        [](const SignatureParameter& parameter) {
            return parameter.systemValue == D3D_NAME_IS_FRONT_FACE;
        });
    return contract.renderTargetCount == 6 &&
        hasConstantBuffer(2, 6 * 16) &&
        hasConstantBuffer(12, 41 * 16) &&
        hasResource(0) && hasResource(1) && hasResource(2) &&
        hasFrontFace &&
        disassembly.comparisonSamplers.empty();
}

InputProfile DetectInputProfile(
    const ShaderContract& contract,
    const DisassemblyInfo& disassembly)
{
    if (IsDirectionalLightingContract(contract, disassembly))
        return InputProfile::DirectionalLighting;
    if (IsAmbientIblContract(contract, disassembly))
        return InputProfile::AmbientIbl;
    if (IsDeferredPrepassContract(contract, disassembly))
        return InputProfile::DeferredPrepass;
    return InputProfile::Unshaped;
}

float ScenarioControl(
    const InputScenario& scenario,
    const std::string& name,
    float fallback)
{
    const auto found = scenario.controls.find(name);
    return found != scenario.controls.end() ? found->second : fallback;
}

std::vector<InputScenario> BuildInputScenarios(
    InputProfile profile,
    Fixture fixture,
    UINT randomSeedCount,
    UINT seedBase)
{
    std::vector<InputScenario> scenarios;
    scenarios.reserve(randomSeedCount + 24);
    for (UINT seed = 0; seed < randomSeedCount; ++seed)
        scenarios.push_back(
            {seed, seedBase + seed, false, "random-" + std::to_string(seed), {}});

    const auto add = [&](const std::string& id,
                         std::initializer_list<std::pair<const std::string, float>>
                             overrides) {
        InputScenario scenario;
        scenario.id = static_cast<UINT>(scenarios.size());
        scenario.randomSeed =
            seedBase + 0xA511E9B3u + scenario.id * 0x9E3779B9u;
        scenario.dedicated = true;
        scenario.semanticId = id;
        if (profile == InputProfile::AmbientIbl)
        {
            scenario.controls = {
                {"ambient_depth", 1.0f},
                {"ambient_ibl", 1.0f},
                {"ambient_skin", 1.0f},
                {"ambient_rough01", 0.35f},
                {"ambient_tap_skin", 1.0f},
            };
        }
        else if (profile == InputProfile::DirectionalLighting)
        {
            scenario.controls = {
                {"directional_depth", 1.0f},
                {"directional_cascade", 2.0f},
                {"directional_fade", 0.0f},
                {"directional_light", 1.0f},
                {"directional_brdf", 0.0f},
                {"directional_skin", 0.0f},
                {"directional_roughness", 0.45f},
            };
        }
        else if (profile == InputProfile::DeferredPrepass)
        {
            scenario.controls = {
                {"prepass_bypass_fade", 0.0f},
                {"prepass_front_face", 1.0f},
                {"prepass_normal_clamped", 0.0f},
                {"prepass_smooth_gate_zero", 0.0f},
                {"prepass_smooth_span", 1.0f},
                {"prepass_flag_a", 0.0f},
                {"prepass_flag_b", 0.0f},
                {"prepass_scroll_x", 1.0f},
                {"prepass_scroll_y", 1.0f},
            };
        }
        for (const auto& [name, value] : overrides)
            scenario.controls[name] = value;
        scenarios.push_back(std::move(scenario));
    };

    if (profile == InputProfile::AmbientIbl)
    {
        add("depth-near", {{"ambient_depth", -1.0f}});
        if (fixture == Fixture::Adversarial)
            add("depth-equal", {{"ambient_depth", 0.0f}});
        add("depth-far", {{"ambient_depth", 1.0f}});
        add("ibl-off", {{"ambient_ibl", 0.0f}});
        add("ibl-on", {{"ambient_ibl", 1.0f}});
        add("material-default", {{"ambient_skin", 0.0f}});
        add("material-skin", {{"ambient_skin", 1.0f}});
        add("roughness-zero", {{"ambient_rough01", 0.0f}});
        add("roughness-mid", {{"ambient_rough01", 0.35f}});
        add("roughness-high", {{"ambient_rough01", 0.7f}});
        add("skin-taps-default", {{"ambient_tap_skin", 0.0f}});
        add("skin-taps-skin", {{"ambient_tap_skin", 1.0f}});
    }
    else if (profile == InputProfile::DirectionalLighting)
    {
        add("depth-near", {{"directional_depth", -1.0f}});
        if (fixture == Fixture::Adversarial)
            add("depth-equal", {{"directional_depth", 0.0f}});
        add("depth-far", {{"directional_depth", 1.0f}});
        add("cascade-0", {{"directional_cascade", 0.0f}});
        add("cascade-blend", {{"directional_cascade", 2.0f}});
        add("cascade-1", {{"directional_cascade", 1.0f}});
        add("fade-inside", {{"directional_fade", 0.0f}});
        add("fade-outside", {{"directional_fade", 1.0f}});
        add("light-front", {{"directional_light", 1.0f}});
        add("light-grazing", {{"directional_light", 0.0f}});
        add("light-back", {{"directional_light", -1.0f}});
        add("material-default", {{"directional_skin", 0.0f}});
        add("material-skin", {{"directional_skin", 1.0f}});
        add("roughness-low", {{"directional_roughness", 0.1f}});
        add("roughness-mid", {{"directional_roughness", 0.45f}});
        add("roughness-high", {{"directional_roughness", 0.85f}});
        add("brdf-fallback", {{"directional_brdf", 1.0f}});
        add("brdf-nonunity", {{"directional_brdf", 2.0f}});
    }
    else if (profile == InputProfile::DeferredPrepass)
    {
        add("alpha-active", {{"prepass_bypass_fade", 0.0f}});
        add("alpha-bypass", {{"prepass_bypass_fade", 1.0f}});
        add("face-front", {{"prepass_front_face", 1.0f}});
        add("face-back", {{"prepass_front_face", 0.0f}});
        add("normal-interior", {{"prepass_normal_clamped", 0.0f}});
        add("normal-clamped", {{"prepass_normal_clamped", 1.0f}});
        add("smooth-gate-zero", {{"prepass_smooth_gate_zero", 1.0f}});
        add("smooth-gate-live", {{"prepass_smooth_gate_zero", 0.0f}});
        add("smooth-direct", {{"prepass_smooth_span", 0.0f}});
        add("smooth-span", {{"prepass_smooth_span", 1.0f}});
        add("flags-none", {});
        add("flags-a", {{"prepass_flag_a", 1.0f}});
        add("flags-b", {{"prepass_flag_b", 1.0f}});
        add("flags-both", {{"prepass_flag_a", 1.0f}, {"prepass_flag_b", 1.0f}});
        add("scroll-positive", {});
        add("scroll-negative",
            {{"prepass_scroll_x", -1.0f}, {"prepass_scroll_y", -1.0f}});
    }
    return scenarios;
}

void AddExecutionConfigurationHash(
    Sha256& hash,
    const Options& options,
    InputProfile profile,
    const ShaderContract& contract,
    const std::vector<InputScenario>& scenarios,
    const D3D11_RASTERIZER_DESC& clockwiseRasterizer,
    const D3D11_RASTERIZER_DESC& counterClockwiseRasterizer,
    const D3D11_DEPTH_STENCIL_DESC& depthStencil,
    const FrontFaceProbeResult& frontFaceProbe)
{
    hash.AddString("fo4cs.shader-exec-inputs-v1");
    hash.AddString(FO4CS_EXEC_HARNESS_SOURCE_SHA256);
    hash.AddString(InputProfileName(profile));
    hash.AddString(FixtureName(options.fixture));
    hash.AddValue(options.width);
    hash.AddValue(options.height);
    hash.AddValue(options.seedBase);
    hash.AddValue(options.seeds);
    for (UINT index = 0; index < options.seeds; ++index)
        hash.AddValue(options.seedBase + index);
    for (const InputScenario& scenario : scenarios)
    {
        hash.AddValue(scenario.id);
        hash.AddValue(scenario.randomSeed);
        hash.AddString(scenario.semanticId);
    }

    hash.AddString("generated-fullscreen-triangle-v1");
    hash.AddValue(contract.renderTargetCount);
    for (const SignatureParameter& input : contract.inputs)
    {
        hash.AddString(input.semanticName);
        hash.AddValue(input.semanticIndex);
        hash.AddValue(input.shaderRegister);
        hash.AddValue(input.mask);
        hash.AddValue(input.componentType);
        hash.AddValue(input.systemValue);
    }

    hash.AddString("viewport");
    const float topLeft = 0.0f;
    const float viewportWidth = static_cast<float>(options.width);
    const float viewportHeight = static_cast<float>(options.height);
    const float minimumDepth = 0.0f;
    const float maximumDepth = 1.0f;
    hash.AddValue(topLeft);
    hash.AddValue(topLeft);
    hash.AddValue(viewportWidth);
    hash.AddValue(viewportHeight);
    hash.AddValue(minimumDepth);
    hash.AddValue(maximumDepth);

    const auto addRasterizer = [&hash](const D3D11_RASTERIZER_DESC& descriptor) {
        hash.AddValue(descriptor.FillMode);
        hash.AddValue(descriptor.CullMode);
        hash.AddValue(descriptor.FrontCounterClockwise);
        hash.AddValue(descriptor.DepthBias);
        hash.AddValue(descriptor.DepthBiasClamp);
        hash.AddValue(descriptor.SlopeScaledDepthBias);
        hash.AddValue(descriptor.DepthClipEnable);
        hash.AddValue(descriptor.ScissorEnable);
        hash.AddValue(descriptor.MultisampleEnable);
        hash.AddValue(descriptor.AntialiasedLineEnable);
    };
    hash.AddString("rasterizer-clockwise-front");
    addRasterizer(clockwiseRasterizer);
    hash.AddString("rasterizer-counter-clockwise-front");
    addRasterizer(counterClockwiseRasterizer);

    hash.AddString("depth-stencil");
    hash.AddValue(depthStencil.DepthEnable);
    hash.AddValue(depthStencil.DepthWriteMask);
    hash.AddValue(depthStencil.DepthFunc);
    hash.AddValue(depthStencil.StencilEnable);
    hash.AddValue(depthStencil.StencilReadMask);
    hash.AddValue(depthStencil.StencilWriteMask);
    hash.AddString("driver=WARP");
    hash.AddString("feature-level=11_0");
    hash.AddString("render-target=R32G32B32A32_FLOAT");
    hash.AddString(FrontFaceProbeVersion);
    hash.AddString(FrontFaceProbeVertexSource);
    hash.AddString(FrontFaceProbePixelSource);
    hash.AddValue(frontFaceProbe.clockwiseStateFront);
    hash.AddValue(frontFaceProbe.counterClockwiseStateFront);
}

void SetVector(
    std::vector<float>& values,
    UINT vectorIndex,
    const std::array<float, 4>& vector)
{
    const std::size_t offset = static_cast<std::size_t>(vectorIndex) * 4;
    if (offset + vector.size() > values.size())
        ThrowFailure("probe constant-buffer shape exceeds reflected size");
    std::copy(vector.begin(), vector.end(), values.begin() + offset);
}

using Matrix4 = std::array<std::array<float, 4>, 4>;

void SetMatrix(
    std::vector<float>& values,
    UINT firstRow,
    const Matrix4& matrix)
{
    for (UINT row = 0; row < 4; ++row)
        SetVector(values, firstRow + row, matrix[row]);
}

Matrix4 ReadMatrix(const std::vector<float>& values, UINT firstRow)
{
    Matrix4 matrix{};
    const std::size_t offset = static_cast<std::size_t>(firstRow) * 4;
    if (offset + 16 > values.size())
        ThrowFailure("matrix_assertion: matrix exceeds uploaded constant buffer");
    std::memcpy(matrix.data(), values.data() + offset, 16 * sizeof(float));
    return matrix;
}

double MatrixDeterminant(Matrix4 matrix)
{
    double determinant = 1.0;
    for (UINT pivot = 0; pivot < 4; ++pivot)
    {
        UINT selected = pivot;
        for (UINT row = pivot + 1; row < 4; ++row)
        {
            if (std::abs(matrix[row][pivot]) >
                std::abs(matrix[selected][pivot]))
            {
                selected = row;
            }
        }
        if (std::abs(matrix[selected][pivot]) < 1.0e-12)
            return 0.0;
        if (selected != pivot)
        {
            std::swap(matrix[selected], matrix[pivot]);
            determinant = -determinant;
        }
        const double diagonal = matrix[pivot][pivot];
        determinant *= diagonal;
        for (UINT row = pivot + 1; row < 4; ++row)
        {
            const double factor = matrix[row][pivot] / diagonal;
            for (UINT column = pivot; column < 4; ++column)
            {
                matrix[row][column] = static_cast<float>(
                    matrix[row][column] - factor * matrix[pivot][column]);
            }
        }
    }
    return determinant;
}

void AssertMatrixPair(
    const std::string& name,
    const Matrix4& left,
    const Matrix4& right)
{
    if (std::abs(MatrixDeterminant(left)) <= 0.1 ||
        std::abs(MatrixDeterminant(right)) <= 0.1)
    {
        ThrowFailure("matrix_assertion: " + name + " determinant");
    }
    bool nearEqual = true;
    for (UINT row = 0; row < 4; ++row)
    {
        for (UINT column = 0; column < 4; ++column)
        {
            if (std::abs(left[row][column] - right[row][column]) > 1.0e-5f)
                nearEqual = false;
        }
    }
    if (nearEqual)
        ThrowFailure("matrix_assertion: " + name + " banks are near-equal");
}

void ShapeSharedCameraConstants(
    UINT bindPoint,
    std::vector<float>& values,
    std::mt19937& random)
{
    if (bindPoint != 12 || values.size() < 28 * 4)
        return;

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float angle = (unit(random) * 2.0f - 1.0f) * 3.14159265f;
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    SetVector(values, 0, {cosine, 0.0f, -sine, 0.0f});
    SetVector(values, 1, {0.0f, 1.0f, 0.0f, 0.0f});
    SetVector(values, 2, {sine, 0.0f, cosine, 0.0f});
    SetVector(values, 3, {0.0f, 0.0f, 0.0f, 1.0f});
    SetVector(values, 4, {1.2f, 0.0f, 0.0f, 0.0f});
    SetVector(values, 5, {0.0f, 2.1f, 0.0f, 0.0f});
    SetVector(values, 6, {0.0f, 0.0f, 1.001f, 1.0f});
    SetVector(values, 7, {0.0f, 0.0f, -0.1001f, 0.0f});
    SetVector(values, 8, {1.0f, 0.0f, 0.0f, 0.0f});
    SetVector(values, 9, {0.0f, 1.0f, 0.0f, 0.0f});
    SetVector(values, 10, {0.0f, 0.0f, 1.0f, 0.0f});
    SetVector(values, 11, {0.0f, 0.0f, 0.0f, 1.0f});
    SetVector(values, 12, {cosine, 0.0f, sine, 0.0f});
    SetVector(values, 13, {0.0f, 1.0f, 0.0f, 0.0f});
    SetVector(values, 14, {-sine, 0.0f, cosine, 0.0f});
    SetVector(values, 15, {0.0f, 0.0f, 0.0f, 1.0f});
    SetVector(values, 16, {cosine, 0.0f, -sine, 0.0f});
    SetVector(values, 17, {0.0f, 1.0f, 0.0f, 0.0f});
    SetVector(values, 18, {sine, 0.0f, cosine, 0.0f});
    SetVector(values, 19, {0.1f, 0.001f, 0.0f, 0.0f});

    const Matrix4 farReprojection{{
        {1.15f, -0.06f, 0.08f, -0.13f},
        {0.05f, 0.78f, -0.04f, 0.09f},
        {-0.01f, 0.04f, 1.10f, -0.15f},
        {-0.02f, 0.01f, 0.08f, 1.20f},
    }};
    const Matrix4 nearReprojection{{
        {0.70f, 0.08f, 0.03f, 0.11f},
        {-0.04f, 0.55f, 0.02f, -0.07f},
        {0.02f, -0.03f, 0.90f, 0.20f},
        {0.01f, 0.02f, 0.05f, 1.00f},
    }};
    SetMatrix(values, 20, farReprojection);
    SetMatrix(values, 24, nearReprojection);
    AssertMatrixPair("reprojection", farReprojection, nearReprojection);
}

void ShapeDirectionalLightingConstants(
    UINT bindPoint,
    std::vector<float>& values,
    UINT width,
    UINT height,
    const InputScenario& scenario)
{
    if (bindPoint == 12)
    {
        SetVector(values, 28, {0.02f, 2.0f, 1.0f, 2.0f});
        SetVector(values, 29, {0.36f, -0.4f, 0.0f, 0.0f});
        SetVector(values, 30, {0.0f, 0.5f, 0.0f, 0.0f});
        return;
    }
    if (bindPoint != 2)
        return;

    SetVector(values, 0, {
        1.0f / static_cast<float>(width),
        1.0f / static_cast<float>(height),
        1.0f / static_cast<float>(width),
        1.0f / static_cast<float>(height),
    });
    const float lightFacing =
        ScenarioControl(scenario, "directional_light", 1.0f);
    const float brdfScenario =
        ScenarioControl(scenario, "directional_brdf", 0.0f);
    SetVector(values, 1, brdfScenario == 1.0f
        ? std::array<float, 4>{0.0f, 0.0f, -1.0f, 0.0f}
        : (brdfScenario == 2.0f
            ? std::array<float, 4>{0.9797959f, 0.0f, -0.2f, 0.0f}
            : (lightFacing > 0.5f
                ? std::array<float, 4>{0.6f, 0.0f, 0.8f, 0.0f}
                : (lightFacing < -0.5f
                    ? std::array<float, 4>{-0.6f, 0.0f, -0.8f, 0.0f}
                    : std::array<float, 4>{0.0f, 1.0f, 0.0f, 0.0f}))));
    SetVector(values, 2, {
        std::abs(values[8]) + 0.5f,
        std::abs(values[9]) + 0.5f,
        std::abs(values[10]) + 0.5f,
        0.0f,
    });
    SetVector(values, 6, {0.08f, 0.02f, 0.01f, 0.35f});
    SetVector(values, 7, {-0.03f, 0.07f, 0.02f, 0.32f});
    SetVector(values, 8, {0.02f, -0.03f, 0.09f, 0.30f});
    SetVector(values, 10, {0.35f, 0.65f, 0.0f, 0.0f});
    const Matrix4 cascade0{{
        {0.80f, 0.03f, 0.01f, 0.50f},
        {-0.02f, 0.70f, 0.04f, 0.50f},
        {0.01f, -0.03f, 0.60f, 0.50f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    const Matrix4 cascade1{{
        {0.55f, -0.04f, 0.02f, 0.45f},
        {0.03f, 0.65f, 0.01f, 0.55f},
        {-0.02f, 0.03f, 0.75f, 0.60f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    for (UINT row = 0; row < 3; ++row)
    {
        SetVector(values, 11 + row, cascade0[row]);
        SetVector(values, 14 + row, cascade1[row]);
    }
    AssertMatrixPair("cascade", cascade0, cascade1);
    SetVector(values, 20, {0.0f, 0.0f, 1.0f / 64.0f, 0.0f});
    SetVector(values, 21, {0.0f, 0.0f, 0.0f, 64.0f});
    SetVector(values, 22, {0.0f, 0.0f, 0.0f, 64.0f});
    const bool outsideFade =
        ScenarioControl(scenario, "directional_fade", 0.0f) != 0.0f;
    SetVector(values, 24, {
        outsideFade ? 1.0e-4f : 1.0e6f,
        0.0f, 0.0f, 0.0f,
    });
}

void ShapeAmbientConstants(
    UINT bindPoint,
    std::vector<float>& values,
    UINT width,
    UINT height,
    std::mt19937& random)
{
    (void)width;
    (void)height;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (bindPoint == 12)
    {
        SetVector(values, 28, {0.02f, 0.5f, 0.2f, 0.8f});
        SetVector(values, 29, {0.36f, -0.4f, 0.0f, 0.0f});
        SetVector(values, 30, {0.0f, unit(random), 0.0f, 0.0f});
        return;
    }

    if (bindPoint == 0)
    {
        SetVector(values, 0, {
            4.0f,
            1.0f,
            0.5f + unit(random),
            0.0f,
        });
        SetVector(values, 1, {0.5f + unit(random), 0.0f, 0.0f, 0.0f});
        SetVector(values, 2, {0.0f, 0.0f, 0.5f + unit(random), 0.0f});
        return;
    }

    if (bindPoint == 2)
    {
        SetVector(values, 0, {
            0.0f,
            0.0f,
            1.0f,
            1.0f,
        });
        SetVector(values, 5, {1.0f, 1.0f, 0.0f, 0.0f});
    }
}

void ShapeDeferredPrepassConstants(
    UINT bindPoint,
    std::vector<float>& values,
    const InputScenario& scenario)
{
    if (bindPoint == 12)
    {
        SetVector(values, 30, {0.65f, 0.0f, 0.0f, 0.0f});
        const Matrix4 previous{{
            {0.85f, 0.05f, 0.02f, -0.10f},
            {-0.03f, 0.75f, 0.04f, 0.08f},
            {0.01f, -0.02f, 0.95f, 0.12f},
            {0.02f, 0.01f, 0.06f, 1.10f},
        }};
        const Matrix4 current{{
            {1.10f, -0.04f, 0.03f, 0.14f},
            {0.02f, 0.90f, -0.05f, -0.06f},
            {-0.01f, 0.03f, 1.05f, -0.09f},
            {-0.02f, 0.02f, 0.04f, 0.95f},
        }};
        SetMatrix(values, 31, previous);
        SetMatrix(values, 37, current);
        AssertMatrixPair("prepass-motion", previous, current);
        return;
    }
    if (bindPoint != 2)
        return;

    const bool bypass =
        ScenarioControl(scenario, "prepass_bypass_fade", 0.0f) != 0.0f;
    const bool span =
        ScenarioControl(scenario, "prepass_smooth_span", 1.0f) != 0.0f;
    const bool gateZero =
        ScenarioControl(scenario, "prepass_smooth_gate_zero", 0.0f) != 0.0f;
    const bool flagA =
        ScenarioControl(scenario, "prepass_flag_a", 0.0f) != 0.0f;
    const bool flagB =
        ScenarioControl(scenario, "prepass_flag_b", 0.0f) != 0.0f;
    const float scrollX =
        ScenarioControl(scenario, "prepass_scroll_x", 1.0f);
    const float scrollY =
        ScenarioControl(scenario, "prepass_scroll_y", 1.0f);
    SetVector(values, 0, {0.25f, 0.5f, 0.8f, 1.0f});
    SetVector(values, 1, {0.7f, 0.8f, 0.9f, 0.0f});
    SetVector(values, 2, {
        scrollX > 0.0f ? 0.35f : -0.35f,
        scrollY > 0.0f ? 0.6f : -0.6f,
        0.0f, 0.0f});
    SetVector(values, 4, {
        flagB ? 1.0f : 0.0f,
        flagA ? 1.0f : 0.0f,
        0.5f,
        bypass ? -1.0f : 0.5f});
    SetVector(values, 5, {
        5.0f,
        span ? 1.0f : 0.0f,
        0.2f,
        gateZero ? -0.2f : 0.8f});
}

void FillUnitRandom(std::vector<float>& values, std::mt19937& random)
{
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    for (float& value : values)
        value = distribution(random);
}

void StoreEncodedUnitNormal(
    float* destination,
    float normalX,
    float normalY,
    float normalZ,
    std::mt19937& random)
{
    const float encodeScale = std::sqrt(2.0f / (1.0f - normalZ));
    destination[0] = (normalX * encodeScale + 2.0f) * 0.25f;
    destination[1] = (normalY * encodeScale + 2.0f) * 0.25f;
    destination[2] = std::uniform_real_distribution<float>(0.0f, 1.0f)(random);
    destination[3] = std::uniform_real_distribution<float>(0.0f, 1.0f)(random);
}

void FillDeferredNormalTexture(
    std::vector<float>& values,
    std::size_t pixelCount,
    std::mt19937& random,
    bool fixedFacing)
{
    std::uniform_real_distribution<float> zDistribution(-0.95f, 0.95f);
    std::uniform_real_distribution<float> azimuthDistribution(0.0f, 6.28318531f);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        float normalX = 0.6f;
        float normalY = 0.0f;
        float normalZ = 0.8f;
        if (!fixedFacing)
        {
            normalZ = zDistribution(random);
            const float azimuth = azimuthDistribution(random);
            const float radial =
                std::sqrt(std::max(0.0f, 1.0f - normalZ * normalZ));
            normalX = radial * std::cos(azimuth);
            normalY = radial * std::sin(azimuth);
        }
        StoreEncodedUnitNormal(
            values.data() + pixel * 4,
            normalX, normalY, normalZ, random);
    }
}

void FillDeferredDepthTexture(
    std::vector<float>& values,
    std::size_t pixelCount,
    std::mt19937& random,
    float fixedDepth)
{
    std::uniform_real_distribution<float> depthDistribution(0.5f, 1.0f);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const float depth =
            fixedDepth >= 0.0f ? fixedDepth : depthDistribution(random);
        values[pixel * 4 + 0] = depth;
        values[pixel * 4 + 1] = depth;
        values[pixel * 4 + 2] = depth;
        values[pixel * 4 + 3] = 1.0f;
    }
}

void FillAmbientTexture(
    std::vector<float>& values,
    UINT bindPoint,
    UINT width,
    UINT height,
    UINT arraySize,
    std::mt19937& random,
    Fixture fixture,
    const InputScenario& scenario)
{
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * height * arraySize;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto range = [&random](float minimum, float maximum) {
        return std::uniform_real_distribution<float>(minimum, maximum)(random);
    };

    if (bindPoint == 1)
    {
        FillDeferredNormalTexture(values, pixelCount, random, false);
        return;
    }

    if (bindPoint == 2)
    {
        const UINT cubeSlice = std::uniform_int_distribution<UINT>(0, 3)(random);
        const bool hasIbl =
            ScenarioControl(scenario, "ambient_ibl", 1.0f) != 0.0f;
        const float encodedSlice = hasIbl
            ? (static_cast<float>(cubeSlice) + 1.25f) / 255.0f
            : 0.0f;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            values[pixel * 4 + 0] = unit(random);
            values[pixel * 4 + 1] = encodedSlice;
            values[pixel * 4 + 2] = scenario.dedicated
                ? 0.5f
                : range(0.05f, 0.8f);
            values[pixel * 4 + 3] = unit(random);
        }
        return;
    }

    if (bindPoint == 3)
    {
        const bool skin = ScenarioControl(
            scenario, "ambient_skin",
            static_cast<float>(std::uniform_int_distribution<int>(0, 1)(random))) != 0.0f;
        const bool tapSkin =
            ScenarioControl(scenario, "ambient_tap_skin", 1.0f) != 0.0f;
        const float selectedRoughness =
            ScenarioControl(scenario, "ambient_rough01", -1.0f);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            if (selectedRoughness >= 0.0f)
            {
                values[pixel * 4 + 0] =
                    selectedRoughness == 0.0f && fixture == Fixture::Native
                    ? 0.25f
                    : std::min(1.0f, selectedRoughness + 0.3f);
            }
            else
            {
                values[pixel * 4 + 0] =
                    pixel % 16 == 0 ? 0.3f : range(0.0f, 1.0f);
            }
            values[pixel * 4 + 1] = range(0.05f, 0.5f);
            values[pixel * 4 + 2] = unit(random);
            const bool pixelSkin =
                skin && (tapSkin || pixel == 0);
            values[pixel * 4 + 3] =
                (pixelSkin ? 5.0f : 1.0f) / 255.0f;
        }
        return;
    }

    if (bindPoint == 7 || bindPoint == 15)
    {
        if (bindPoint == 15)
        {
            const float baseDepth = range(0.55f, 0.7f);
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                const float x = static_cast<float>(pixel % width) /
                    static_cast<float>(width - 1);
                const float y = static_cast<float>((pixel / width) % height) /
                    static_cast<float>(height - 1);
                const float depth =
                    std::min(1.0f, baseDepth + (x + y) * 0.1f);
                values[pixel * 4 + 0] = depth;
                values[pixel * 4 + 1] = depth;
                values[pixel * 4 + 2] = depth;
                values[pixel * 4 + 3] = 1.0f;
            }
            return;
        }
        float fixedDepth = -1.0f;
        if (scenario.dedicated)
        {
            const float mode =
                ScenarioControl(scenario, "ambient_depth", 1.0f);
            if (fixture == Fixture::Adversarial)
            {
                const float boundary = 0.01f;
                fixedDepth = mode < -0.5f
                    ? std::nextafter(boundary, 0.0f)
                    : (mode > 0.5f
                        ? std::nextafter(
                            boundary, std::numeric_limits<float>::infinity())
                        : boundary);
            }
            else
            {
                fixedDepth = mode < 0.0f ? 0.005f : 0.75f;
            }
        }
        FillDeferredDepthTexture(values, pixelCount, random, fixedDepth);
        return;
    }

    float minimum = 0.0f;
    float maximum = 1.0f;
    if (bindPoint == 5 || bindPoint == 6 ||
        bindPoint == 11 || bindPoint == 12)
    {
        minimum = 0.01f;
        maximum = 0.25f;
    }
    else if (bindPoint == 8 || bindPoint == 10 || bindPoint == 14)
    {
        minimum = 0.01f;
        maximum = 1.0f;
    }

    if (scenario.dedicated && (bindPoint == 11 || bindPoint == 12))
    {
        const Pixel fixed = bindPoint == 11
            ? Pixel{0.125f, 0.25f, 0.375f, 1.0f}
            : Pixel{0.75f, 0.5f, 0.25f, 1.0f};
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            std::copy(fixed.begin(), fixed.end(), values.begin() + pixel * 4);
        return;
    }

    if (bindPoint == 10)
    {
        const Pixel baseColor = {
            range(minimum, maximum),
            range(minimum, maximum),
            range(minimum, maximum),
            1.0f,
        };
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const float x = static_cast<float>(pixel % width) /
                static_cast<float>(width - 1);
            const float y = static_cast<float>((pixel / width) % height) /
                static_cast<float>(height - 1);
            values[pixel * 4 + 0] =
                std::min(1.0f, baseColor[0] + x * 0.1f);
            values[pixel * 4 + 1] =
                std::min(1.0f, baseColor[1] + y * 0.1f);
            values[pixel * 4 + 2] =
                std::min(1.0f, baseColor[2] + (x + y) * 0.05f);
            values[pixel * 4 + 3] = 1.0f;
        }
        return;
    }

    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        if (bindPoint == 9)
        {
            const float ao = range(0.2f, 1.0f);
            values[pixel * 4 + 0] = ao;
            values[pixel * 4 + 1] = ao;
            values[pixel * 4 + 2] = ao;
            values[pixel * 4 + 3] = 1.0f;
        }
        else
        {
            values[pixel * 4 + 0] = range(minimum, maximum);
            values[pixel * 4 + 1] = range(minimum, maximum);
            values[pixel * 4 + 2] = range(minimum, maximum);
            values[pixel * 4 + 3] =
                bindPoint == 8 ? 1.0f : range(0.0f, 1.0f);
        }
    }
}

void FillDirectionalTexture(
    std::vector<float>& values,
    UINT bindPoint,
    UINT width,
    UINT height,
    UINT arraySize,
    std::mt19937& random,
    Fixture fixture,
    const InputScenario& scenario)
{
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * height * arraySize;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (bindPoint == 1)
    {
        const float brdfScenario =
            ScenarioControl(scenario, "directional_brdf", 0.0f);
        if (brdfScenario != 0.0f)
        {
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
            {
                StoreEncodedUnitNormal(
                    values.data() + pixel * 4,
                    0.0f, 0.0f, -1.0f, random);
            }
        }
        else
        {
            FillDeferredNormalTexture(
                values, pixelCount, random, scenario.dedicated);
        }
        return;
    }
    if (bindPoint == 2)
    {
        const bool skin =
            ScenarioControl(scenario, "directional_skin", 0.0f) != 0.0f;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const float roughness =
                ScenarioControl(scenario, "directional_roughness", 0.45f);
            values[pixel * 4 + 0] = skin ? 0.6f : 1.0f - roughness;
            values[pixel * 4 + 1] = skin ? 0.0f : unit(random);
            values[pixel * 4 + 2] = skin ? 0.8f : unit(random);
            values[pixel * 4 + 3] = (skin ? 1.0f : 2.0f) / 255.0f;
        }
        return;
    }
    if (bindPoint == 3)
    {
        float fixedDepth = -1.0f;
        if (scenario.dedicated)
        {
            if (scenario.semanticId.rfind("cascade-", 0) == 0)
            {
                const float cascade =
                    ScenarioControl(scenario, "directional_cascade", 2.0f);
                fixedDepth = cascade == 0.0f ? 0.2f :
                    (cascade == 1.0f ? 0.8f : 0.5f);
            }
            else
            {
                const float mode =
                    ScenarioControl(scenario, "directional_depth", 1.0f);
                if (fixture == Fixture::Adversarial)
                {
                    const float boundary = 0.01f;
                    fixedDepth = mode < -0.5f
                        ? std::nextafter(boundary, 0.0f)
                        : (mode > 0.5f
                            ? std::nextafter(
                                boundary,
                                std::numeric_limits<float>::infinity())
                            : boundary);
                }
                else
                {
                    fixedDepth = mode < 0.0f ? 0.005f : 0.75f;
                }
            }
        }
        FillDeferredDepthTexture(values, pixelCount, random, fixedDepth);
        return;
    }
    FillUnitRandom(values, random);
}

void FillDeferredPrepassTexture(
    std::vector<float>& values,
    UINT bindPoint,
    UINT width,
    UINT height,
    UINT arraySize,
    std::mt19937& random,
    const InputScenario& scenario)
{
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * height * arraySize;
    if (bindPoint == 1)
    {
        const bool clamped =
            ScenarioControl(scenario, "prepass_normal_clamped", 0.0f) != 0.0f;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const float x = scenario.dedicated
                ? (clamped ? 1.0f : 0.25f)
                : std::uniform_real_distribution<float>(-0.6f, 0.6f)(random);
            const float yLimit =
                std::sqrt(std::max(0.0f, 0.36f - x * x));
            const float y = scenario.dedicated
                ? (clamped ? 1.0f : -0.25f)
                : std::uniform_real_distribution<float>(-yLimit, yLimit)(random);
            values[pixel * 4 + 0] = x * 0.5f + 0.5f;
            values[pixel * 4 + 1] = y * 0.5f + 0.5f;
            values[pixel * 4 + 2] = 0.65f;
            values[pixel * 4 + 3] = 0.55f;
        }
        return;
    }
    FillUnitRandom(values, random);
}

void FillProfileTexture(
    std::vector<float>& values,
    InputProfile profile,
    UINT bindPoint,
    UINT width,
    UINT height,
    UINT arraySize,
    std::mt19937& random,
    Fixture fixture,
    const InputScenario& scenario)
{
    if (profile == InputProfile::AmbientIbl)
    {
        FillAmbientTexture(
            values, bindPoint, width, height, arraySize, random, fixture, scenario);
    }
    else if (profile == InputProfile::DirectionalLighting)
    {
        FillDirectionalTexture(
            values, bindPoint, width, height, arraySize, random, fixture, scenario);
    }
    else if (profile == InputProfile::DeferredPrepass)
    {
        FillDeferredPrepassTexture(
            values, bindPoint, width, height, arraySize, random, scenario);
    }
    else
    {
        FillUnitRandom(values, random);
    }
}

BoundResource CreateBufferResource(
    ID3D11Device* device,
    const ResourceBinding& binding,
    UINT bindPoint,
    std::mt19937& random)
{
    constexpr UINT elementCount = 64;
    std::vector<float> values(elementCount * 4);
    FillUnitRandom(values, random);

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = static_cast<UINT>(values.size() * sizeof(float));
    bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = values.data();

    ComPtr<ID3D11Buffer> buffer;
    CheckHRESULT(device->CreateBuffer(&bufferDesc, &initialData, &buffer),
                 "CreateBuffer for t" + std::to_string(bindPoint));

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    viewDesc.ViewDimension = binding.dimension;
    if (binding.dimension == D3D_SRV_DIMENSION_BUFFEREX)
    {
        viewDesc.BufferEx.FirstElement = 0;
        viewDesc.BufferEx.NumElements = elementCount;
    }
    else
    {
        viewDesc.Buffer.FirstElement = 0;
        viewDesc.Buffer.NumElements = elementCount;
    }

    BoundResource result;
    result.bindPoint = bindPoint;
    CheckHRESULT(device->CreateShaderResourceView(buffer.Get(), &viewDesc, &result.view),
                 "CreateShaderResourceView for t" + std::to_string(bindPoint));
    result.resource = buffer;
    return result;
}

BoundResource CreateTexture1DResource(
    ID3D11Device* device,
    const ResourceBinding& binding,
    UINT bindPoint,
    std::mt19937& random)
{
    constexpr UINT width = 64;
    const UINT arraySize =
        binding.dimension == D3D_SRV_DIMENSION_TEXTURE1DARRAY ? 4u : 1u;
    std::vector<float> values(width * arraySize * 4);
    FillUnitRandom(values, random);

    std::vector<D3D11_SUBRESOURCE_DATA> initialData(arraySize);
    for (UINT slice = 0; slice < arraySize; ++slice)
    {
        initialData[slice].pSysMem = values.data() + slice * width * 4;
        initialData[slice].SysMemPitch = width * sizeof(Pixel);
    }

    D3D11_TEXTURE1D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = arraySize;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture1D> texture;
    CheckHRESULT(device->CreateTexture1D(
                     &textureDesc, initialData.data(), &texture),
                 "CreateTexture1D for t" + std::to_string(bindPoint));

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = textureDesc.Format;
    viewDesc.ViewDimension = binding.dimension;
    if (binding.dimension == D3D_SRV_DIMENSION_TEXTURE1DARRAY)
    {
        viewDesc.Texture1DArray.MostDetailedMip = 0;
        viewDesc.Texture1DArray.MipLevels = 1;
        viewDesc.Texture1DArray.FirstArraySlice = 0;
        viewDesc.Texture1DArray.ArraySize = arraySize;
    }
    else
    {
        viewDesc.Texture1D.MostDetailedMip = 0;
        viewDesc.Texture1D.MipLevels = 1;
    }

    BoundResource result;
    result.bindPoint = bindPoint;
    CheckHRESULT(device->CreateShaderResourceView(texture.Get(), &viewDesc, &result.view),
                 "CreateShaderResourceView for t" + std::to_string(bindPoint));
    result.resource = texture;
    return result;
}

const char* FormatName(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return "R32G32B32A32_FLOAT";
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R16G16_FLOAT:
        return "R16G16_FLOAT";
    case DXGI_FORMAT_R16G16_UNORM:
        return "R16G16_UNORM";
    case DXGI_FORMAT_R8G8_UNORM:
        return "R8G8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R24G8_TYPELESS:
        return "R24G8_TYPELESS";
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return "R24_UNORM_X8_TYPELESS";
    case DXGI_FORMAT_R16_UNORM:
        return "R16_UNORM";
    default:
        return "UNKNOWN";
    }
}

struct TextureFormatSpec
{
    DXGI_FORMAT resource = DXGI_FORMAT_R32G32B32A32_FLOAT;
    DXGI_FORMAT view = DXGI_FORMAT_R32G32B32A32_FLOAT;
    UINT bytesPerPixel = 16;
};

TextureFormatSpec SelectTextureFormat(
    InputProfile profile,
    Fixture fixture,
    UINT bindPoint)
{
    if (fixture == Fixture::Adversarial)
        return {};
    if (profile == InputProfile::DirectionalLighting)
    {
        if (bindPoint == 0)
            return {
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4};
        if (bindPoint == 1)
            return {DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UNORM, 4};
        if (bindPoint == 2)
            return {
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM, 4};
        if (bindPoint == 3)
            return {
                DXGI_FORMAT_R24G8_TYPELESS,
                DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 4};
        if (bindPoint == 5)
            return {DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UNORM, 2};
    }
    else if (profile == InputProfile::AmbientIbl)
    {
        if (bindPoint == 1)
            return {DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UNORM, 4};
        if (bindPoint == 2 || bindPoint == 3)
            return {
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM, 4};
        if (bindPoint == 7 || bindPoint == 15)
            return {DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16_FLOAT, 4};
    }
    else if (profile == InputProfile::DeferredPrepass)
    {
        if (bindPoint == 0)
            return {
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4};
        if (bindPoint == 1)
            return {DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, 2};
        if (bindPoint == 2)
            return {
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM, 4};
    }
    return {
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_FLOAT, 8};
}

std::uint16_t FloatToHalf(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent == 0xFFu)
    {
        if (mantissa == 0)
            return static_cast<std::uint16_t>(sign | 0x7C00u);
        const std::uint16_t payload = static_cast<std::uint16_t>(
            std::max<std::uint32_t>(1u, mantissa >> 13));
        return static_cast<std::uint16_t>(sign | 0x7C00u | payload);
    }

    int halfExponent = static_cast<int>(exponent) - 127 + 15;
    if (halfExponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (halfExponent <= 0)
    {
        if (halfExponent < -10)
            return static_cast<std::uint16_t>(sign);
        mantissa |= 0x800000u;
        const unsigned shift = static_cast<unsigned>(14 - halfExponent);
        std::uint32_t rounded = mantissa >> shift;
        const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1u);
        if (remainder > halfway ||
            (remainder == halfway && (rounded & 1u) != 0))
        {
            ++rounded;
        }
        return static_cast<std::uint16_t>(sign | rounded);
    }

    std::uint32_t halfMantissa = mantissa >> 13;
    const std::uint32_t remainder = mantissa & 0x1FFFu;
    if (remainder > 0x1000u ||
        (remainder == 0x1000u && (halfMantissa & 1u) != 0))
    {
        ++halfMantissa;
        if (halfMantissa == 0x400u)
        {
            halfMantissa = 0;
            ++halfExponent;
            if (halfExponent >= 31)
                return static_cast<std::uint16_t>(sign | 0x7C00u);
        }
    }
    return static_cast<std::uint16_t>(
        sign |
        (static_cast<std::uint32_t>(halfExponent) << 10) |
        halfMantissa);
}

float HalfToFloat(std::uint16_t value)
{
    const std::uint32_t sign =
        static_cast<std::uint32_t>(value & 0x8000u) << 16;
    std::uint32_t exponent = (value >> 10) & 0x1Fu;
    std::uint32_t mantissa = value & 0x3FFu;
    std::uint32_t bits = 0;
    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            bits = sign;
        }
        else
        {
            int adjusted = -14;
            while ((mantissa & 0x400u) == 0)
            {
                mantissa <<= 1;
                --adjusted;
            }
            mantissa &= 0x3FFu;
            bits = sign |
                (static_cast<std::uint32_t>(adjusted + 127) << 23) |
                (mantissa << 13);
        }
    }
    else if (exponent == 31)
    {
        bits = sign | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

float LinearToSrgb(float value)
{
    value = std::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

float SrgbToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

std::uint32_t QuantizeUnorm(float value, std::uint32_t maximum)
{
    return static_cast<std::uint32_t>(
        std::lround(std::clamp(value, 0.0f, 1.0f) * maximum));
}

struct TexturePayload
{
    std::vector<std::uint8_t> bytes;
    std::vector<float> decoded;
};

TexturePayload EncodeTexture(
    const std::vector<float>& source,
    const TextureFormatSpec& format)
{
    const std::size_t pixelCount = source.size() / 4;
    TexturePayload payload;
    payload.bytes.resize(pixelCount * format.bytesPerPixel);
    payload.decoded.assign(pixelCount * 4, 0.0f);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const float* input = source.data() + pixel * 4;
        float* decoded = payload.decoded.data() + pixel * 4;
        std::uint8_t* output =
            payload.bytes.data() + pixel * format.bytesPerPixel;
        if (format.view == DXGI_FORMAT_R32G32B32A32_FLOAT)
        {
            std::memcpy(output, input, 4 * sizeof(float));
            std::copy(input, input + 4, decoded);
        }
        else if (format.view == DXGI_FORMAT_R16G16B16A16_FLOAT)
        {
            for (UINT channel = 0; channel < 4; ++channel)
            {
                const std::uint16_t encoded = FloatToHalf(input[channel]);
                std::memcpy(output + channel * 2, &encoded, sizeof(encoded));
                decoded[channel] = HalfToFloat(encoded);
            }
        }
        else if (format.view == DXGI_FORMAT_R16G16_FLOAT)
        {
            for (UINT channel = 0; channel < 2; ++channel)
            {
                const std::uint16_t encoded = FloatToHalf(input[channel]);
                std::memcpy(output + channel * 2, &encoded, sizeof(encoded));
                decoded[channel] = HalfToFloat(encoded);
            }
            decoded[3] = 1.0f;
        }
        else if (format.view == DXGI_FORMAT_R16G16_UNORM)
        {
            for (UINT channel = 0; channel < 2; ++channel)
            {
                const std::uint16_t encoded = static_cast<std::uint16_t>(
                    QuantizeUnorm(input[channel], 65535));
                std::memcpy(output + channel * 2, &encoded, sizeof(encoded));
                decoded[channel] = static_cast<float>(encoded) / 65535.0f;
            }
            decoded[3] = 1.0f;
        }
        else if (format.view == DXGI_FORMAT_R8G8_UNORM)
        {
            for (UINT channel = 0; channel < 2; ++channel)
            {
                output[channel] = static_cast<std::uint8_t>(
                    QuantizeUnorm(input[channel], 255));
                decoded[channel] = static_cast<float>(output[channel]) / 255.0f;
            }
            decoded[3] = 1.0f;
        }
        else if (format.view == DXGI_FORMAT_R8G8B8A8_UNORM ||
                 format.view == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            const bool srgb =
                format.view == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            for (UINT channel = 0; channel < 4; ++channel)
            {
                const float encodedValue =
                    srgb && channel < 3 ? LinearToSrgb(input[channel]) : input[channel];
                output[channel] = static_cast<std::uint8_t>(
                    QuantizeUnorm(encodedValue, 255));
                const float normalized =
                    static_cast<float>(output[channel]) / 255.0f;
                decoded[channel] =
                    srgb && channel < 3 ? SrgbToLinear(normalized) : normalized;
            }
        }
        else if (format.view == DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
        {
            const std::uint32_t encoded =
                QuantizeUnorm(input[0], 0xFFFFFFu);
            std::memcpy(output, &encoded, sizeof(encoded));
            decoded[0] = static_cast<float>(encoded) / 16777215.0f;
            decoded[3] = 1.0f;
        }
        else if (format.view == DXGI_FORMAT_R16_UNORM)
        {
            const std::uint16_t encoded = static_cast<std::uint16_t>(
                QuantizeUnorm(input[0], 65535));
            std::memcpy(output, &encoded, sizeof(encoded));
            decoded[0] = static_cast<float>(encoded) / 65535.0f;
            decoded[3] = 1.0f;
        }
        else
        {
            ThrowFailure("unsupported native texture format");
        }
    }
    return payload;
}

BoundResource CreateTexture2DResource(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const ResourceBinding& binding,
    UINT bindPoint,
    std::mt19937& random,
    InputProfile profile,
    Fixture fixture,
    const InputScenario& scenario)
{
    constexpr UINT width = 64;
    constexpr UINT height = 64;
    const bool multisampled =
        binding.dimension == D3D_SRV_DIMENSION_TEXTURE2DMS ||
        binding.dimension == D3D_SRV_DIMENSION_TEXTURE2DMSARRAY;
    UINT arraySize = 1;
    if (binding.dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY ||
        binding.dimension == D3D_SRV_DIMENSION_TEXTURE2DMSARRAY)
        arraySize = 4;
    else if (binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBE)
        arraySize = 6;
    else if (binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBEARRAY)
        arraySize = profile == InputProfile::AmbientIbl ? 24u : 6u;

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = arraySize;
    const TextureFormatSpec format =
        SelectTextureFormat(profile, fixture, bindPoint);
    textureDesc.Format = format.resource;
    textureDesc.SampleDesc.Count = multisampled ? 4u : 1u;
    textureDesc.Usage = multisampled ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
        (multisampled ? D3D11_BIND_RENDER_TARGET : 0u);
    if (binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBE ||
        binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBEARRAY)
        textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    std::vector<float> values;
    TexturePayload payload;
    std::vector<D3D11_SUBRESOURCE_DATA> initialData;
    if (!multisampled)
    {
        values.resize(static_cast<std::size_t>(width) * height * arraySize * 4);
        FillProfileTexture(
            values, profile, bindPoint, width, height, arraySize,
            random, fixture, scenario);
        payload = EncodeTexture(values, format);
        initialData.resize(arraySize);
        const std::size_t bytesPerSlice =
            static_cast<std::size_t>(width) * height * format.bytesPerPixel;
        for (UINT slice = 0; slice < arraySize; ++slice)
        {
            initialData[slice].pSysMem =
                payload.bytes.data() + slice * bytesPerSlice;
            initialData[slice].SysMemPitch = width * format.bytesPerPixel;
            initialData[slice].SysMemSlicePitch =
                width * height * format.bytesPerPixel;
        }
    }

    ComPtr<ID3D11Texture2D> texture;
    CheckHRESULT(device->CreateTexture2D(
                     &textureDesc,
                     initialData.empty() ? nullptr : initialData.data(),
                     &texture),
                 "CreateTexture2D for t" + std::to_string(bindPoint));

    if (multisampled)
    {
        std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
        for (UINT slice = 0; slice < arraySize; ++slice)
        {
            D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};
            targetDesc.Format = textureDesc.Format;
            if (arraySize == 1)
            {
                targetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                targetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                targetDesc.Texture2DMSArray.FirstArraySlice = slice;
                targetDesc.Texture2DMSArray.ArraySize = 1;
            }
            ComPtr<ID3D11RenderTargetView> target;
            CheckHRESULT(device->CreateRenderTargetView(texture.Get(), &targetDesc, &target),
                         "CreateRenderTargetView for multisampled SRV");
            const float clear[4] = {
                distribution(random), distribution(random),
                distribution(random), distribution(random),
            };
            context->ClearRenderTargetView(target.Get(), clear);
        }
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = format.view;
    viewDesc.ViewDimension = binding.dimension;
    switch (binding.dimension)
    {
    case D3D_SRV_DIMENSION_TEXTURE2D:
        viewDesc.Texture2D.MostDetailedMip = 0;
        viewDesc.Texture2D.MipLevels = 1;
        break;
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
        viewDesc.Texture2DArray.MostDetailedMip = 0;
        viewDesc.Texture2DArray.MipLevels = 1;
        viewDesc.Texture2DArray.FirstArraySlice = 0;
        viewDesc.Texture2DArray.ArraySize = arraySize;
        break;
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
        break;
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
        viewDesc.Texture2DMSArray.FirstArraySlice = 0;
        viewDesc.Texture2DMSArray.ArraySize = arraySize;
        break;
    case D3D_SRV_DIMENSION_TEXTURECUBE:
        viewDesc.TextureCube.MostDetailedMip = 0;
        viewDesc.TextureCube.MipLevels = 1;
        break;
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        viewDesc.TextureCubeArray.MostDetailedMip = 0;
        viewDesc.TextureCubeArray.MipLevels = 1;
        viewDesc.TextureCubeArray.First2DArrayFace = 0;
        viewDesc.TextureCubeArray.NumCubes = arraySize / 6;
        break;
    default:
        ThrowFailure("invalid Texture2D SRV dimension");
    }

    BoundResource result;
    result.bindPoint = bindPoint;
    result.width = width;
    result.height = height;
    result.arraySize = arraySize;
    result.encodedBytes = std::move(payload.bytes);
    result.decodedValues = std::move(payload.decoded);
    result.format = {
        bindPoint,
        DimensionName(binding.dimension),
        FormatName(format.resource),
        FormatName(format.view),
    };
    CheckHRESULT(device->CreateShaderResourceView(texture.Get(), &viewDesc, &result.view),
                 "CreateShaderResourceView for t" + std::to_string(bindPoint));
    result.resource = texture;
    return result;
}

BoundResource CreateTexture3DResource(
    ID3D11Device* device,
    UINT bindPoint,
    std::mt19937& random)
{
    constexpr UINT width = 32;
    constexpr UINT height = 32;
    constexpr UINT depth = 16;
    std::vector<float> values(
        static_cast<std::size_t>(width) * height * depth * 4);
    FillUnitRandom(values, random);

    D3D11_TEXTURE3D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.Depth = depth;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = values.data();
    initialData.SysMemPitch = width * sizeof(Pixel);
    initialData.SysMemSlicePitch = width * height * sizeof(Pixel);

    ComPtr<ID3D11Texture3D> texture;
    CheckHRESULT(device->CreateTexture3D(&textureDesc, &initialData, &texture),
                 "CreateTexture3D for t" + std::to_string(bindPoint));

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = textureDesc.Format;
    viewDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE3D;
    viewDesc.Texture3D.MostDetailedMip = 0;
    viewDesc.Texture3D.MipLevels = 1;

    BoundResource result;
    result.bindPoint = bindPoint;
    CheckHRESULT(device->CreateShaderResourceView(texture.Get(), &viewDesc, &result.view),
                 "CreateShaderResourceView for t" + std::to_string(bindPoint));
    result.resource = texture;
    return result;
}

BoundResource CreateResource(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const ResourceBinding& binding,
    UINT bindPoint,
    std::mt19937& random,
    InputProfile profile,
    Fixture fixture,
    const InputScenario& scenario)
{
    switch (binding.dimension)
    {
    case D3D_SRV_DIMENSION_BUFFER:
    case D3D_SRV_DIMENSION_BUFFEREX:
        return CreateBufferResource(device, binding, bindPoint, random);
    case D3D_SRV_DIMENSION_TEXTURE1D:
    case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
        return CreateTexture1DResource(device, binding, bindPoint, random);
    case D3D_SRV_DIMENSION_TEXTURE2D:
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
    case D3D_SRV_DIMENSION_TEXTURE2DMS:
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
    case D3D_SRV_DIMENSION_TEXTURECUBE:
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
        return CreateTexture2DResource(
            device, context, binding, bindPoint, random, profile, fixture, scenario);
    case D3D_SRV_DIMENSION_TEXTURE3D:
        return CreateTexture3DResource(device, bindPoint, random);
    default:
        ThrowFailure(
            "unsupported SRV dimension for t" + std::to_string(bindPoint) +
            ": " + DimensionName(binding.dimension));
    }
}

SeedResources CreateSeedResources(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const ShaderContract& contract,
    const DisassemblyInfo& disassembly,
    InputProfile profile,
    Fixture fixture,
    const InputScenario& scenario,
    UINT width,
    UINT height,
    bool gpuFrontFace,
    Sha256& inputHash)
{
    std::mt19937 random(scenario.randomSeed);
    SeedResources result;
    const bool shaped = profile != InputProfile::Unshaped;

    for (const ConstantBufferBinding& binding : contract.constantBuffers)
    {
        if (binding.size == 0 || binding.size % 16 != 0)
            ThrowFailure("invalid reflected constant-buffer size");
        std::vector<float> values(binding.size / sizeof(float));
        if (shaped)
        {
            std::uniform_real_distribution<float> modest(-0.25f, 0.25f);
            for (float& value : values)
                value = modest(random);
        }
        else
        {
            for (float& value : values)
                value = RandomConstant(random);
        }
        if (shaped)
            ShapeSharedCameraConstants(binding.bindPoint, values, random);
        if (profile == InputProfile::DirectionalLighting)
            ShapeDirectionalLightingConstants(
                binding.bindPoint, values, width, height, scenario);
        else if (profile == InputProfile::AmbientIbl)
            ShapeAmbientConstants(
                binding.bindPoint, values, width, height, random);
        else if (profile == InputProfile::DeferredPrepass)
            ShapeDeferredPrepassConstants(
                binding.bindPoint, values, scenario);

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = binding.size;
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = values.data();

        BoundConstantBuffer constantBuffer;
        constantBuffer.bindPoint = binding.bindPoint;
        constantBuffer.values = values;
        CheckHRESULT(device->CreateBuffer(
                         &bufferDesc, &initialData, &constantBuffer.buffer),
                     "CreateBuffer for b" + std::to_string(binding.bindPoint));
        result.constantBuffers.push_back(std::move(constantBuffer));
    }

    for (const ResourceBinding& binding : contract.resources)
    {
        for (UINT offset = 0; offset < binding.bindCount; ++offset)
        {
            result.resources.push_back(CreateResource(
                device, context, binding, binding.bindPoint + offset, random,
                profile, fixture, scenario));
        }
    }

    for (const SamplerBinding& binding : contract.samplers)
    {
        for (UINT offset = 0; offset < binding.bindCount; ++offset)
        {
            BoundSampler sampler;
            sampler.bindPoint = binding.bindPoint + offset;
            const bool comparison =
                disassembly.comparisonSamplers.count(sampler.bindPoint) != 0;

            D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.Filter = comparison
                ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
                : (profile == InputProfile::AmbientIbl &&
                   sampler.bindPoint == 3
                    ? D3D11_FILTER_MIN_MAG_MIP_POINT
                    : D3D11_FILTER_MIN_MAG_MIP_LINEAR);
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            samplerDesc.ComparisonFunc =
                comparison ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_NEVER;
            samplerDesc.MinLOD = 0.0f;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

            CheckHRESULT(device->CreateSamplerState(&samplerDesc, &sampler.sampler),
                         "CreateSamplerState for s" +
                             std::to_string(sampler.bindPoint));
            sampler.descriptor = samplerDesc;
            result.samplers.push_back(std::move(sampler));
        }
    }

    inputHash.AddString("scenario");
    inputHash.AddValue(scenario.id);
    inputHash.AddValue(scenario.randomSeed);
    inputHash.AddValue(scenario.dedicated);
    inputHash.AddString(scenario.semanticId);
    inputHash.AddString(FixtureName(fixture));
    inputHash.AddValue(gpuFrontFace);
    for (const BoundConstantBuffer& buffer : result.constantBuffers)
    {
        inputHash.AddString("constant-buffer");
        inputHash.AddValue(buffer.bindPoint);
        const std::uint64_t byteCount =
            buffer.values.size() * sizeof(float);
        inputHash.AddValue(byteCount);
        inputHash.Add(buffer.values.data(), static_cast<std::size_t>(byteCount));
    }
    for (const BoundResource& resource : result.resources)
    {
        if (resource.encodedBytes.empty() ||
            resource.format.resourceFormat.empty() ||
            resource.format.srvFormat.empty())
        {
            ThrowFailure(
                "unsupported_input_resource: t" +
                std::to_string(resource.bindPoint));
        }
        inputHash.AddString("resource");
        inputHash.AddValue(resource.bindPoint);
        inputHash.AddValue(resource.width);
        inputHash.AddValue(resource.height);
        inputHash.AddValue(resource.arraySize);
        inputHash.AddString(resource.format.dimension);
        inputHash.AddString(resource.format.resourceFormat);
        inputHash.AddString(resource.format.srvFormat);
        const std::uint64_t byteCount = resource.encodedBytes.size();
        inputHash.AddValue(byteCount);
        inputHash.Add(resource.encodedBytes.data(), resource.encodedBytes.size());
        result.formats.push_back(resource.format);
    }
    for (const BoundSampler& sampler : result.samplers)
    {
        inputHash.AddString("sampler");
        inputHash.AddValue(sampler.bindPoint);
        inputHash.AddValue(sampler.descriptor.Filter);
        inputHash.AddValue(sampler.descriptor.AddressU);
        inputHash.AddValue(sampler.descriptor.AddressV);
        inputHash.AddValue(sampler.descriptor.AddressW);
        inputHash.AddValue(sampler.descriptor.MipLODBias);
        inputHash.AddValue(sampler.descriptor.MaxAnisotropy);
        inputHash.AddValue(sampler.descriptor.ComparisonFunc);
        for (const float component : sampler.descriptor.BorderColor)
            inputHash.AddValue(component);
        inputHash.AddValue(sampler.descriptor.MinLOD);
        inputHash.AddValue(sampler.descriptor.MaxLOD);
    }
    return result;
}

void BindSeedResources(ID3D11DeviceContext* context, const SeedResources& resources)
{
    std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>
        emptyConstantBuffers{};
    std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT>
        emptyResources{};
    std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT>
        emptySamplers{};
    context->PSSetConstantBuffers(
        0, static_cast<UINT>(emptyConstantBuffers.size()),
        emptyConstantBuffers.data());
    context->PSSetShaderResources(
        0, static_cast<UINT>(emptyResources.size()), emptyResources.data());
    context->PSSetSamplers(
        0, static_cast<UINT>(emptySamplers.size()), emptySamplers.data());

    for (const BoundConstantBuffer& binding : resources.constantBuffers)
    {
        ID3D11Buffer* buffer = binding.buffer.Get();
        context->PSSetConstantBuffers(binding.bindPoint, 1, &buffer);
    }
    for (const BoundResource& binding : resources.resources)
    {
        ID3D11ShaderResourceView* view = binding.view.Get();
        context->PSSetShaderResources(binding.bindPoint, 1, &view);
    }
    for (const BoundSampler& binding : resources.samplers)
    {
        ID3D11SamplerState* sampler = binding.sampler.Get();
        context->PSSetSamplers(binding.bindPoint, 1, &sampler);
    }
}

const BoundResource& FindResource(
    const SeedResources& resources,
    UINT bindPoint)
{
    const auto found = std::find_if(
        resources.resources.begin(), resources.resources.end(),
        [bindPoint](const BoundResource& resource) {
            return resource.bindPoint == bindPoint;
        });
    if (found == resources.resources.end() || found->decodedValues.empty())
        ThrowFailure("bucket classification resource missing");
    return *found;
}

const BoundConstantBuffer& FindConstantBuffer(
    const SeedResources& resources,
    UINT bindPoint)
{
    const auto found = std::find_if(
        resources.constantBuffers.begin(), resources.constantBuffers.end(),
        [bindPoint](const BoundConstantBuffer& buffer) {
            return buffer.bindPoint == bindPoint;
        });
    if (found == resources.constantBuffers.end())
        ThrowFailure("bucket classification constant buffer missing");
    return *found;
}

const BoundSampler& FindSampler(
    const SeedResources& resources,
    UINT bindPoint)
{
    const auto found = std::find_if(
        resources.samplers.begin(), resources.samplers.end(),
        [bindPoint](const BoundSampler& sampler) {
            return sampler.bindPoint == bindPoint;
        });
    if (found == resources.samplers.end())
        ThrowFailure("bucket classification sampler missing");
    return *found;
}

std::array<float, 4> ConstantVector(
    const SeedResources& resources,
    UINT bindPoint,
    UINT vectorIndex)
{
    const std::vector<float>& values =
        FindConstantBuffer(resources, bindPoint).values;
    const std::size_t offset = static_cast<std::size_t>(vectorIndex) * 4;
    if (offset + 4 > values.size())
        ThrowFailure("bucket classification constant vector missing");
    return {
        values[offset], values[offset + 1],
        values[offset + 2], values[offset + 3]};
}

Pixel ResourcePixel(
    const BoundResource& resource,
    UINT x = 0,
    UINT y = 0,
    UINT slice = 0)
{
    if (x >= resource.width || y >= resource.height ||
        slice >= resource.arraySize)
    {
        ThrowFailure("bucket classification texel out of range");
    }
    const std::size_t index =
        ((static_cast<std::size_t>(slice) * resource.height + y) *
         resource.width + x) * 4;
    return {
        resource.decodedValues[index],
        resource.decodedValues[index + 1],
        resource.decodedValues[index + 2],
        resource.decodedValues[index + 3],
    };
}

UINT WrapTexel(int coordinate, UINT dimension)
{
    const int signedDimension = static_cast<int>(dimension);
    return static_cast<UINT>(
        (coordinate % signedDimension + signedDimension) % signedDimension);
}

Pixel SampleResource2D(
    const BoundResource& resource,
    const BoundSampler& sampler,
    float u,
    float v)
{
    if (sampler.descriptor.AddressU != D3D11_TEXTURE_ADDRESS_WRAP ||
        sampler.descriptor.AddressV != D3D11_TEXTURE_ADDRESS_WRAP)
    {
        ThrowFailure("bucket classification sampler addressing unsupported");
    }
    if (sampler.descriptor.Filter == D3D11_FILTER_MIN_MAG_MIP_POINT)
    {
        const UINT x = WrapTexel(
            static_cast<int>(std::floor(u * resource.width)),
            resource.width);
        const UINT y = WrapTexel(
            static_cast<int>(std::floor(v * resource.height)),
            resource.height);
        return ResourcePixel(resource, x, y);
    }

    const float texelX = u * resource.width - 0.5f;
    const float texelY = v * resource.height - 0.5f;
    const int x0 = static_cast<int>(std::floor(texelX));
    const int y0 = static_cast<int>(std::floor(texelY));
    const float weightX = texelX - std::floor(texelX);
    const float weightY = texelY - std::floor(texelY);
    const Pixel samples[4] = {
        ResourcePixel(
            resource, WrapTexel(x0, resource.width),
            WrapTexel(y0, resource.height)),
        ResourcePixel(
            resource, WrapTexel(x0 + 1, resource.width),
            WrapTexel(y0, resource.height)),
        ResourcePixel(
            resource, WrapTexel(x0, resource.width),
            WrapTexel(y0 + 1, resource.height)),
        ResourcePixel(
            resource, WrapTexel(x0 + 1, resource.width),
            WrapTexel(y0 + 1, resource.height)),
    };
    Pixel result{};
    for (UINT channel = 0; channel < 4; ++channel)
    {
        const float top =
            samples[0][channel] * (1.0f - weightX) +
            samples[1][channel] * weightX;
        const float bottom =
            samples[2][channel] * (1.0f - weightX) +
            samples[3][channel] * weightX;
        result[channel] =
            top * (1.0f - weightY) + bottom * weightY;
    }
    return result;
}

void AddUniformBucket(
    std::map<std::string, BucketMask>& buckets,
    const std::string& name,
    bool matches,
    std::uint64_t population,
    std::size_t pixelCount)
{
    if (!matches)
        return;
    BucketMask mask;
    mask.population = population;
    mask.pixels.assign(pixelCount, 1);
    buckets.emplace(name, std::move(mask));
}

std::array<float, 3> DecodeDeferredNormal(const Pixel& encoded)
{
    const float x = encoded[0] * 4.0f - 2.0f;
    const float y = encoded[1] * 4.0f - 2.0f;
    const float lengthSquared = x * x + y * y;
    const float scale = std::sqrt(std::max(0.0f, 1.0f - lengthSquared * 0.25f));
    return {
        x * scale,
        y * scale,
        -(1.0f - lengthSquared * 0.5f),
    };
}

float DotRow(const std::array<float, 4>& row, const Pixel& value)
{
    return row[0] * value[0] + row[1] * value[1] +
        row[2] * value[2] + row[3] * value[3];
}

std::map<std::string, BucketMask> ClassifyBuckets(
    InputProfile profile,
    const InputScenario& scenario,
    const SeedResources& resources,
    UINT width,
    UINT height,
    bool gpuFrontFace)
{
    std::map<std::string, BucketMask> buckets;
    if (!scenario.dedicated)
        return buckets;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    const float depthBoundary = 0.01f;

    if (profile == InputProfile::AmbientIbl)
    {
        const Pixel depth = ResourcePixel(FindResource(resources, 7));
        AddUniformBucket(
            buckets, "depth.near", depth[1] < depthBoundary,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "depth.equal", depth[1] == depthBoundary,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "depth.far", depth[1] > depthBoundary,
            pixelCount, pixelCount);

        const Pixel material = ResourcePixel(FindResource(resources, 2));
        AddUniformBucket(
            buckets, "ibl.off", material[1] <= 0.001961f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "ibl.on", material[1] > 0.001961f,
            pixelCount, pixelCount);

        const BoundResource& shadingResource = FindResource(resources, 3);
        const BoundSampler& shadingSampler = FindSampler(resources, 3);
        const Pixel shading =
            SampleResource2D(shadingResource, shadingSampler, 0.0f, 0.0f);
        const bool skin = std::abs(shading[3] * 255.0f - 5.0f) < 0.25f;
        AddUniformBucket(
            buckets, "material.skin", skin, pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "material.default", !skin, pixelCount, pixelCount);
        const float roughness = std::clamp(shading[0] - 0.3f, 0.0f, 1.0f);
        AddUniformBucket(
            buckets, "roughness.zero", roughness == 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "roughness.mid", roughness > 0.0f && roughness < 0.6f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "roughness.high", roughness >= 0.6f,
            pixelCount, pixelCount);

        if (skin)
        {
            static const std::array<float, 10> tapOffsets{
                -2.0f, -1.28f, -0.72f, -0.32f, -0.08f,
                0.08f, 0.32f, 0.72f, 1.28f, 2.0f};
            const Pixel blurDepth = ResourcePixel(FindResource(resources, 15));
            const auto blurConstants = ConstantVector(resources, 0, 0);
            const float depthMask = depth[1] >= depthBoundary ? 1.0f : 0.0f;
            const float centerRef =
                (depthMask * blurConstants[2] + 1.0f) * blurDepth[1];
            const float tapBaseX =
                blurConstants[0] * 0.078125f / centerRef;
            const float tapBaseY =
                blurConstants[0] * 0.138890f / centerRef;
            UINT skinTaps = 0;
            UINT defaultTaps = 0;
            for (const float offset : tapOffsets)
            {
                const Pixel tap = SampleResource2D(
                    shadingResource, shadingSampler,
                    tapBaseX * offset, tapBaseY * offset);
                if (std::abs(tap[3] * 255.0f - 5.0f) < 0.25f)
                    ++skinTaps;
                else
                    ++defaultTaps;
            }
            AddUniformBucket(
                buckets, "skin-taps.skin", skinTaps != 0,
                pixelCount * skinTaps, pixelCount);
            AddUniformBucket(
                buckets, "skin-taps.default", defaultTaps != 0,
                pixelCount * defaultTaps, pixelCount);
        }
    }
    else if (profile == InputProfile::DirectionalLighting)
    {
        const BoundResource& depthResource = FindResource(resources, 3);
        const BoundSampler& depthSampler = FindSampler(resources, 3);
        const BoundResource& materialResource = FindResource(resources, 2);
        const BoundSampler& materialSampler = FindSampler(resources, 2);
        const BoundResource& normalResource = FindResource(resources, 1);
        const BoundSampler& normalSampler = FindSampler(resources, 1);
        const Pixel depth = ResourcePixel(depthResource);
        AddUniformBucket(
            buckets, "depth.near", depth[0] < depthBoundary,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "depth.equal", depth[0] == depthBoundary,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "depth.far", depth[0] > depthBoundary,
            pixelCount, pixelCount);
        const float linearizedDepth = depth[0] < depthBoundary
            ? depth[0] * 100.0f
            : depth[0] * 1.01f - 0.01f;
        const auto cascadeRange = ConstantVector(resources, 2, 10);
        const bool cascade0 = linearizedDepth < cascadeRange[1];
        const bool cascade1 = cascadeRange[0] < linearizedDepth;
        AddUniformBucket(
            buckets, "cascade.0-only", cascade0 && !cascade1,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "cascade.blend", cascade0 && cascade1,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "cascade.1-only", !cascade0 && cascade1,
            pixelCount, pixelCount);

        const Pixel material = ResourcePixel(materialResource);
        const bool skin =
            std::abs(material[3] * 255.0f - 1.0f) < 0.25f;
        AddUniformBucket(
            buckets, "material.skin", skin, pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "material.default", !skin, pixelCount, pixelCount);
        const float roughness = 1.0f - material[0];
        AddUniformBucket(
            buckets, "roughness.low", roughness < 0.25f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "roughness.mid",
            roughness >= 0.25f && roughness < 0.7f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "roughness.high", roughness >= 0.7f,
            pixelCount, pixelCount);

        const std::array<float, 3> normal =
            DecodeDeferredNormal(ResourcePixel(normalResource));
        const auto light = ConstantVector(resources, 2, 1);
        const float normalDotLight =
            normal[0] * light[0] + normal[1] * light[1] +
            normal[2] * light[2];
        AddUniformBucket(
            buckets, "light.front", normalDotLight > 0.5f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "light.grazing", std::abs(normalDotLight) <= 0.1f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "light.back", normalDotLight < -0.5f,
            pixelCount, pixelCount);

        const std::array<std::array<float, 4>, 4> farRows{
            ConstantVector(resources, 12, 20),
            ConstantVector(resources, 12, 21),
            ConstantVector(resources, 12, 22),
            ConstantVector(resources, 12, 23),
        };
        const std::array<std::array<float, 4>, 4> nearRows{
            ConstantVector(resources, 12, 24),
            ConstantVector(resources, 12, 25),
            ConstantVector(resources, 12, 26),
            ConstantVector(resources, 12, 27),
        };
        const auto screen = ConstantVector(resources, 2, 0);
        const float fadeLimit = ConstantVector(resources, 2, 24)[0];
        BucketMask inside;
        BucketMask outside;
        BucketMask brdfPeak;
        BucketMask brdfFallback;
        BucketMask brdfUnityRatio;
        BucketMask brdfNonunityRatio;
        inside.pixels.assign(pixelCount, 0);
        outside.pixels.assign(pixelCount, 0);
        brdfPeak.pixels.assign(pixelCount, 0);
        brdfFallback.pixels.assign(pixelCount, 0);
        brdfUnityRatio.pixels.assign(pixelCount, 0);
        brdfNonunityRatio.pixels.assign(pixelCount, 0);
        constexpr float selectorEpsilon = 1.0e-3f;
        for (UINT y = 0; y < height; ++y)
        {
            for (UINT x = 0; x < width; ++x)
            {
                const float positionX = static_cast<float>(x) + 0.5f;
                const float positionY = static_cast<float>(y) + 0.5f;
                const float sampleU = positionX * screen[0];
                const float sampleV = positionY * screen[1];
                const Pixel sampledDepth = SampleResource2D(
                    depthResource, depthSampler, sampleU, sampleV);
                const float sampledLinearizedDepth =
                    sampledDepth[0] < depthBoundary
                    ? sampledDepth[0] * 100.0f
                    : sampledDepth[0] * 1.01f - 0.01f;
                const auto& sampledRows =
                    sampledDepth[0] < depthBoundary ? nearRows : farRows;
                const Pixel position{
                    positionX * screen[2] * 2.0f - 1.0f,
                    positionY * screen[3] * -2.0f + 1.0f,
                    sampledLinearizedDepth,
                    1.0f,
                };
                const float w = DotRow(sampledRows[3], position);
                const float px = DotRow(sampledRows[0], position) / w;
                const float py = DotRow(sampledRows[1], position) / w;
                const float pz = DotRow(sampledRows[2], position) / w;
                const bool isInside =
                    (px * px + py * py + pz * pz) / fadeLimit < 1.0f;
                const std::size_t index =
                    static_cast<std::size_t>(y) * width + x;
                (isInside ? inside.pixels : outside.pixels)[index] = 1;
                ++(isInside ? inside.population : outside.population);

                const Pixel sampledMaterial = SampleResource2D(
                    materialResource, materialSampler, sampleU, sampleV);
                const bool sampledSkin =
                    std::abs(sampledMaterial[3] * 255.0f - 1.0f) < 0.25f;
                if (!sampledSkin)
                {
                    const std::array<float, 3> sampledNormal =
                        DecodeDeferredNormal(SampleResource2D(
                            normalResource, normalSampler, sampleU, sampleV));
                    const float sampledNormalDotLight =
                        sampledNormal[0] * light[0] +
                        sampledNormal[1] * light[1] +
                        sampledNormal[2] * light[2];
                    const float positionLengthSquared =
                        px * px + py * py + pz * pz;
                    if (positionLengthSquared > 0.0f)
                    {
                        const float reciprocalLength =
                            1.0f / std::sqrt(positionLengthSquared);
                        const std::array<float, 3> viewDirection{
                            -px * reciprocalLength,
                            -py * reciprocalLength,
                            -pz * reciprocalLength,
                        };
                        std::array<float, 3> halfVector{
                            light[0] + viewDirection[0],
                            light[1] + viewDirection[1],
                            light[2] + viewDirection[2],
                        };
                        const float halfLengthSquared =
                            halfVector[0] * halfVector[0] +
                            halfVector[1] * halfVector[1] +
                            halfVector[2] * halfVector[2];
                        if (halfLengthSquared > 0.0f)
                        {
                            const float halfReciprocalLength =
                                1.0f / std::sqrt(halfLengthSquared);
                            for (float& component : halfVector)
                                component *= halfReciprocalLength;
                            const float normalDotView = std::clamp(
                                sampledNormal[0] * viewDirection[0] +
                                sampledNormal[1] * viewDirection[1] +
                                sampledNormal[2] * viewDirection[2],
                                0.0f, 1.0f);
                            const float normalDotLightClamped =
                                std::clamp(
                                    sampledNormalDotLight, 0.0f, 1.0f);
                            const float viewDotHalf = std::clamp(
                                viewDirection[0] * halfVector[0] +
                                viewDirection[1] * halfVector[1] +
                                viewDirection[2] * halfVector[2],
                                0.0f, 1.0f);
                            const float normalDotHalf = std::clamp(
                                sampledNormal[0] * halfVector[0] +
                                sampledNormal[1] * halfVector[1] +
                                sampledNormal[2] * halfVector[2],
                                0.0f, 1.0f);
                            const float minimumNormalDot =
                                std::min(
                                    normalDotLightClamped, normalDotView);
                            const float peakDelta =
                                viewDotHalf -
                                2.0f * normalDotHalf * minimumNormalDot;
                            if (peakDelta > selectorEpsilon)
                            {
                                brdfPeak.pixels[index] = 1;
                                ++brdfPeak.population;
                            }
                            else if (peakDelta < -selectorEpsilon)
                            {
                                brdfFallback.pixels[index] = 1;
                                ++brdfFallback.population;
                            }

                            const float ratioDelta =
                                normalDotView - normalDotLightClamped;
                            if (ratioDelta < -selectorEpsilon)
                            {
                                brdfUnityRatio.pixels[index] = 1;
                                ++brdfUnityRatio.population;
                            }
                            else if (ratioDelta > selectorEpsilon)
                            {
                                brdfNonunityRatio.pixels[index] = 1;
                                ++brdfNonunityRatio.population;
                            }
                        }
                    }
                }
            }
        }
        if (inside.population != 0)
            buckets.emplace("distance-fade.inside", std::move(inside));
        if (outside.population != 0)
            buckets.emplace("distance-fade.outside", std::move(outside));
        if (brdfPeak.population != 0)
            buckets.emplace("brdf.peak", std::move(brdfPeak));
        if (brdfFallback.population != 0)
            buckets.emplace("brdf.fallback", std::move(brdfFallback));
        if (brdfUnityRatio.population != 0)
            buckets.emplace("brdf.unity-ratio", std::move(brdfUnityRatio));
        if (brdfNonunityRatio.population != 0)
            buckets.emplace(
                "brdf.nonunity-ratio", std::move(brdfNonunityRatio));
    }
    else if (profile == InputProfile::DeferredPrepass)
    {
        const auto fade = ConstantVector(resources, 2, 4);
        const bool bypass = fade[3] == -1.0f;
        AddUniformBucket(
            buckets, "alpha.bypass", bypass, pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "alpha.active", !bypass, pixelCount, pixelCount);
        const bool front = gpuFrontFace;
        AddUniformBucket(
            buckets, "face.front", front, pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "face.back", !front, pixelCount, pixelCount);

        const Pixel normalMap = ResourcePixel(FindResource(resources, 1));
        const float nx = normalMap[0] * 2.0f - 1.0f;
        const float ny = normalMap[1] * 2.0f - 1.0f;
        const float normalLength = nx * nx + ny * ny;
        AddUniformBucket(
            buckets, "normal.xy-interior", normalLength < 1.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "normal.xy-clamped", normalLength >= 1.0f,
            pixelCount, pixelCount);
        const float nz =
            std::sqrt(1.0f - std::clamp(normalLength, 0.0f, 1.0f));
        const float tangentZ = front ? nz : -nz;
        const float axis = -tangentZ;
        AddUniformBucket(
            buckets, "normal.axis-negative", axis < 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "normal.axis-clamped", axis >= 0.0f,
            pixelCount, pixelCount);

        const auto smoothness = ConstantVector(resources, 2, 5);
        AddUniformBucket(
            buckets, "smoothness.gate-zero", smoothness[3] < 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "smoothness.gate-live", smoothness[3] >= 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "smoothness.direct", smoothness[1] == 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "smoothness.span", smoothness[1] != 0.0f,
            pixelCount, pixelCount);

        const float globalFade = ConstantVector(resources, 12, 30)[0];
        const bool bitA = globalFade != 0.0f && fade[1] != 0.0f;
        const bool bitB = fade[0] != 0.0f;
        AddUniformBucket(
            buckets, "flags.none", !bitA && !bitB,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "flags.a", bitA && !bitB,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "flags.b", !bitA && bitB,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "flags.both", bitA && bitB,
            pixelCount, pixelCount);

        const auto scroll = ConstantVector(resources, 2, 2);
        AddUniformBucket(
            buckets, "scroll.x-positive", scroll[0] >= 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "scroll.x-negative", scroll[0] < 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "scroll.y-positive", scroll[1] >= 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "scroll.y-negative", scroll[1] < 0.0f,
            pixelCount, pixelCount);
        AddUniformBucket(
            buckets, "motion.distinct-banks", true,
            pixelCount, pixelCount);
    }
    return buckets;
}

RenderOutputs RenderShader(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11VertexShader* vertexShader,
    ID3D11PixelShader* pixelShader,
    ID3D11RasterizerState* rasterizerState,
    ID3D11DepthStencilState* depthStencilState,
    const SeedResources& seedResources,
    UINT renderTargetCount,
    UINT width,
    UINT height)
{
    std::vector<ComPtr<ID3D11Texture2D>> targets(renderTargetCount);
    std::vector<ComPtr<ID3D11RenderTargetView>> targetViews(renderTargetCount);
    std::vector<ComPtr<ID3D11Texture2D>> stagingTextures(renderTargetCount);

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    std::vector<ID3D11RenderTargetView*> rawTargetViews(renderTargetCount);
    for (UINT index = 0; index < renderTargetCount; ++index)
    {
        CheckHRESULT(device->CreateTexture2D(&textureDesc, nullptr, &targets[index]),
                     "CreateTexture2D for render target");
        CheckHRESULT(device->CreateRenderTargetView(
                         targets[index].Get(), nullptr, &targetViews[index]),
                     "CreateRenderTargetView");
        CheckHRESULT(device->CreateTexture2D(
                         &stagingDesc, nullptr, &stagingTextures[index]),
                     "CreateTexture2D for readback");
        rawTargetViews[index] = targetViews[index].Get();
        const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(targetViews[index].Get(), clear);
    }

    BindSeedResources(context, seedResources);
    context->OMSetRenderTargets(renderTargetCount, rawTargetViews.data(), nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    context->RSSetState(rasterizerState);
    context->OMSetDepthStencilState(depthStencilState, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader, nullptr, 0);
    context->PSSetShader(pixelShader, nullptr, 0);
    context->Draw(3, 0);

    context->OMSetRenderTargets(0, nullptr, nullptr);
    for (UINT index = 0; index < renderTargetCount; ++index)
        context->CopyResource(stagingTextures[index].Get(), targets[index].Get());

    RenderOutputs outputs(renderTargetCount);
    for (UINT target = 0; target < renderTargetCount; ++target)
    {
        outputs[target].resize(static_cast<std::size_t>(width) * height);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        CheckHRESULT(context->Map(
                         stagingTextures[target].Get(), 0, D3D11_MAP_READ, 0, &mapped),
                     "Map readback texture");
        for (UINT y = 0; y < height; ++y)
        {
            const auto* sourceRow = static_cast<const std::uint8_t*>(mapped.pData) +
                static_cast<std::size_t>(mapped.RowPitch) * y;
            std::memcpy(
                outputs[target].data() + static_cast<std::size_t>(y) * width,
                sourceRow,
                static_cast<std::size_t>(width) * sizeof(Pixel));
        }
        context->Unmap(stagingTextures[target].Get(), 0);
    }
    return outputs;
}

struct ValueComparison
{
    bool divergent = false;
    double absoluteDifference = 0.0;
    double relativeDifference = 0.0;
};

ValueComparison CompareValue(
    float reference,
    float candidate,
    float toleranceAbsolute,
    float toleranceRelative)
{
    const bool referenceNan = std::isnan(reference);
    const bool candidateNan = std::isnan(candidate);
    if (referenceNan && candidateNan)
        return {};

    const bool referenceInfinite = std::isinf(reference);
    const bool candidateInfinite = std::isinf(candidate);
    if (referenceInfinite && candidateInfinite &&
        std::signbit(reference) == std::signbit(candidate))
        return {};

    if (!std::isfinite(reference) || !std::isfinite(candidate))
    {
        return {
            true,
            static_cast<double>(std::numeric_limits<float>::max()),
            static_cast<double>(std::numeric_limits<float>::max()),
        };
    }

    const double absoluteDifference =
        std::abs(static_cast<double>(reference) - static_cast<double>(candidate));
    const double scale = std::max(
        std::abs(static_cast<double>(reference)),
        std::abs(static_cast<double>(candidate)));
    const double relativeDifference =
        scale > 0.0 ? absoluteDifference / scale : 0.0;
    return {
        absoluteDifference > toleranceAbsolute &&
            absoluteDifference > static_cast<double>(toleranceRelative) * scale,
        absoluteDifference,
        relativeDifference,
    };
}

void RecordTopDivergence(ComparisonStats& stats, DivergentPixel divergence)
{
    stats.topDivergences.push_back(std::move(divergence));
    std::sort(
        stats.topDivergences.begin(), stats.topDivergences.end(),
        [](const DivergentPixel& left, const DivergentPixel& right) {
            return left.score > right.score;
        });
    if (stats.topDivergences.size() > 10)
        stats.topDivergences.resize(10);
}

void MergeComparisonStats(
    ComparisonStats& aggregate,
    const ComparisonStats& scenario)
{
    aggregate.totalChannels += scenario.totalChannels;
    aggregate.totalPixels += scenario.totalPixels;
    aggregate.divergentChannels += scenario.divergentChannels;
    aggregate.divergentPixels += scenario.divergentPixels;
    aggregate.maximumAbsoluteDifference = std::max(
        aggregate.maximumAbsoluteDifference,
        scenario.maximumAbsoluteDifference);
    aggregate.maximumRelativeDifference = std::max(
        aggregate.maximumRelativeDifference,
        scenario.maximumRelativeDifference);
    aggregate.absoluteDifferenceSum += scenario.absoluteDifferenceSum;
    aggregate.relativeDifferenceSum += scenario.relativeDifferenceSum;
    if (scenario.worst.present &&
        (!aggregate.worst.present ||
         scenario.worst.absoluteDifference >
             aggregate.worst.absoluteDifference))
    {
        aggregate.worst = scenario.worst;
    }
    for (const DivergentPixel& divergence : scenario.topDivergences)
        RecordTopDivergence(aggregate, divergence);
}

void CompareOutputs(
    ComparisonStats& stats,
    const RenderOutputs& reference,
    const RenderOutputs& candidate,
    UINT seed,
    UINT width,
    float toleranceAbsolute,
    float toleranceRelative,
    const std::vector<std::uint8_t>* pixelMask = nullptr)
{
    if (reference.size() != candidate.size())
        ThrowFailure("render-target count changed between shader executions");
    if (reference.empty())
        ThrowFailure("render-target output is empty");
    std::vector<std::uint8_t> divergentPixelFlags(
        reference.front().size(), 0);
    if (pixelMask == nullptr)
    {
        stats.totalPixels += reference.front().size();
    }
    else
    {
        stats.totalPixels += static_cast<std::uint64_t>(
            std::count(pixelMask->begin(), pixelMask->end(), std::uint8_t{1}));
    }

    for (UINT target = 0; target < reference.size(); ++target)
    {
        if (reference[target].size() != candidate[target].size())
            ThrowFailure("render-target dimensions changed between shader executions");

        for (std::size_t pixelIndex = 0;
             pixelIndex < reference[target].size(); ++pixelIndex)
        {
            if (pixelMask != nullptr &&
                (pixelIndex >= pixelMask->size() || (*pixelMask)[pixelIndex] == 0))
            {
                continue;
            }
            bool pixelDivergent = false;
            double pixelScore = 0.0;
            for (UINT channel = 0; channel < 4; ++channel)
            {
                const ValueComparison difference = CompareValue(
                    reference[target][pixelIndex][channel],
                    candidate[target][pixelIndex][channel],
                    toleranceAbsolute, toleranceRelative);
                ++stats.totalChannels;
                stats.maximumAbsoluteDifference =
                    std::max(stats.maximumAbsoluteDifference,
                             difference.absoluteDifference);
                stats.maximumRelativeDifference =
                    std::max(stats.maximumRelativeDifference,
                             difference.relativeDifference);
                stats.absoluteDifferenceSum += difference.absoluteDifference;
                stats.relativeDifferenceSum += difference.relativeDifference;

                if (!stats.worst.present ||
                    difference.absoluteDifference > stats.worst.absoluteDifference)
                {
                    stats.worst = {
                        true,
                        seed,
                        target,
                        static_cast<UINT>(pixelIndex % width),
                        static_cast<UINT>(pixelIndex / width),
                        channel,
                        reference[target][pixelIndex][channel],
                        candidate[target][pixelIndex][channel],
                        difference.absoluteDifference,
                        difference.relativeDifference,
                    };
                }

                if (difference.divergent)
                {
                    ++stats.divergentChannels;
                    pixelDivergent = true;
                    pixelScore = std::max(pixelScore, difference.absoluteDifference);
                }
            }

            if (pixelDivergent)
            {
                divergentPixelFlags[pixelIndex] = 1;
                RecordTopDivergence(stats, {
                    seed,
                    target,
                    static_cast<UINT>(pixelIndex % width),
                    static_cast<UINT>(pixelIndex / width),
                    reference[target][pixelIndex],
                    candidate[target][pixelIndex],
                    pixelScore,
                });
            }
        }
    }
    stats.divergentPixels += static_cast<std::uint64_t>(
        std::count(
            divergentPixelFlags.begin(), divergentPixelFlags.end(),
            std::uint8_t{1}));
}

void PrintPixel(const Pixel& pixel)
{
    std::cout << "(";
    for (UINT channel = 0; channel < 4; ++channel)
    {
        if (channel != 0)
            std::cout << ", ";
        std::cout << pixel[channel];
    }
    std::cout << ")";
}

void PrintReport(
    const Options& options,
    const ShaderContract& contract,
    const ComparisonStats& stats,
    UINT dedicatedScenarioCount,
    const std::map<std::string, CoverageStats>& coverage,
    InputProfile profile,
    bool complete)
{
    const bool passed = stats.divergentPixels == 0 && complete;
    std::cout << (passed ? "PASS" : (complete ? "DIVERGE" : "UNPROVEN")) << "\n"
              << "  input profile: " << InputProfileName(profile) << "\n"
              << "  fixture: " << FixtureName(options.fixture) << "\n"
              << "  random seeds: " << options.seeds
              << "  dedicated scenarios: " << dedicatedScenarioCount
              << "  size: " << options.width << "x" << options.height
              << "  render targets: " << contract.renderTargetCount << "\n"
              << "  compared pixel-channels: " << stats.totalChannels << "\n"
              << "  compared pixels: " << stats.totalPixels << "\n"
              << "  divergent pixels: " << stats.divergentPixels
              << "  divergent channels: " << stats.divergentChannels << "\n"
              << std::scientific << std::setprecision(7)
              << "  max abs diff: " << stats.maximumAbsoluteDifference
              << "  mean abs diff: "
              << (stats.totalChannels != 0
                    ? stats.absoluteDifferenceSum / stats.totalChannels : 0.0)
              << "  max rel diff: " << stats.maximumRelativeDifference
              << "  mean rel diff: "
              << (stats.totalChannels != 0
                    ? stats.relativeDifferenceSum / stats.totalChannels : 0.0)
              << "\n";

    if (!coverage.empty())
    {
        std::cout << "  coverage:\n";
        for (const auto& [bucket, bucketStats] : coverage)
        {
            std::cout << "    " << bucket
                      << ": population " << bucketStats.population
                      << "  max abs "
                      << bucketStats.comparison.maximumAbsoluteDifference
                      << "  divergent pixels "
                      << bucketStats.comparison.divergentPixels
                      << "\n";
        }
    }

    if (stats.worst.present)
    {
        std::cout << "  worst: seed " << stats.worst.seed
                  << " rt " << stats.worst.renderTarget
                  << " (" << stats.worst.x << "," << stats.worst.y << ")"
                  << " channel " << stats.worst.channel
                  << " ref " << stats.worst.reference
                  << " cand " << stats.worst.candidate << "\n";
    }

    if (options.verbose && !stats.topDivergences.empty())
    {
        std::cout << "  top divergent pixels:\n";
        for (const DivergentPixel& divergence : stats.topDivergences)
        {
            std::cout << "    seed " << divergence.seed
                      << " rt " << divergence.renderTarget
                      << " (" << divergence.x << "," << divergence.y << ") ref ";
            PrintPixel(divergence.reference);
            std::cout << " cand ";
            PrintPixel(divergence.candidate);
            std::cout << "\n";
        }
    }
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream escaped;
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (character < 0x20)
            {
                escaped << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0')
                        << static_cast<unsigned>(character)
                        << std::dec;
            }
            else
            {
                escaped << static_cast<char>(character);
            }
        }
    }
    return escaped.str();
}

void WriteMetricsJson(std::ostream& stream, const ComparisonStats& stats)
{
    const double meanAbsolute = stats.totalChannels != 0
        ? stats.absoluteDifferenceSum / stats.totalChannels : 0.0;
    const double meanRelative = stats.totalChannels != 0
        ? stats.relativeDifferenceSum / stats.totalChannels : 0.0;
    stream << "{\"total_pixels\":" << stats.totalPixels
           << ",\"total_channels\":" << stats.totalChannels
           << ",\"divergent_channels\":" << stats.divergentChannels
           << ",\"divergent_pixels\":" << stats.divergentPixels
           << ",\"max_absolute_error\":" << std::setprecision(17)
           << stats.maximumAbsoluteDifference
           << ",\"mean_absolute_error\":" << meanAbsolute
           << ",\"max_relative_error\":" << stats.maximumRelativeDifference
           << ",\"mean_relative_error\":" << meanRelative << "}";
}

void WriteMeasurementReport(
    const Options& options,
    InputProfile profile,
    const std::string& generatedInputHash,
    const ComparisonStats& aggregate,
    const std::map<std::string, CoverageStats>& coverage,
    std::vector<ResourceFormatRecord> formats,
    const std::vector<InputScenario>& scenarios,
    const std::vector<std::pair<std::string, std::string>>& failures)
{
    if (options.measurementJsonPath.empty())
        return;
    std::sort(formats.begin(), formats.end());
    formats.erase(std::unique(
        formats.begin(), formats.end(),
        [](const ResourceFormatRecord& left, const ResourceFormatRecord& right) {
            return !(left < right) && !(right < left);
        }), formats.end());
    std::ofstream stream(options.measurementJsonPath, std::ios::binary);
    if (!stream)
        ThrowFailure("could not open measurement JSON");
    const char* verdict = !failures.empty()
        ? "UNPROVEN"
        : (aggregate.divergentPixels == 0 ? "PASS" : "FAIL");
    stream << "{\"schema\":\"fo4cs.shader-exec-measurement\""
           << ",\"schema_version\":1"
           << ",\"harness_version\":3"
           << ",\"source_sha256\":\""
           << FO4CS_EXEC_HARNESS_SOURCE_SHA256 << "\""
           << ",\"profile\":\"" << InputProfileName(profile) << "\""
           << ",\"fixture\":\"" << FixtureName(options.fixture) << "\""
           << ",\"width\":" << options.width
           << ",\"height\":" << options.height
           << ",\"execution_environment\":{"
           << "\"driver_type\":\"WARP\","
           << "\"feature_level\":\"11_0\","
           << "\"runtime_fingerprint\":\"system-d3d11-runtime\","
           << "\"limitation\":\"runtime binary identity is external to this receipt\"}"
           << ",\"measurement_format\":\"R32G32B32A32_FLOAT\""
           << ",\"generated_inputs_sha256\":\"" << generatedInputHash << "\""
           << ",\"seed_base\":" << options.seedBase
           << ",\"seeds\":[";
    for (UINT index = 0; index < options.seeds; ++index)
    {
        if (index != 0)
            stream << ",";
        stream << options.seedBase + index;
    }
    stream << "],\"scenario_seeds\":[";
    for (std::size_t index = 0; index < scenarios.size(); ++index)
    {
        if (index != 0)
            stream << ",";
        stream << scenarios[index].randomSeed;
    }
    stream << "],\"formats\":[";
    for (std::size_t index = 0; index < formats.size(); ++index)
    {
        if (index != 0)
            stream << ",";
        const ResourceFormatRecord& format = formats[index];
        stream << "{\"bind_point\":" << format.bindPoint
               << ",\"dimension\":\"" << JsonEscape(format.dimension) << "\""
               << ",\"resource_format\":\""
               << JsonEscape(format.resourceFormat) << "\""
               << ",\"srv_format\":\"" << JsonEscape(format.srvFormat) << "\"}";
    }
    stream << "],\"matrix_assertions\":{"
           << "\"minimum_absolute_determinant\":0.1,"
           << "\"distinct_epsilon\":1e-05,\"verdict\":\"PASS\"}"
           << ",\"aggregate\":";
    WriteMetricsJson(stream, aggregate);
    stream << ",\"buckets\":[";
    std::size_t bucketIndex = 0;
    for (const auto& [name, bucket] : coverage)
    {
        if (bucketIndex++ != 0)
            stream << ",";
        stream << "{\"name\":\"" << JsonEscape(name) << "\""
               << ",\"population\":" << bucket.population
               << ",\"required_minimum\":"
               << options.minimumBucketPopulation << ",";
        std::ostringstream metrics;
        WriteMetricsJson(metrics, bucket.comparison);
        const std::string text = metrics.str();
        stream << text.substr(1);
    }
    stream << "],\"failures\":[";
    for (std::size_t index = 0; index < failures.size(); ++index)
    {
        if (index != 0)
            stream << ",";
        stream << "{\"code\":\"" << JsonEscape(failures[index].first)
               << "\",\"detail\":\"" << JsonEscape(failures[index].second)
               << "\"}";
    }
    stream << "],\"verdict\":\"" << verdict << "\"}\n";
    if (!stream)
        ThrowFailure("could not write measurement JSON");
}

struct TextureResource
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
};

struct ComputeOutput
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11UnorderedAccessView> view;
    ComPtr<ID3D11Texture2D> staging;
};

struct ScratchUav
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11UnorderedAccessView> view;
};

struct DepthPyramidOutput
{
    TextureResource resource;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 5> mipViews;
    ComPtr<ID3D11Texture2D> staging;
};

struct alignas(16) XeGTAOConstants
{
    std::array<float, 4> ndcToViewMul{};
    std::array<float, 4> ndcToViewAdd{};
    std::array<float, 2> textureDimensions{};
    std::array<float, 2> reciprocalTextureDimensions{};
    std::array<float, 2> frameDimensions{};
    std::array<float, 2> reciprocalFrameDimensions{};
    std::uint32_t frameIndex = 0;
    std::uint32_t numSlices = 0;
    std::uint32_t numSteps = 0;
    float minimumScreenRadius = 0.0f;
    float aoRadius = 0.0f;
    float effectRadius = 0.0f;
    float thickness = 0.0f;
    float aoPower = 0.0f;
    std::array<float, 2> depthFadeRange{};
    float depthFadeScale = 0.0f;
    float padding = 0.0f;
};
static_assert(sizeof(XeGTAOConstants) == 7 * 16);

struct alignas(16) UpstreamSSGIConstants
{
    std::array<float, 16> previousInverseView{};
    std::array<float, 4> ndcToViewMul{};
    std::array<float, 4> ndcToViewAdd{};
    std::array<float, 2> textureDimensions{};
    std::array<float, 2> reciprocalTextureDimensions{};
    std::array<float, 2> frameDimensions{};
    std::array<float, 2> reciprocalFrameDimensions{};
    std::uint32_t frameIndex = 0;
    std::uint32_t numSlices = 0;
    std::uint32_t numSteps = 0;
    float minimumScreenRadius = 0.0f;
    float aoRadius = 0.0f;
    float giRadius = 0.0f;
    float effectRadius = 0.0f;
    float thickness = 0.0f;
    std::array<float, 2> depthFadeRange{};
    float depthFadeScale = 0.0f;
    float giSaturation = 0.0f;
    float giDistanceCompensation = 0.0f;
    float giCompensationMaximumDistance = 0.0f;
    float padding1 = 0.0f;
    float aoPower = 0.0f;
    float giStrength = 0.0f;
    float depthDisocclusion = 0.0f;
    float normalDisocclusion = 0.0f;
    std::uint32_t maximumAccumulationFrames = 0;
    float blurRadius = 0.0f;
    float distanceNormalisation = 0.0f;
    std::array<float, 2> padding{};
};
static_assert(sizeof(UpstreamSSGIConstants) == 14 * 16);

template <class Constants>
ComPtr<ID3D11Buffer> CreateConstantBuffer(
    ID3D11Device* device,
    const Constants& constants)
{
    static_assert(sizeof(Constants) % 16 == 0);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(sizeof(Constants));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = &constants;

    ComPtr<ID3D11Buffer> buffer;
    CheckHRESULT(
        device->CreateBuffer(&desc, &initialData, &buffer),
        "CreateBuffer for XeGTAO constants");
    return buffer;
}

std::array<float, 3> Normalize(const std::array<float, 3>& value)
{
    const float length = std::sqrt(
        value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    if (length == 0.0f)
        ThrowFailure("cannot normalize zero-length synthetic normal");
    return {value[0] / length, value[1] / length, value[2] / length};
}

std::array<float, 2> EncodeUpstreamNormal(const std::array<float, 3>& input)
{
    std::array<float, 3> normal{-input[0], -input[1], -input[2]};
    const float reciprocalLength =
        1.0f / (std::abs(normal[0]) + std::abs(normal[1]) + std::abs(normal[2]));
    for (float& component : normal)
        component *= reciprocalLength;
    if (normal[2] < 0.0f)
    {
        const float oldX = normal[0];
        normal[0] = (1.0f - std::abs(normal[1])) * (oldX >= 0.0f ? 1.0f : -1.0f);
        normal[1] = (1.0f - std::abs(oldX)) *
            (normal[1] >= 0.0f ? 1.0f : -1.0f);
    }
    return {normal[0] * 0.5f + 0.5f, normal[1] * 0.5f + 0.5f};
}

std::array<float, 3> DecodeUpstreamNormal(const std::array<float, 2>& encoded)
{
    std::array<float, 3> normal{
        encoded[0] * 2.0f - 1.0f,
        encoded[1] * 2.0f - 1.0f,
        0.0f,
    };
    normal[2] = 1.0f - std::abs(normal[0]) - std::abs(normal[1]);
    const float folded = std::max(-normal[2], 0.0f);
    normal[0] += normal[0] >= 0.0f ? -folded : folded;
    normal[1] += normal[1] >= 0.0f ? -folded : folded;
    const std::array<float, 3> decoded = Normalize(normal);
    return {-decoded[0], -decoded[1], -decoded[2]};
}

void BuildXeGTAOScene(
    UINT width,
    UINT height,
    std::vector<float>& depths,
    std::vector<float>& ndcDepths,
    std::vector<Pixel>& rawNormals,
    std::vector<Pixel>& encodedNormals)
{
    depths.resize(static_cast<std::size_t>(width) * height);
    ndcDepths.resize(depths.size());
    rawNormals.resize(depths.size());
    encodedNormals.resize(depths.size());

    const std::array<float, 3> sphereCenter{5.0f, -3.0f, 52.0f};
    constexpr float sphereRadius = 13.0f;
    const std::array<float, 3> planeNormal =
        Normalize({0.12f, -0.08f, -1.0f});

    for (UINT y = 0; y < height; ++y)
    {
        for (UINT x = 0; x < width; ++x)
        {
            const float rayX =
                2.0f * (static_cast<float>(x) + 0.5f) / width - 1.0f;
            const float rayY =
                1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / height;
            const std::array<float, 3> ray{rayX, rayY, 1.0f};
            const float planeDepth = 78.0f / (1.0f - 0.12f * rayX +
                                                  0.08f * rayY);

            const float a = rayX * rayX + rayY * rayY + 1.0f;
            const float rayDotCenter =
                rayX * sphereCenter[0] + rayY * sphereCenter[1] + sphereCenter[2];
            const float c =
                sphereCenter[0] * sphereCenter[0] +
                sphereCenter[1] * sphereCenter[1] +
                sphereCenter[2] * sphereCenter[2] -
                sphereRadius * sphereRadius;
            const float discriminant =
                rayDotCenter * rayDotCenter - a * c;
            float depth = planeDepth;
            std::array<float, 3> normal = planeNormal;
            if (discriminant >= 0.0f)
            {
                const float sphereDepth =
                    (rayDotCenter - std::sqrt(discriminant)) / a;
                if (sphereDepth > 0.0f && sphereDepth < planeDepth)
                {
                    depth = sphereDepth;
                    normal = Normalize({
                        sphereDepth * rayX - sphereCenter[0],
                        sphereDepth * rayY - sphereCenter[1],
                        sphereDepth - sphereCenter[2],
                    });
                }
            }

            const std::array<float, 2> encoded = EncodeUpstreamNormal(normal);
            const std::array<float, 3> decoded = DecodeUpstreamNormal(encoded);
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const float ndcDepth = 1.0f - 1.0f / depth;
            depths[index] = 1.0f / (1.0f - ndcDepth);
            ndcDepths[index] = ndcDepth;
            rawNormals[index] = {decoded[0], decoded[1], decoded[2], 1.0f};
            encodedNormals[index] = {encoded[0], encoded[1], 0.0f, 1.0f};
        }
    }
}

TextureResource CreateScalarTexture(
    ID3D11Device* device,
    UINT width,
    UINT height,
    const std::vector<float>& values,
    const char* label)
{
    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = values.data();
    initialData.SysMemPitch = width * sizeof(float);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    TextureResource result;
    CheckHRESULT(
        device->CreateTexture2D(&desc, &initialData, &result.texture),
        std::string("CreateTexture2D for ") + label);
    CheckHRESULT(
        device->CreateShaderResourceView(result.texture.Get(), nullptr, &result.view),
        std::string("CreateShaderResourceView for ") + label);
    return result;
}

DepthPyramidOutput CreateDepthPyramidOutput(
    ID3D11Device* device,
    UINT width,
    UINT height)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 5;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_UNORDERED_ACCESS;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    DepthPyramidOutput output;
    CheckHRESULT(
        device->CreateTexture2D(&desc, nullptr, &output.resource.texture),
        "CreateTexture2D for XeGTAO filtered depth");
    CheckHRESULT(
        device->CreateShaderResourceView(
            output.resource.texture.Get(), nullptr, &output.resource.view),
        "CreateShaderResourceView for XeGTAO filtered depth");
    for (UINT mip = 0; mip < output.mipViews.size(); ++mip)
    {
        D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
        viewDesc.Format = desc.Format;
        viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipSlice = mip;
        CheckHRESULT(
            device->CreateUnorderedAccessView(
                output.resource.texture.Get(), &viewDesc, &output.mipViews[mip]),
            "CreateUnorderedAccessView for XeGTAO filtered depth");
    }
    CheckHRESULT(
        device->CreateTexture2D(&stagingDesc, nullptr, &output.staging),
        "CreateTexture2D for XeGTAO filtered-depth readback");
    return output;
}

std::vector<std::vector<float>> DispatchXeGTAOPrefilter(
    ID3D11DeviceContext* context,
    ID3D11ComputeShader* shader,
    bool upstream,
    ID3D11Buffer* constants,
    ID3D11Buffer* sharedDataConstants,
    const TextureResource& source,
    ID3D11SamplerState* sampler,
    DepthPyramidOutput& output,
    UINT width,
    UINT height)
{
    context->ClearState();
    ID3D11Buffer* constantBuffer = constants;
    context->CSSetConstantBuffers(upstream ? 1u : 0u, 1, &constantBuffer);
    if (upstream)
        context->CSSetConstantBuffers(5, 1, &sharedDataConstants);

    ID3D11ShaderResourceView* sourceView = source.view.Get();
    context->CSSetShaderResources(0, 1, &sourceView);
    context->CSSetSamplers(0, 1, &sampler);

    std::array<ID3D11UnorderedAccessView*, 5> views{};
    for (std::size_t index = 0; index < views.size(); ++index)
        views[index] = output.mipViews[index].Get();
    context->CSSetUnorderedAccessViews(
        0, static_cast<UINT>(views.size()), views.data(), nullptr);
    context->CSSetShader(shader, nullptr, 0);
    context->Dispatch(width / 16, height / 16, 1);

    views.fill(nullptr);
    context->CSSetUnorderedAccessViews(
        0, static_cast<UINT>(views.size()), views.data(), nullptr);
    context->CopyResource(output.staging.Get(), output.resource.texture.Get());

    std::vector<std::vector<float>> levels(output.mipViews.size());
    UINT mipWidth = width;
    UINT mipHeight = height;
    for (UINT mip = 0; mip < levels.size(); ++mip)
    {
        levels[mip].resize(static_cast<std::size_t>(mipWidth) * mipHeight);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        CheckHRESULT(
            context->Map(output.staging.Get(), mip, D3D11_MAP_READ, 0, &mapped),
            "Map XeGTAO filtered-depth readback");
        for (UINT y = 0; y < mipHeight; ++y)
        {
            const auto* sourceRow =
                static_cast<const std::uint8_t*>(mapped.pData) +
                static_cast<std::size_t>(y) * mapped.RowPitch;
            std::memcpy(
                levels[mip].data() + static_cast<std::size_t>(y) * mipWidth,
                sourceRow,
                static_cast<std::size_t>(mipWidth) * sizeof(float));
        }
        context->Unmap(output.staging.Get(), mip);
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
    }
    return levels;
}

TextureResource CreateFloat4Texture(
    ID3D11Device* device,
    UINT width,
    UINT height,
    const std::vector<Pixel>& pixels,
    const char* label)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = pixels.data();
    initialData.SysMemPitch = width * sizeof(Pixel);

    TextureResource result;
    CheckHRESULT(
        device->CreateTexture2D(&desc, &initialData, &result.texture),
        std::string("CreateTexture2D for ") + label);
    CheckHRESULT(
        device->CreateShaderResourceView(result.texture.Get(), nullptr, &result.view),
        std::string("CreateShaderResourceView for ") + label);
    return result;
}

std::uint32_t HashNoise(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

TextureResource CreateNoiseTexture(ID3D11Device* device)
{
    constexpr UINT noiseWidth = 128;
    constexpr UINT noiseHeight = 128;
    std::vector<Pixel> pixels(noiseWidth * noiseHeight);
    for (UINT y = 0; y < noiseHeight; ++y)
    {
        for (UINT x = 0; x < noiseWidth; ++x)
        {
            const std::uint32_t hash =
                HashNoise(x + y * noiseWidth + 0x9E3779B9u);
            const std::uint32_t second = HashNoise(hash);
            pixels[static_cast<std::size_t>(y) * noiseWidth + x] = {
                static_cast<float>(hash & 0xFFFFu) / 65535.0f,
                static_cast<float>(second & 0xFFFFu) / 65535.0f,
                0.0f,
                1.0f,
            };
        }
    }
    return CreateFloat4Texture(
        device, noiseWidth, noiseHeight, pixels, "XeGTAO noise");
}

ComputeOutput CreateComputeOutput(
    ID3D11Device* device,
    UINT width,
    UINT height)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComputeOutput output;
    CheckHRESULT(
        device->CreateTexture2D(&desc, nullptr, &output.texture),
        "CreateTexture2D for XeGTAO output");
    CheckHRESULT(
        device->CreateUnorderedAccessView(
            output.texture.Get(), nullptr, &output.view),
        "CreateUnorderedAccessView for XeGTAO output");
    CheckHRESULT(
        device->CreateTexture2D(&stagingDesc, nullptr, &output.staging),
        "CreateTexture2D for XeGTAO readback");
    return output;
}

ScratchUav CreateScratchUav(
    ID3D11Device* device,
    UINT width,
    UINT height)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

    ScratchUav scratch;
    CheckHRESULT(
        device->CreateTexture2D(&desc, nullptr, &scratch.texture),
        "CreateTexture2D for upstream scratch UAV");
    CheckHRESULT(
        device->CreateUnorderedAccessView(
            scratch.texture.Get(), nullptr, &scratch.view),
        "CreateUnorderedAccessView for upstream scratch UAV");
    return scratch;
}

std::vector<float> DispatchXeGTAO(
    ID3D11DeviceContext* context,
    ID3D11ComputeShader* shader,
    bool upstream,
    ID3D11Buffer* constants,
    ID3D11Buffer* upstreamFrameConstants,
    const TextureResource& depth,
    const TextureResource& normals,
    const TextureResource& noise,
    ID3D11SamplerState* sampler,
    const std::array<ScratchUav, 3>* upstreamScratch,
    ComputeOutput& output,
    UINT width,
    UINT height)
{
    context->ClearState();
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->ClearUnorderedAccessViewFloat(output.view.Get(), clear);

    ID3D11Buffer* constantBuffer = constants;
    context->CSSetConstantBuffers(upstream ? 1u : 0u, 1, &constantBuffer);
    if (upstream)
        context->CSSetConstantBuffers(12, 1, &upstreamFrameConstants);

    ID3D11ShaderResourceView* depthView = depth.view.Get();
    ID3D11ShaderResourceView* normalView = normals.view.Get();
    ID3D11ShaderResourceView* noiseView = noise.view.Get();
    context->CSSetShaderResources(0, 1, &depthView);
    context->CSSetShaderResources(upstream ? 8u : 1u, 1, &normalView);
    context->CSSetShaderResources(upstream ? 3u : 2u, 1, &noiseView);
    context->CSSetSamplers(0, 1, &sampler);

    std::array<ID3D11UnorderedAccessView*, 5> outputViews{};
    outputViews[0] = output.view.Get();
    UINT outputViewCount = 1;
    if (upstream)
    {
        if (upstreamScratch == nullptr)
            ThrowFailure("upstream XeGTAO scratch UAVs are missing");
        outputViews[1] = (*upstreamScratch)[0].view.Get();
        outputViews[2] = (*upstreamScratch)[1].view.Get();
        outputViews[4] = (*upstreamScratch)[2].view.Get();
        outputViewCount = static_cast<UINT>(outputViews.size());
    }
    context->CSSetUnorderedAccessViews(
        0, outputViewCount, outputViews.data(), nullptr);
    context->CSSetShader(shader, nullptr, 0);
    context->Dispatch(width / 8, height / 8, 1);

    outputViews.fill(nullptr);
    context->CSSetUnorderedAccessViews(
        0, outputViewCount, outputViews.data(), nullptr);
    context->CopyResource(output.staging.Get(), output.texture.Get());

    std::vector<float> pixels(static_cast<std::size_t>(width) * height);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    CheckHRESULT(
        context->Map(output.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
        "Map XeGTAO readback");
    for (UINT y = 0; y < height; ++y)
    {
        const auto* source =
            static_cast<const std::uint8_t*>(mapped.pData) +
            static_cast<std::size_t>(y) * mapped.RowPitch;
        std::memcpy(
            pixels.data() + static_cast<std::size_t>(y) * width,
            source,
            static_cast<std::size_t>(width) * sizeof(float));
    }
    context->Unmap(output.staging.Get(), 0);
    return pixels;
}

int RunXeGTAO(
    const Options& options,
    const std::vector<std::uint8_t>& referenceBytecode,
    const std::vector<std::uint8_t>& candidateBytecode,
    const std::vector<std::uint8_t>& referencePrefilterBytecode,
    const std::vector<std::uint8_t>& candidatePrefilterBytecode)
{
    if (options.width < 32 || options.height < 32 ||
        options.width % 16 != 0 || options.height % 16 != 0)
    {
        ThrowFailure("XeGTAO dimensions must be at least 32 and divisible by 16");
    }

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL createdFeatureLevel{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    CheckHRESULT(
        D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            &featureLevel, 1, D3D11_SDK_VERSION,
            &device, &createdFeatureLevel, &context),
        "D3D11CreateDevice(WARP)");
    if (createdFeatureLevel != D3D_FEATURE_LEVEL_11_0)
        ThrowFailure("WARP did not create a feature-level 11_0 device");

    ComPtr<ID3D11ComputeShader> referenceShader;
    ComPtr<ID3D11ComputeShader> candidateShader;
    ComPtr<ID3D11ComputeShader> referencePrefilterShader;
    ComPtr<ID3D11ComputeShader> candidatePrefilterShader;
    CheckHRESULT(
        device->CreateComputeShader(
            referenceBytecode.data(), referenceBytecode.size(), nullptr,
            &referenceShader),
        "CreateComputeShader(reference)");
    CheckHRESULT(
        device->CreateComputeShader(
            candidateBytecode.data(), candidateBytecode.size(), nullptr,
            &candidateShader),
        "CreateComputeShader(candidate)");
    CheckHRESULT(
        device->CreateComputeShader(
            referencePrefilterBytecode.data(), referencePrefilterBytecode.size(),
            nullptr, &referencePrefilterShader),
        "CreateComputeShader(reference prefilter)");
    CheckHRESULT(
        device->CreateComputeShader(
            candidatePrefilterBytecode.data(), candidatePrefilterBytecode.size(),
            nullptr, &candidatePrefilterShader),
        "CreateComputeShader(candidate prefilter)");

    std::vector<float> depths;
    std::vector<float> ndcDepths;
    std::vector<Pixel> rawNormals;
    std::vector<Pixel> encodedNormals;
    BuildXeGTAOScene(
        options.width, options.height, depths, ndcDepths,
        rawNormals, encodedNormals);
    const TextureResource candidateDepthInput = CreateScalarTexture(
        device.Get(), options.width, options.height, depths,
        "linear view-space depth");
    const TextureResource upstreamDepthInput = CreateScalarTexture(
        device.Get(), options.width, options.height, ndcDepths,
        "upstream NDC depth");
    const TextureResource candidateNormals = CreateFloat4Texture(
        device.Get(), options.width, options.height, rawNormals,
        "raw view-space normals");
    const TextureResource upstreamNormals = CreateFloat4Texture(
        device.Get(), options.width, options.height, encodedNormals,
        "upstream octahedral normals");
    const TextureResource noise = CreateNoiseTexture(device.Get());

    XeGTAOConstants candidateConstants{};
    candidateConstants.ndcToViewMul = {2.0f, -2.0f, 0.0f, 0.0f};
    candidateConstants.ndcToViewAdd = {-1.0f, 1.0f, 0.0f, 0.0f};
    candidateConstants.textureDimensions = {
        static_cast<float>(options.width), static_cast<float>(options.height)};
    candidateConstants.reciprocalTextureDimensions = {
        1.0f / options.width, 1.0f / options.height};
    candidateConstants.frameDimensions = candidateConstants.textureDimensions;
    candidateConstants.reciprocalFrameDimensions =
        candidateConstants.reciprocalTextureDimensions;
    candidateConstants.frameIndex = 0;
    candidateConstants.numSlices = 3;
    candidateConstants.numSteps = 8;
    candidateConstants.minimumScreenRadius = 3.0f;
    candidateConstants.aoRadius = 1.0f;
    candidateConstants.effectRadius = 35.0f;
    candidateConstants.thickness = 8.0f;
    candidateConstants.aoPower = 1.5f;
    // Overlap scene depths (~39-97) so depthFade varies and the needGI upper cull fires.
    candidateConstants.depthFadeRange = {60.0f, 90.0f};
    candidateConstants.depthFadeScale = 0.025f;

    UpstreamSSGIConstants upstreamConstants{};
    upstreamConstants.ndcToViewMul = candidateConstants.ndcToViewMul;
    upstreamConstants.ndcToViewAdd = candidateConstants.ndcToViewAdd;
    upstreamConstants.textureDimensions = candidateConstants.textureDimensions;
    upstreamConstants.reciprocalTextureDimensions =
        candidateConstants.reciprocalTextureDimensions;
    upstreamConstants.frameDimensions = candidateConstants.frameDimensions;
    upstreamConstants.reciprocalFrameDimensions =
        candidateConstants.reciprocalFrameDimensions;
    upstreamConstants.frameIndex = candidateConstants.frameIndex;
    upstreamConstants.numSlices = candidateConstants.numSlices;
    upstreamConstants.numSteps = candidateConstants.numSteps;
    upstreamConstants.minimumScreenRadius =
        candidateConstants.minimumScreenRadius;
    upstreamConstants.aoRadius = candidateConstants.aoRadius;
    upstreamConstants.effectRadius = candidateConstants.effectRadius;
    upstreamConstants.thickness = candidateConstants.thickness;
    upstreamConstants.depthFadeRange = candidateConstants.depthFadeRange;
    upstreamConstants.depthFadeScale = candidateConstants.depthFadeScale;
    upstreamConstants.aoPower = candidateConstants.aoPower;

    std::array<float, 45 * 4> upstreamFrameData{};
    for (UINT diagonal = 0; diagonal < 4; ++diagonal)
        upstreamFrameData[28 * 4 + diagonal * 4 + diagonal] = 1.0f;
    std::array<float, 64 * 4> upstreamSharedData{};
    upstreamSharedData[36 * 4 + 0] = 1.0f;
    upstreamSharedData[36 * 4 + 2] = 1.0f;
    upstreamSharedData[36 * 4 + 3] = 1.0f;

    const ComPtr<ID3D11Buffer> candidateConstantBuffer =
        CreateConstantBuffer(device.Get(), candidateConstants);
    const ComPtr<ID3D11Buffer> upstreamConstantBuffer =
        CreateConstantBuffer(device.Get(), upstreamConstants);
    const ComPtr<ID3D11Buffer> upstreamFrameBuffer =
        CreateConstantBuffer(device.Get(), upstreamFrameData);
    const ComPtr<ID3D11Buffer> upstreamSharedDataBuffer =
        CreateConstantBuffer(device.Get(), upstreamSharedData);

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    CheckHRESULT(
        device->CreateSamplerState(&samplerDesc, &sampler),
        "CreateSamplerState for XeGTAO");

    DepthPyramidOutput referenceDepth =
        CreateDepthPyramidOutput(device.Get(), options.width, options.height);
    DepthPyramidOutput candidateDepth =
        CreateDepthPyramidOutput(device.Get(), options.width, options.height);
    const std::vector<std::vector<float>> referenceDepthLevels =
        DispatchXeGTAOPrefilter(
            context.Get(), referencePrefilterShader.Get(), true,
            upstreamConstantBuffer.Get(), upstreamSharedDataBuffer.Get(),
            upstreamDepthInput, sampler.Get(), referenceDepth,
            options.width, options.height);
    const std::vector<std::vector<float>> candidateDepthLevels =
        DispatchXeGTAOPrefilter(
            context.Get(), candidatePrefilterShader.Get(), false,
            candidateConstantBuffer.Get(), nullptr,
            candidateDepthInput, sampler.Get(), candidateDepth,
            options.width, options.height);

    std::uint64_t prefilterDivergentValues = 0;
    double prefilterMaximumAbsoluteDifference = 0.0;
    double prefilterMaximumRelativeDifference = 0.0;
    std::uint64_t prefilterComparedValues = 0;
    for (std::size_t level = 0; level < referenceDepthLevels.size(); ++level)
    {
        if (referenceDepthLevels[level].size() !=
            candidateDepthLevels[level].size())
        {
            ThrowFailure("XeGTAO filtered-depth dimensions differ");
        }
        for (std::size_t index = 0;
             index < referenceDepthLevels[level].size(); ++index)
        {
            const ValueComparison difference = CompareValue(
                referenceDepthLevels[level][index],
                candidateDepthLevels[level][index],
                options.toleranceAbsolute, options.toleranceRelative);
            ++prefilterComparedValues;
            if (difference.divergent)
                ++prefilterDivergentValues;
            prefilterMaximumAbsoluteDifference = std::max(
                prefilterMaximumAbsoluteDifference,
                difference.absoluteDifference);
            prefilterMaximumRelativeDifference = std::max(
                prefilterMaximumRelativeDifference,
                difference.relativeDifference);
        }
    }

    ComputeOutput referenceOutput =
        CreateComputeOutput(device.Get(), options.width, options.height);
    ComputeOutput candidateOutput =
        CreateComputeOutput(device.Get(), options.width, options.height);
    const std::array<ScratchUav, 3> upstreamScratch{
        CreateScratchUav(device.Get(), options.width, options.height),
        CreateScratchUav(device.Get(), options.width, options.height),
        CreateScratchUav(device.Get(), options.width, options.height),
    };
    const std::vector<float> referencePixels = DispatchXeGTAO(
        context.Get(), referenceShader.Get(), true,
        upstreamConstantBuffer.Get(), upstreamFrameBuffer.Get(),
        referenceDepth.resource, upstreamNormals, noise, sampler.Get(),
        &upstreamScratch, referenceOutput,
        options.width, options.height);
    const std::vector<float> candidatePixels = DispatchXeGTAO(
        context.Get(), candidateShader.Get(), false,
        candidateConstantBuffer.Get(), nullptr,
        candidateDepth.resource, candidateNormals, noise, sampler.Get(),
        nullptr, candidateOutput,
        options.width, options.height);

    std::uint64_t divergentPixels = 0;
    double maximumAbsoluteDifference = 0.0;
    double maximumRelativeDifference = 0.0;
    std::size_t worstIndex = 0;
    float minimumOutput = std::numeric_limits<float>::infinity();
    float maximumOutput = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < referencePixels.size(); ++index)
    {
        const ValueComparison difference = CompareValue(
            referencePixels[index], candidatePixels[index],
            options.toleranceAbsolute, options.toleranceRelative);
        if (difference.divergent)
            ++divergentPixels;
        if (difference.absoluteDifference > maximumAbsoluteDifference)
        {
            maximumAbsoluteDifference = difference.absoluteDifference;
            worstIndex = index;
        }
        maximumRelativeDifference =
            std::max(maximumRelativeDifference, difference.relativeDifference);
        minimumOutput = std::min(minimumOutput, referencePixels[index]);
        maximumOutput = std::max(maximumOutput, referencePixels[index]);
    }
    if (!std::isfinite(minimumOutput) || !std::isfinite(maximumOutput) ||
        maximumOutput - minimumOutput < 1.0e-3f)
    {
        ThrowFailure("XeGTAO synthetic scene did not produce a finite, nontrivial AO image");
    }

    const bool passed =
        prefilterDivergentValues == 0 && divergentPixels == 0;
    std::cout << (passed ? "PASS" : "DIVERGE") << "\n"
              << "  input profile: xegtao-ao\n"
              << "  scene: tilted-plane+sphere\n"
              << "  size: " << options.width << "x" << options.height << "\n"
              << "  prefilter compared values: " << prefilterComparedValues << "\n"
              << "  prefilter divergent values: "
              << prefilterDivergentValues << "\n"
              << "  compared pixels: " << referencePixels.size() << "\n"
              << "  divergent pixels: " << divergentPixels << "\n"
              << std::scientific << std::setprecision(7)
              << "  prefilter max abs diff: "
              << prefilterMaximumAbsoluteDifference << "\n"
              << "  prefilter max rel diff: "
              << prefilterMaximumRelativeDifference << "\n"
              << "  max abs diff: " << maximumAbsoluteDifference << "\n"
              << "  max rel diff: " << maximumRelativeDifference << "\n"
              << "  output range: [" << minimumOutput << ", "
              << maximumOutput << "]\n";
    if (maximumAbsoluteDifference > 0.0)
    {
        std::cout << "  worst: (" << worstIndex % options.width << ","
                  << worstIndex / options.width << ") ref "
                  << referencePixels[worstIndex] << " cand "
                  << candidatePixels[worstIndex] << "\n";
    }
    return passed ? 0 : 1;
}

UINT ParseUnsigned(const std::string& option, const char* value)
{
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed);
    if (consumed != std::strlen(value) || parsed == 0 ||
        parsed > std::numeric_limits<UINT>::max())
        ThrowFailure("invalid value for " + option + ": " + value);
    return static_cast<UINT>(parsed);
}

UINT ParseNonnegativeUnsigned(const std::string& option, const char* value)
{
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed);
    if (consumed != std::strlen(value) ||
        parsed > std::numeric_limits<UINT>::max())
    {
        ThrowFailure("invalid value for " + option + ": " + value);
    }
    return static_cast<UINT>(parsed);
}

float ParseFloat(const std::string& option, const char* value)
{
    std::size_t consumed = 0;
    const float parsed = std::stof(value, &consumed);
    if (consumed != std::strlen(value) || !std::isfinite(parsed) || parsed < 0.0f)
        ThrowFailure("invalid value for " + option + ": " + value);
    return parsed;
}

std::string SelfTestConfigurationHash(
    UINT width,
    UINT height,
    const FrontFaceProbeResult& frontFaceProbe)
{
    Options options;
    options.width = width;
    options.height = height;
    options.seeds = 1;
    options.seedBase = 17;
    ShaderContract contract;
    contract.renderTargetCount = 1;
    contract.inputs.push_back({
        "SV_Position", 0, 0, 0xFu,
        D3D_REGISTER_COMPONENT_FLOAT32, D3D_NAME_POSITION});
    const std::vector<InputScenario> scenarios{
        {0, 17, false, "random-0", {}}};
    D3D11_RASTERIZER_DESC clockwise{};
    clockwise.FillMode = D3D11_FILL_SOLID;
    clockwise.CullMode = D3D11_CULL_NONE;
    clockwise.DepthClipEnable = TRUE;
    D3D11_RASTERIZER_DESC counterClockwise = clockwise;
    counterClockwise.FrontCounterClockwise = TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    Sha256 hash;
    AddExecutionConfigurationHash(
        hash, options, InputProfile::AmbientIbl, contract, scenarios,
        clockwise, counterClockwise, depth, frontFaceProbe);
    return hash.Finish();
}

int RunSelfTest()
{
    const std::array<std::pair<float, std::uint32_t>, 9> vectors{{
        {0.0f, 0x0000u},
        {1.0f, 0x3C00u},
        {0.5f, 0x3800u},
        {0.4999f, 0x3800u},
        {std::nextafter(1.0f, 0.0f), 0x3C00u},
        {std::nextafter(1.0f, 2.0f), 0x3C00u},
        {6.103515625e-5f, 0x0400u},
        {5.960464477539063e-8f, 0x0001u},
        {65504.0f, 0x7BFFu},
    }};
    for (const auto& [value, expected] : vectors)
    {
        if (FloatToHalf(value) != expected)
            ThrowFailure("self_test: binary16 conversion");
    }
    if (HalfToFloat(0x3800u) != 0.5f ||
        HalfToFloat(0x3C00u) != 1.0f ||
        HalfToFloat(0x0000u) != 0.0f)
    {
        ThrowFailure("self_test: binary16 decode");
    }
    D3D_FEATURE_LEVEL requestedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL createdFeatureLevel{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    CheckHRESULT(
        D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            &requestedFeatureLevel, 1, D3D11_SDK_VERSION,
            &device, &createdFeatureLevel, &context),
        "self_test: D3D11CreateDevice(WARP)");
    if (createdFeatureLevel != D3D_FEATURE_LEVEL_11_0)
        ThrowFailure("self_test: WARP feature level");

    D3D11_RASTERIZER_DESC clockwise{};
    clockwise.FillMode = D3D11_FILL_SOLID;
    clockwise.CullMode = D3D11_CULL_NONE;
    clockwise.DepthClipEnable = TRUE;
    D3D11_RASTERIZER_DESC counterClockwise = clockwise;
    counterClockwise.FrontCounterClockwise = TRUE;
    ComPtr<ID3D11RasterizerState> clockwiseState;
    ComPtr<ID3D11RasterizerState> counterClockwiseState;
    CheckHRESULT(
        device->CreateRasterizerState(&clockwise, &clockwiseState),
        "self_test: CreateRasterizerState");
    CheckHRESULT(
        device->CreateRasterizerState(
            &counterClockwise, &counterClockwiseState),
        "self_test: CreateRasterizerState(counter-clockwise)");
    const FrontFaceProbeResult frontFaceProbe = ProbeFrontFaceStates(
        device.Get(), context.Get(),
        clockwiseState.Get(), counterClockwiseState.Get());
    if (SelfTestConfigurationHash(16, 16, frontFaceProbe) ==
        SelfTestConfigurationHash(17, 16, frontFaceProbe))
    {
        ThrowFailure("self_test: dimension hash");
    }
    const std::vector<RuntimeComponent> runtimeComponents =
        RuntimeComponents();
    std::cout
        << "{\"schema\":\"fo4cs.shader-exec-self-test\","
        << "\"schema_version\":1,\"harness_version\":3,"
        << "\"source_sha256\":\"" << FO4CS_EXEC_HARNESS_SOURCE_SHA256 << "\","
        << "\"front_face_probe\":{\"version\":\""
        << FrontFaceProbeVersion << "\",\"clockwise_state_front\":"
        << (frontFaceProbe.clockwiseStateFront ? "true" : "false")
        << ",\"counter_clockwise_state_front\":"
        << (frontFaceProbe.counterClockwiseStateFront ? "true" : "false")
        << "},"
        << "\"runtime_components\":[";
    for (std::size_t index = 0; index < runtimeComponents.size(); ++index)
    {
        if (index != 0)
            std::cout << ",";
        const RuntimeComponent& component = runtimeComponents[index];
        std::cout << "{\"name\":\"" << JsonEscape(component.name)
                  << "\",\"state\":\"" << JsonEscape(component.state) << "\"";
        if (component.state == "available")
        {
            std::cout << ",\"version\":\"" << JsonEscape(component.version)
                      << "\",\"sha256\":\"" << component.sha256
                      << "\",\"size\":" << component.size;
        }
        std::cout << "}";
    }
    std::cout
        << "],\"tests\":{\"binary16\":\"PASS\","
        << "\"dimension_hash\":\"PASS\","
        << "\"front_face_probe\":\"PASS\"},"
        << "\"verdict\":\"PASS\"}\n";
    return 0;
}

Options ParseOptions(int argumentCount, char** arguments)
{
    if (argumentCount == 2 && std::string(arguments[1]) == "--self-test")
    {
        Options options;
        options.selfTest = true;
        return options;
    }
    if (argumentCount < 3)
    {
        ThrowFailure(
            "usage: shader_exec_diff.exe <reference.dxbc> <candidate.dxbc> "
            "[--seeds N] [--width W] [--height H] [--tol-abs A] "
            "[--tol-rel R] [--fixture adversarial|native] "
            "[--seed-base N] [--measurement-json PATH] [--verbose] [--xegtao-ao "
            "--reference-prefilter PATH --candidate-prefilter PATH]");
    }

    Options options;
    options.referencePath = arguments[1];
    options.candidatePath = arguments[2];
    for (int index = 3; index < argumentCount; ++index)
    {
        const std::string option = arguments[index];
        if (option == "--verbose")
        {
            options.verbose = true;
            continue;
        }
        if (option == "--xegtao-ao")
        {
            options.xegtaoAo = true;
            continue;
        }
        if (index + 1 >= argumentCount)
            ThrowFailure("missing value for " + option);

        const char* value = arguments[++index];
        if (option == "--seeds")
            options.seeds = ParseUnsigned(option, value);
        else if (option == "--seed-base")
            options.seedBase = ParseNonnegativeUnsigned(option, value);
        else if (option == "--width")
            options.width = ParseUnsigned(option, value);
        else if (option == "--height")
            options.height = ParseUnsigned(option, value);
        else if (option == "--tol-abs")
            options.toleranceAbsolute = ParseFloat(option, value);
        else if (option == "--tol-rel")
            options.toleranceRelative = ParseFloat(option, value);
        else if (option == "--fixture")
        {
            const std::string fixture = value;
            if (fixture == "adversarial")
                options.fixture = Fixture::Adversarial;
            else if (fixture == "native")
                options.fixture = Fixture::Native;
            else
                ThrowFailure("invalid value for --fixture: " + fixture);
        }
        else if (option == "--measurement-json")
            options.measurementJsonPath = value;
        else if (option == "--minimum-bucket-population")
            options.minimumBucketPopulation = ParseUnsigned(option, value);
        else if (option == "--required-bucket")
            options.requiredBuckets.push_back(value);
        else if (option == "--reference-prefilter")
            options.referencePrefilterPath = value;
        else if (option == "--candidate-prefilter")
            options.candidatePrefilterPath = value;
        else
            ThrowFailure("unknown option: " + option);
    }
    return options;
}

int Run(const Options& options)
{
    if (options.selfTest)
        return RunSelfTest();
    const std::vector<std::uint8_t> referenceBytecode =
        ReadFile(options.referencePath);
    const std::vector<std::uint8_t> candidateBytecode =
        ReadFile(options.candidatePath);

    if (options.xegtaoAo)
    {
        if (options.referencePrefilterPath.empty() ||
            options.candidatePrefilterPath.empty())
        {
            ThrowFailure("XeGTAO mode requires both prefilter shader paths");
        }
        const std::vector<std::uint8_t> referencePrefilterBytecode =
            ReadFile(options.referencePrefilterPath);
        const std::vector<std::uint8_t> candidatePrefilterBytecode =
            ReadFile(options.candidatePrefilterPath);
        return RunXeGTAO(
            options, referenceBytecode, candidateBytecode,
            referencePrefilterBytecode, candidatePrefilterBytecode);
    }

    ShaderContract referenceContract = ReflectShader(referenceBytecode);
    ShaderContract candidateContract = ReflectShader(candidateBytecode);
    const DisassemblyInfo referenceDisassembly =
        InspectDisassembly(referenceBytecode);
    const DisassemblyInfo candidateDisassembly =
        InspectDisassembly(candidateBytecode);
    ApplyDisassemblyContract(referenceContract, referenceDisassembly);
    ApplyDisassemblyContract(candidateContract, candidateDisassembly);
    WarnContractDifferences(
        referenceContract, candidateContract,
        referenceDisassembly, candidateDisassembly);
    const InputProfile profile =
        DetectInputProfile(referenceContract, referenceDisassembly);
    if (profile == InputProfile::Unshaped)
        ThrowFailure("unshaped: no measured input profile");
    const std::vector<InputScenario> scenarios =
        BuildInputScenarios(
            profile, options.fixture, options.seeds, options.seedBase);

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL createdFeatureLevel{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    CheckHRESULT(
        D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            &featureLevel, 1, D3D11_SDK_VERSION,
            &device, &createdFeatureLevel, &context),
        "D3D11CreateDevice(WARP)");
    if (createdFeatureLevel != D3D_FEATURE_LEVEL_11_0)
        ThrowFailure("WARP did not create a feature-level 11_0 device");

    ComPtr<ID3D11PixelShader> referenceShader;
    ComPtr<ID3D11PixelShader> candidateShader;
    CheckHRESULT(
        device->CreatePixelShader(
            referenceBytecode.data(), referenceBytecode.size(), nullptr,
            &referenceShader),
        "CreatePixelShader(reference)");
    CheckHRESULT(
        device->CreatePixelShader(
            candidateBytecode.data(), candidateBytecode.size(), nullptr,
            &candidateShader),
        "CreatePixelShader(candidate)");
    const ComPtr<ID3D11VertexShader> vertexShader =
        CreatePassthroughVertexShader(
            device.Get(), referenceContract, profile,
            options.width, options.height);

    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    const D3D11_RASTERIZER_DESC clockwiseRasterizerDesc = rasterizerDesc;
    ComPtr<ID3D11RasterizerState> clockwiseRasterizerState;
    CheckHRESULT(device->CreateRasterizerState(
                     &rasterizerDesc, &clockwiseRasterizerState),
                 "CreateRasterizerState");
    rasterizerDesc.FrontCounterClockwise = TRUE;
    const D3D11_RASTERIZER_DESC counterClockwiseRasterizerDesc = rasterizerDesc;
    ComPtr<ID3D11RasterizerState> counterClockwiseRasterizerState;
    CheckHRESULT(device->CreateRasterizerState(
                     &rasterizerDesc, &counterClockwiseRasterizerState),
                 "CreateRasterizerState(counter-clockwise)");
    const FrontFaceProbeResult frontFaceProbe = ProbeFrontFaceStates(
        device.Get(), context.Get(),
        clockwiseRasterizerState.Get(),
        counterClockwiseRasterizerState.Get());

    D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> depthStencilState;
    CheckHRESULT(device->CreateDepthStencilState(
                     &depthStencilDesc, &depthStencilState),
                 "CreateDepthStencilState");

    ComparisonStats stats;
    std::map<std::string, CoverageStats> coverage;
    std::vector<ResourceFormatRecord> formats;
    std::vector<std::pair<std::string, std::string>> failures;
    Sha256 inputHash;
    AddExecutionConfigurationHash(
        inputHash, options, profile, referenceContract, scenarios,
        clockwiseRasterizerDesc, counterClockwiseRasterizerDesc,
        depthStencilDesc, frontFaceProbe);
    for (const InputScenario& scenario : scenarios)
    {
        context->ClearState();
        const bool requestedFrontFace =
            ScenarioControl(scenario, "prepass_front_face", 1.0f) != 0.0f;
        const bool useClockwiseState =
            requestedFrontFace == frontFaceProbe.clockwiseStateFront;
        ID3D11RasterizerState* rasterizerState = useClockwiseState
            ? clockwiseRasterizerState.Get()
            : counterClockwiseRasterizerState.Get();
        const bool gpuFrontFace = useClockwiseState
            ? frontFaceProbe.clockwiseStateFront
            : frontFaceProbe.counterClockwiseStateFront;
        const SeedResources seedResources = CreateSeedResources(
            device.Get(), context.Get(), referenceContract,
            referenceDisassembly, profile,
            options.fixture, scenario, options.width, options.height,
            gpuFrontFace, inputHash);
        formats.insert(
            formats.end(),
            seedResources.formats.begin(), seedResources.formats.end());
        const std::map<std::string, BucketMask> bucketMasks =
            ClassifyBuckets(
                profile, scenario, seedResources,
                options.width, options.height, gpuFrontFace);
        const RenderOutputs referenceOutputs = RenderShader(
            device.Get(), context.Get(), vertexShader.Get(), referenceShader.Get(),
            rasterizerState, depthStencilState.Get(), seedResources,
            referenceContract.renderTargetCount, options.width, options.height);
        const RenderOutputs candidateOutputs = RenderShader(
            device.Get(), context.Get(), vertexShader.Get(), candidateShader.Get(),
            rasterizerState, depthStencilState.Get(), seedResources,
            referenceContract.renderTargetCount, options.width, options.height);
        ComparisonStats scenarioStats;
        CompareOutputs(
            scenarioStats, referenceOutputs, candidateOutputs, scenario.randomSeed,
            options.width,
            options.toleranceAbsolute, options.toleranceRelative);
        MergeComparisonStats(stats, scenarioStats);
        for (const auto& [bucketName, mask] : bucketMasks)
        {
            ComparisonStats bucketScenario;
            CompareOutputs(
                bucketScenario, referenceOutputs, candidateOutputs,
                scenario.randomSeed, options.width,
                options.toleranceAbsolute, options.toleranceRelative,
                &mask.pixels);
            CoverageStats& bucket = coverage[bucketName];
            bucket.population += mask.population;
            MergeComparisonStats(bucket.comparison, bucketScenario);
        }
    }

    for (const std::string& required : options.requiredBuckets)
    {
        const auto found = coverage.find(required);
        if (found == coverage.end())
            failures.emplace_back("missing_bucket", required);
        else if (found->second.population < options.minimumBucketPopulation)
            failures.emplace_back("bucket_population", required);
    }
    const std::string generatedInputHash = inputHash.Finish();
    WriteMeasurementReport(
        options, profile, generatedInputHash, stats,
        coverage, formats, scenarios, failures);
    PrintReport(
        options, referenceContract, stats,
        static_cast<UINT>(scenarios.size()) - options.seeds,
        coverage, profile, failures.empty());
    if (!failures.empty())
        return 2;
    return stats.divergentPixels == 0 ? 0 : 1;
}

std::pair<std::string, int> ClassifyFailure(const std::string& message)
{
    for (const std::string& code : {
             std::string("contract_mismatch"),
             std::string("front_face_probe"),
             std::string("matrix_assertion"),
             std::string("unsupported_input_resource"),
             std::string("unshaped")})
    {
        if (message.rfind(code, 0) == 0)
            return {code, 2};
    }
    return {"internal_failure", 3};
}

void WriteFailureReport(
    const Options& options,
    const std::string& code)
{
    if (options.measurementJsonPath.empty())
        return;
    std::ofstream stream(options.measurementJsonPath, std::ios::binary);
    if (!stream)
        return;
    stream << "{\"schema\":\"fo4cs.shader-exec-measurement\","
           << "\"schema_version\":1,\"harness_version\":3,"
           << "\"source_sha256\":\""
           << FO4CS_EXEC_HARNESS_SOURCE_SHA256 << "\","
           << "\"profile\":\"unshaped\",\"fixture\":\""
           << FixtureName(options.fixture) << "\","
           << "\"width\":" << options.width
           << ",\"height\":" << options.height << ","
           << "\"execution_environment\":{\"driver_type\":\"WARP\","
           << "\"feature_level\":\"11_0\","
           << "\"runtime_fingerprint\":\"system-d3d11-runtime\","
           << "\"limitation\":\"runtime binary identity is external to this receipt\"},"
           << "\"measurement_format\":\"R32G32B32A32_FLOAT\","
           << "\"generated_inputs_sha256\":\"\","
           << "\"seed_base\":" << options.seedBase
           << ",\"seeds\":[],\"scenario_seeds\":[],"
           << "\"formats\":[],\"matrix_assertions\":{\"verdict\":\"FAIL\"},"
           << "\"aggregate\":";
    WriteMetricsJson(stream, {});
    stream << ",\"buckets\":[],\"failures\":[{\"code\":\""
           << JsonEscape(code) << "\",\"detail\":\""
           << JsonEscape(code) << "\"}],\"verdict\":\"UNPROVEN\"}\n";
}
}

int main(int argumentCount, char** arguments)
{
    try
    {
        const Options options = ParseOptions(argumentCount, arguments);
        try
        {
            return Run(options);
        }
        catch (const std::exception& error)
        {
            const auto [code, exitCode] = ClassifyFailure(error.what());
            WriteFailureReport(options, code);
            std::cerr << "ERROR: " << error.what() << "\n";
            return exitCode;
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 3;
    }
}
