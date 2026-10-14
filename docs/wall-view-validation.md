# Andar selecionado e paredes abaixadas

Checkpoint 2026-10-14. `odh-saved-scene --level 1|2 --wall-mode up|down`
seleciona a apresentação do lote salvo. `--wall-mode` exige `--walls`, que exige
`--floors`. Default2/up mantém os snapshots publicados em8cf1344 byte a byte.
É seleção gráfica: entidades, bancos, threads e grades do World continuam
preservados. Ainda não há lote jogável ou execução de threads salvos.

## Contrato

Referência FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:
`UILotControl.cs:1340–1367`, `WallComponent.cs:123–128,156–180,299–322,446–461,
494–509,538–579,628–785,792–807,877–912`, `FloorComponent.cs:45–55`,
`WorldEntities.cs:90–93,134,192`, `WorldWallProvider.cs:77–122`,
`ObjectComponent.cs:448–478`, `VMGameObject.cs:48–59`.
Implementação própria; não constitui prova do renderer original.

`odh::WallView` contém level1..2/down. O modo uniforme corresponde a uma máscara
Cutaway toda true. Apenas o andar selecionado pode cortar; inferiores continuam
Up e superiores não desenham. Pisos, paredes, junções, objetos e avatars respeitam
o andar. Caches gráficos não precisam resolver sprites de faces/entidades acima,
mas o estado lógico permanece carregado. O centro automático da câmera considera
as entidades visíveis nesse andar; não reproduz a câmera interativa original.

Working style cardinal1 vira Down; fences2/12/13/14 ficam Up. Diagonal TR raw1
vira Down, raw255 e outros permanecem Up. Down seleciona o estilo básico,
ignorando override da janela. A face cardinal Door cortada e seus endpoints são
omitidos; esse guard não é estendido às diagonais. Records de portas omitidas
ficam em `wall_omitted_doors`, sem ler seus sprites. Rotação transporta cuts dos
owners vizinhos e preserva o fallback de borda específico da referência.

Máscara global Down=`2*style+1+(zoom-1)*512`. Se o **Far Down** falta, todos os
zooms usam Up, inclusive quando há Near/Medium Down; esse caso é registrado em
`global_down_up_fallback`, separado de `mask_fallback` para alpha do padrão.
Se Far Down existe, os três Down são exigidos pelo perfil do initializer.
Máscaras dos objetos possuem seis SPR ordenados Up3/Down3, mas o caminho uniforme
normal ignora o override quando corta. Destino e depth das faces são os mesmos
Up/Down: só a máscara muda.

Junções acumulam flags Up/Down separadamente. Qualquer flag Up nonzero prevalece,
mesmo se não tiver frame; não há OR dos dois grupos. Up Z=`level*2.95+alt`;
Down Z=`.3+(level-1)*2.95+alt`. SPR/frame/depth/destinos são os mesmos já publicados,
com alpha próprio e submissão antes das faces. Doors cortadas não contribuem
endpoints cardinais. O formato `WallJunction.down` torna essa escolha explícita.

Todos os sete endereços lineares usados por GetCutEdges precisam estar dentro
da grade para **cada** parede nonzero do andar selecionado quando Down. Isso
inclui fences. O acesso upstream pode fazer spill ou sair da grade; o core
preserva spill válido e rejeita acesso inválido, sem inventar clamp ou vizinho
exterior false. Não gera cutaway por mouse/proximidade/rooms: esses modos exigem
roommap, histórico de rooms, rotação e overlays parciais252/253 com estilos7/8.

Objetos não são ocultados automaticamente por ArchitecturalDoor: essa regra está
comentada na referência. `HideForCutaway` vem de Flags bank8 bit10; depende também
de posição arredondada, AdjacentWall/OccupiedWalls e dois pontos do grid.
O snapshot rejeita objetos visíveis marcados nesse bit no andar cortado até
reconstruir a ocupação runtime, em vez de desenhá-los com visibilidade presumida.
Nenhuma entidade nativa House07/House03 estava marcada. Portas e janelas visíveis
continuam como objetos conforme os bancos salvos, mesmo com suas faces abaixadas.
Isso não comprova a apresentação do original. Full cutaway/visibilidade runtime
continua pendente. Não há mudanças em VM ou geração de bindings por reInit.

## Validação

88 testes configurados:87 passaram e1 X11 foi pulado em Debug e ASAN/UBSAN.
Core20.563 checks, adapter224, pisos21.598; incluem níveis1/2, defaults,
inferiores Up/top Down/higher hidden, normalização/fences/diagonais, doors e
endpoints, rotação/owners, prioridade Up, bordas inválidas/spill, Far Down fallback
all-zoom, three-chunk dependency, destinos/depth e cache/recursos acima ignorados.
O adapter tem oracle sintético full RGB/depth/counters para os três zooms Down.
Pisos com nível superior não resolvido são omitidos da imagem sem alterar raw.
Compilação sem avisos; LSan off por ptrace, sem leak check. CLI rejeitou level0/3,
modo inválido e modo sem --walls antes de escrita.

Os cinco snapshots default2/up tiveram **todos os hashes JSON/PPM/depth iguais**
a8cf1344. Sete vistas novas tiveram dumps Debug/ASAN idênticos. Python independente
comparou todos os cuts/faces/owners/omissões, flags/frame/altura de junções,
SPR/paletas, posicionamento/depth; decodificou SPR# e gerou mapas Z por float32.
House07 teve RGB/depth finais byte a byte exatos em três vistas Down, recompondo
junções/parede e objetos parcialmente transparentes sobre o passe previamente
validado de terrain/floor/avatar; contadores dos objetos também coincidiram.
House03 teve oracle de seleção/placement/depth, filtros e igualdade entre builds,
sem oracle completo independente de cor final. Não é nova prova do decoder
completo do save, da geometria dos avatars ou de execução original.

| Vista | Faces desenhadas | Faces Down | Portas omitidas | Junções | Junções Down | Objetos acima omitidos |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
|H07 level1 Down r0/r2 zoom3 e r0 zoom1|63|45|6|49|49|0|
|H01 level1 Down zoom1|0|0|0|0|0|0|
|H03 level1 Up zoom1|105|0|0|72|0|36|
|H03 level1 Down zoom1|99|68|6|72|72|36|
|H03 level2 Down zoom1|178|40|2|114|42|0|

H07 Down near cache31frames427584pixels2137920bytes; far26640pixels133200bytes.
H03 level1 Down cache29frames21088pixels105440bytes; level2 Down43frames37344pixels
186720bytes. Budgets únicos existentes continuam valendo; bytes não são RSS ou
medida de desempenho New3DS. Sem mask fallback nesse corpus. Onze inputs
comerciais tiveram hashes iguais antes/depois. As vistas H07 close e H03 level1
Down wide foram inspecionadas; não comparadas a screenshots originais.

H07 level1 Down r0 zoom3 PPM SHA256:
`9f721977151d35dc4e15c4cd7066290f376d69a87c65b4134bbc48639711cf95`;
depth `2908aaec58209eef661d110125409c18e0bf272af1d0e9ba55ac8e01fe37f045`.
Relatórios ignorados: `verify-saved-wall-view.py`,
`saved-wall-view-native-validation.json`, `wall-view-previous-baseline-hashes.json`,
`wall-view-baseline-check.log`, `house*-wall-view-*`, logs de builds/CTest e
`wall-view-oracle-base.*`. Recursos comerciais permanecem fora do Git.
Próximos passos: visibilidade runtime/room cutaway, câmera/input Linux e continuação
VM salva. A meta integral continua ativa.
