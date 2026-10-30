# Revisão do Z80 — 30/10/2026

Primeira rodada de revisão da CPU NMOS, iniciada no commit `acd2240f0c2f`.
O VDP não foi alterado nesta rodada. A revisão combina leitura do código,
documentação oficial, testes dirigidos de interação entre ciclos e comparação
com vetores externos. Não constitui certificação elétrica de um Z80 físico.

## Fontes e critérios

- [Zilog Z80 CPU User Manual, UM008011-0816](https://www.zilog.com/docs/z80/um0080.pdf):
  instruções, flags documentadas, M-cycles, interrupções, WAIT e BUSREQ.
- [Zilog Z80 CPU Peripherals, UM008101-0601](https://www.zilog.com/docs/z80/um0081.pdf):
  identificação de RETI pela cadeia de interrupções, seção Return from Interrupt
  Cycle e figura 13, páginas impressas 29–30.
- [Zilog Product Specification PS017801-0602](https://www.zilog.com/docs/z80/ps0178.pdf):
  referência complementar dos sinais NMOS/CMOS. Não se aplicam timings ou
  extensões de instruções de Z180/eZ80 a este núcleo NMOS.
- [SingleStepTests/z80](https://github.com/SingleStepTests/z80): comparação
  complementar de registradores, flags, memória, portas e amostras de barramento.
  Os bits não documentados e latches WZ/Q/P não são certificados pelo manual.

Um resultado final correto não basta para validar a interação com DMA ou com
periféricos durante a instrução. Por isso, as regressões observam também quando
ocorrem as transferências e as notificações externas.

## Problemas reproduzidos e corrigidos

### CPU-01 — BUSREQ nos ciclos internos

Os T-states internos eram representados por uma única operação `idle`, que
prolongava o ciclo anterior. Isso não distinguia uma extensão daquele ciclo
de um M-cycle interno independente. BUSACK podia ser adiado até depois de
limites nos quais o manual permite a cessão do barramento.

Reprodução mínima: `ADD HL,BC`, com BUSREQ solicitado durante M1. A divisão
documentada é `4 + 4 + 3`; a CPU precisava ceder o barramento após T4, antes
do primeiro ciclo interno. O teste falhava com BUSACK ausente nesse limite.

A implementação agora distingue:

- `idle`: extensão do M-cycle atual; não aceita BUSREQ no meio dele;
- `internalCycle`: M-cycle interno independente, com limite próprio para
  BUSREQ e sem prolongamento por WAIT.

Foram revisados os ciclos internos de somas de 16 bits, desvios relativos,
indexação, RRD/RLD, comparações e repetições em bloco. Extensões de ciclos de
PUSH, CALL, INC/DEC de pares, acesso à memória e I/O continuam agregadas ao
ciclo correto. A quantidade total de T-states e o comportamento sem DMA
permanecem iguais.

Teste: **311 posições de solicitação em 26 padrões de instrução**, verificando
ausência de BUSACK antecipado, ausência de transferências durante BUSACK e
retomada com os mesmos registradores, memória e efeitos externos da referência.

### CPU-02 — RETI notificado após a pilha

O callback `Z80Bus::reti()` ocorria no fim da instrução, depois das duas
leituras da pilha. Os periféricos Z80 identificam a sequência de opcodes
antes dessas leituras, conforme a figura 13 de UM0081.

A notificação foi movida para a decodificação de `ED 4D`, ao concluir a segunda
busca M1. Na convenção por T-state do emulador isso ocorre após T8, em vez
de T14, para RETI sem prefixos ou esperas. O retorno continua levando 14 T-states.
Não se pretende reproduzir a borda elétrica de meio clock do periférico.

Os testes verificam a notificação antes da pilha, BUSREQ imediatamente depois
da decodificação, restauração de snapshots anteriores e posteriores ao evento
e ausência de notificação para RETN e aliases que não apresentam `ED 4D`.

### CPU-03 — setState durante uma sequência de prefixos

Após um lote de 256 prefixos DD/FD, `step()` cede o controle, mas a instrução
permanece pendente. `setState()` verificava apenas a atividade do sequenciador
e aceitava substituir registradores nesse ponto, contrariando seu contrato.

Agora o método exige `instructionBoundary()`. A instalação interna do estado
foi separada para que `restore()` continue aceitando snapshots de qualquer
fase, inclusive a continuação dos prefixos. O teste antigo de prefixos foi
ajustado para configurar IFF/EI antes de iniciar a sequência e usar Snapshot
ao restaurar uma execução incompleta.

### TEST-01 — validação externa vazia aceita como sucesso

Um filtro sem correspondências produzia `0 vectors; 0 files with failures`
e código de saída zero. O executor Python agora rejeita execuções vazias,
origens inexistentes e limites negativos. O filtro inexistente foi reproduzido
antes da alteração e passou a retornar erro de argumentos depois dela.

## Cobertura desta rodada

| Área | Evidência |
| --- | --- |
| ULA, cargas, pilha, desvios, CB/ED/DD/FD/DDCB/FDCB e flags | Leitura da implementação, testes locais e nova execução dos vetores externos. |
| M-cycles e BUSREQ | 311 casos dirigidos, incluindo extensões que não devem ceder o barramento antecipadamente. |
| IM 0 | Os 256 opcodes são comparados à execução normal, com dois T-states extras e operandos restantes obtidos da memória. |
| NMI/INT/EI/DI | Prioridade com BUSREQ, retenção de borda NMI, NMI aninhada, RETN e EI consecutivos seguidos de DI. |
| RETI | Ordem temporal, DMA entre decode e pilha, snapshots e aliases. |
| WAIT e snapshots | Escrita de memória, IN, OUT, IM 1 e NMI; esperas de 1, 3, 7 e 17 T-states, sem efeitos antecipados ou duplicados. |
| API e prefixos | Fronteira real de instrução, rejeição de setState e restauração de prefixos pendentes. |

Os novos grupos são `z80_busreq_tests`, `z80_reti_timing_tests`,
`z80_control_tests` e `z80_wait_snapshot_tests`, além de `z80_tests`.
Executam sem SDL ou ROMs comerciais.

Resultado final desta rodada:

| Configuração | CPU | Suíte completa |
| --- | --- | --- |
| Windows Debug | 5/5 grupos passaram | Não repetida integralmente nesta configuração. |
| Windows Release | 5/5 grupos passaram | 17/19; as duas falhas de vídeo listadas abaixo. |
| Linux/WSL Debug, ASan + UBSan | 5/5 grupos passaram, sem diagnóstico dos sanitizers | 14/16; as mesmas duas falhas de vídeo. |

O executável em `bin/` foi novamente vinculado com a configuração Release
após a validação Debug, pois ambas as pastas de build usam esse destino.

O arquivo externo local contém **1.604.000 vetores em 1.604 arquivos** e foi
comparado antes e depois das correções, sem divergências. Para identificar
exatamente o material reutilizado, o SHA-256 de `singlestep.zip` é:

```text
9a635d9361e3358575996cd5ae7bda9b897be43f431e41ecc5fa08b9c9b73fca
```

O ZIP local se apresenta como `z80-main`; esta execução não consulta o HEAD
remoto nem deduz uma revisão Git a partir do nome do arquivo. A revisão
registrada na validação anterior permanece como informação histórica em Z80.md.

O executor `tests/Core/RunZ80Vectors.py` e `docs/Z80.md` são arquivos locais
ignorados pelas regras atuais do repositório. Seus ajustes locais também foram
verificados; as regras de ignore foram preservadas. Este relatório e os novos
testes C++ podem ser versionados normalmente.

## Falhas de vídeo preexistentes

A suíte geral revelou duas falhas que também foram reproduzidas em uma
compilação Release separada, com GUI desabilitada, extraída diretamente do
commit anterior às alterações da CPU:

- `tms9918_sprite_tests`: `First overflow remains latched and rejected sprites cannot collide`.
- `sg1000_vdp_tests`: `CPU-driven background frame differs from expected pixels`.

Essa reprodução separa as falhas das alterações desta rodada; não determina
se a causa está no VDP ou nas expectativas dos testes. Elas permanecem
pendentes para a revisão específica do vídeo. Não foram alterados o VDP nem
os respectivos testes para fazer a suíte passar.

Atualização da rodada de vídeo: a causa foi identificada como expectativas
do limite de quatro sprites com a preferência ilimitada habilitada por padrão.
Os testes agora escolhem explicitamente o perfil necessário; a suíte voltou
a passar. Os detalhes estão em [TMS9918Audit.md](TMS9918Audit.md).

## Limites e trabalho posterior

- O barramento usa amostras por T-state, não bordas elétricas de meio clock.
- RESET é uma operação da API; não há simulação de largura do pulso físico.
- O manual não especifica todas as combinações de instruções e sinais nos
  intervalos analógicos mínimos. Os vetores usados também não exercitam todos
  os pulsos possíveis de INT/NMI/WAIT/BUSREQ.
- ZEXDOC/ZEXALL não foram reexecutados integralmente nesta rodada. Os registros
  anteriores são da etapa semântica anterior à migração por T-state.
- Uma próxima rodada pode ampliar a captura de sinais em hardware. Passar nos
  testes descritos não significa ausência de quaisquer outros bugs.
