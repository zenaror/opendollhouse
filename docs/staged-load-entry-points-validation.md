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
Operações BHAV sem primitiva fornecida falham fechadas. Por isso, este preparador
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

`odh-wall-appearance HOUSE --load-stage-probe --global-resource ARCHIVE ENTRY`
prepara uma cópia do World salvo, resolve cada definição pelo recurso exato
selecionado e executa as ações Load em ordem crescente de ID, com limite de
1.000.000 de instruções. O JSON marca que nenhum executável foi usado e que o
World não foi publicado. Uma ação principal ausente é registrada em
`missing_root_actions` e retorna falso, como em
`VMEntity.ExecuteGenericEntryPoint` do Simitone; uma chamada privada interna
ausente ainda interrompe a preparação.

Na instalação Complete Collection consultada em 2026-10-24, o probe preparou
182 entidades de House07 e 245 de House05. House07 completou 3 ações e parou na
entidade 4, ação 4157, após 5 instruções: primitiva 10 não suportada. House05
completou 4 ações e parou na entidade 6, ação 4109, após 5 instruções: leitura
de escopo 26, índice 16516 não suportada. Esses pontos marcam o próximo trabalho
de interpretação; nenhum dos lotes completou a restauração e a etapa continua
desconectada do loader do jogo. Os arquivos comerciais foram somente lidos.

Após a instalação de PangoCairo 1.52.1 no ambiente, `linux_text_renderer` e
`staged_load_entry_points` passaram. Isso valida a biblioteca de texto e a etapa
sintética; não substitui os testes de janela, que continuam indisponíveis sem
Xvfb neste ambiente.

`tests/staged_load_entry_points.cpp` usa apenas BHAVs sintéticos. Verifica
ordem de execução, isolamento entre donos, retorno falso síncrono, escrita na
cópia, descarte de escrita GhostImage, rejeição de tabela/ordem incompleta,
ação sem dono, falha numa ação posterior, orçamento agregado e preservação
byte a byte do snapshot de origem e leitura de registradores salvos. Não valida
a cadeia comercial de Windows 4105/4104, o resolvedor dos recursos, o loader
Linux ou equivalência com o executável original.
