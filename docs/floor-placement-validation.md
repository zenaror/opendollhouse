# Validação isolada de FloorChangeValid

`odh_floor_placement` traduz a rotina `VMEntity.FloorChangeValid` do Simitone
para uma função C++ pura. Seus inputs podem vir do banco OBJM salvo:
`VMStackObjectVariable.PlacementFlags` é `ObjectData[42]`; `LevelOffset` é
`VMOBJDVariable` 56 no OBJD, byte offset 112. O leitor OBJD136/138 expõe esse
campo como opcional quando os bytes estão presentes. Os valores `65535` e `65534` representam,
respectivamente, piscina e água; flags relevantes e resultados de rejeição
seguem a enumeração `VMPlacementFlags`/`VMPlacementError` da referência.
Inclui a diferença entre `VMGameObject` e outras entidades no caso de piso
comum. `RequireFirstLevel` não é consultado por essa rotina.

Proveniência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, FreeSO
submódulo `f86bbd34b2112296ed786741f640a0535886d180`,
`tso.simantics/Entities/VMEntity.cs:FloorChangeValid` e enumerações adjacentes.

`odh-saved-floor-placement` conecta uma entidade já decodificada do save a uma
consulta de piso preciso na arquitetura preparada e aplica somente essa regra.
Flags vêm de `ObjectData[42]`, preservando os bits mesmo quando a estrutura do
OBJM os representa como `int16`; `LevelOffset` ausente e coordenadas sem piso
resolvido resultam em `unsupported`, sem assumir valor padrão.
Se a preparação reportar alguma referência FLRm declarada sem resolução, o
adaptador também retorna `unsupported` de forma conservadora, pois o valor do
piso retido não prova a identidade do recurso naquela posição.
`odh-floor-placement-tests` cobre a regra isolada. Já
`odh-saved-floor-placement-tests` cobre o adaptador com objetos, avatar,
terreno, níveis superiores, flags com bit alto, offset ausente, coordenada fora
do lote, alias declarado não resolvido e os sentinelas de piscina/água.

O adaptador continua fora do runtime. Um resultado `allowed` apenas passou por
`FloorChangeValid`; não significa que o destino esteja livre ou alcançável.

Isto não implementa `PositionValid`, validação de paredes ou inclinações,
limites do lote, ocupação, portas, salas, footprints, multitile ou o mapa
dinâmico de obstáculos de `GetAvatarPlace`. Portanto, o resultado não prova
que uma posição é alcançável/segura e não deve ser usado para liberar o
`Routing45` no Main. Consulte também
[`route-geometry-validation.md`](route-geometry-validation.md).
