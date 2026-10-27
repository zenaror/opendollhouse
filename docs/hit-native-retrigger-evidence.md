# Banco fresh 50–62 e atualização ativa sem reset

Evidência estática de 2026-10-15, base ODH `84a66b2`. O banco global nativo
50–62 tem defaults explícitos; `interrupt`55 começa em zero. Uma chamada
admitida para controller ativo, não pausado e com esse valor zero atualiza
argumentos e controles sem passar pelo reset de PC, bancos ou binding de
patch. Esse é um ramo específico, não um contrato completo de retrigger.

## Fonte e método

Imagem externa `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Base PE `0x400000`; nos trechos `.text`/`.rdata` selecionados, offset de
arquivo = VA − `0x400000`. Os globais `0x6f....` são endereços de runtime,
sem offset de arquivo inferido. Nenhum original foi executado ou alterado;
nenhum projeto Ghidra ou referência foi modificado.

Foram lidos os exports existentes de `0057dc59`, `0057d6f8`, `0057d967` e
`0057376b`, confirmados por leitura pontual de assembly. Seleção com bytes,
VAs, offsets e vtable em registro arquivado não distribuído.
Complementa [identidade/ciclo](hit-native-track-identity-evidence.md),
[controles iniciais](hit-native-controls-evidence.md) e
[contribuições](hit-native-source-contributions-evidence.md).

O HSM externo `SoundData/SimsGeneratedHitSource.hsm`, SHA256
`ca6bef035809b2cdacdc2c14a98582cd4c4702c53cc6640beceaa548c06d55b6`, nomeia
patch50, tempo52, target53, interrupt55 e Spl57. Nomes são apoio ao tracing;
não ampliam os intervalos comprovados dos bancos.

## Defaults completos e overlays do controller

Startup `0x57ca49` zera EBX em `0x57ca4e`. `0x57cacb–0x57cad4` chama
`0x580664(count=13,start=50)` sobre o banco global `0x6f6848`.
O header em +`0x0c`, endereço `0x6f6854`, recebe o **ponteiro** do payload
inline em +`0x1c`, isto é, `0x6f6864`. Não confundir o endereço do ponteiro
com o primeiro valor do banco. O construtor inicialmente preenche as 13
palavras com `INT32_MIN`; as escritas seguintes do startup fixam cada default.

| ID | Nome HSM | Default global | Store VA / offset |
|---:|---|---:|---|
| 50 | patch | 0 | `0x57cc16` / `0x17cc16` |
| 51 | WhatToDoWithUpdate | 0 | `0x57cc1c` / `0x17cc1c` |
| 52 | tempo | 120 | `0x57cc22` / `0x17cc22` |
| 53 | target | 0 | `0x57cc2c` / `0x17cc2c` |
| 54 | mutegroup | 0 | `0x57cc32` / `0x17cc32` |
| 55 | interrupt | 0 | `0x57cc38` / `0x17cc38` |
| 56 | IsPositioned | 0 | `0x57cc3e` / `0x17cc3e` |
| 57 | Spl | 20 | `0x57cc44` / `0x17cc44` |
| 58 | MultipleInstances | 0 | `0x57cc4e` / `0x17cc4e` |
| 59 | AssociatedTrack1 | 0 | `0x57cc54` / `0x17cc54` |
| 60 | AssociatedTrack2 | 0 | `0x57cc5a` / `0x17cc5a` |
| 61 | AssociatedTrack3 | 0 | `0x57cc60` / `0x17cc60` |
| 62 | AssociatedTrack4 | 0 | `0x57cc66` / `0x17cc66` |

O construtor do controller `0x57d6f8` cria outro banco 50–62 em
`this+0x1d4`, também inicialmente sentinela. Seu payload começa em
`this+0x1f0`. Ele escreve apenas dois campos nesse intervalo a partir dos
argumentos HOT: patch em `this+0x1f0` (`0x57d794`) e Spl em `this+0x20c`
(`0x57d79d`). O parser passa Spl e patch em `0x57bb1e` e `0x57bb1c`; o
campo de hitlist usado para resolução do entry point é separado, +`0x31c`.
Volume18 e duckpri25 pertencem ao outro banco e não substituem interrupt55.

O controller liga o fallback +`0x14` desse banco ao global `0x6f6848`
(`0x57d777`). O getter de metadata +`0xb4` chama `0x5805ba` a partir de
`this+0x280`, cujo next-bank chega a `this+0x1d4`. O getter segue fallback
quando encontra `INT32_MIN`. No initializer fresh, o loop de
`0x57dd33–0x57dd53` copia o global para o banco de execução `this+0xcc`;
`0x57dd98–0x57ddb6` reaplica os valores efetivos do controller.

Consequências comprovadas no perfil fresh de startup/HOT:

- patch bruto `INT32_MIN` permanece na metadata, mas ID50 efetivo é **0**.
- Spl bruto `INT32_MIN` permanece na metadata, mas ID57 efetivo é **20**.
- patch/Spl zero são valores explícitos; Spl zero não vira20.
- Os demais IDs51–62 mantêm os defaults acima, inclusive interrupt55=0.

A hipótese anterior de conservar `INT32_MIN` como ID50 inicial deve ser
corrigida por essa prova do fallback. O builder pode guardar separadamente
os bits brutos da metadata. Se `HotTrack.sound_pressure_level` estiver ausente
ou não resolvido, não há valor raw inferível: o perfil completo deve recusar
ou manter explicitamente desconhecido antes de qualquer chooser/backend.
Não substituir ausência por20 como se fosse uma sentinela presente.

Esses defaults valem para o estado fresh aqui traçado. Não afirmam que um
controller já usado ou configurado externamente ainda tenha55=0. O ramo ativo
consulta o valor **corrente** de55, não a tabela de startup. Não é necessário
inventar uma interface de overrides arbitrários50–62 para o perfil HOT fresh.

### Limites dos bancos e identidade de patch

Em `0x57da1a`, os bancos locais são 17–47 (`this+0x1c`), 50–62
(`this+0xcc`) e 1–16 (`this+0x178`), encadeados nessa ordem. O último segue
para o banco global113–127, que segue para90–113. Na cadeia fresh, IDs48/49
e63–69 não têm intervalo correspondente. A cadeia termina com next nulo;
o getter/setter genérico não fornece um default zero para IDs fora das faixas.
O HSM nomear AssociatedTrack5–8 em63–66 não prova armazenamento válido nessa
imagem. Permanecem fora do perfil suportado, sem ampliar esta investigação.

ID50 representa o valor de binding corrente, não a escolha antecipada de
hitlist do host. `0x57de4d–0x57de60` obtém ID50 e chama `0x57f91e`, que grava
o valor em `this+0xe8` (`0x57f97b`), exatamente a posição de ID50 no banco
de execução. Se zero, não busca/cria objeto de patch; se não zero, resolve
o objeto e faz binding. Um perfil cujo patch raw é zero ou sentinela pode
portanto ter ID50=0 mesmo que a seleção diagnóstica ODH tenha escolhido um
patch de hitlist. Não substituir ID50 pelo resultado do chooser para igualar
essas duas representações, nem adicionar overrides que contradigam o plano
de seleção sem contrato próprio.

## Atualização ativa, não pausada, interrupt55=0

Condições externas continuam necessárias: evento admitido pelo serviço,
controller existente, ArgStyle2 e fonte válida para o caller, além do gate
de grupo do initializer. Grupo desabilitado pode retornar antes da sequência
abaixo (`0x57dc62–0x57dc9e`). O dispatcher já pode ter alterado contribuições
antes desse retorno; uma primitiva de estado não implementa todos esses gates.

Passado o gate, a ordem em `0x57dca5–0x57dd2d` é:

1. Limpa o byte +`0x1cf` e fade_on45 diretamente em `this+0xa8`.
2. Faz `this+0xc8=this`, restaurando o provider da própria thread.
3. Escreve R1, R2 e R3 pelos writers públicos +`0x84`, nessa ordem, com
   terceiro argumento zero. Os IDs1–3 não executam os side effects de patch
   ou de controles espaciais do writer.
4. Verifica ativo; o ramo pausado tem outro caminho. No ramo não pausado
   escreve zero em +`0x1c8`, consulta55 pelo getter público e, se zero,
   salta diretamente para o retorno em `0x57ded2`.

O salto evita todos os loops de reset, os stores ArgStyle2 de
`0x57de08–0x57de27`, o binding `0x57f91e`, a resolução de entry point,
os resets PC/loop/listas e o store isplaying46. Portanto mantém PC corrente,
estado de execução e notas existentes; não chama chooser nem executa NoteOn
nessa atualização. Preserva especificamente source47, patch50 e os outros
bancos, exceto45 e os três argumentos já descritos. R1 pode mudar enquanto
source47 continua com a origem anterior. A nova tripla não deve passar pelo
builder fresh, que sobrescreveria47/46 e outros valores.

No caller tipo1/ArgStyle2, `0x5738e7–0x5738ff` aplica extvol19 e depois
pan20, com setters `0x57d873` e `0x57d899`. Esses setters chamam o writer
público; este estabelece zero/positive/sign em +`0x1cc/+0x1cd/+0x1ce`
(`0x57e199–0x57e1be`) e chama a atualização dos controles do objeto de áudio
para19/20. Essa rotina lê variáveis e aplica controles ao áudio já existente;
não chama binding de patch nem modifica os flags da thread. Assim, após
o último setter, zero=(pan==0), positive=(pan>0), sign=(pan<0).
Sem os setters pós-espaciais, o último writer dessa sequência teria sido R3.

Ativação +`0x5c` em `0x57df5c` ainda limpa pausa, pode retomar o objeto de
áudio, grava deadline com o Clock corrente e reinsere a thread na fila por
`0x57d310` (remove ocorrência anterior antes de inserir). Não altera os flags
de interpretação nessa rotina. Logo "sem reset de PC" não significa ausência
de efeitos no scheduler, no áudio ou no deadline. Não converter Clock em
polls ODH/World nem afirmar implementação desse lifecycle pela atualização
portátil dos valores.

### Provider próprio não é sinônimo de target53 zerado

Getters/writers públicos delegam para `this+0xc8` quando esse ponteiro aponta
para outro objeto, exceto para ID53 (`0x57e10f–0x57e18e`). Escrita normal em
53 pode resolver outro controller e instalar esse provider (`0x57e311`).
O retrigger força o ponteiro de volta à própria thread **sem zerar ID53**.
A consulta de55 que decide o ramo já é local depois dessa mudança. Não
inferir valor53=0 a partir do provider próprio, nem que um valor53 residual
continue indicando delegação ativa após retrigger.

O ODH ainda não implementa esse provider compartilhado. Uma API limitada ao
perfil próprio deve declarar essa restrição e recusar domínios que exigiriam
a semântica ausente; conservar apenas um número53 não emula o redirecionamento.

## Fronteira de implementação sustentada

Há prova suficiente para ampliar o builder fresh com50–62, metadata Spl
explícita e sentinelas de patch/Spl; e para uma primitiva separada que, sob
precondições explícitas, atualize R1–R3,45,19,20 e flags sem reinicializar
PC, source47, patch50, chooser, handles ou budgets. Recusar perfil não nativo,
55 desconhecido/não zero, estado incompatível ou provider não suportado é
mais preciso que simular os outros ramos. Falhas de validação devem ocorrer
antes de mutação, como política portátil ODH, sem atribuí-la ao original.

Essa primitiva não completa admissão, contribuição de fontes, pausa/retomada,
restart quando55!=0, grupos, Clock, controles físicos do backend ou binding
World por geração. Atualizar o estado raw e preservar ownership existente não
autoriza alegar que o áudio ou a VM já reproduzem todo o retrigger nativo.
