# Revisão do TMS9918A — 30/10/2026

## Escopo e referências

Revisão do perfil NTSC de 16 KiB implementado em `Core::TMS9918`: portas,
endereçamento, status/INT, modos de background, sprites, sequenciamento por
dot e restauração de estados. O padrão do emulador permanece
`spriteUnlimited = true`, conforme a preferência definida para o projeto.

A fonte normativa é o [Data Manual da Texas Instruments, de 1982](https://peacockmedia.software/tms/TMS9918datasheet.pdf),
especialmente as seções 2.1–2.4 e 3.6. Foram confrontados o protocolo de
pré-busca/incremento, reconhecimento de status, condição F/IE para INT,
formatos dos quatro modos, prioridade e geometria dos sprites. O manual
define quatro sprites por linha; a opção de 32 é uma extensão do emulador.

Como referências complementares de software, foram consultados
[MAME](https://github.com/mamedev/mame/blob/master/src/devices/video/tms9928a.cpp)
e [openMSX](https://github.com/openMSX/openMSX/blob/master/src/video/VDP.cc).
Eles não substituem medições do TMS9918A. Em particular, detalhes de corridas
entre transferências de VRAM e os dois bytes de controle não devem ser
modificados apenas para igualar um desses emuladores. O comportamento atual
do ponteiro/latch acompanha o caminho MSX1 do openMSX; não foi alterado.

## Resultados

### Expectativas de sprites ajustadas

As duas falhas anteriores, em `tms9918_sprite_tests` e `sg1000_vdp_tests`,
eram expectativas do limite de quatro rodando com o padrão ilimitado.
Não eram uma regressão da CPU nem um defeito demonstrado da renderização.

- As cenas que verificam o hardware agora chamam `setSpriteUnlimited(false)`.
- A integração com ROM própria verifica os oito sprites de cada faixa, como
  exige o padrão ilimitado escolhido para o emulador.
- Há verificações do padrão `true`, de ambos os modos e da preservação da
  preferência após reset e restauração.

### Proteção da seleção após restauração

`restore()` aceita contagens de zero a 32 e fases de raster válidas, sem
reconstituir toda a história que levou àquele estado. Era possível restaurar
uma seleção já cheia em um dot que ainda avalia outro candidato.
`evaluateSprite()` então acrescentava o sprite 33, escrevendo além de
`evaluatedSprites` e atingindo o banco adjacente dentro de `State`.

O teste reproduziu a falha antes da correção. A avaliação agora interrompe
a seleção quando o banco está cheio. Essa proteção cobre estados sintéticos
ou inconsistentes aceitos pela API; não foi encontrada uma ROM que produza
essa sequência pela execução normal do VDP.

## Testes adicionais

`tests/Core/TMS9918AuditTests.cpp` acrescenta dois grupos ao CTest:

- `tms9918_status_audit_tests`: 512 cenários, combinando os 256 valores de
  status com as duas preferências de sprites. Confere F/IE, ausência de INT
  por C/5S isolados, reconhecimento, retenção do índice, cancelamento do
  comando parcial e preservação de uma transferência de VRAM pendente.
- `tms9918_snapshot_audit_tests`: 5.472 restaurações, uma em cada meio-dot de
  uma linha nos quatro modos e nas duas preferências de sprites. Inclui
  comandos parciais, transferências pendentes, seleção e buscas; compara
  todos os campos do estado, incluindo os dois quadros, após o próximo
  clock. Também reproduz o caso da seleção saturada.

Os testes anteriores continuam cobrindo pixels com resultados esperados
independentes, os tamanhos/ampliações, transparência/colisão, terminador,
quinto sprite, acessos por porta e ROMs executadas pelo Z80. O teste de
snapshots verifica determinismo; sozinho, não prova fidelidade ao hardware.

Validação desta rodada:

- Windows, C++17, Release: **21/21 testes passaram**, incluindo janela SDL dummy.
- Linux/WSL, Debug, AddressSanitizer e UndefinedBehaviorSanitizer:
  **18/18 testes do núcleo passaram**, sem erros dos sanitizadores.

Os registros locais ficam em `build/vdp-validation/audit-before.txt`,
`audit-release-tests.txt` e no `Testing/Temporary/LastTest.log` da compilação
com sanitizadores. Esses artefatos não são versionados.

## Limites que permanecem

Não foi confirmada outra divergência funcional nas regras revisadas dentro
do perfil atual. Isso não certifica precisão elétrica nem ausência de bugs.

Esta revisão original antecede a complementação de colisões horizontais
descrita abaixo. Permanecem pendentes colisões em linhas totalmente fora
da área vertical ativa, PAL, ligação de DRAM de 4/8 KiB,
vídeo externo e combinações não documentadas dos bits de modo.

A preparação de acesso da CPU, os slots e as mudanças de registradores no
raster têm testes do modelo implementado e referências complementares de
temporização. Ainda precisam de captura em hardware para validar atrasos
de amostragem, transições verticais e liberação de slots não usados. Não
foram introduzidas alterações de timing sem essa evidência.

## Repasse complementar — colisão, portas e temporização

### Colisão fora da imagem

O manual da TI, seção 2.3.2, inclui sprites transparentes e sprites parcial
ou totalmente fora da tela na detecção de coincidência. Foi adicionado ao
perfil o teste horizontal nos intervalos X=-32..-1 e X=256..286, usando os
dados já buscados e sem alterar o quadro visível. A borda esquerda usa o
banco da próxima linha; a direita usa o da linha corrente. A opção ilimitada
continua ativa por padrão; os testes também verificam o limite de hardware.

Essa interpretação segue o manual, mas não representa validação no chip:
o [SpriteChecker do openMSX](https://github.com/openMSX/openMSX/blob/master/src/video/SpriteChecker.cc)
e o [VDP TMS9918 do MAME](https://github.com/mamedev/mame/blob/master/src/devices/video/tms9928a.cpp)
restringem a colisão horizontal à área visível. Seus modelos e os instantes
adotados por este núcleo não comprovam o comportamento elétrico do TMS9918.
A avaliação vertical ainda é restrita às linhas 0–191; sprites inteiramente
fora dessas linhas requerem investigação específica.

`tms9918_sprite_tests` cobre as duas bordas, cor zero, reconhecimento e nova
colisão, limite de quatro, modo ilimitado e snapshot entre meios de dot.
`sg1000_vdp_tests` configura a VRAM pelas portas e usa uma ROM sintética: o
Z80 lê BF, detecta a flag e a registra na RAM, sem desenhar pixels fora da tela.
Uma segunda ROM autônoma prepara a própria VRAM e mostra o resultado pela
cor do backdrop. Os testes confirmam o contrato do modelo, não uma captura de
hardware. Uma variante de controle posiciona os dois sprites sem sobreposição
e deve permanecer vermelha; a SAT inteira é inicializada para evitar falsos
positivos durante a ativação da tela.

Para gerar a ROM autônoma e as demonstrações existentes no Windows:

```text
cmake --build build-release --target sg1000_vdp_tests
build-release/sg1000_vdp_tests.exe build/sg-final-diagnostics
```

O arquivo `offscreen-collision-probe.sg` tem 32 KiB e não contém ROM comercial.
Ele configura dois sprites inteiramente à esquerda da imagem e consulta a flag
pela CPU. Começa vermelho e muda para verde quando a flag é observada; se
continuar vermelho, a flag não foi observada ou a configuração inicial falhou.
Este teste permite investigar a divergência em um SG-1000 real com um cartucho
gravável, mas não identifica o dot exato nem as colisões verticais fora da tela.
No perfil atual do emulador, o teste fica verde.

SHA-256 do arquivo gerado nesta execução:
`87ab2566acee08a8ed46326c0625f06739abc5ca7de04a4547cc47706c2fa328`.

### Portas e barramento não acionado

As leituras sem dispositivo em `00–7F` retornam `FF` por convenção, assim
como bytes de ROM simples não fornecidos pelo arquivo. O código não simula
retenção, flutuação ou decaimento elétrico dos dados. Escritas em `00–3F`
e `C0–FF` são ignoradas sem expansão conectada. Os testes percorrem essas
faixas e verificam ausência de efeitos no VDP, PSG e WAIT.

Para os controles, a leitura em `C0–FF` continua espelhada por A0. A
[investigação do circuito SG-1000](https://nicole.express/2023/neo-geo-eat-your-heart-out.html)
e o [mapa de portas do SMS Power!](https://www.smspower.org/Development/IOPortMapSG)
apoiam essa interpretação; o [driver do MAME](https://github.com/mamedev/mame/blob/master/src/mame/sega/sg1000.cpp)
modela somente DC–DF. Essa diferença não justifica alterar o circuito que
já reproduzimos. O valor do barramento flutuante precisa de medição na placa.

### Comparação das janelas de acesso à VRAM

`tms9918_vram_tests` percorre cada meio-dot de uma linha e executa duas
escritas concluídas por fase, com clock nominal de 10.738.635 Hz. A tabela
abaixo compara o atraso do **modelo** com a tabela 2-2 do manual da TI;
os limites em ciclos foram arredondados para cima a partir dos microssegundos
nominais e não devem ser interpretados como tolerâncias elétricas precisas.

| Condição | Modelo, ciclos do cristal | Referência TI aproximada, ciclos |
| --- | --- | --- |
| Graphics I e II | 14–77 | até 86 |
| Text | 14–25 | até 34 |
| Multicolor | 14–73 | até 38 |
| Tela apagada ou intervalo vertical | 14–21 | até 22 |

No Multicolor, 169 de 1.368 pedidos por linha ultrapassaram os 38 ciclos
nominais; 55 desses pedidos começaram em dots que o nosso raster considera
ativos. O pior caso começou no dot 242 e levou 73 ciclos. O modelo usa 14
ciclos de preparação, aproximadamente 1,30 µs, enquanto o manual descreve
aproximadamente 2 µs; as origens exatas dos strobes precisam ser comparadas.

A [análise de temporização do openMSX](https://openmsx.org/vdp-vram-timing/vdp-timing-2.html)
aponta uma janela longa na borda horizontal que também não se concilia com a
tabela resumida de Multicolor. Seu diagrama do TMS9918 é uma reconstrução;
as medições do artigo foram feitas em V9938. Mantivemos os slots atuais e
registramos a divergência; não há dados suficientes para escolher uma correção
ou afirmar que o modelo corresponde ao TMS9918 físico.

Para validação em hardware, seria necessário capturar os strobes de CPU e os
acessos VRAM ao longo da linha nos modos citados, testar colisões de borda
com sprites de cores/padrões conhecidos e observar as leituras flutuantes
após valores anteriores diferentes. Não havia captura de placa nesta rodada.

### Execuções de verificação

- Windows Release: **22/22 testes passaram**, incluindo o diagnóstico autônomo
  e o controle negativo. Os testes de interface foram atualizados para os
  mapeamentos e a preferência de VSync já presentes no workspace, sem alterar
  essas preferências.
- Linux/WSL Debug, AddressSanitizer e UndefinedBehaviorSanitizer: **19/19
  testes passaram**, sem erros relatados.
- Wonder Boy, 600 quadros sem janela: hash combinado de PCM, RAM, VRAM e
  imagem permaneceu `3a7e79257b3584e0`, com PC=`010B`, SP=`C400` e
  35.841.600 T-states, como na medição anterior. A taxa observada foi de
  aproximadamente 400 FPS; esse teste não substitui uma partida completa.
