# Changelog

Todas as mudanças notáveis do R2SEGA8 são documentadas aqui.

## [Unreleased]

- Classe base das máquinas renomeada de `System` para `Board`, com criação
  pela fábrica `createBoard()` e integração à `Engine`.

- Entrada da aplicação em `Emulator.cpp`, com inicialização e loop de emulação
  organizados em `Systems/Engine.cpp` pela classe `Engine` e seu método `run()`.

- Z80 NMOS em C++17 com execução por T-state, instruções básicas e prefixadas,
  interrupções, HALT, WAIT, BUSRQ/BUSACK e snapshots no meio de uma instrução.
- Execução do SG-1000 pelo barramento de ROM/RAM, orçamento exato de ciclos
  e notificação do barramento por T-state para a futura integração dos periféricos.
- Testes da ULA, flags, instruções, interrupções, fases de acesso e restauração;
  executores opcionais de vetores SingleStepTests e dos exercitadores ZEXDOC/ZEXALL.

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
