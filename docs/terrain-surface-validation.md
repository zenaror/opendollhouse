# Base do terreno salvo

Checkpoint de 2026-10-14. `odh-saved-scene --floors FLOORS_IFF --terrain-theme 0`
preenche a base do solo entre os pisos reais, com cores do grid salvo e normais
do relevo. Tema0 é grama explícita; não é inferido de filename ou estação.
Pisos, avatares, objetos e World permanecem como no checkpoint anterior. O
resultado ainda é um snapshot CPU: paredes, rooms, threads/filas em execução e
interação Linux seguem pendentes.

## Perfil observado

FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73` (MPL-2.0):
`VMTS1Activator` conversão de Arry6, `TerrainComponent.GetGrassState`,
`GetNormalAt`, `RegenTerrain`, `_3DFloorGeometry.FullReset/DrawFloor`,
`LotTypeGrassInfo` e `GrassShader.LightDot`. Foi clonado o submódulo
FSOMonoGame `74f7ef558111e62a7d0a2228b6f9714980e000fa` (Ms-PL), na revisão
fixada pelo FreeSO, para conferir `Color.Lerp`/`MathHelper.Lerp`. Consulte
[referências](references.md). Checkouts são locais, ignorados e somente consulta;
ODH não depende desses runtimes C#.

O core usa Arry6 original sem modificá-lo. No desenho, transforma cada valor em
`(127-byte) mod 256`. A consulta de um canto usa índice linear
`(y-1)*width+x-1`, preservando o deslocamento da referência. As cores de tema
são lerp float32 dos valores observados, truncadas para byte antes de normalizar
para RGB0..1. Há oito temas explícitos:0 grama,1 areia,2 rocha,3 neve,4 água,
5 grama escura,6 outono,7 nuvem. Areia usa grama clara e areia escura, conforme
a seleção LightType/DarkType do carregador. A escolha do tema não implementa
água ou piscina: grids ativos ainda falham no perfil atual.

O terreno só desenha material0 do piso ground-level, inclusive metades vazias
de diagonais. Conserva o interior `x/y=1..dimension-2` do FullReset. Uma diagonal
16 escolhe TR/BR/BL ou TL/TR/BL;32 escolhe TL/TR/BR ou BR/BL/TL. Os dois campos
zero usam o material do piso como fallback. Não acrescenta uma superfície de
solo nos vazios do segundo andar. Camadas superiores existentes continuam
sendo desenhadas pelo compositor de pisos.

As normais somam diferenças de elevação em world units com componentes
horizontais de passo1, depois normalizam. Preservam as condições de borda da
referência, inclusive `x>1` e `y>1`. O core simplifica algebricamente as rotações
ortogonais dos vetores, sem reproduzir resíduos de trigonometria GPU. Elevação
usa Arry0byte1*10/fator3/160/BaseAlt0 do checkpoint de
[pisos e altura](saved-floor-validation.md). Cores e normais são interpoladas
afinamente nos triângulos do raster CPU, que agora oferece modulação RGB por
vértice e luz direcional up `(dot(normal,up)+1)/2` opcionais. Os defaults
anteriores do raster permanecem iguais.

Esse passe CPU não implementa gamma do shader, room maps, cores/luz externas do
relógio, fade, reflexos, estação ou lâminas de grama. A luz up fixa é explícita;
não representa a iluminação completa do original nem todo o shader FreeSO.
A base de grama sem textura é uma etapa da geometria real, com dados salvos;
textura/noise para lâminas e passes posteriores continuam pendentes. Não há
alegação de equivalência visual ao executável comercial.

## Limites e preservação

Somente grids quadrados2..64, com floor/wall/altitude/grass extents exatos e
camadas auxiliares válidas. Até3844 células internas e7688 triângulos. A reserva
máxima de triângulos nesta compilação Linux64 é1.107.072 bytes (144 por
RasterTriangle); House07 usa910.512 bytes de elementos. JSON separa
`terrain_geometry_bytes` de `terrain_reserved_triangle_bytes`. Isso não é RSS.
O raster mantém seus limites de viewport1Mi pixels, até16Mi candidates e
validação de valores finitos. Cada cor de vértice deve estar em0..1 mesmo quando
a modulação estiver desativada. Não aloca texturas de noise ou buffers de grama
volumétrica: a base usa uma textura branca de1 pixel.

A composição usa os merges já validados de profundidade, terreno→pisos→avatares
→objetos. O buffer temporário de piso é liberado depois do merge; sem terreno,
a imagem de piso é movida como antes. Buffers de frames/merge e geometria são
limitados separadamente, sem alegação de orçamento final do New3DS. Nenhuma
entidade fora da tela é removida, nem há alteração de posição lógica, bancos,
threads dormentes ou arquivos comerciais.

## Evidência

82 testes configurados:81 passaram e1 janela X11 pulada em Debug e ASAN/UBSAN.
LSan desativado por ptrace; não houve leak check. Sem avisos de compilação.
Novos testes incluem70.260 verificações sintéticas de solo/cor/relevo/diagonais/
zoom/rotação/limites; raster agora tem87 verificações, incluindo barycentric RGB,
luz up, preservação do perfil default e rejeição de cores inválidas.

Python independente leu RLE/transpose dos IFF originais e produziu todos os
triângulos/cantos, cores quantizadas e normais das cinco vistas abaixo. Os bytes
float32 de posição/normal/cor coincidiram com um export da API C++. Em House07r0z1,
um rasterizador Python separado confirmou todos os bytes RGB e float32 depth
do passe de terreno isolado:1.107.472 candidates/553.790 escritas. A composição
completa tem igualdade Debug/ASAN de JSON/PPM/depth e inspeção visual; não houve
um segundo oracle completo de todos os passes finais com piso/avatares/objetos.

| Casa/vista | Tiles inteiros | Metades | Triângulos solo | Pixels cobertos finais |
| --- | ---: | ---: | ---: | ---: |
|07 r0 z3 640x400|3161|1|6323|256000|
|07 r2 z3 640x400|3161|1|6323|256000|
|07 r0 z1 1024x768|3161|1|6323|697494|
|01 r0 z1 1024x768|3412|0|6824|567541|
|03 r0 z1 1024x768|3316|0|6632|621954|

A metade de solo em House07(31,30) é a primeira horizontal, complementar ao
piso8 da segunda metade. Os snapshots sem `--terrain-theme` conservam PPM/depth
idênticos ao checkpoint f2a4fbe nas cinco vistas. Com terreno, arrays de objetos,
avatares, materiais e desenhos de pisos e camera center permanecem idênticos.
Dez recursos comerciais tiveram hashes comparados antes/depois, sem mudanças.
House07 close e House03 ampla foram inspecionadas. Portas/janelas são sprites
de objetos: sua presença não significa que paredes tenham sido implementadas.

House07r0z3 PPM SHA256:
`d5e854634d197b171247a0265d97c34be85f1614e58c1f67bea0b7c62081371f`;
depth:
`520b5a6221778f7a40e958e6ea28db4b15554873668e6ae01bac986162eb8029`.
House07r0z1 PPM:
`cfbc3a988586297602d79d7cfd4c92838e4b44f1e26991af888de54c5cf13750`;
depth:
`a9690ccd89ab2f4540182e008afd89b676af026f1d1e31fe6ee3db7637f5aef7`.

Evidência local ignorada: registro arquivado não distribuído,
`terrain-export.cpp`, `recheck-terrain-metadata.py`,
`terrain-surface-native-validation.json`, logs `terrain-surface-*.log` e
`house*-terrain-r*-z*.{json,ppm,depth,png,geometry}`. O export de terreno isolado
usa `.geometry.ppm` e `.geometry.depth`. Nenhum recurso comercial é versionado.

## Reprodução e pendências

Usar `--terrain-theme 0` no comando do compositor de pisos, com fontes explícitas
de House/Objects/Characters/Animation/Textures/Neighborhood e floors.iff. A opção
exige `--floors`; outros temas0..7 são escolhas explícitas, não weather detection.
Checks:

```sh
ctest --test-dir build --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

Próximo foco: materiais/segmentos de paredes, rooms/obstáculos/portais e
restauração da execução de threads/filas para interação Linux. Lâminas, iluminação
e calibração com o original continuam pendentes. Meta integral ativa; não há
lote jogável ou validação em consoles.
