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

O manual inclui coincidência de sprites fora da tela; o núcleo atual testa
colisão somente na área ativa de 256×192, limitação já documentada.
Ela permanece pendente, assim como PAL, ligação de DRAM de 4/8 KiB,
vídeo externo e combinações não documentadas dos bits de modo.

A preparação de acesso da CPU, os slots e as mudanças de registradores no
raster têm testes do modelo implementado e referências complementares de
temporização. Ainda precisam de captura em hardware para validar atrasos
de amostragem, transições verticais e liberação de slots não usados. Não
foram introduzidas alterações de timing sem essa evidência.
