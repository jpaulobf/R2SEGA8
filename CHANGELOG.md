# Changelog

Todas as mudanças notáveis do R2SEGA8 são documentadas aqui.

## [Unreleased]

- Backgrounds Graphics II, Text e Multicolor do TMS9918, com máscaras e bancos
  de padrões/cores, caracteres de 6×8 e blocos de cor de 4×4.
- Testes de pixels e ROMs próprias dos quatro modos, com inicialização pelo
  Z80, conferência de VBlank/INT e exportação das imagens para revisão.

- TMS9918 inicial: VRAM de 16 KiB, oito registradores, portas de dados/controle,
  buffer de leitura, incremento de endereço e latches de controle.
- Raster NTSC e interrupção de VBlank ligados ao clock e a INT do Z80 no SG-1000,
  com decodificação dos espelhos de porta 80–BF.
- Background Graphics I e backdrop em quadros RGBA, apresentados por callback
  com textura SDL e filtro nearest, mantendo a janela independente do núcleo.
- Testes de VDP, integração Z80/VDP e apresentação; ROM própria de demonstração
  exportável com conferência de todos os pixels e atendimento de VBlank por uma ISR emulada.

- Interface desacoplada da emulação: `Window` e `FileManager` comunicam ações
  e consultas por callbacks; a `Engine` inspeciona ROMs, cria cartuchos e aciona a `Board`.
- Teste da janela independente do núcleo, incluindo erros de callbacks e notificação de saída.

- Limite provisório de 60 FPS na `Engine`, com pausa entre quadros e orçamento
  de CPU por quadro; controle de ritmo removido da janela e VSync desativado por enquanto.

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
