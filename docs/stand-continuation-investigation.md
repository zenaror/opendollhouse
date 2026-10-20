# Investigação da continuação após Stand — 2026-10-20

## Achado ODH

O avatar do probe termina fora da cadeira com `Posture=0`, mas conserva o
`sit-loop`. A causa está na sequência do host e do recurso: o BHAV Stand4102
executa `Animation` opcode44/ID0 antes de gravar `Posture=0`; o controlador
resolve o reset imediatamente a partir de `person_data[0]` e, nesse momento,
seleciona a animação sentada. Depois da rotina terminal, os post-ticks continuam
Playback e a Main da cadeira, mas não retomam a VM terminal do avatar. O
renderer usa o clipe que Playback fornece; ele não escolhe uma pose por conta
própria.

Isso explica a saída documentada em
[stand-validation.md](stand-validation.md) e
[live-view-validation.md](live-view-validation.md), sem justificar uma regra
inventada que force standing quando `Posture` muda ou quando Stand termina.
Essa regra esconderia a ordem dos dados e não representaria a continuação de
rota/queue.

## Referência Simitone

Checkout local `refs/upstream/Simitone` no commit
`4906db3156616bfaffc10ce0963cf08062975fef`; submódulo FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`. Em
`Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs`, o caminho TS1
inicializa World, VM e controle de lote e chama `vm.Update()`. No código ativo
TS1 de `FreeSO/TSOClient/tso.simantics`, reset também escolhe a animação com a
postura observada naquele instante (`Primitives/VMAnimateSim.cs`); a atribuição
de PersonData não instala um clipe de pé (`Entities/VMAvatar.cs`).

A continuação é parte do ciclo da VM: uma stack vazia retorna a Main
(`Engine/VMThread.cs`); ao retornar true de Stand, `VMRoutingFrame` volta a
`INITIAL`, normaliza postura sentada para zero e continua o plano, podendo
iniciar caminhada. O fim da curva de caminhada instala `WalkAnimations[3]`.
Isso é referência de lifecycle, não prova de que todo retorno Stand dispara
imediatamente um reset de animação específico nem prova da temporização do EXE
original. “Function20” em notas anteriores deve ser lido como opcode20 / Run
Functional Tree, com Function9 → entrypoint27/Stand; não é uma função de índice
20.

## Identidade do original e limites

A análise estática local foi limitada a leitura do arquivo
`../referencias/The Sims/Sims.exe.bak`; SHA256 atual:
`32857c25e00594a9bc5d700f7b373b6df59ab76ec51b36aa1d30f3dd973eff58`.
Identidade PE: i386, ImageBase `0x400000`, 4.422.663 bytes. Esse hash difere
de `6e598c…` em relatórios anteriores; VAs/RVAs desses relatórios não foram
transferidas para este arquivo. O mapeamento RVA→raw foi conferido por seção.

As rotinas/callbacks exatos do original para retorno Stand, escrita de Posture
e reset idle seguem desconhecidos. A ausência de strings específicas não
prova ausência de código. O EXE não foi executado e esta investigação não
afirma equivalência com o original.

## Host permanente e próxima integração

`odh-person` já mantém uma VM Main permanente, `Sleep`, `InteractionQueue` e
Playback no mesmo relógio; fila, espera, conclusão e retomada no tick seguinte
estão documentadas em [person-host-validation.md](person-host-validation.md)
e [interaction-validation.md](interaction-validation.md). A evidência de
Main/gestos chegou a 669 instruções/296 ticks e parou em um serviço de som
unsupported, conforme [hand-gesture-validation.md](hand-gesture-validation.md).
Isso corrige o handoff anterior: não precisamos criar outro scheduler para
representar essas transições.

O probe da cadeira `odh-bhav` ainda inicia Sit/Stand por `Vm::start` e termina
o avatar ao fim da fase. O resultado sentado nele descreve esse probe isolado;
não demonstra que a sessão permanente de `odh-person` não continue Main. O
host permanente, por sua vez, ainda não possui posição/rota de lote ou
renderer ligado ao Main. A próxima unidade é conectar o clip/CFP selecionado
por Playback ao `SceneRenderer`, preservando identidade e ownership dos
recursos e usando a sessão existente.

Contrato CFP/renderer implementado em
[scene-cfp-provider-validation.md](scene-cfp-provider-validation.md), em Linux
headless com FAR/BCF/CFP sintéticos. A revisão independente passou os gates
Debug e ASan/UBSan; isso ainda não liga `odh-person` ao renderer.

Matriz coberta para o contrato CFP/renderer:

| Comportamento | Casos mínimos | Ambiente e limite da evidência |
|---|---|---|
| Compatibilidade do renderer atual | Um FAR contém header e CFP; saída RGB/depth permanece idêntica | CTest sintético, sem alegar equivalência ao jogo |
| CFP em FAR separado | Header vem de uma fonte e CFP de outra; render deve igualar o caso fixture com ambos no mesmo FAR | Resolver explícito pela identidade origem/entry, sem busca ambígua por nome |
| Falha de recurso | Source/entry/CFP ausente ou ambíguo falha sem publicar estado parcial | Fixtures de erro e atomicidade |
| Ownership e orçamento | Handle compartilhado entre Playback/renderer não duplica planos ou cobrança e respeita limites | Caches/handles limitados, sem medir RSS ou orçamento de portátil |
| Continuação de Main | Espera, ação true/false, remoção/reuso, budget e fault permanecem por tick | Gates headless do host; não prova scheduler completo do original |

Depois dessa unidade, integrar posição/arquitetura de lote e janela; então
validar interação com cadeira via o Main contínuo em vez do restart do probe.
Recursos comerciais devem permanecer externos e identificados separadamente.

| Comportamento | Casos mínimos | Ambiente e limite da evidência |
|---|---|---|
| Reset e continuação de animação | Reset com postura1, depois escrita0; clipe só muda após nova instrução válida | Teste portátil; não prova rotina original |
| Retorno de ação | Stand true retoma o chamador e permite continuidade de rota; false/bloqueio não cria pose de pé | World/VM/queue com recursos explicitamente resolvidos |
| Main e fila | Main em idle, fila vazia, ação enfileirada, execução, retorno e retomada no tick correto | Scheduler headless; não inferir ordem global ainda não reproduzida |
| Identidade e orçamento | Remoção/reuso de entidade, VM restart, ausência de recurso e limite/fault | Gates de geração, tickets e orçamento explícitos |
| Resultado visual | Posture, attachment, caller/PC, clipe e frame registrados antes/depois da continuação | Cena Linux separada; pose vem da simulação, sem correção do renderer |

`odh-person` é um host de sessão diagnóstico, ainda não um app de lote nem uma
build jogável Linux.
