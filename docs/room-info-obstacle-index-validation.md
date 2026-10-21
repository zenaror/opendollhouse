# Índice de footprints RoomInfo

`RoomInfoObstacleIndex` modela a propriedade de footprints que o Simitone mantém
nas listas `RoomInfo.StaticObstacles` e `RoomInfo.DynamicObstacles`. A indexação
é por proprietário, sala, caixa e coleção. `rebuild` só publica um inventário
completo, validado e limitado a 4096 owners; atualizações explícitas suportam
registro/reinserção, mudança de sala/caixa, reclassificação e remoção. Uma
transição inválida ou owner ausente durante move/reclassify invalida o índice e
descarta os footprints antigos para que uma consulta não use estado obsoleto.
`move_footprint` publica o estado final atomicamente; o intervalo transitório
em que `UnregisterObjectPos(..., roomChange=false)` remove apenas a entrada
estática não é representado.

A consulta reproduz a diferença observada em `GetAvatarPlace`: footprints da
lista estática incluem o alvo (a política multitile vem depois); a lista
dinâmica exclui o alvo. Resultados vêm ordenados por ObjectID e usam interseção
estrita. O índice não executa BHAVs, calcula o estado de `StaticFootprint`,
atualiza-se a partir do `World`, ou afirma que um inventário de save representa
as listas runtime. O caller só deve marcá-lo completo depois de uma enumeração
ou sequência de eventos completa e com geometria/coleção conhecidas.

Referência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, submódulo
FreeSO `f86bbd34b2112296ed786741f640a0535886d180`, `VMContext.cs:582-620,
858-867, 937-1032, 1190-1235`; `VMEntity.cs:819-834, 1434-1454`. A classificação
estática/dinâmica deriva do estado do owner e pode mudar; o snapshot salvo não
é suficiente para construir um índice completo.

`odh-room-info-obstacles-tests` cobre 22 verificações sintéticas: source
incompleto, particionamento por sala, mistura/ordenação, exclusão dinâmica do
alvo, arestas que apenas se tocam, movimento entre salas, reclassificação,
remoção, identidade ausente, duplicatas e orçamento. É um contrato de dados
para futura integração com o World, não validação runtime do lote.
