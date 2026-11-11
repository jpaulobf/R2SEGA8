#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace R2SEGA8::Window
{
    // Define a referência usada para calcular uma dimensão do passe.
    enum class ShaderScale
    {
        Source,   // Dimensão da imagem recebida pelo passe atual.
        Viewport, // Dimensão da área de apresentação do jogo.
        Absolute  // O fator representa diretamente uma quantidade de pixels.
    };

    // Configuração de um passe descrito por um preset .cgp.
    struct ShaderPass
    {
        std::filesystem::path source; // Arquivo .cg, relativo ao preset após a leitura.
        ShaderScale scaleX = ShaderScale::Source, scaleY = ShaderScale::Source;
        double factorX = 1.0, factorY = 1.0;        // Fatores horizontais e verticais.
        bool linear = false, floatingPoint = false; // Filtragem da entrada e formato da saída.
        bool srgb = false;                          // Converte a saída linear para sRGB ao gravar a textura.
        unsigned frameCountMod = 0;                 // Módulo do contador de quadros; zero desativa o módulo.
    };

    // Largura e altura em pixels de uma imagem ou textura intermediária.
    struct ShaderPassSize
    {
        int width, height;
    };

    // Lê o subconjunto de .cgp usado pelos presets disponíveis, verifica os
    // shaders e seus includes e, quando presente, localiza uma LUT sem filtro
    // linear. Configurações não suportadas são rejeitadas explicitamente.
    // Também lê valores numéricos das chaves listadas em parameters; cabe ao
    // backend verificar se consegue aplicá-los. Em caso de sucesso, preenche
    // passes, lookupTexture e parameters (quando fornecidos),
    // limpa error e retorna true. Em caso de falha, retorna false e descreve
    // o erro; os resultados anteriores não são substituídos.
    bool loadShaderPreset(const std::filesystem::path &path, std::vector<ShaderPass> &passes, std::string &error,
                          std::filesystem::path *lookupTexture = nullptr,
                          std::map<std::string, std::string> *parameters = nullptr);

    // Calcula o tamanho de saída de cada passe, em ordem. A escala Source usa
    // a saída do passe anterior; no primeiro passe, usa a imagem de origem.
    // Retorna false com um diagnóstico se as dimensões forem inválidas, sem
    // substituir sizes. No sucesso, preenche sizes e limpa error.
    bool planShaderPasses(const std::vector<ShaderPass> &passes, ShaderPassSize source,
                          ShaderPassSize viewport, std::vector<ShaderPassSize> &sizes, std::string &error);
}