#pragma once
#include "ShaderPreset.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace R2SEGA8::Window
{
    // Executa os passes dos presets de vídeo em sequência. O nome é histórico:
    // além do NTSC, a implementação atende aos outros presets integrados.
    // Atualmente o backend usa Direct3D 11 no Windows; nas demais plataformas
    // initialize e render informam que o backend não está disponível.
    class NtscShaderPipeline
    {
    public:
        NtscShaderPipeline();
        ~NtscShaderPipeline();

        // Lê o .cgp, prepara os recursos e compila os shaders. Retorna false
        // com o diagnóstico em error se alguma etapa falhar. Uma nova
        // inicialização só substitui a pipeline anterior quando tem sucesso.
        bool initialize(const std::filesystem::path &preset, std::string &error);

        // Processa uma imagem ARGB8888. pixels aponta para o primeiro pixel
        // da área de origem e pitchPixels é a distância entre linhas em pixels,
        // permitindo passar um recorte sem copiar o framebuffer de entrada.
        // source informa o tamanho da área; viewport define o tamanho da saída.
        // No sucesso, output recebe viewport.width * viewport.height pixels e
        // error é limpo. Em caso de falha, retorna false com o diagnóstico;
        // não há garantia sobre o conteúdo de output após a falha.
        bool render(const uint32_t *pixels, int pitchPixels, ShaderPassSize source,
                    ShaderPassSize viewport, std::vector<uint32_t> &output, std::string &error);

    private:
        // Mantém os tipos e recursos gráficos fora da interface pública.
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}