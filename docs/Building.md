# Compilação e testes

O projeto requer **CMake 3.16+**, um **compilador C++17** e **Git**.

## Aplicação completa

A primeira configuração baixa revisões fixadas de SDL3 e Dear ImGui e precisa
de acesso à internet. No Linux, instale também os pacotes de desenvolvimento
do backend de vídeo escolhido, como X11 ou Wayland.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Para MinGW no Windows, acrescente `-G Ninja` ao primeiro comando. O Ninja
deve estar disponível no PATH, junto do compilador.

O executável é gerado em `bin/` ou `bin/Debug/` quando o gerador oferece
múltiplas configurações. Para uma compilação Release, use `Release` no lugar
de `Debug` nos comandos correspondentes.

Para jogar e avaliar continuidade de áudio, prefira **Release**. A emulação
por T-state/dot precisa produzir cada quadro dentro do prazo do console;
uma compilação Debug sem otimizações pode não sustentar tempo real, mesmo
quando todos os testes funcionais passam. As duas configurações continuam
disponíveis em pastas de build separadas.

## Núcleo sem interface gráfica

Esta configuração compila a CPU, o carregamento de ROMs e os sistemas sem
baixar SDL3 ou Dear ImGui. Os testes não precisam de janela ou dispositivo de áudio.

```sh
cmake -S . -B build-core -DR2SEGA8_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core --config Debug --parallel
ctest --test-dir build-core -C Debug --output-on-failure
```

Use pastas de build distintas para configurações com e sem interface, ou
para compiladores diferentes.

## Validação adicional da CPU

Os testes locais são executados pelo CTest. Os vetores SingleStepTests e
os exercitadores ZEXDOC/ZEXALL são opcionais e precisam ser obtidos separadamente.
Os comandos e limites dessa validação estão em [Z80.md](Z80.md).

## Diagnóstico externo do VDP do Master System

O alvo `sms_vdp_diagnostic` executa, sem SDL, os 55 resultados numéricos das
três primeiras páginas do [SMS VDP Test](https://github.com/FluBBaOfWard/SmsVdpTest).
O executor usa NTSC, pulsa o botão 1 e lê os resultados na RAM; 21 é aprovado
apenas quando a ROM identifica NTSC. Os testes 20 e 37–40 não existem nesta
sequência. A página adicional de medições de HCounter não é avaliada.
A ROM e o montador são obtidos separadamente e ficam fora das fontes versionadas.

Para reproduzir a ROM usada nesta medição, em PowerShell, a partir da raiz:

```powershell
git clone https://github.com/FluBBaOfWard/SmsVdpTest.git build/sms-vdp-test-source
git -C build/sms-vdp-test-source checkout 490c00b38c83012dee5a7a582097f080c210a3e7
git clone https://github.com/vhelin/wla-dx.git build/wla-dx-source
git -C build/wla-dx-source checkout e96defaff17b09e6eda90b47cd62f9cb3f05c51d
cmake -S build/wla-dx-source -B build/wla-dx-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wla-dx-build --target wla-z80 wlalink
Push-Location build/sms-vdp-test-source
& ../wla-dx-build/binaries/wla-z80.exe -o object.o testmain.asm
Set-Content -Encoding ascii linkfile "[objects]`nobject.o"
& ../wla-dx-build/binaries/wlalink.exe -d -S linkfile VDPTest.sms
Pop-Location
cmake --build build-core --target sms_vdp_diagnostic
build-core/sms_vdp_diagnostic.exe build/sms-vdp-test-source/VDPTest.sms
```

Configure `build-core` conforme a seção anterior. A ROM gerada tem 32768 bytes
e SHA-256 `353e6d2522ae50a3bdea9ed393698b682e5d32a55e6b98bffa10bb21f23319a5`;
o executor verifica tamanho e FNV-1a antes de usar os endereços específicos
desta compilação. Código de saída 0 significa que todos passaram, 1 que houve
falhas, 2 que a entrada é inválida ou a execução ficou incompleta e 77 que a
ROM não foi encontrada. Um limite
de 1800 quadros evita espera infinita. O CTest registra cada um dos 55 resultados
individualmente, incluindo as falhas conhecidas. Por padrão procura a ROM em
`<pasta-de-build>/sms-vdp-test-source/VDPTest.sms`; configure
`-DR2SEGA8_SMS_VDP_TEST_ROM=<caminho>` para usar outra localização. Sem a ROM,
os 55 testes são marcados como não executados; uma ROM incorreta causa falha.
Cada teste executa a ROM novamente; use `ctest --parallel` para reduzir o tempo.

Medição inicial em 05/11/2026, Windows x64, Release: 45 passaram e 10 falharam.
Após implementar o latch TH e o contador horizontal, a mesma ROM passou em
**49 testes e falhou em 6**: 25 (instante de mudança do VCounter), 32 e 34
(instante das interrupções de quadro e linha), 35 (instante do flag VINT),
46 (colisão fora da área visível) e 52 (instante de colisão). Os testes 23,
24, 30 e 60 passaram nesta medição. A fase exata do contador e dos eventos
do VDP continua sujeita a investigação; esses resultados não são uma pontuação
geral de precisão ou validação dos demais consoles.

Após o ajuste dos instantes de HBlank, VCounter e VINT, a mesma execução
registra **51 aprovados e 4 falhas**. Os testes de temporização 25, 34 e 35
passaram; o teste 32 (fase da interrupção de quadro) continua falhando. Os
testes de sprites 46 e 52 também falham e o teste 60, que antes passava,
passou a falhar após a alteração de fase. A ordem e fase dos eventos continuam
aproximadas e exigem mais comparação com hardware; o VDP é compartilhado pelo
Game Gear, que não foi validado por essa ROM. Testes locais verificam os novos
limites como regressão, mas não são uma referência independente de hardware.

Nova medição após alinhar o flag de overflow à janela do VINT: **52 aprovados,
3 falhas**. O teste 60 voltou a passar; permanecem os testes 32, 46 e 52. No
segundo trecho do teste 32 a ROM espera o HCounter `84` depois da interrupção
de quadro e o emulador registra `81`; isso ainda não identifica por si só se
a diferença está na fase da IRQ, no reconhecimento pelo Z80 ou em ambos.

Após ajustar a fase da colisão no perfil Master System, a ROM registra **53
aprovados e 2 falhas**: 32 e 46. O teste 52 passa com uma aproximação de dez
dots entre o pixel processado e o latch da colisão; a posição visual é mantida.
Essa aproximação ainda não modela uma fila de pixels nem alterações de
registradores durante esse intervalo e não foi validada no Game Gear.

Após ajustar o VBlank do Master System, a ROM registra **54 aprovados e
1 falha**. O teste 32 lê `$82` e `$84` depois das duas IRQs e o teste 35
passa nas leituras antes e depois de o bit de status aparecer. Na origem do
raster do emulador, o bit é atualizado no dot 317 e a IRQ fica pendente no
dot 318 se o bit ainda estiver ativo. Uma leitura da porta BF entre os dois
eventos reconhece o VBlank e evita que o mesmo evento acione INT. Uma ROM
sintética verifica os dois caminhos, inclusive ao restaurar o estado salvo
entre o latch e a IRQ.

### Investigação do caso 46

O teste 46 coloca dois sprites no Y bruto `$F0`, marca o fim da tabela de
atributos dos sprites (SAT) com `$D0` e envia 256 bytes ao VDP. A rotina só
reescreve os atributos dos dois primeiros sprites. Ela também reativa a tela
em `R1`, lê `$BF` para limpar os flags e aguarda o contador vertical passar
por `$11` e depois `$10` antes de ler o resultado.

Nos [experimentos em hardware de JoppyFurr e enik2](https://www.smspower.org/forums/19415-OffScreenSpriteCollisionDetectionBehaviour),
o simples movimento de sprites pela borda no SMS PAL deixou `COL` zerado.
Já o caso 46 foi sensível aos bytes `SAT+$84` e `SAT+$89`, herdados da sequência
anterior, e ao momento em que a tela era habilitada. Uma
[captura do barramento de um Game Gear VA5](https://www.smspower.org/forums/10695-SMSVDPTester?start=50)
identificou leituras de refresco da SAT durante o apagamento em Mode 4. É uma
pista para a origem dos dados que chegam ao circuito de colisão. A captura
ainda não determina como esses dados acionam `COL` no Master System.

Para observar a sequência exata no emulador, use a ROM identificada acima.

```powershell
build/sms_vdp_diagnostic.exe build/sms-vdp-test-source/VDPTest.sms --trace-46
```

No perfil NTSC, a tela é reativada na linha 168, dot 0, com `SAT+$84=$70`
e `SAT+$89=$41`. A leitura de limpeza devolve `$00` no dot 37 da mesma linha.
A leitura final ocorre na linha 16 do quadro seguinte e devolve `$80`
(VBlank com `COL` zerado). As posições do trace são registradas ao final do
T-state emulado.

Em uma compilação experimental, estender a detecção normal de colisão do
Mode 4 às linhas fora da área ativa acionou `COL` na linha 243, dot 138. O
diagnóstico externo registrou 55 aprovados. O teste de controle do núcleo
também passou a sinalizar colisão entre sprites inteiramente fora da área
ativa com a tela já ligada, em desacordo com as observações em hardware.
O núcleo mantém a detecção atual e o CTest continua registrando a falha 46.

Para isolar o mecanismo em hardware, o experimento é medir o bit `$20` de
`$BF` após habilitar a tela, variando apenas um byte da SAT por execução e
depois variando a fase da escrita em `R1`, com a revisão do VDP identificada.

O estado do Master System agora é gravado na versão 2 para preservar a porta
de controle e o valor capturado do HCounter. Estados de versão 1 continuam
legíveis, com as linhas configuradas como entradas e o latch inicializado em
zero; versões anteriores do emulador não leem os novos estados.
