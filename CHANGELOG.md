# Changelog

Todas as mudanças notáveis do R2SEGA8 são documentadas aqui.

## [Unreleased]

- Seletor de unidades no diálogo de ROMs do Windows, com navegação para a raiz
  do disco selecionado e tratamento de unidades indisponíveis.

- Janela SDL3 com interface Dear ImGui, menu File > Open, Ctrl+O e abertura
  por arrastar arquivos. Seletor de ROMs com navegação por pastas e escolha do sistema.
- Leitura de ROMs brutas, sugestão por extensão e cabeçalho Sega, inspeção de
  ROM/RAM e preservação da máquina ativa em caso de falha no carregamento.
- Classes próprias para SG-1000, SC-3000, Master System e Game Gear. Primeiro
  mapa de memória do SG-1000 para ROM simples e RAM espelhada.
- CMake adaptado ao projeto e testes de carregamento, memória e interface.

## [0.0.1] - 2026-10-20

Primeira versão do R2SEGA8, ainda em desenvolvimento.
