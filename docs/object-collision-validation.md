# Campos e shape de colisão OBJD

O leitor de metadados OBJD136/138 agora expõe `TileWidth` e `FootprintMask`
quando o payload contém ambos os campos. Ele continua aceitando views curtas
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

Testes sintéticos confirmam offsets, payload com campos presentes, view curta,
truncamento, todas as oito direções, fallback zero-width, flags sem obstáculo,
avatar ±3, coordenada inválida e a regra de interseção. Essa fórmula ainda não foi comparada
valor a valor com uma execução FreeSO de oracle ou amostra completa de objetos
originais/Legacy. O validador de goal ainda terá de cobrir paredes, portas,
piso, ocupação dinâmica e limites antes de habilitar Routing45 no Main.
