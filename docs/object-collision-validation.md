# Campos e shape de colisão OBJD

O leitor de metadados OBJD136/138 agora expõe `LevelOffset` quando o payload
alcança o campo (índice `VMOBJDVariable` 56, byte offset 112), além de
`TileWidth` e `FootprintMask` quando o payload contém ambos. Ele continua aceitando views curtas
para os callers que só precisam do prefixo estável até TreeTableID; campos
ausentes ficam `nullopt`. O `header_extent` indica o maior prefixo realmente
inspecionado, não validação integral do OBJD.

Proveniência: no checkout local Simitone em `4906db3156616bfaffc10ce0963cf08062975fef`, `OBJD.cs` define a ordem dos
campos (`refs/upstream/Simitone/FreeSO/TSOClient/tso.files/Formats/IFF/Chunks/OBJD.cs`).
Na tabela, TileWidth é o campo65, byte offset134; FootprintMask é o campo75,
offset154, ambos após o DWORD de versão. O modelo de colisão em
`VMGameObject.GetObstacle` usa os dois: TileWidth define metade da largura em
unidades de 1/16 tile (zero usa o fallback de8) e FootprintMask ajusta os quatro
limites depois da rotação. A leitura do OBJD preserva os valores binários sem
validar a definição inteira.

`odh_collision` agora reproduz essa fórmula de bounds, rotação para direções
cardinais e fallback de TileWidth zero (`VMGameObject.cs:355–380`). Para
diagonais, a referência `DirectionToWallOff` retorna zero (`VMEntity.cs:1515–1529`);
a rotina preserva esse comportamento. O
resultado é `present`, `absent` (HasZeroExtent ou contido) ou `unsupported`
(dados incompletos/fora do perfil). A shape candidata do avatar é o retângulo
centro±3 unidades usado por `VMAvatar.GetObstacle` (`VMAvatar.cs:985–1003`);
interseção entre boxes segue a regra estrita de `VMObstacle.Intersects`
(`VMObstacle.cs:121–124`), em que tocar bordas não colide.

Isso ainda não reproduz regra integral de placement: flags de avatar/ghost,
rooms, portas, floors, objetos ignorados, multitile e políticas de ocupação
precisam ser aplicados pelo chamador. Não conectar ao roteamento antes dessas
verificações.

`odh-wall-appearance --portal-probe` também emite `collision_candidates` por
entidade, juntando o prefixo OBJM salvo com a entidade restaurada no World. A
caixa é classificada como candidata geométrica, ausência ou unsupported; o
campo `route_clearance` permanece false. Uma auditoria separada localiza a sala
geométrica do centro e compara a caixa expandida em 3 unidades com os
retângulos RoomObs completos daquela sala. Objetos contidos e objetos comuns com `HasZeroExtent` não geram
obstáculo persistente. Avatares sem `HasZeroExtent` ficam unsupported porque o
estado transient `KillTimeout` não está no save. Portas arquitetônicas ficam
unsupported mesmo quando a flag `HasZeroExtent` sugere ausência de obstáculo:
o probe não conhece a solidez runtime da porta. Em alguns desses casos, `box`
pode estar presente como geometria possível, sem afirmar que ela esteja
registrada como obstáculo.

O adapter exige identidade e posição iguais entre OBJM e World; posição
divergente, posição sentinela e OBJD sem `TileWidth`/`FootprintMask` não viram
espaço livre. O footprint bruto de OBJM não é usado como fallback. Esse
inventário ainda não modela janelas, paredes, múltiplas peças, obstáculos
dinâmicos ou a ordem de seleção de portas do runtime.

A posição validada fica vinculada ao próprio candidato, impedindo que uma
auditoria combine a caixa de uma entidade com a posição de outra. Para avatares,
a shape centro±3 é expandida pelo roteador em mais ±3, chegando a raio total 6;
objetos usam a mesma expansão de roteamento sobre sua caixa OBJD.

No House07 local, o probe classificou os 182 registros: 91 candidatos
geométricos, 69 ausentes e 22 `unsupported`. Destes, oito não têm posição
utilizável, dois avatares não permitem resolver `KillTimeout` e 12 portas
dependem de estado runtime. Entre candidatos com estado e sala conhecidos,
50 caixas não intersectam RoomObs e 41 intersectam; as 22 entidades
`unsupported` continuam assim e 69 são `not_applicable`. Isso só avalia
interseções com limites da sala, não `wallObs`, elegibilidade dinâmica,
EntryPoint 5, caminho ou clearance. `routing_clearance` permanece false. O
JSON de trabalho está em `/tmp/odh-house07-room-boundary-audit.json`, fora do
repositório e sem assets incorporados.

A revisão seguinte da auditoria verifica adicionalmente as quatro faixas
externas de um tile criadas em `VMRoutingFrame.AttemptWalk`, mantendo as
contagens de interseção com RoomObs e com esses limites separadas no JSON.
No probe House07, zero candidatos cruzaram as faixas externas. Ambos os testes
continuam sendo sobreposições por entidade: não verificam o segmento entre
posições nem as regras runtime.

Testes sintéticos confirmam offsets, payload com campos presentes, view curta,
truncamento, todas as oito direções, fallback zero-width, flags sem obstáculo,
avatar ±3, coordenada inválida e a regra de interseção. Essa fórmula ainda não foi comparada
valor a valor com uma execução FreeSO de oracle ou amostra completa de objetos
originais/Legacy. O validador de goal ainda terá de cobrir paredes, portas,
piso, ocupação dinâmica e limites antes de habilitar Routing45 no Main.
