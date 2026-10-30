# Changelog

Todas as mudanças notáveis do R2SEGA8 são documentadas aqui.

## [Unreleased]

- Board e placas concretas dos consoles movidas para Core; seleção dos consoles
  isolada em Systems/BoardFactory. SDL e suporte da Engine permanecem fora do Core.

- Corrigidas pausas artificiais de áudio causadas por tratar a fila de entrada
  vazia da SDL como underrun e reiniciar repetidamente a preparação de 40 ms.
- Engine passa a usar 59.736 T-states por quadro NTSC do SG-1000, equivalentes
  a 59,922743 Hz, com prazos racionais e recuperação limitada de pequenos atrasos.
- Reduzido o trabalho repetido do mixer/amostrador sem mudar o PCM de Wonder Boy
  na comparação offline de dez segundos.

- File > Reset (F12) reinicia a máquina com a ROM carregada e preserva os ajustes
  de áudio; File > Unload libera a máquina, limpa áudio e restaura o título.

- Menu Audio com ativação e volume de Master, Tone 1/2/3 e Noise, preservados
  durante a sessão e nas trocas de ROM, sem alterar o estado do hardware emulado.

- Áudio do SG-1000 com PSG SN76489AN: três tons, ruído periódico/branco,
  atenuação de 2 dB, divisor zero da variante TI e READY ligado a WAIT da CPU.
- Geração PCM mono a 48 kHz pelo clock emulado e reprodução SDL3, com fila
  limitada, limpeza ao trocar a ROM e recuperação após esvaziamento.
- Testes do PSG, integração CPU/VDP/PSG e reprodução com dispositivo SDL dummy.

- Corrigida a inicialização de The Castle (Japan): reconhecimento pelo tamanho e
  CRC-32 e suporte aos 8 KiB de RAM do cartucho em `8000–9FFF`, separados da RAM
  interna do SG-1000. Testes cobrem limites, ROM espelhada, isolamento da memória,
  reset e execução da CPU usando a RAM adicional.

- Seleção de ROM com carga imediata ao clicar, identificação pela extensão
  SG/SC/SMS/GG sem distinção de maiúsculas e remoção da etapa de confirmação.
- Drag-and-drop e caminhos de arquivo também carregam diretamente; falhas
  mantêm o diálogo aberto sem repetir tentativas ou substituir a ROM anterior.
- ZIP e identificação automática de BIN adiados; o seletor informa essa limitação.

- Janela principal dedicada ao vídeo, com MemoryInspector e callbacks de ROM/RAM
  preservados para futuras janelas de Debug.
- Menu Display com 1X–5X e Ctrl+1–5, padrão 3X, tamanho de vídeo separado do menu
  e apresentação centralizada com proporção preservada em toda a área disponível.
- Política de tamanho fixo com maximização: bloqueio de bordas/comando de resize
  no Windows e retorno ao preset para tamanhos normais arbitrários.
- Fullscreen adiado para modos borderless com proporção ou stretched;
  Exclusive Fullscreen excluído do plano.

- Controles do SG-1000: enum lógico, maps de teclado/gamepad, dois jogadores,
  analógico com zona morta e detecção inicial/hotplug pela SDL3.
- Portas DC/DD e espelhos ativos em zero, Start como Pause/Hold por NMI e
  turbo sincronizado aos quadros emulados, inclusive durante HALT/WAIT.
- Callbacks de eventos/disponibilidade mantêm Window independente do núcleo;
  foco, captura e diálogos liberam as entradas. Testes incluem controles virtuais.

- Aplicação temporal dos registradores do TMS9918: formato da célula preservado
  em trocas de modo e avanço dos latches de saída durante apagamento.
- Condição comum de habilitação de sprites para seleção, buscas, composição e
  colisão; novas fases de saída incluídas nos snapshots.
- Testes de alterações de R0–R7 entre dots/fetches, troca de slots com acesso
  pendente e ROM Z80 com mudanças de cor durante o raster.

- Sequenciador de VRAM do TMS9918: slots de CPU por modo, refresh, preparação
  de acessos, pré-busca adiada e substituição de pedidos rápidos sem fila.
- Fetches de nome, cor e padrão em fases por dot, com células de pré-busca,
  latches de saída e estado completo para retomada.
- Testes dos mapas de slots, escritas entre fases de background e ROMs Z80
  comparando transferências rápidas e espaçadas com o display ativo.

- Sprites do TMS9918 com seleção e buscas distribuídas por dot, SIZE/MAG,
  early clock, prioridade, transparência, colisão e flag/índice do quinto sprite.
- Limite padrão de quatro sprites por linha e opção no núcleo para até 32,
  com fetch adicional por dot e estado completo para retomada.
- Testes de sprites, fases do fetch, snapshots e ROM própria com conferência
  de todos os pixels e reconhecimento de colisão/overflow pela ISR do Z80.

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
