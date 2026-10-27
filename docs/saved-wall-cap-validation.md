# Junções no snapshot salvo

Checkpoint 2026-10-14. O passe `--walls` do `odh-saved-scene` agora desenha as
junções reais das paredes erguidas. Complementa `saved-wall-validation.md` e
`wall-junction-validation.md`; não executa o lote nem implementa down/cutaway,
rooms ou prova visual do jogo original.

## Integração

Referência FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
`WallComponent`, `WorldWallProvider` e `_2DWorldBatch`. O núcleo95b48d0 gera
flags/frame e depth conforme o contrato já validado. O adapter resolve SPR#4096
far,4097 medium,4098 near, com paleta explícita ou DEFAULT_PALT preto marcado.
Cada frame usa RGB e alpha próprios; não recebe pintura/máscara de objeto.
Junções são submetidas antes das faces WALL, como o agrupamento Z_BUFFER da
referência, preservando o primeiro fragmento em empate.

Posição fonte `(x-.5,y-.5,level*2.95+altitude(x,y))`; level é1..2 e altitude
usa o centro inteiro do tile. Cadge/anchor/project_world seguem o mesmo perfil
CPU dos objetos/paredes. Destinos/depth mapas são os três específicos de junção.
Não há normalização de near/far depth ou calibração GPU/original presumida.
Iluminação/gamma de rooms continuam ausentes.

Cache de frames/pixels/bytes e limites de visitas fonte/áreas/candidatos são
compartilhados entre junções e faces. Não existe orçamento paralelo que permita
ultrapassar o limite total. Frames fora da tela também são resolvidos; nenhuma
entidade/simulação é descartada. Cache1Mi pixels RGBA+índices continua limitado.
`wall_cached_*` e `wall_*_samples` agora contam o passe combinado. `wall_layers`
continua contando faces; `wall_junction_draws` registra separadamente flags,
frame, recurso/paleta, posição, tamanho e back/front depth. `wall_junctions_unmapped`
registra sites nonzero sem entrada no mapa. Falha de resolução/composição ocorre
antes do export; inputs permanecem intactos. Falha de escrita pode deixar exports
parciais. O cache pode crescer até os limites após uma falha.

O perfil de spill linear do FreeSO permanece explícito, sem promoção a regra
original. Missing/empty frames e identidades ambíguas continuam sendo erros;
nenhuma junção recebe uma forma geométrica inventada.

## Validação

88 testes configurados:87 passaram e1 backend X11 pulado, em Debug e ASAN/UBSAN.
Após a extensão final dos testes, o target afetado passou novamente nos dois
builds:171 verificações, incluindo12 vistas, fixtures46frames, caches, alpha
próprio/buracos, missing/unmapped/fences, oracle de RGB/depth/contadores e empate
real cap/parede por quantização float, preservando cor da junção submetida primeiro.
Compilação sem avisos; LSan desativado por ptrace, sem leak check.

Python independente comparou cinco snapshots Debug/ASAN: JSON/PPM/depth iguais.
Conferiu todos os flags/frames/SPR#/paletas e posições/depth das junções, além das
faces já verificadas. Decodificou SPR# selecionados diretamente dos recursos
originais e gerou mapas Z com incrementos float32 explícitos. House07 teve
composição final RGB/depth float32 byte a byte exata em três vistas: junções,
paredes e todos os objetos SPR2 (inclusive alpha parcial) sobre o passe
terrain/floor/avatar previamente validado. Contadores dos objetos coincidiram.
Não é um oracle novo de toda a geometria dos avatars/terreno ou do save/VM.
House03 teve seleção/placement/depth e igualdade entre builds, sem oracle
independente completo da cor final. Não houve empate wall/cap/avatar no corpus
House07 testado; o caso de empate foi coberto sinteticamente.

| Snapshot | Faces | Junções | Frames únicos | Pixels de cache | Bytes frames | Candidatos combinados | Escritos combinados |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
|House07 r0 zoom3|69|49|34|525632|2628160|537856|256998|
|House07 r2 zoom3|69|49|34|525632|2628160|394304|151054|
|House07 r0 zoom1|69|49|34|32720|163600|100992|44992|
|House01 r0 zoom1|0|0|0|0|0|0|0|
|House03 r0 zoom1|180|114|40|35232|176160|250700|98990|

Todos os flags foram mapeados, sem fallback de máscara. Junções nativas usam
paleta explícita. Records de faces são exatamente os do checkpoint6125552.
O lote House01 sem paredes permaneceu idêntico em RGB/depth. Snapshots sem
--walls mantiveram os cinco baselines anteriores. Onze inputs comerciais tiveram
hashes iguais antes/depois. Bytes são de frames, não RSS/GPU ou performance New3DS.
A vista close House07 foi inspecionada visualmente; o topo agora recebe os
recursos de junção. Isso não constitui comparação com screenshot original.

House07 r0 zoom3 PPM SHA256:
`d985015ebe010d9173bd6ca6c850ea5b98c604faf4fb9675b97defb9a71ca5eb`;
depth: `85e2479138c13187bf8203de82b9a740220785237f9444db947061abbf3e9d77`.

Relatórios ignorados: registro arquivado não distribuído,
`saved-wall-cap-native-validation.json`, `house*-wall-caps-r*-z*`, logs de build e
CTest, `walls-oracle-base.*`. Recursos/imagens comerciais não entram no Git.
Próximo passo: contrato de down/cutaway e seleção de andar, então topologia de
rooms e continuação VM/interação Linux. A meta integral continua ativa.
