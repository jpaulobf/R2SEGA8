# Apresentação da janela

A janela principal apresenta somente o vídeo e os menus File e Display.
Cartridge ROM e System RAM não são consultados nem desenhados nessa tela.
O callback `memory`, os tipos neutros de consulta e o desenho hexadecimal
foram preservados em `Window::MemoryInspector` para futuras janelas de Debug.

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
