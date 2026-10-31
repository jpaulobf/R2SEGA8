# Controles do SG-1000

## Separação de responsabilidades

`Common::Button` define Up, Down, Left, Right, A, B, TurboA, TurboB, Select e
Start, sem SDL. `ButtonMask` usa bits ativos em 1 e `InputState` contém os dois
jogadores. A/B representam os botões 1/2 neste perfil do SG-1000.

`Systems::Input` adapta teclado e SDL Gamepad por `std::map`, combinando as
fontes por OR. `mapKey(tecla, botão, jogador)` permite reatribuir teclas;
`mapGamepad(posição, botão)` configura as posições físicas dos dois gamepads.
Uma nova associação de teclado também remove a pressão memorizada da tecla.
A edição por menu e a persistência dos mapeamentos ficam para uma etapa futura.

Window encaminha eventos SDL e a disponibilidade de entrada pelos callbacks
`inputEvent` e `inputAvailability`. Ela continua sem conhecer Core, Board ou
Engine. A Engine entrega os eventos a Input e publica o estado combinado em
`Board::setInput()`. O SG-1000 interpreta essas intenções e entrega seis linhas
por jogador ao `Core::IO::Joypad`, que não depende de SDL.

## Associações padrão

| Função | Teclado P1 | Posição SDL do gamepad / layout Xbox |
| --- | --- | --- |
| Direções | WASD ou setas | D-pad ou analógico esquerdo |
| A / botão 1 | J | South / A |
| B / botão 2 | K | East / B |
| Turbo A | U | West / X |
| Turbo B | I | North / Y |
| Start → Pause/Hold do console | Enter | Start |
| Select, sem efeito neste SG-1000 | Backspace | Back/View |

As letras impressas variam entre controles; a associação usa a posição lógica da SDL.
O teclado é atribuído a P1 por padrão, mas a API permite teclas para P2.

TAB mantido solicita avanço rápido à Engine, quando permitido em
`Hacks > Enable Fast-Forward` (ativado por padrão). Não pertence ao enum
de botões do console. Soltura, captura, perda de foco e minimização liberam
o estado memorizado; o áudio fica silenciado durante o avanço rápido.
Veja [Display.md](Display.md) para os três modos de execução.

Pause/Break alterna a pausa manual pela callback da janela, sem integrar o
enum de botões do console. Menus e diálogos pausam temporariamente e não
removem essa intenção manual ao fechar. Durante a pausa, entradas de jogo
e TAB são liberados; o atalho de pausa permanece disponível.

O analógico usa zona morta de 16000 em cada eixo: os limites exatos são
neutros e valores além deles ativam uma direção. Não há filtro de direções
opostas: fontes simultâneas mantêm as respectivas linhas pressionadas.
Teclas com Ctrl/Alt/GUI e eventos de repetição não iniciam novas pressões.
Solturas são tratadas mesmo quando a entrada está bloqueada.

## Detecção e ciclo de vida dos dispositivos

Input enumera gamepads já conectados e processa eventos de adição, remoção e
remapeamento, inclusive enquanto a interface captura os comandos. Os dois
primeiros dispositivos reconhecidos pela SDL ocupam P1/P2. A remoção de P1
não desloca P2: a vaga livre recebe o próximo dispositivo conectado, inclusive
um terceiro controle que estava aguardando. Eventos duplicados não abrem duas
referências para o mesmo dispositivo.

O reconhecimento de Xbox/8BitDo depende dos drivers e mapeamentos incluídos
na versão SDL utilizada e do modo de conexão do dispositivo. Remapeamentos
SDL são lidos pelos handles abertos. Não há download automático de banco de
mapeamentos nem promessa de compatibilidade com todo modo/GUID desses modelos.
Os testes automatizados usam gamepads virtuais da SDL; controles físicos
precisam de revisão manual no ambiente do usuário.

A Engine inicializa Input depois da janela. Um guard encerra os gamepads antes
de Window chamar SDL_Quit, inclusive em exceções. Shutdown é idempotente.
Falhas na inicialização dos gamepads são relatadas, preservando o teclado.

## Foco e captura pela interface

Perder foco, minimizar, abrir o seletor ou deixar ImGui capturar teclado/texto
libera a entrada na placa e esquece as teclas memorizadas. Popups e menus também
bloqueiam os controles. A disponibilidade é notificada após os eventos e após
o desenho, inclusive quando um menu abre sem outro evento de teclado.
A navegação ociosa do ImGui não captura automaticamente as setas do jogo.
Eventos de foco/teclado de outra janela não são enviados ao adaptador.

A reativação exige uma nova pressão para teclas esquecidas; gamepads são
amostrados pelo seu estado físico atual. Trocar a ROM limpa as teclas, e a
nova placa nasce com todas as linhas liberadas. A emulação continua executando
enquanto um diálogo bloqueia a entrada; isso não implementa uma pausa do host.

## Portas e Pause/Hold

O perfil decodifica C0–FF, com o byte alto do endereço de I/O ignorado. DC/DD
são os endereços usuais; DE/DF também espelham as leituras. Pressionado retorna
zero, e a leitura não consome ou limpa o estado:

| Porta | Bits | Linhas |
| --- | --- | --- |
| Par, como DC | 0–5 | P1 Up, Down, Left, Right, 1, 2 |
| Par, como DC | 6–7 | P2 Up, Down |
| Ímpar, como DD | 0–3 | P2 Left, Right, 1, 2 |
| Ímpar, como DD | 4–7 | Altos neste perfil sem expansão |

Escritas nessas portas são ignoradas; teclado SK-1100 e outros periféricos não
estão implementados. Select não altera as linhas físicas do SG-1000. Start de
qualquer jogador representa o botão Pause/Hold da máquina, pela NMI do Z80.
Uma pressão cria uma borda; manter pressionado não repete a NMI. Soltar e
pressionar novamente permite outra NMI. O handler da ROM decide o que fazer;
a Engine não para a emulação ao receber Start.

`setInput()` atualiza os dois jogadores em conjunto: transferir uma pressão de
Start de um controle para outro não cria um intervalo lógico de soltura que
gere NMI extra. `setButtons()` permanece disponível para atualizar um jogador.

## Turbo e clock emulado

Input publica TurboA/TurboB como intenções, sem ligá-los ao FPS do host.
A placa resolve os pulsos pelo número de quadros completos do VDP: ativo em
`frame % 4 == 3`. A normalização ocorre ao publicar a entrada e a cada nova
publicação de quadro, dentro do clock da placa. Funciona durante HALT/WAIT,
sem exigir outra amostragem da SDL. Uma associação normal mantém o botão
pressionado mesmo quando a fase de turbo está desligada.

## Validação

- `sg1000_input_tests`: todas as 4096 combinações dos dois controles, polaridade,
  espelhos, leituras IN pela CPU, NMI em 0066, pressão mantida, troca da fonte de
  Start e turbo exatamente na fronteira do quadro emulado.
- `input_tests`: teclas sobrepostas, remapeamento por jogador, captura/foco,
  enumeração inicial, conexão/remoção real de gamepads virtuais, terceiro
  dispositivo aguardando, posições, eixos/zona morta e reinicialização.
- `window_smoke_tests`: callbacks de eventos/disponibilidade, foco de outra
  janela, solturas bloqueadas, captura e seletor bloqueando antes do primeiro
  desenho, além dos testes existentes de apresentação e arquivos.

## Fontes

- [Mapa de portas do SG-1000 no SMS Power!](https://www.smspower.org/Development/IOPortMapSG):
  referência comunitária para C0–FF. O [driver do SG-1000 no MAME](https://github.com/mamedev/mame/blob/master/src/mame/sega/sg1000.cpp)
  serve como referência de software para a divisão das linhas entre os dois
  registradores e a ligação de Pause/Hold à NMI; seu mapa atual enumera DC–DF.
- [SDL Gamepad](https://wiki.libsdl.org/SDL3/CategoryGamepad),
  [enumeração](https://wiki.libsdl.org/SDL3/SDL_GetGamepads) e
  [joysticks virtuais](https://wiki.libsdl.org/SDL3/SDL_AttachVirtualJoystick):
  APIs usadas pelo adaptador e pelos testes.
