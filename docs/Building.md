# Compilação e testes

O projeto requer **CMake 3.16+**, um **compilador C++17** e **Git**.

## Aplicação completa

A primeira configuração baixa revisões fixadas de SDL3 e Dear ImGui e precisa
de acesso à internet. No Linux, instale também os pacotes de desenvolvimento
do backend de vídeo escolhido, como X11 ou Wayland.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Para MinGW no Windows, acrescente `-G Ninja` ao primeiro comando. O Ninja
deve estar disponível no PATH, junto do compilador.

O executável é gerado em `bin/` ou `bin/Debug/` quando o gerador oferece
múltiplas configurações. Para uma compilação Release, use `Release` no lugar
de `Debug` nos comandos correspondentes.

## Núcleo sem interface gráfica

Esta configuração compila a CPU, o carregamento de ROMs e os sistemas sem
baixar SDL3 ou Dear ImGui. Os testes não precisam de janela ou dispositivo de áudio.

```sh
cmake -S . -B build-core -DR2SEGA8_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core --config Debug --parallel
ctest --test-dir build-core -C Debug --output-on-failure
```

Use pastas de build distintas para configurações com e sem interface, ou
para compiladores diferentes.

## Validação adicional da CPU

Os testes locais são executados pelo CTest. Os vetores SingleStepTests e
os exercitadores ZEXDOC/ZEXALL são opcionais e precisam ser obtidos separadamente.
Os comandos e limites dessa validação estão em [Z80.md](Z80.md).
