#include "NtscShaderPipeline.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace R2SEGA8::Window
{
    namespace
    {
        // Gerencia uma referência COM e libera o recurso ao sair do escopo.
        // put() descarta a referência anterior antes de receber outra da API.
        template <class T>
        class Com
        {
        public:
            ~Com() { reset(); }
            Com() = default;
            Com(const Com &) = delete;
            Com &operator=(const Com &) = delete;
            T *get() const { return ptr; }
            T **put()
            {
                reset();
                return &ptr;
            }
            T *operator->() const { return ptr; }
            void reset()
            {
                if (ptr)
                    ptr->Release();
                ptr = nullptr;
            }

        private:
            T *ptr = nullptr;
        };

        void require(HRESULT result, const char *operation)
        {
            if (FAILED(result))
            {
                std::ostringstream s;
                s << operation << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
                throw std::runtime_error(s.str());
            }
        }

        std::string expand(const std::filesystem::path &path, unsigned depth = 0)
        {
            // Expande includes relativos antes da compilação. O limite também
            // evita recursão indefinida se houver dependências cíclicas.
            if (depth > 32)
                throw std::runtime_error("Shader include nesting exceeds limit");
            std::ifstream file(path);
            if (!file)
                throw std::runtime_error("Cannot open shader: " + path.string());
            std::ostringstream output;
            std::string line;
            std::smatch match;
            const std::regex includePattern(R"re(^\s*#\s*include\s*"([^"]+)")re");
            while (std::getline(file, line))
            {
                if (std::regex_search(line, match, includePattern))
                    output << expand(path.parent_path() / match[1].str(), depth + 1);
                else
                    output << line << '\n';
            }
            return output.str();
        }

        void replaceAll(std::string &s, const std::string &from, const std::string &to)
        {
            for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
                s.replace(pos, from.size(), to);
        }

        struct Vertex
        {
            float position[4], uv[2], unused[2], color[4];
        };
        struct Constants
        {
            // A disposição e o preenchimento acompanham os buffers HLSL.
            float video[2], texture[2], output[2], frame, direction, rotation, padding[3];
        };
        // Cada saída intermediária pode ser escrita como alvo e lida como textura.
        struct Target
        {
            Com<ID3D11Texture2D> texture;
            Com<ID3D11RenderTargetView> target;
            Com<ID3D11ShaderResourceView> resource;
        };
        struct Program
        {
            Com<ID3D11VertexShader> vertex;
            Com<ID3D11PixelShader> pixel;
            Com<ID3D11InputLayout> layout;
            unsigned previousDistance = 0; // Distância indicada por PASSPREVn.
            bool explicitPointSampling = false;
        };
    }

    struct NtscShaderPipeline::Impl
    {
        // O dispositivo é independente do renderer SDL da janela principal.
        Com<ID3D11Device> device;
        Com<ID3D11DeviceContext> context;
        Com<ID3D11Buffer> vertices, constants, previousConstants;
        Com<ID3D11SamplerState> nearest, linear;
        std::vector<ShaderPass> passes;
        std::map<std::string, std::string> parameterDefaults;
        std::vector<std::unique_ptr<Program>> programs;
        std::unique_ptr<Target> input;
        std::vector<std::unique_ptr<Target>> targets;
        Com<ID3D11Texture2D> staging;
        Com<ID3D11Texture2D> lookupTexture;
        Com<ID3D11ShaderResourceView> lookupResource;
        ShaderPassSize cachedSource{0, 0}, cachedViewport{0, 0};
        uint64_t frame = 0;

        void loadLookup(const std::filesystem::path &path)
        {
            // O WIC decodifica a LUT para RGBA sem pré-multiplicar o alfa;
            // a textura resultante é imutável e reutilizada entre quadros.
            struct ComSession
            {
                HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                ~ComSession()
                {
                    if (SUCCEEDED(result))
                        CoUninitialize();
                }
            } session;
            if (FAILED(session.result) && session.result != RPC_E_CHANGED_MODE)
                require(session.result, "Initialize COM");
            Com<IWICImagingFactory> factory;
            require(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_IWICImagingFactory, reinterpret_cast<void **>(factory.put())),
                    "Create image decoder");
            Com<IWICBitmapDecoder> decoder;
            require(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                       decoder.put()),
                    "Open shader LUT");
            Com<IWICBitmapFrameDecode> frame;
            require(decoder->GetFrame(0, frame.put()), "Read shader LUT frame");
            Com<IWICFormatConverter> converter;
            require(factory->CreateFormatConverter(converter.put()), "Create LUT converter");
            require(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                          nullptr, 0, WICBitmapPaletteTypeCustom),
                    "Convert LUT to RGBA");
            UINT width = 0, height = 0;
            require(converter->GetSize(&width, &height), "Read LUT dimensions");
            if (!width || !height || width > 4096 || height > 4096)
                throw std::runtime_error("Invalid LUT dimensions");
            std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
            require(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data()), "Read LUT pixels");
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = rgba.data();
            data.SysMemPitch = width * 4;
            require(device->CreateTexture2D(&desc, &data, lookupTexture.put()), "Create LUT texture");
            require(device->CreateShaderResourceView(lookupTexture.get(), nullptr, lookupResource.put()), "Create LUT resource");
        }

        void compile(const ShaderPass &pass)
        {
            // Os presets integrados usam interfaces Cg diferentes. As
            // adaptações abaixo ocorrem apenas na cópia carregada em memória;
            // elas não alteram os arquivos originais dos shaders.
            std::string source = expand(pass.source);
            if (pass.source.filename() == "crt-lottes.cg")
            {
                // A implementação usa os defaults dos #pragma e as opções do
                // preset, mesmo sem habilitar uniforms PARAMETER_UNIFORM.
                for (const auto &parameter : parameterDefaults)
                {
                    const std::regex definition("(^|\\n)[ \\t]*#define[ \\t]+" + parameter.first + "[ \\t]+[^\\r\\n]*");
                    source = std::regex_replace(source, definition, "$1#define " + parameter.first + " " + parameter.second);
                }
            }
            unsigned previousDistance = 0;
            std::smatch previousMatch;
            if (std::regex_search(source, previousMatch, std::regex(R"(PASSPREV([0-9]+))")))
                previousDistance = static_cast<unsigned>(std::stoul(previousMatch[1].str()));
            const bool legacy = source.find("COMPAT_IN_VERTEX") == std::string::npos;
            if (legacy)
            {
                // Adapta as entradas Cg antigas para buffers e texturas HLSL.
                // PASSPREVn e ORIG serão associados às texturas no render().
                source = std::regex_replace(source, std::regex(R"(struct\s+prev\s*\{[^}]*\}\s*;)"), "");
                source = std::regex_replace(source, std::regex(R"(struct\s+orig\s*\{[^}]*\}\s*;)"), "");
                source = std::regex_replace(source, std::regex(R"(struct\s+input\s*\{([^}]*)\}\s*;)"),
                                            "struct input {$1};\ncbuffer LegacyInput : register(b0) { input IN; };\n");
                source = std::regex_replace(source, std::regex(R"(uniform\s+float4x4\s+modelViewProj\s*,)"), "");
                source = std::regex_replace(source, std::regex(R"(,\s*prev\s+PASSPREV[0-9]+)"), "");
                source = std::regex_replace(source, std::regex(R"(prev\s+PASSPREV[0-9]+\s*,)"), "");
                source = std::regex_replace(source, std::regex(R"(orig\s+ORIG\s*,)"), "");
                source = std::regex_replace(source, std::regex(R"(,\s*uniform\s+input\s+IN)"), "");
                source = std::regex_replace(source, std::regex(R"(,\s*uniform\s+sampler2D\s+\w+\s*:\s*TEXUNIT0)"), "");
                source = std::regex_replace(source, std::regex(R"(float4\s+position\s*:\s*POSITION\s*;)"), "float4 position : SV_POSITION;",
                                            std::regex_constants::format_first_only);
                source = std::regex_replace(source, std::regex(R"(\)\s*:\s*COLOR\s*\{)"), ") : SV_Target {");
                source = std::regex_replace(source, std::regex(R"(PASSPREV[0-9]+\.texture_size)"), "previousSize");
                source = std::regex_replace(source, std::regex(R"(PASSPREV[0-9]+\.tex_coord)"), "texCoord");
                source = std::regex_replace(source, std::regex(R"(PASSPREV[0-9]+\.texture)"), "previousTexture");
                source = std::regex_replace(source, std::regex(R"(#ifdef\s+HLSL_4\s+float4\s+position\s*=\s*VIN\.position;\s*float2\s+texCoord\s*=\s*VIN\.texCoord;\s*#else\s+OUT\.Color\s*=\s*color;\s*#endif)"), "");
                replaceAll(source, "ORIG.texture", "previousTexture");
                for (const auto &parameter : parameterDefaults)
                    replaceAll(source, "const static float " + parameter.first + " = 0.0;",
                               "const static float " + parameter.first + " = " + parameter.second + ";");
                replaceAll(source, "float4(wa*wb)", "float4(wa*wb, wa*wb, wa*wb, wa*wb)");
                replaceAll(source, "float4(1)", "float4(1, 1, 1, 1)");
                source = "Texture2D s0 : register(t0);\nTexture2D s_p : register(t0);\nTexture2D decal : register(t0);\n"
                         "Texture2D previousTexture : register(t1);\nSamplerState sourceSampler : register(s0);\n"
                         "cbuffer PreviousInfo : register(b1) { float2 previousSize; };\n"
                         "#define tex2D(t, uv) t.Sample(sourceSampler, uv)\n" +
                         source;
            }
            else
            {
                const auto vertexInput = source.find("float4 position : COMPAT_POS;");
                if (vertexInput == std::string::npos)
                    throw std::runtime_error("Unsupported shader vertex input");
                source.replace(vertexInput, std::strlen("float4 position : COMPAT_POS;"), "float4 position : POSITION;");
            }
            // O quad já está em coordenadas de recorte, então a matriz é a
            // identidade. A matriz NTSC precisa ser static para que o HLSL
            // use o inicializador em vez de esperar um uniform externo.
            replaceAll(source, "mul(modelViewProj, position)", "position");
            replaceAll(source, "const float3x3 mix_mat", "static const float3x3 mix_mat");
            D3D_SHADER_MACRO macros[] = {{"HLSL_4", "1"}, {nullptr, nullptr}};
            auto program = std::make_unique<Program>();
            program->previousDistance = previousDistance;
            // Este shader usa COMPAT_SamplePoint em todas as leituras, mesmo
            // quando o preset configura filtragem linear para a textura.
            program->explicitPointSampling = pass.source.filename() == "crt-hyllian-3d.cg";
            Com<ID3DBlob> vs, ps, diagnostics;
            auto runCompiler = [&](const char *entry, const char *profile, Com<ID3DBlob> &blob)
            {
                HRESULT result = D3DCompile(source.data(), source.size(), pass.source.string().c_str(), macros,
                                            nullptr, entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                                            blob.put(), diagnostics.put());
                if (FAILED(result))
                    throw std::runtime_error("Shader compilation failed for " + pass.source.string() + ": " +
                                             (diagnostics.get() ? static_cast<const char *>(diagnostics->GetBufferPointer()) : "unknown error"));
            };
            runCompiler("main_vertex", "vs_4_0", vs);
            runCompiler("main_fragment", "ps_4_0", ps);
            require(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, program->vertex.put()), "CreateVertexShader");
            require(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, program->pixel.put()), "CreatePixelShader");
            D3D11_INPUT_ELEMENT_DESC elements[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {legacy ? "COLOR" : "TEXCOORD", legacy ? 0u : 1u,
                 legacy ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32_FLOAT, 0, legacy ? 32u : 24u, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            require(device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), program->layout.put()), "CreateInputLayout");
            programs.push_back(std::move(program));
        }

        void initialize(const std::filesystem::path &preset)
        {
            std::string error;
            std::filesystem::path lookup;
            std::map<std::string, std::string> overrides;
            if (!loadShaderPreset(preset, passes, error, &lookup, &overrides))
                throw std::runtime_error(error);
            if (passes.size() != 1 && passes.size() != 2 && passes.size() != 4 && passes.size() != 3 && passes.size() != 6 && passes.size() != 7 && passes.size() != 8)
                throw std::runtime_error("Unsupported number of shader passes");
            // Recolhe os valores padrão declarados pelos passes. Isso não
            // implementa uma interface geral de parâmetros configuráveis.
            const std::regex parameterPattern(R"re(#pragma\s+parameter\s+(\w+)\s+"[^"]*"\s+([-+0-9.eE]+))re");
            for (const auto &pass : passes)
            {
                const std::string text = expand(pass.source);
                for (std::sregex_iterator it(text.begin(), text.end(), parameterPattern), end; it != end; ++it)
                    parameterDefaults.emplace((*it)[1].str(), (*it)[2].str());
            }
            for (const auto &parameter : overrides)
            {
                if (!parameterDefaults.count(parameter.first) || passes.size() != 1 ||
                    passes[0].source.filename() != "crt-lottes.cg")
                    throw std::runtime_error("Unsupported shader parameter: " + parameter.first);
                parameterDefaults[parameter.first] = parameter.second;
            }
            D3D_FEATURE_LEVEL level;
            HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                           D3D11_SDK_VERSION, device.put(), &level, context.put());
            // Se não houver dispositivo de hardware, tenta o WARP por software.
            if (FAILED(hr))
                hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                       D3D11_SDK_VERSION, device.put(), &level, context.put());
            require(hr, "D3D11CreateDevice");
            if (!lookup.empty())
                loadLookup(lookup);
            for (const auto &pass : passes)
                compile(pass);
            const Vertex quad[] = {{{-1, 1, 0, 1}, {0, 0}, {}, {1, 1, 1, 1}}, {{1, 1, 0, 1}, {1, 0}, {}, {1, 1, 1, 1}}, {{-1, -1, 0, 1}, {0, 1}, {}, {1, 1, 1, 1}}, {{1, -1, 0, 1}, {1, 1}, {}, {1, 1, 1, 1}}};
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(quad);
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = quad;
            require(device->CreateBuffer(&desc, &data, vertices.put()), "Create vertex buffer");
            desc = {};
            desc.ByteWidth = sizeof(Constants);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            require(device->CreateBuffer(&desc, nullptr, constants.put()), "Create constant buffer");
            desc.ByteWidth = 16;
            require(device->CreateBuffer(&desc, nullptr, previousConstants.put()), "Create previous pass constant buffer");
            D3D11_SAMPLER_DESC sampler{};
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            require(device->CreateSamplerState(&sampler, nearest.put()), "Create nearest sampler");
            sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            require(device->CreateSamplerState(&sampler, linear.put()), "Create linear sampler");
        }

        std::unique_ptr<Target> createTarget(ShaderPassSize size, bool floating, bool srgb = false)
        {
            auto result = std::make_unique<Target>();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = size.width;
            desc.Height = size.height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = floating ? DXGI_FORMAT_R32G32B32A32_FLOAT : srgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                                                                           : DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            require(device->CreateTexture2D(&desc, nullptr, result->texture.put()), "Create pass texture");
            require(device->CreateRenderTargetView(result->texture.get(), nullptr, result->target.put()), "Create render target");
            require(device->CreateShaderResourceView(result->texture.get(), nullptr, result->resource.put()), "Create shader resource");
            return result;
        }

        void render(const uint32_t *pixels, int pitch, ShaderPassSize source, ShaderPassSize viewport, std::vector<uint32_t> &output)
        {
            std::string error;
            std::vector<ShaderPassSize> sizes;
            if (!pixels || pitch < source.width || !planShaderPasses(passes, source, viewport, sizes, error))
                throw std::runtime_error(error.empty() ? "Invalid input frame" : error);
            if (sizes.back().width != viewport.width || sizes.back().height != viewport.height || passes.back().floatingPoint)
                throw std::runtime_error("NTSC final pass must produce an 8-bit viewport-sized image");
            if (!input || source.width != cachedSource.width || source.height != cachedSource.height ||
                viewport.width != cachedViewport.width || viewport.height != cachedViewport.height)
            {
                // Recria as texturas apenas quando a origem ou a saída muda.
                // Prepara todos os novos recursos antes de substituir os atuais.
                auto newInput = createTarget(source, false);
                std::vector<std::unique_ptr<Target>> newTargets;
                for (size_t i = 0; i < passes.size(); ++i)
                    newTargets.push_back(createTarget(sizes[i], passes[i].floatingPoint, passes[i].srgb));
                D3D11_TEXTURE2D_DESC desc{};
                newTargets.back()->texture->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                Com<ID3D11Texture2D> newStaging;
                require(device->CreateTexture2D(&desc, nullptr, newStaging.put()), "Create readback texture");
                // Mantém uma referência própria após a destruição de newStaging.
                ID3D11Texture2D *stagingTexture = newStaging.get();
                stagingTexture->AddRef();
                staging.reset();
                *staging.put() = stagingTexture;
                input = std::move(newInput);
                targets = std::move(newTargets);
                cachedSource = source;
                cachedViewport = viewport;
            }
            output.resize(static_cast<size_t>(viewport.width) * viewport.height);
            context->UpdateSubresource(input->texture.get(), 0, nullptr, pixels, pitch * sizeof(uint32_t), 0);
            UINT stride = sizeof(Vertex), offset = 0;
            ID3D11Buffer *vertex = vertices.get(), *constant = constants.get();
            context->IASetVertexBuffers(0, 1, &vertex, &stride, &offset);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->VSSetConstantBuffers(0, 1, &constant);
            context->PSSetConstantBuffers(0, 1, &constant);
            ID3D11Buffer *previousConstant = previousConstants.get();
            context->VSSetConstantBuffers(1, 1, &previousConstant);
            context->PSSetConstantBuffers(1, 1, &previousConstant);
            ShaderPassSize previous = source;
            for (size_t i = 0; i < passes.size(); ++i)
            {
                // Atualiza as dimensões e o contador esperados por este passe.
                Constants c = {{float(previous.width), float(previous.height)}, {float(previous.width), float(previous.height)}, {float(sizes[i].width), float(sizes[i].height)}, float(passes[i].frameCountMod ? frame % passes[i].frameCountMod : frame), 1.0f, 0.0f, {0.0f, 0.0f, 0.0f}};
                context->UpdateSubresource(constants.get(), 0, nullptr, &c, 0, 0);
                D3D11_VIEWPORT vp{0, 0, float(sizes[i].width), float(sizes[i].height), 0, 1};
                context->RSSetViewports(1, &vp);
                ID3D11RenderTargetView *rt = targets[i]->target.get();
                context->OMSetRenderTargets(1, &rt, nullptr);
                context->IASetInputLayout(programs[i]->layout.get());
                context->VSSetShader(programs[i]->vertex.get(), nullptr, 0);
                context->PSSetShader(programs[i]->pixel.get(), nullptr, 0);
                const unsigned distance = programs[i]->previousDistance;
                if (distance > i + 1)
                    throw std::runtime_error("Previous shader pass is unavailable");
                // PASSPREVn pode apontar para uma saída anterior ou para o
                // framebuffer original. Quando não há PASSPREV, a imagem
                // original também atende aos shaders que usam ORIG.
                const bool useOriginal = !distance || distance == i + 1;
                const auto previousSize = useOriginal ? source : sizes[i - distance];
                const float previousData[] = {float(previousSize.width), float(previousSize.height), 0, 0};
                context->UpdateSubresource(previousConstants.get(), 0, nullptr, previousData, 0, 0);
                ID3D11ShaderResourceView *previousResource = useOriginal ? input->resource.get() : targets[i - distance]->resource.get();
                // O slot 0 recebe a entrada do passe; o slot 1 recebe a LUT,
                // quando existente, ou a textura anterior/original solicitada.
                ID3D11ShaderResourceView *resources[] = {i ? targets[i - 1]->resource.get() : input->resource.get(),
                                                         lookupResource.get() ? lookupResource.get() : previousResource};
                context->PSSetShaderResources(0, 2, resources);
                const bool useLinear = passes[i].linear && !programs[i]->explicitPointSampling;
                ID3D11SamplerState *samplers[] = {useLinear ? linear.get() : nearest.get(), useLinear ? linear.get() : nearest.get()};
                context->PSSetSamplers(0, 2, samplers);
                context->Draw(4, 0);
                ID3D11ShaderResourceView *none[] = {nullptr, nullptr};
                context->PSSetShaderResources(0, 2, none);
                context->OMSetRenderTargets(0, nullptr, nullptr);
                previous = sizes[i];
            }
            // A textura de staging permite copiar o resultado para a CPU;
            // Window o envia novamente ao renderer SDL para apresentação.
            context->CopyResource(staging.get(), targets.back()->texture.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped), "Read shader output");
            for (int y = 0; y < viewport.height; ++y)
                std::memcpy(output.data() + static_cast<size_t>(y) * viewport.width,
                            static_cast<const char *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                            static_cast<size_t>(viewport.width) * sizeof(uint32_t));
            context->Unmap(staging.get(), 0);
            ++frame;
        }
    };

    NtscShaderPipeline::NtscShaderPipeline() = default;
    NtscShaderPipeline::~NtscShaderPipeline() = default;
    bool NtscShaderPipeline::initialize(const std::filesystem::path &preset, std::string &error)
    {
        try
        {
            // Preserva uma pipeline já inicializada se a nova preparação falhar.
            auto candidate = std::make_unique<Impl>();
            candidate->initialize(preset);
            impl = std::move(candidate);
            error.clear();
            return true;
        }
        catch (const std::exception &e)
        {
            error = e.what();
            return false;
        }
    }
    bool NtscShaderPipeline::render(const uint32_t *pixels, int pitch, ShaderPassSize source, ShaderPassSize viewport,
                                    std::vector<uint32_t> &output, std::string &error)
    {
        try
        {
            if (!impl)
                throw std::runtime_error("Shader pipeline is not initialized");
            impl->render(pixels, pitch, source, viewport, output);
            error.clear();
            return true;
        }
        catch (const std::exception &e)
        {
            error = e.what();
            return false;
        }
    }
}
#else
namespace R2SEGA8::Window
{
    // Mantém a interface disponível nas outras plataformas, sem alegar que
    // existe um backend de shaders implementado para elas.
    struct NtscShaderPipeline::Impl
    {
    };
    NtscShaderPipeline::NtscShaderPipeline() = default;
    NtscShaderPipeline::~NtscShaderPipeline() = default;
    bool NtscShaderPipeline::initialize(const std::filesystem::path &, std::string &error)
    {
        error = "NTSC shader backend is only available on Windows";
        return false;
    }
    bool NtscShaderPipeline::render(const uint32_t *, int, ShaderPassSize, ShaderPassSize, std::vector<uint32_t> &, std::string &error)
    {
        error = "NTSC shader backend is only available on Windows";
        return false;
    }
}
#endif
