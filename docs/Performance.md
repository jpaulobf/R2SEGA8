# Investigação do avanço rápido — 30/10/2026

## Revisão de execução e estados — 31/10/2026

Uma medição local em Windows Release, sem janela, usou uma ROM sintética
com CPU, VDP e PSG ativos. Em três execuções, o tempo mediano para 600
quadros passou de 1.418 ms para 1.392 ms. É uma diferença pequena, sujeita
a ruído, e não representa uma garantia de desempenho com jogos reais.

Na mesma máquina, 150 gravações de estado passaram de 167 ms para 81 ms,
e 150 restaurações passaram de 97 ms para 53 ms. A serialização agora copia
arrays e vetores de bytes em blocos, e a leitura verifica o checksum sem
copiar todo o conteúdo do arquivo. O estado gerado na medição manteve 416.926
bytes e hash `61a5658bb572bf78`. O número de quadro consultado a cada
T-state também passou a ser lido diretamente, sem montar uma visão de vídeo.

Esses resultados medem operações do núcleo, não a interface, o disco ou a
latência de um controle físico. O programa e a ROM sintética usados na medição
ficam em `build/performance_session_probe.cpp`, fora do controle de versão.

## Gargalo confirmado

O modo de avanço rápido já removia sleep e VSync, mas a execução da máquina
tinha pouca margem acima do ritmo nativo. A primeira medição de Wonder Boy
foi feita sem SDL e sem janela: 600 quadros levaram 6,83 s, cerca de 88 FPS.
Portanto, o problema não podia ser atribuído somente à apresentação.

O teste isolado da CPU executando NOPs consumiu 4,84 s para o orçamento de
600 quadros. VDP em Graphics I consumiu 0,35 s e PSG 0,056 s. Esses testes
isolados usam cargas diferentes da ROM e não devem ser somados como um
perfil percentual dela; serviram para localizar o custo excessivo da CPU.

## Retomada das micro-operações no Windows

O decodificador usa setjmp/longjmp para suspender o cálculo ao agendar uma
micro-operação. O sequenciador continua executando exatamente um T-state por
tick, e somente ele faz transferências externas no barramento.

No MinGW x64, o macro setjmp normalmente fornece um frame ao runtime,
permitindo o desenrolamento de pilha com SEH. O próprio
[header do MinGW-w64](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/crt/setjmp.h)
usa `_setjmp(buffer, NULL)` no caminho sem SEH. O custo do desenrolamento
não é apropriado para milhões de suspensões de micro-operações. A
[documentação da Microsoft](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/setjmp)
também descreve a relação entre longjmp e stack unwinding no Windows.

`Z80::resume()` agora chama `_setjmp(yieldPoint_, nullptr)` somente em
MinGW x64. O buffer e longjmp continuam pertencendo ao mesmo runtime; não
há mistura com os builtins do GCC. Outros compiladores/plataformas conservam
o caminho padrão. Essa escolha depende do contrato já existente de que
nenhum temporário não trivial/RAII esteja no trecho suspenso do decodificador.
Não foi desligado o tratamento de exceções do projeto nem alterado o timing
emulado da CPU.

## Medições antes e depois

Build Release com `-O3`, ROM Wonder Boy (Japan), 600 quadros em cada execução,
todos os chips ativos, sem janela e com PCM coletado normalmente:

| Carga | Antes | Depois |
| --- | --- | --- |
| Máquina completa / Wonder Boy | 6,83 s / 87,9 FPS | 1,47 s / 407,6 FPS |
| CPU isolada / NOP | 4,84 s / 124 FPS equivalentes | 0,49 s / 1.218 FPS equivalentes |

O teste da ROM produziu o mesmo checksum FNV-1a dos bytes PCM, RAM, VRAM e
último quadro, antes e depois: `3a7e79257b3584e0`. Também preservou
PC=`010B`, SP=`C400` e 35.841.600 T-states. O resultado offline não garante
a mesma taxa com janela, outros jogos ou outra carga do computador; tampouco
representa 1.000 FPS para a máquina completa.

Os artefatos locais ficam em `build/fast-forward-before.txt`,
`build/fast-forward-after.txt`, `build/fast_forward_probe.cpp` e
`build/component_probe.cpp`, fora do controle de versão.

## Apresentação e medição de FPS

O avanço rápido também foi desacoplado da relação de um quadro por
apresentação: executa até oito quadros e apresenta o mais recente.
Eventos e entradas são consultados entre os quadros, inclusive para soltar
TAB, minimizar ou encerrar. Os modos nativo e VSync conservam suas regras.
O PSG continua avançando, mas o PCM é descartado durante o avanço rápido.

`Systems::FrameCounter` mede todos os quadros emulados, inclusive os que não
foram apresentados, e atualiza a média parcial a cada quadro. A janela é
renovada a cada 500 ms ou mais sem apagar o valor publicado. Trocas de ritmo
por TAB ou VSync preservam a contagem e não introduzem espera para publicar
a média. Somente carga, reset e descarga da máquina zeram a medição. A Window consulta FPS
por callback e mantém o título no formato:

```text
R2SEGA8 | FPS : 407.60 | Wonder Boy (Japan).sg
```

## Validação

- Suíte Windows Release: 22 testes passaram, incluindo menus, TAB, título
  nativo e o novo contador com relógio controlado.
- Suíte Windows Debug sem GUI: 19 testes passaram, exercitando o caminho
  MinGW x64 sem otimização. Resultado em `build/fast-forward-debug-tests.txt`.
- SingleStepTests: 1.604.000 vetores passaram, sem arquivos com falhas.
  Resultado em `build/fast-forward-vectors.txt`; o conjunto externo é o
  mesmo identificado pelo SHA-256 em [Z80Audit.md](Z80Audit.md).
- A medição de throughput acima é offline. A apresentação física e a taxa
  observada no título devem ser conferidas com uma janela real.
