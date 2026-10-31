# Apresentação da janela

A janela principal apresenta somente o vídeo e os menus File, Display, Audio e Hacks.
Cartridge ROM e System RAM não são consultados nem desenhados nessa tela.
O callback `memory`, os tipos neutros de consulta e o desenho hexadecimal
foram preservados em `Window::MemoryInspector` para futuras janelas de Debug.

## VSync e avanço rápido

`Display > VSync` é a primeira opção, separada das escalas por um divisor.
Começa ativada. `Hacks > Enable Fast-Forward` começa ativada e permite
acelerar enquanto TAB estiver pressionado. As preferências pertencem à sessão
da Engine e são consultadas/alteradas pela Window somente por callbacks.

| Modo efetivo | Execução e apresentação |
| --- | --- |
| VSync desativado | Clock original da placa, com prazos racionais e sleep; SG-1000 NTSC a aproximadamente 59,922743 Hz. |
| VSync ativado | Um quadro emulado por apresentação; a espera vertical da SDL controla o ritmo, sem sleep ou limite adicional. |
| TAB pressionado com fast-forward permitido | Até oito quadros por apresentação, sem VSync nem sleep; PCM descartado e saída silenciada. |

O lote de avanço rápido consulta eventos e entradas entre os quadros, permitindo
soltar TAB sem esperar a apresentação seguinte. Nenhum clock da máquina é
descartado; somente imagens intermediárias deixam de ser apresentadas.

## FPS no título

O título usa `R2SEGA8 | FPS : 59.92 | Nome da ROM.sg`. A Engine mede os
quadros emulados por segundo real e fornece a média parcial à Window por
callback a cada quadro, sem esperar fechar a janela de 500 ms. Quadros intermediários do avanço rápido também
entram na conta; chamadas de renderização sem emulação não entram.

Sem ROM, a indicação é zero e o nome do arquivo é removido. Carga e reset da
máquina reiniciam a medição. Trocas de modo, inclusive pressionar/soltar TAB,
preservam o valor e a contagem atual; o quadro seguinte já atualiza a média.
A renovação periódica da janela também preserva o último valor. A Window preserva o nome da ROM e só
atualiza o título nativo quando o texto muda. A análise do gargalo de execução
no MinGW está registrada em [Performance.md](Performance.md).

## Transições entre modos

O modo VSync acompanha a frequência do monitor. Nesta versão, um monitor acima
da frequência original também acelera a emulação nesse modo; a separação entre
cadência de emulação e apresentação fica para uma revisão futura.

TAB suspende temporariamente VSync sem apagar a preferência do menu. Soltar TAB
restaura o modo anterior. Captura pela interface, abertura de menus/diálogos,
perda de foco, minimização, reset e troca de ROM liberam a tecla memorizada.
Desativar a opção em Hacks também impede o avanço rápido. A fila de áudio é
limpa ao entrar e sair dele; os ajustes de volume/ativação são preservados e o
PSG continua sendo emulado. Turbo dos controles continua seguindo os quadros
emulados, inclusive em avanço rápido.

As trocas de modo reiniciam os prazos, sem recuperar tempo do modo anterior.
Minimizar usa o limitador nativo mesmo com VSync selecionado, evitando depender
de apresentações suspensas para limitar o loop. Se a SDL rejeitar a ativação
de VSync, a Engine registra a falha e retorna ao ritmo nativo com a opção
desmarcada. A configuração do renderizador usa
[`SDL_SetRenderVSync`](https://wiki.libsdl.org/SDL3/SDL_SetRenderVSync).

Os testes de janela verificam os callbacks dos dois menus, a configuração SDL,
a suspensão temporária sem alterar a preferência e TAB através do ImGui/Input.
Os testes de entrada cobrem soltura, captura, modificadores, repetição, perda
de foco e minimização. A espera física pelo monitor precisa de teste com uma
janela real; o backend dummy não certifica sincronização de hardware.

## Pausa manual e menus

Pause/Break alterna a pausa manual quando há uma ROM carregada. Repetições
automáticas, atalhos modificados e eventos de outras janelas são ignorados.
O atalho continua disponível dentro de um menu, mesmo com captura do teclado.

Abrir menus ou o seletor de ROM solicita uma pausa temporária pelo callback
`menuPause`. Fechá-los libera somente essa solicitação: a execução continua
suspensa se Pause/Break tiver solicitado uma pausa manual. `Systems::PauseState`
mantém as duas origens independentes; a Window não conhece CPU, Board ou Engine.

Durante a pausa, a Engine mantém eventos e apresentação, sem avançar CPU,
VDP ou PSG. A saída de áudio é limpa, o avanço rápido é interrompido e as
entradas memorizadas são liberadas. Retomar inicia novos prazos para não
recuperar o tempo pausado. O contador de FPS preserva o valor e a contagem,
excluindo da média o tempo em que a emulação ficou suspensa.

## Escalas e proporção

Display oferece 1X, 2X, 3X, 4X e 5X, com atalhos Ctrl+1 até Ctrl+5.
A escala padrão é 3X. As dimensões indicam a área de vídeo em pixels:

| Escala | Vídeo SG-1000 |
| --- | --- |
| 1X | 256×192 |
| 2X | 512×384 |
| 3X | 768×576 |
| 4X | 1024×768 |
| 5X | 1280×960 |

A janela é centralizada no monitor atual ao abrir e a cada escolha de escala,
inclusive quando precisa restaurar uma janela maximizada. O monitor escolhido
é preservado durante a restauração assíncrona.

A altura da barra de menus é acrescentada ao tamanho cliente, sem retirar
pixels da imagem. A densidade de pixels da janela converte os presets para
as coordenadas lógicas do sistema; densidades fracionárias podem exigir
arredondamento de um pixel. A barra usa o tamanho real da fonte do ImGui.

O vídeo ocupa a maior área disponível abaixo do menu, sem o antigo limite
de 384 pixels de altura nem margens de conteúdo na janela principal. A
proporção nativa de apresentação é 4:3 para o perfil SG-1000. Ao maximizar,
a imagem fica centralizada, com barras no espaço que exceder essa proporção.
O filtro permanece nearest. A seleção de escala em uma janela maximizada
solicita restauração e aplica o preset; ações do menu são aplicadas depois
de apresentar o quadro com a geometria usada durante seu desenho.

`DisplaySize` recebe largura/altura da aplicação. A Engine fornece 256×192;
Window continua sem incluir Core, Board ou Engine. O aspecto de apresentação
usa essa configuração neutra, sem deduzir o console ou inspecionar a ROM.

## Redimensionamento e maximização

A SDL exige `SDL_WINDOW_RESIZABLE` para permitir maximização. Por isso a
política de tamanho é separada dessa flag:

- No Windows, uma subclassificação da janela nativa bloqueia as bordas de
  redimensionamento e o comando SC_SIZE, preservando mover, minimizar,
  maximizar e restaurar. A subclassificação é removida antes de destruir a janela.
- Eventos que deixem a janela normal com um tamanho arbitrário restauram o
  preset, sem impor tamanho mínimo/máximo que também limite a maximização.
  Esse mecanismo serve de fallback nas demais plataformas; o bloqueio nativo
  de bordas foi implementado especificamente para Win32.
- Restauração e troca de escala respeitam os eventos assíncronos da SDL.
  O seletor de arquivos ajusta seus limites ao viewport, inclusive em 1X.

Fullscreen fica para uma etapa futura: **borderless com proporção preservada**
e **borderless stretched**. Exclusive Fullscreen não faz parte do plano e
nenhum modo fullscreen é ativado nesta etapa.

## Validação

`window_smoke_tests` confere 3X inicial, todos os atalhos 1X–5X, escolha pelo
menu Display, rejeição de repetição/eventos de outra janela, correção de
tamanho normal arbitrário e retorno ao preset após uma maximização simulada.
Os vértices enviados ao renderizador verificam tamanho exato da imagem,
centralização e proporção em áreas largas e altas. O callback de memória
permanece sem chamadas na janela principal.

O backend dummy não maximiza uma janela nativa. A simulação cobre o contrato
SDL da política e o layout; bordas e botões reais do gerenciador de janelas
continuam sujeitos à revisão manual no desktop.

Referências: [SDL_SetWindowResizable](https://wiki.libsdl.org/SDL3/SDL_SetWindowResizable)
e [SDL_MaximizeWindow](https://wiki.libsdl.org/SDL3/SDL_MaximizeWindow).
