# Campos de colisão OBJD

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
limites depois da rotação. Esta leitura apenas preserva os valores binários;
ainda não calcula obstáculos nem reproduz o fallback, flags, contenção ou
colisões de grupo.

Testes sintéticos confirmam offsets, payload com campos presentes, view curta e
truncada. Ainda falta comparar valores decodificados de objetos originais e
Legacy e implementar a transformação para `VMEntityObstacle` com fixtures de
rotação. Mesmo com isso, o validador de goal também terá de cobrir paredes,
portas, piso, ocupação dinâmica e limites antes de habilitar Routing45 no Main.
