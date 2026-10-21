# Consulta RoomInfo reconstruída do World vivo

`query_world_room_info` enumera o `World` atual e deriva um snapshot limitado a
4096 entidades antes de cada consulta. Isso evita reusar uma fotografia depois
de movimentos, contenção, remoção, mudança de flags ou reclassificação entre as
consultas. Footprints são calculados com `object_collision_shape`, Definition
Index único por GUID, flags vivas em ObjectData[8] e `Entity::static_footprint`.
Entidades fora do mundo, contidas e objetos com `HasZeroExtent` não entram no
índice. Avatares usam o raio 3 de `GetObstacle` somente quando o chamador fornece
o `KillTimeout` runtime atual; o World ainda não armazena esse campo.

O chamador também fornece um resolver de sala para a posição viva. A API não
usa automaticamente `GeometricRoomMap`: o mapa atual é um candidato geométrico
sem portas/portais e só pode ser fornecido como resolver quando for conhecido
que coincide com a sala runtime. Definição ausente/ambígua, flag não inicializada,
sala não resolvida, `KillTimeout` desconhecido, geometria inválida ou excesso
de entidades torna a consulta `unsupported`, sem retornar interseções parciais.
Os resolvers recebem cópias dos dados, nunca referências a entidades. A API
valida novamente a projeção espacial depois dos callbacks e recusa o resultado
se o World tiver mudado durante a montagem.
O alvo da query é o avatar colocado: a lista estática o mantém; a dinâmica o
exclui. Isso é o contrato de montagem de GetAvatarPlace, não a política completa
de placement. EntryPoint 5, grupo multitile, flags Allow/Disallow, ghost,
EntryPoint 26, obstáculos de arquitetura e portais ainda não são avaliados.

Referência Simitone fixada em `4906db3156616bfaffc10ce0963cf08062975fef`,
submódulo FreeSO `f86bbd34b2112296ed786741f640a0535886d180`:
`VMGameObject.cs:355-382` calcula footprint por TileWidth/FootprintMask e
exclui `HasZeroExtent`/contenção; `VMAvatar.cs:985-1004` acrescenta KillTimeout
e radius 3; `VMEntity.cs:1436-1452` move o footprint entre coleções; e
`VMContext.cs:937-1032,1190-1235` registra posição e consulta as duas listas.
São regras da referência, sem confirmação contra o executável original.

`odh-world-room-info-tests` cobre fixture sintética com movimento entre salas,
contenção, zero extent, inclusão/exclusão do alvo estático/dinâmico, ordenação,
falha fechada para flags/definição/sala/KillTimeout desconhecidos e orçamento.
Também cobre mutação do World disparada pelo resolver de sala.
Esses testes comprovam somente a transformação `World`→índice para providers
explicitamente fornecidos. Main ainda não chama a API; não é prova de clearance,
rota ou interação jogável no lote.
