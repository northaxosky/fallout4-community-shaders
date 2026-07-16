#define NOMINMAX

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
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
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
struct Options
{
    std::string referencePath;
    std::string candidatePath;
    UINT seeds = 8;
    UINT width = 256;
    UINT height = 256;
    float toleranceAbsolute = 2.0e-3f;
    float toleranceRelative = 1.0e-2f;
    bool verbose = false;
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

struct BoundConstantBuffer
{
    UINT bindPoint = 0;
    ComPtr<ID3D11Buffer> buffer;
};

struct BoundResource
{
    UINT bindPoint = 0;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11ShaderResourceView> view;
};

struct BoundSampler
{
    UINT bindPoint = 0;
    ComPtr<ID3D11SamplerState> sampler;
};

struct SeedResources
{
    std::vector<BoundConstantBuffer> constantBuffers;
    std::vector<BoundResource> resources;
    std::vector<BoundSampler> samplers;
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
    std::uint64_t totalChannels = 0;
    std::uint64_t divergentChannels = 0;
    std::uint64_t divergentPixels = 0;
    double maximumAbsoluteDifference = 0.0;
    double maximumRelativeDifference = 0.0;
    WorstComponent worst;
    std::vector<DivergentPixel> topDivergences;
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
    {
        return;
    }
    std::cerr << "WARNING: candidate " << label << " contract differs from reference\n";
    for (const auto& [bindPoint, value] : reference)
    {
        const auto found = candidate.find(bindPoint);
        if (found == candidate.end())
            std::cerr << "  reference-only register " << bindPoint << "\n";
        else if (found->second != value)
            std::cerr << "  register " << bindPoint << " has a different declaration\n";
    }
    for (const auto& [bindPoint, value] : candidate)
    {
        (void)value;
        if (reference.count(bindPoint) == 0)
            std::cerr << "  candidate-only register " << bindPoint << "\n";
    }
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
    {
        std::cerr << "WARNING: candidate sampler-register contract differs from reference\n";
        for (const UINT bindPoint : referenceSamplers)
            if (candidateSamplers.count(bindPoint) == 0)
                std::cerr << "  reference-only register " << bindPoint << "\n";
        for (const UINT bindPoint : candidateSamplers)
            if (referenceSamplers.count(bindPoint) == 0)
                std::cerr << "  candidate-only register " << bindPoint << "\n";
    }
    if (referenceDisassembly.comparisonSamplers !=
        candidateDisassembly.comparisonSamplers)
    {
        std::cerr << "WARNING: candidate comparison-sampler declarations differ from reference\n";
    }

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
    {
        std::cerr << "WARNING: candidate input signature differs from reference\n";
    }

    std::vector<decltype(signatureKey(reference.outputs.front()))> referenceOutputs;
    std::vector<decltype(signatureKey(reference.outputs.front()))> candidateOutputs;
    for (const SignatureParameter& parameter : reference.outputs)
        referenceOutputs.push_back(signatureKey(parameter));
    for (const SignatureParameter& parameter : candidate.outputs)
        candidateOutputs.push_back(signatureKey(parameter));
    if (referenceOutputs != candidateOutputs)
    {
        std::cerr << "WARNING: candidate output signature differs from reference\n";
    }
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

ComPtr<ID3D11VertexShader> CreatePassthroughVertexShader(
    ID3D11Device* device,
    const ShaderContract& contract,
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
            source << "  output.value" << index << " = "
                   << ValueExpression(parameter.componentType, ComponentCount(parameter.mask),
                                      width, height)
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

void ShapeDirectionalLightingConstants(
    UINT bindPoint,
    std::vector<float>& values,
    UINT width,
    UINT height)
{
    // Stable transforms keep shadow-path differences visible in final outputs.
    if (bindPoint == 12)
    {
        for (const UINT firstRow : {20u, 24u})
        {
            SetVector(values, firstRow + 0, {1.0f, 0.0f, 0.0f, 0.0f});
            SetVector(values, firstRow + 1, {0.0f, 1.0f, 0.0f, 0.0f});
            SetVector(values, firstRow + 2, {0.0f, 0.0f, 1.0f, 0.0f});
            SetVector(values, firstRow + 3, {0.0f, 0.0f, 0.0f, 1.0f});
        }
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
    SetVector(values, 1, {0.0f, 0.0f, -1.0f, 0.0f});
    SetVector(values, 2, {
        std::abs(values[8]) + 0.5f,
        std::abs(values[9]) + 0.5f,
        std::abs(values[10]) + 0.5f,
        0.0f,
    });
    SetVector(values, 10, {0.35f, 0.65f, 0.0f, 0.0f});
    for (const UINT firstRow : {11u, 14u})
    {
        SetVector(values, firstRow + 0, {0.5f, 0.0f, 0.0f, 0.5f});
        SetVector(values, firstRow + 1, {0.0f, 0.5f, 0.0f, 0.5f});
        SetVector(values, firstRow + 2, {0.0f, 0.0f, 0.25f, 0.5f});
    }
    SetVector(values, 20, {0.0f, 0.0f, 1.0f / 64.0f, 0.0f});
    SetVector(values, 21, {0.0f, 0.0f, 0.0f, 64.0f});
    SetVector(values, 22, {0.0f, 0.0f, 0.0f, 64.0f});
    SetVector(values, 24, {16.0f, 0.0f, 0.0f, 0.0f});
}

void ShapeAmbientConstants(
    UINT bindPoint,
    std::vector<float>& values,
    UINT width,
    UINT height,
    std::mt19937& random)
{
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    if (bindPoint == 12)
    {
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

        const float scaleX = 0.7f + unit(random) * 0.3f;
        const float scaleY = 0.4f + unit(random) * 0.25f;
        for (const UINT firstRow : {20u, 24u})
        {
            SetVector(values, firstRow + 0, {scaleX, 0.0f, 0.0f, 0.0f});
            SetVector(values, firstRow + 1, {0.0f, scaleY, 0.0f, 0.0f});
            SetVector(values, firstRow + 2, {0.0f, 0.0f, 1.0f, 0.0f});
            SetVector(values, firstRow + 3, {0.0f, 0.0f, 0.0f, 1.0f});
        }
        SetVector(values, 28, {0.02f, 0.5f, 0.2f, 0.8f});
        SetVector(values, 29, {0.36f, -0.4f, 0.0f, 0.0f});
        SetVector(values, 30, {0.0f, unit(random), 0.0f, 0.0f});
        return;
    }

    if (bindPoint == 0)
    {
        SetVector(values, 0, {
            1.0f / static_cast<float>(width),
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
            1.0f / static_cast<float>(width),
            1.0f / static_cast<float>(height),
            1.0f,
            1.0f,
        });
        SetVector(values, 5, {1.0f, 1.0f, 0.0f, 0.0f});
    }
}

void FillUnitRandom(std::vector<float>& values, std::mt19937& random)
{
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    for (float& value : values)
        value = distribution(random);
}

void FillAmbientTexture(
    std::vector<float>& values,
    UINT bindPoint,
    UINT width,
    UINT height,
    UINT arraySize,
    std::mt19937& random)
{
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * height * arraySize;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto range = [&random](float minimum, float maximum) {
        return std::uniform_real_distribution<float>(minimum, maximum)(random);
    };

    if (bindPoint == 1)
    {
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const float z = range(-0.95f, 0.95f);
            const float azimuth = range(0.0f, 6.28318531f);
            const float radial = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float normalX = radial * std::cos(azimuth);
            const float normalY = radial * std::sin(azimuth);
            const float encodeScale = std::sqrt(2.0f / (1.0f - z));
            values[pixel * 4 + 0] = (normalX * encodeScale + 2.0f) * 0.25f;
            values[pixel * 4 + 1] = (normalY * encodeScale + 2.0f) * 0.25f;
            values[pixel * 4 + 2] = unit(random);
            values[pixel * 4 + 3] = unit(random);
        }
        return;
    }

    if (bindPoint == 2)
    {
        const UINT cubeSlice = std::uniform_int_distribution<UINT>(0, 3)(random);
        const float encodedSlice =
            (static_cast<float>(cubeSlice) + 1.25f) / 255.0f;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            values[pixel * 4 + 0] = unit(random);
            values[pixel * 4 + 1] = encodedSlice;
            values[pixel * 4 + 2] = range(0.05f, 0.8f);
            values[pixel * 4 + 3] = unit(random);
        }
        return;
    }

    if (bindPoint == 3)
    {
        const float materialId =
            (std::uniform_int_distribution<int>(0, 1)(random) == 0 ? 1.0f : 5.0f) /
            255.0f;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            values[pixel * 4 + 0] = range(0.35f, 0.95f);
            values[pixel * 4 + 1] = range(0.05f, 0.5f);
            values[pixel * 4 + 2] = unit(random);
            values[pixel * 4 + 3] = materialId;
        }
        return;
    }

    if (bindPoint == 7 || bindPoint == 15)
    {
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const float depth = range(0.5f, 1.0f);
            values[pixel * 4 + 0] = depth;
            values[pixel * 4 + 1] = depth;
            values[pixel * 4 + 2] = depth;
            values[pixel * 4 + 3] = 1.0f;
        }
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

BoundResource CreateTexture2DResource(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    const ResourceBinding& binding,
    UINT bindPoint,
    std::mt19937& random,
    bool ambientIbl)
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
        arraySize = ambientIbl ? 24u : 6u;

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = arraySize;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    textureDesc.SampleDesc.Count = multisampled ? 4u : 1u;
    textureDesc.Usage = multisampled ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
        (multisampled ? D3D11_BIND_RENDER_TARGET : 0u);
    if (binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBE ||
        binding.dimension == D3D_SRV_DIMENSION_TEXTURECUBEARRAY)
        textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    std::vector<float> values;
    std::vector<D3D11_SUBRESOURCE_DATA> initialData;
    if (!multisampled)
    {
        values.resize(static_cast<std::size_t>(width) * height * arraySize * 4);
        if (ambientIbl)
            FillAmbientTexture(
                values, bindPoint, width, height, arraySize, random);
        else
            FillUnitRandom(values, random);
        initialData.resize(arraySize);
        const std::size_t floatsPerSlice =
            static_cast<std::size_t>(width) * height * 4;
        for (UINT slice = 0; slice < arraySize; ++slice)
        {
            initialData[slice].pSysMem = values.data() + slice * floatsPerSlice;
            initialData[slice].SysMemPitch = width * sizeof(Pixel);
            initialData[slice].SysMemSlicePitch =
                width * height * sizeof(Pixel);
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
    viewDesc.Format = textureDesc.Format;
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
    bool ambientIbl)
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
            device, context, binding, bindPoint, random, ambientIbl);
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
    UINT seed,
    UINT width,
    UINT height)
{
    std::mt19937 random(seed);
    SeedResources result;
    const bool directionalLighting =
        IsDirectionalLightingContract(contract, disassembly);
    const bool ambientIbl = IsAmbientIblContract(contract, disassembly);

    for (const ConstantBufferBinding& binding : contract.constantBuffers)
    {
        if (binding.size == 0 || binding.size % 16 != 0)
            ThrowFailure("invalid reflected constant-buffer size");
        std::vector<float> values(binding.size / sizeof(float));
        if (ambientIbl)
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
        if (directionalLighting)
            ShapeDirectionalLightingConstants(
                binding.bindPoint, values, width, height);
        else if (ambientIbl)
            ShapeAmbientConstants(
                binding.bindPoint, values, width, height, random);

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = binding.size;
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = values.data();

        BoundConstantBuffer constantBuffer;
        constantBuffer.bindPoint = binding.bindPoint;
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
                ambientIbl));
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
                : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
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
            result.samplers.push_back(std::move(sampler));
        }
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
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
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

void CompareOutputs(
    ComparisonStats& stats,
    const RenderOutputs& reference,
    const RenderOutputs& candidate,
    UINT seed,
    UINT width,
    float toleranceAbsolute,
    float toleranceRelative)
{
    if (reference.size() != candidate.size())
        ThrowFailure("render-target count changed between shader executions");

    for (UINT target = 0; target < reference.size(); ++target)
    {
        if (reference[target].size() != candidate[target].size())
            ThrowFailure("render-target dimensions changed between shader executions");

        for (std::size_t pixelIndex = 0;
             pixelIndex < reference[target].size(); ++pixelIndex)
        {
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
                ++stats.divergentPixels;
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
    const ComparisonStats& stats)
{
    const bool passed = stats.divergentPixels == 0;
    std::cout << (passed ? "PASS" : "DIVERGE") << "\n"
              << "  seeds: " << options.seeds
              << "  size: " << options.width << "x" << options.height
              << "  render targets: " << contract.renderTargetCount << "\n"
              << "  compared pixel-channels: " << stats.totalChannels << "\n"
              << "  divergent pixels: " << stats.divergentPixels
              << "  divergent channels: " << stats.divergentChannels << "\n"
              << std::scientific << std::setprecision(7)
              << "  max abs diff: " << stats.maximumAbsoluteDifference
              << "  max rel diff: " << stats.maximumRelativeDifference << "\n";

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

UINT ParseUnsigned(const std::string& option, const char* value)
{
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed);
    if (consumed != std::strlen(value) || parsed == 0 ||
        parsed > std::numeric_limits<UINT>::max())
        ThrowFailure("invalid value for " + option + ": " + value);
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

Options ParseOptions(int argumentCount, char** arguments)
{
    if (argumentCount < 3)
    {
        ThrowFailure(
            "usage: shader_exec_diff.exe <reference.dxbc> <candidate.dxbc> "
            "[--seeds N] [--width W] [--height H] [--tol-abs A] "
            "[--tol-rel R] [--verbose]");
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
        if (index + 1 >= argumentCount)
            ThrowFailure("missing value for " + option);

        const char* value = arguments[++index];
        if (option == "--seeds")
            options.seeds = ParseUnsigned(option, value);
        else if (option == "--width")
            options.width = ParseUnsigned(option, value);
        else if (option == "--height")
            options.height = ParseUnsigned(option, value);
        else if (option == "--tol-abs")
            options.toleranceAbsolute = ParseFloat(option, value);
        else if (option == "--tol-rel")
            options.toleranceRelative = ParseFloat(option, value);
        else
            ThrowFailure("unknown option: " + option);
    }
    return options;
}

int Run(const Options& options)
{
    const std::vector<std::uint8_t> referenceBytecode =
        ReadFile(options.referencePath);
    const std::vector<std::uint8_t> candidateBytecode =
        ReadFile(options.candidatePath);

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
            device.Get(), referenceContract, options.width, options.height);

    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rasterizerState;
    CheckHRESULT(device->CreateRasterizerState(
                     &rasterizerDesc, &rasterizerState),
                 "CreateRasterizerState");

    D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = FALSE;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> depthStencilState;
    CheckHRESULT(device->CreateDepthStencilState(
                     &depthStencilDesc, &depthStencilState),
                 "CreateDepthStencilState");

    ComparisonStats stats;
    for (UINT seed = 0; seed < options.seeds; ++seed)
    {
        context->ClearState();
        const SeedResources seedResources = CreateSeedResources(
            device.Get(), context.Get(), referenceContract,
            referenceDisassembly, seed, options.width, options.height);
        const RenderOutputs referenceOutputs = RenderShader(
            device.Get(), context.Get(), vertexShader.Get(), referenceShader.Get(),
            rasterizerState.Get(), depthStencilState.Get(), seedResources,
            referenceContract.renderTargetCount, options.width, options.height);
        const RenderOutputs candidateOutputs = RenderShader(
            device.Get(), context.Get(), vertexShader.Get(), candidateShader.Get(),
            rasterizerState.Get(), depthStencilState.Get(), seedResources,
            referenceContract.renderTargetCount, options.width, options.height);
        CompareOutputs(
            stats, referenceOutputs, candidateOutputs, seed,
            options.width,
            options.toleranceAbsolute, options.toleranceRelative);
    }

    PrintReport(options, referenceContract, stats);
    return stats.divergentPixels == 0 ? 0 : 1;
}
}

int main(int argumentCount, char** arguments)
{
    try
    {
        return Run(ParseOptions(argumentCount, arguments));
    }
    catch (const std::exception& error)
    {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 3;
    }
}
