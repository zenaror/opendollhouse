# Main da cadeira, espera e conforto — 2026-10-13

O host Linux agora executa duas VMs no mesmo World: a interação do avatar e a
Main4096 da cadeira barata. A notificação49 realmente interrompe a espera da
cadeira; o BHAV aumenta Comfort6 usando as constantes BCON do recurso original.
É uma cena construída de teste, sem queue geral, RNG real, decaimento, rendering
ou confirmação da temporização pelo executável original.

## Leitura e estado explícitos

BCON contém byte de contagem, byte de flags e palavras LE16 sem conversão
assumida de sinal. O leitor valida a extensão antes de alocar, aceita contagem
zero e preserva a extensão restante. A contagem limita cada tabela a255 palavras.
Na Main, scope26 suporta somente tuning privado: data uint16 menor que8192,
tabela4096+(data>>7), índice data&127. Palavras são convertidas explicitamente
para int16. Tabela/índice ausente, escrita, semiglobal/global/OTF são unsupported;
não se usa fallback zero.

DiningChairs.iff BCON4097 tem flags128 e12 valores:
`1,2,3,4,1,1,20,30,40,60,30,30`. No caminho barato, tuning128 incrementa1
e tuning134 compara o conforto com20. A condição do BHAV é <=20; não é uma
regra inventada de clamp em20. A execução validada começa em0 e termina em3.

World acrescenta bancos explícitos de até16 motivos para avatares, scopes14/15
e escrita limitada ao default da referência: [-100,max(valor anterior,100)].
Não há TuningCache, inicialização real do Sim ou decaimento. Os bancos e palavras
de definição contam no orçamento existente; remoção libera a contagem e reuso
de ID cria geração nova. Scope21 é somente leitura de palavras declaradas,
índices0..255. Não deriva informações ausentes de EntityKind.

O probe declara explicitamente no avatar1 definição9=2, classificação de pessoa
da referência, e conforto6=0. Essa declaração é construída; não afirma leitura
de um OBJD real de avatar. Main usa MySlot0 para selecionar o ocupante e seu
ObjectType, enquanto o teste32 do caminho de conforto usa a identidade GUID
da cadeira já fornecida.

## Sleep e contrato da VM

Primitiva0 interpreta os primeiros dois operandos como índice int16 de Args.
Subtrai1 inicialmente e publica Args antes de WAIT. Ao avaliar uma espera,
subtrai elapsed apenas se idleStart!=0 e idleStart<clock; caso contrário subtrai1.
Conversões e subtração preservam wrap16 sem overflow C++.
Interrupção limpa a flag e retorna true sem solicitar RNG. Resultado negativo
natural solicita uma chamada RNG e retorna true. Caso contrário agenda
clock+remaining+1. O sentinela0 é preservado: Args3 no clock0 acorda no3,
subtrai1 novamente e termina no5. Não se corrigiu esse detalhe da referência.

`Sleep` tem endereço estável, uma VM ligada, um pedido e clock limitado auint32.
Ticks iniciais0/1 e posteriores consecutivos são aceitos; gaps, repetições e
overflow são recusados. O mesmo ticket permanece nas reavaliações. Elas leem
Args atual, usam `update_pending` validado e não contam como novo dispatch BHAV.
Essa adaptação assíncrona difere da contagem de instruções do scheduler FreeSO.
`PrimitiveOutcome.write` agora é permitido em WAIT; escrita inválida causa fault
antes de criar pending. Unsupported não aceita output; tickets/bancos/índices
inválidos não alteram o estado.

Restart invalida espera e flag antigas. Remoção/reuso do caller cancela o ticket
antigo por complete(false), sem escrita/RNG/efeito na entidade nova. É política
ODH de cancelamento, não semântica comprovada de morte do original. Dead caller
e retorno true no próximo tick continuam ausentes. Callbacks não podem reentrar
nos controladores/VM. Na conclusão natural imediata, callback RNG precede a
publicação de output pela VM; na conclusão agendada, output é publicado antes.
Callbacks que inspecionam Args não têm equivalência estabelecida entre esses casos.

No CLI, callback RNG apenas conta solicitações. JSON informa rng_backend=none
e rng_cycles_semantics="requested advances only"; não existe PRNG ativo.

## Host de duas VMs

`--main-probe` exige `--stand-after`; `--hold-ticks N` exige Main e aceita0..4096.
Main começa antes da interação, em clock0. A cada tick, Sleep da cadeira avança
antes de rota/playback e execução do avatar; Main é retomada depois dessa VM.
Notify49 interrompe Sleep imediatamente no clock atual, mas não executa Main
dentro do callback do avatar. Isso modela o caso fixo avatarID1/cadeiraID2, em
que o scheduler de referência pode reagendar o objeto de ID maior no mesmo frame.
Não estabelece um scheduler genérico para outras ordens, entidades ou threads.

Hold avança os controladores/Main entre sentar e Stand; post-ticks avança Main
também após a fase final. VMs terminais não são executadas de novo. Rotinas são
copiadas imutáveis para a segunda VM. Main tem trace próprio limitado e falhas
explícitas; scene_status e exit3 sinalizam falha/orçamento da Main mesmo quando
o avatar retorna true. total_steps inclui ambas VMs/fases e tem limite10000.
Não são dois orçamentos independentes. Trace do avatar conserva phase/total_step.
Seleção Main ausente/zero/não privada é erro de entrada; metadata BHAV ausente
é fault da VM.

## Validação

Fixtures sintéticas cobrem Sleep/sentinela/wrap16/pending/reentrada/cancelamento,
motivos/definição/budgets/scopes, BCON/truncamentos e integração Main, interrupção,
hold, tuning ausente, erros e limite compartilhado. Sleep tem98.358 checks,
majoritariamente ticks consecutivos dos limites int16; não são98.358 cenários
independentes. Formats399, World80, Services97 checks sintéticos.
CTest20/20 passou em Debug e ASan/UBSan. O probe nativo sanitizado também
retornou0, sem stderr, e seu JSON é byte a byte igual ao Debug. LSan permanece
desativado (detect_leaks=0) pela limitação ptrace; não se afirma validação de leaks.

Com os recursos comerciais externos e a mesma cadeia de
[stand-validation.md](stand-validation.md), acrescentar:

```sh
--main-probe --hold-ticks 120
```

Probe Debug: exit0, stderr vazio, ambas fases true. Avatar56 dispatches
(29 sentar/169ticks,27 Stand/133ticks); Main38; total94. Hold120,
execution422ticks +post240 =662. Notificação da cadeira: notified.
Conforto inicial0, fim de sentar1, final3; cadeira termina waiting.
Sem hold, outro probe retorna conforto1, Main16 dispatches, total72 e542ticks.
O avatar final está em(48,64,1,dir6), sem recipiente e postura0. O loop final
continua a2o-sit-loop por reset antes de Posture0, como no checkpoint anterior.

Relatórios locais ignorados: registro arquivado não distribuído,
registro arquivado não distribuído e versão sanitizada. Inputs permanecem os hashes
anteriores de DiningChairs/Global/BCFs; nenhum asset comercial entra no Git.

## Referências e lacunas

FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73, MPL2.0 declarada:
[VMSleep.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMSleep.cs),
[VMScheduler.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMScheduler.cs),
[VMAvatar.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs),
[VMMemory.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMMemory.cs),
[BCON.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/BCON.cs).

Faltam queue/dispatch Function20/OBJf, scheduler geral, RNG real, motivos com
tempo/decay e retomada funcional de rota, além de skeleton/compositor Linux,
áudio/HIT/lote/persistência. Sem execução do original ou hardware; Linux
funcional completo continua sendo a meta ativa, com ports posteriores.
