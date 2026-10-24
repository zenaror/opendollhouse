# Preparação transacional da entrada Load

`stage_saved_load_entry_points` executa a EntryPoint 2 em uma cópia privada do
`WorldSnapshot`. A pessoa que chama a função fornece a ordem completa das
entidades, o `CodeOwner` exato de cada ação, as rotinas globais e os bancos
privados/semiglobais por dono, além dos 20 registradores temporários de cada
entidade. O preparador não abre arquivos, não escolhe recursos por precedência
e não publica o resultado no `World` ativo. O adaptador
TS1 do Simitone completa com zeros os registradores ausentes antes de restaurar
uma thread. O preparador exige essa entrada para todas as entidades e a instala
antes de cada ação.

Cada ação segue o formato observado no dispatch imediato do Simitone: objeto é
Caller e Callee, StackObject é zero e os quatro argumentos começam em zero.
Condições são mantidas na tabela, mas não executadas. Ações até 255 são
ignoradas. Retorno verdadeiro ou falso conta como execução concluída; espera,
yield, falha, recurso ausente e esgotamento do orçamento recusam o lote inteiro.
O chamador só recebe um snapshot quando todas as entidades passaram, e decide
se/quando publicá-lo.

O estado `GhostImage` precisa ser informado para cada entidade. Quando marcado,
a ação roda em outra cópia descartável e suas mudanças no `World` candidato não
são mantidas. Essa cópia cobre o estado que o `World` atual representa; a
montagem do grupo multitile e serviços do motor ainda não são implementados.
Operações BHAV sem primitiva fornecida falham fechadas. PlaySound resolve o
FWAV no recurso do dono da ação e depois em `Global.iff`; o pedido fica guardado
junto ao World candidato, sem tocar áudio durante a preparação. Se uma ação
posterior rejeitar o lote, os pedidos guardados são descartados. O preparador
ainda não está ligado ao carregamento de lotes reais.

As ações não-GhostImage usam a mesma cópia privada durante toda a sequência,
como no laço de restauração do Simitone. A referência local está fixada no
submódulo FreeSO `f86bbd34b2112296ed786741f640a0535886d180` do Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`: `NetPlay/Model/Commands/VMBlueprintRestoreCmd.cs:63-66`
executa EntryPoint 2 em sequência; `tso.simantics/Entities/VMEntity.cs:567-620`
monta Caller/Callee e os argumentos padrão; `tso.simantics/Engine/VMThread.cs:92-127`
avalia a ação imediatamente e preserva os registradores temporários da thread
existente. `tso.simantics/Utils/VMTS1ActivatorNew.cs:212-235` converte os
registradores salvos e os completa até 20. Isso documenta o contrato observado
no Simitone; não confirma o executável original.

## Evidência

No inventário estático de House05 e House07, `Windows.iff` usa ação Load 4105.
`odh-bhav` mostrou que 4105 chama 4104; a primeira instrução de 4104 grava
ObjectData[23], seguida de outras gravações em ObjectData do mesmo objeto.
`odh-bhav --run` sem World para nessa gravação por não ter memória de jogo
associada. Isso identifica um caminho candidato para teste, mas não demonstra
execução comercial de Load nem o significado completo dos campos gravados.

### Probe limitado com os dados comerciais

`odh-wall-appearance HOUSE --load-stage-probe --global-resource ARCHIVE ENTRY
[--load-stage-probe-seed UINT64]` prepara uma cópia do World salvo, resolve cada
definição pelo recurso exato selecionado e executa as ações Load em ordem
crescente de ID, com limite de 1.000.000 de instruções. O JSON marca que nenhum
executável foi usado e que o World não foi publicado. O estado global começa
dos valores SIMI salvos, aplicando o perfil bootstrap do Simitone nos índices
17, 20 e 25; assim, `SIMI[10]` continua sendo o número do lote selecionado.
Tuning vem do BCON exato global, privado e semiglobal. O opcode 8 usa o RNG já
implementado pelo projeto e uma semente de diagnóstico explícita (padrão 1;
configurável pela opção acima); essa semente não está salva no lote e não é
apresentada como a semente que o Simitone usaria em uma sessão real. Uma ação
principal ausente é registrada em `missing_root_actions` e retorna falso, como
em `VMEntity.ExecuteGenericEntryPoint` do Simitone; uma chamada privada interna
ausente ainda interrompe a preparação.

Para o perfil de primitivas, o registro do cliente Simitone observado chama
`VMContext.InitVMConfig(false)`: nessa configuração opcode 1 não tem handler;
opcode 10 também não tem handler. A VM encaminha ambos pelo ramo verdadeiro.
Opcode 7 Refresh(Caller, graphic) só invalida cache gráfico, ausente no World
transacional não publicado, então esse caso específico retorna verdadeiro.
O opcode 7 Refresh de pontuação do cômodo agora usa um cálculo separado baseado
no mapa preparado: soma o impacto dos objetos apoiados nos cômodos ligados,
normaliza pela área, aplica o desconto de interior/exterior e limita o resultado
a -100..100. O cálculo só é aceito quando o mapa e a identidade dos recursos
estão resolvidos. Opcode 49 NotifyOutOfIdle não tem thread para interromper
neste contexto de preparação e retorna verdadeiro, como o caminho observado
quando não há thread ativa. Opcode 31 SetToNext funciona no modo exato por GUID
usando a primitiva já existente; os outros modos seguem sem cobertura. Opcode 48
StopAllSounds cancela pedidos de som anteriores do mesmo dono na cópia candidata.
Outros casos de Refresh e operações ainda não implementadas seguem falhando.

Na instalação Complete Collection consultada em 2026-10-24, o probe recebeu
90 definições FAR, 20 definições de personagem IFF e recursos de arquitetura
(10 `.flr`, 13 `.wll` e 20 FARs oficiais, sem a pasta Downloads). Para os recursos
de arquitetura sem alias direto, o mapa preparado usa os fallbacks de lookup
observados no Simitone; a precedência exata da instalação não foi provada.
House05 (245 entidades) e House07 (182 entidades) completaram a preparação de
todas as ações Load nas sementes de diagnóstico 1, 2 e 42:

| Lote | Semente | Ações concluídas | Instruções | Sons enfileirados | Pontuações de cômodo |
|---|---:|---:|---:|---:|---:|
| House05 | 1 | 245/245 | 1515 | 2 | 3 |
| House05 | 2 | 245/245 | 1585 | 2 | 3 |
| House05 | 42 | 245/245 | 1578 | 2 | 3 |
| House07 | 1 | 182/182 | 964 | 1 | 3 |
| House07 | 2 | 182/182 | 1036 | 1 | 3 |
| House07 | 42 | 182/182 | 998 | 1 | 3 |

Isso prova apenas que esses dois lotes atravessam a etapa candidata de Load
com os recursos selecionados. Não prova que o jogo original escolhe os mesmos
arquivos, nem que os valores aleatórios ou o comportamento são equivalentes.
O World continua privado e não publicado, os sons não foram tocados e o
preparador ainda não está ligado ao loader Linux; portanto, ainda não é uma
partida jogável.

Após a instalação de PangoCairo 1.52.1 no ambiente, `linux_text_renderer` e
`staged_load_entry_points` passaram. Isso valida a biblioteca de texto e a etapa
sintética; não substitui os testes de janela, que continuam indisponíveis sem
Xvfb neste ambiente.

`tests/room_score.cpp` cobre a fórmula de pontuação, cômodos conectados,
interior/exterior, limites e rejeição de mapas/entradas sem identidade resolvida.
`tests/staged_load_entry_points.cpp` usa apenas BHAVs sintéticos. Verifica
ordem de execução, isolamento entre donos, retorno falso síncrono, escrita na
cópia, descarte de escrita GhostImage, rejeição de tabela/ordem incompleta,
ação sem dono, falha numa ação posterior, orçamento agregado e preservação
byte a byte do snapshot de origem e leitura de registradores salvos. Também
testa a leitura suplementar de tuning, o fallback do perfil de primitivas para
opcodes sem handler, a passagem do estado RNG explícito ao opcode 8 e o
enfileiramento, cancelamento e descarte de pedidos de som em lotes sintéticos,
além de NotifyOutOfIdle e SetToNext no modo exato. Não valida a
cadeia comercial de Windows 4105/4104, a reprodução de áudio, todos os serviços
de cômodo, o loader Linux ou equivalência com o executável original.
