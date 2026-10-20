# Cena salva com materiais externos e sem avatar

Checkpoint de 2026-10-20. `odh-saved-scene` aceita fontes explícitas repetíveis
`--architecture-resource FILE` (`.flr`, `.wll` ou FAR1, extensão sem distinção
ASCII de caixa). O catálogo, os aliases, o preparo e os renderizadores existentes
agora chegam ao snapshot CLI: FLRm/WALm selecionam ID1, inclusive vazio, com
fallback ID0 somente quando ID1 não existe. Mapas não selecionados não bloqueiam
o render. Nome declarado e não resolvido continua erro; GUIDs de definição
ambíguos continuam erro. Fontes posteriores vencem somente os nomes do catálogo
de arquitetura, conforme o contrato já documentado.

O host preserva seus relatórios por piso/parede/objeto e reutiliza
`ArchitectureResourceCatalog`, `resolve_architecture_aliases`,
`prepare_saved_architecture`, `SavedFloorRenderer` e `SavedWallRenderer`.
`prepared_render_architecture` compartilha o snapshot preparado de materiais e
terreno com `SavedLotRenderer`; não cria outra regra de remapeamento.

FARs de definições podem misturar IFFs com outros arquivos. A varredura passa
somente entradas cuja extensão é `.iff` (ASCII sem distinção de caixa) para o
parser IFF. Um `.iff` malformado continua erro; o filtro não captura exceções de
decode para ignorá-las. Os limites existentes permanecem: 16 fontes de definição,
8192 candidatos, 256 KiB de proveniência, 256 fontes de arquitetura, 65536 palavras
lógicas de retenção do `SavedWorld` e os limites dos parsers/caches/renderizadores.

Recursos de aparência são abertos somente ao encontrar um avatar posicionado no
nível selecionado. Sem avatar não é preciso `--skeleton`; os dois argumentos
posicionais ANIMATION_FAR/TEXTURES_FAR permanecem na interface, mas não são
abertos. Quando não existem entidades visíveis para ancorar a câmera, o host usa
o centro do lote, sem alterar coordenadas salvas. Os guards de PPM/depth incluem
cada nova fonte, aliases lexicais, symlinks, hardlinks e outputs entre si.

## Preview explícito sem restaurar World

`--architecture-only` exige `--floors` e admite pisos e terreno opcionais, com
câmera no centro do lote. Rejeita `--objects`, `--walls`, definições,
`--neighborhood` e `--skeleton`. Esse modo não decodifica ObjM nem restaura
entidades: JSON informa `architecture_only:true`, `world_entities_restored:false`,
`entities:0`, `avatars_rendered:0`, `objects_rendered:false` e `walls_rendered:false`.
O zero descreve o contexto de render, não uma contagem das entidades da casa.
Paredes exigem a restauração normal de World para suas máscaras de portas/janelas.
Nenhum dos modos omite ou neutraliza silenciosamente células pool/water:
o renderizador continua rejeitando esse perfil não implementado.

`architecture_decoded` e `architecture_prepared` distinguem leitura e preparo;
`architecture_loaded:false` continua indicando que o snapshot não ativa a
arquitetura como simulação de lote. `floor_map_resolved`/`wall_map_resolved`
contam declarações resolvidas. Os misses de lookup do preparo ficam separados:
podem representar padrões globais sem declaração. Materiais de piso agora
incluem `source` e `asset`, como já ocorre nas paredes.

## Evidência com dados locais reais

Dados lidos da instalação externa `../referencias/The Sims`, sem mudança nos
originais e sem assets no Git. Imagens/controles/JSON/scripts locais ficam em
`/tmp/odh-saved-scene-house23-*`, `/tmp/odh-saved-scene-house21-*` e
`/tmp/odh-saved-scene-house30-*`. A enumeração do harness é explícita: 113 arquivos
FAR/FLR/WLL ordenados e passados individualmente ao CLI; o CLI não procura pastas.
Foram usadas 12 fontes de definição: Objects, ExpansionPack3, ExpansionPack5,
ExpansionShared e os Downloads CCPlugin, Christmas2000, CopedFlowers, HPPottyPack,
Plants2, Topiaries, UnSnacker e WallLite. Não se presume que essa lista seja o
provider completo ou sua precedência de produção.

House23, nível1, paredes up, zoom1, rotação0, 1024x768, `--objects --floors
GameData/floors.iff --walls GameData/walls.iff`, sem tema de terreno nem skeleton:

| Medida | Resultado |
| --- | ---: |
| Entidades restauradas / avatares | 160 / 0 |
| Retenção auditada de World / limite | 25422 / 65536 palavras lógicas |
| Declarações de piso / parede resolvidas | 3 / 1 |
| Triângulos de piso / camadas de parede / camadas de objeto | 846 / 9 / 70 |
| Pixels cobertos | 115804 |

O snapshot real repetido gera os mesmos PPM e depth. Inspeção visual confirma
pisos, paredes e objetos nessa câmera; não compara o enquadramento ou as cores
com o executável original. SHA-256 do PPM baseline:
`216529231283c0b0173993bd9626a4fda4bba032957af2b1caa71d461613b319`.

Os controles A/B mudam **somente o roteamento explícito de um nome**, usando
symlinks temporários para outro recurso real já instalado. São controles
sintéticos sobre geometria real, não a aparência original da casa:

| Controle | Troca posterior de catálogo | Pixels RGB alterados |
| --- | --- | ---: |
| Piso | `street_concrete1.flr` -> `GameData/Floors/FloorZenStone.flr` | 25784 |
| Parede | `park_stone3.wll` -> `GameData/Walls/WallJadeTile.wll` | 6088 |

No piso, o padrão ODH453 muda de `ExpansionPack3/ExpansionPack3.far`, entrada
`wallsAndfloors\street_concrete1.flr`, para o symlink temporário. Na parede,
o padrão ODH327 muda do mesmo FAR para o symlink de `park_stone3.wll`.
Esses IDs dependem do catálogo desta execução. Ambos os controles preservam o
depth byte a byte, a geometria dos pisos/paredes, registros de objetos e a
cobertura de 115804 pixels. Só o material da família controlada muda no JSON.
Os recursos originais apontados pelos symlinks mantêm seus hashes.

House21 também renderiza no modo `--architecture-only`: cinco aliases de piso,
2668 triângulos e 299136 pixels cobertos. O controle de piso equivalente altera
49024 pixels, preservando depth e geometria. Isso comprova o caminho de pisos
reais sem restaurar World; não comprova paredes ou objetos nesse modo.

House30 tem **duas limitações independentes**, mantidas sem elevar limites ou
remover registros: no modo normal, o World auditado exige 80637 palavras lógicas
para 503 registros, acima de 65536, e retorna
`saved world retained word budget exceeded`. No modo `--architecture-only`, o
World não é lido, mas as células pool/water fazem o renderizador retornar
`pool/water floor geometry unsupported` (com terreno opcional, o guard de
superfície também pode rejeitar antes). Não foi produzida uma cena completa de
House30. House82 também encontra o guard de pool/water.

## Reprodução e cobertura sintética

```sh
cmake -S . -B /tmp/odh-saved-scene-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/odh-saved-scene-build -j6
ctest --test-dir /tmp/odh-saved-scene-build --output-on-failure -j4
```

`tests/saved_scene_cli.py` gera somente fixtures sintéticas. Os 244 checks
incluem render completo sem avatar, lote vazio, ancoragem por objeto salvo,
FLRm/WALm ID1/ID0, fontes loose/FAR, precedência, fonte ausente/malformada,
FAR de definições com IFF e não-IFF, erro de `.iff` malformado, 256/257 fontes,
paths, guards de outputs e determinismo. Um save de 600 entidades passa os
decoders mas é recusado pelo limite inalterado de retenção do World;
`--architecture-only` renderiza seus pisos sem afirmar restauração de entidades.
Os guards de pool e de water são exercitados separadamente nesse modo.
Os testes C++ existentes preservam cobertura de IDs/chunks ambíguos e budgets.

Build Release completo; 138 CTests registrados, 137 executados sem falhas e
`linux_window_backend` pulado sem DISPLAY. A compilação inicial emite um warning
GCC `-Wstringop-overflow` em `src/room_map.cpp:76`, código não alterado nesta
unidade; a compilação incremental final não emite warnings. Este checkpoint
não valida leaks/sanitizers, execução de threads salvas, Main/Init completo,
comportamento do jogo original, iluminação por rooms, equivalência visual,
compatibilidade geral, ports ou hardware. Ele comprova render CPU limitado de
recursos reais passando pelo CLI Linux e controles de materiais observáveis.
