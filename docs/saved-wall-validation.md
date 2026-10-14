# Paredes no snapshot salvo

Checkpoint 2026-10-14. `odh-saved-scene --walls WALLS_IFF --floors FLOORS_IFF`
resolve e desenha paredes erguidas do World salvo no snapshot CPU Linux.
A ordem é terreno/pisos, paredes, avatars e objetos. Recursos comerciais ficam
externos e são lidos sem alteração. Não há continuação dos threads, lote jogável,
rooms/portais, junções, cutaway/down ou prova visual do jogo original.

## Resolução e projeção

Referência FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:
`WallComponent.cs`, `WorldWallProvider.cs`, `VMTS1Activator.DecodeWalls`,
`WorldState.cs`, `Blueprint.GetAltitude` e shader `2DWorldBatch.fx`.
Fontes locais em `refs/upstream/FreeSO`; implementação própria C++.
Usa o renderer 2D, sem misturar o contrato RC desabilitado para esse modo.

`saved_wall_faces` trabalha sobre grades preservadas. Em paredes cardinais,
estilos básicos fora de2/12/13/14 tornam-se1; lados ausentes tornam-se0, como
DecodeWalls. Essa transformação ocorre somente na seleção gráfica. Depois,
estilo working diferente de1 prevalece; working1 usa override de objeto nãozero.
Os saves nativos possuem3/15/22 nos campos básicos: sem essa etapa, estilos
salvos de portas/janelas seriam usados como estilos globais incorretamente.
O relatório `raw_style` pertence ao estilo selecionado após essa transformação,
não ao campo bruto do save; os campos brutos permanecem no decoder arquitetônico.

Rotação determina as faces visíveis TL/TR. Faces BR/BL obtêm estilo e override
dos owners vizinhos, mantendo a pintura da própria face. Diagonais escolhem o
BottomRightPattern rotacionado e TRstyle; rotação diferente de0 não transporta
override diagonal, como RotateWall. A escolha da face vertical contém incerteza
na própria referência; não é uma regra comprovada do original. Segmentos mistos
ou desconhecidos são rejeitados.

Padrões globais0..255 usam SPR# `1536+pattern+(zoom-1)*256`. Máscaras globais up
usam `2*style+(zoom-1)*512`; máscara de objeto usa GUID concreto e os três sprites
up da associação OBJD. GUID/versão/MasterID/Subindex/ObjectType e SpriteBase
são conferidos novamente no recurso aberto. Pattern/frame ausente, source vazio,
ambiguidade ou recurso malformado falham. Style0/máscara SPR# ausente usa alpha
do padrão, fallback marcado por draw. PALT ausente usa DEFAULT_PALT preto opaco
com256 entradas conforme FreeSO, registrado separadamente para padrão/mask.
Isso não troca máscara real por uma forma inventada.

Cada textura é amostrada independentemente pelo compositor descrito em
`wall-sprite-validation.md`. RGB de parede é opaco e sem iluminação/gamma de
rooms. Altitude usa média inteira de quatro cantos raw byte1*10, fator3/160,
BaseAlt0; próximos cantos envolvem a borda e x<=0/y<=0 retorna0. É o perfil
GetAltitude da referência, não a interpolação bilinear usada por objetos.
Destinos são os doze retângulos de WallComponent; origem tira cadge e aplica
anchor da rotação. Depth usa project_world e back/front no espaço não normalizado
compartilhado. Correção de meio pixel/scroll rounded do original segue sem
calibração. Não há quads lisos ou reInit de entidades para simular paredes.

CLI exige --floors para manter altitude consistente, rejeita WALm customizado
não vazio e protege input/output/depth de alias por path/symlink/hardlink.
Também permite snapshot apenas arquitetônico sem avatars posicionados; o centro
ainda deriva das entidades visíveis existentes. Simulação fora da tela permanece
preservada. `walls_rendered:true` significa passe habilitado: um lote sem paredes
pode ter zero camadas, explicitamente contado.

## Limites

Host cache possui16 fontes,256 assets,4096 chunks por IFF/65536 cumulativos,
4096 frames,1Mi pixels únicos RGBA+índices (até5MiB),256KiB provenance de assets
e256KiB de records; FAR8192 entries/256KiB nomes. Índice deve sobreviver ao
renderer; streams/Views e caches são próprios. Grades até64x64x2 e8192 faces,
limites de composição já publicados, incluindo fontes fora da tela. Render
mal sucedido preserva inputs, mas pode preencher cache limitado. O CLI cria
renderer por snapshot e libera cache antes dos outros passes. Bytes reportados
são dos frames, sem alegação de RSS total, GPU ou desempenho New3DS.

## Evidência

87 testes configurados:86 passaram e1 janela X11 foi pulada em Debug e
ASAN/UBSAN. Depois das extensões finais dos testes, o target afetado passou
novamente nos dois builds:98 verificações sintéticas de rotação/diagonal,
normalização/override, resources IFF/FAR/identity, terrain center/edge/wrap,
missing palette versus missing mask, empty/missing rejection e budgets inclusive
fora da tela. Compilação sem avisos; LSan desativado por ptrace, sem leak check.
CLI --walls sem --floors e output alias ao input foram verificados antes de
qualquer escrita numa fixture temporária.

Cinco snapshots tiveram JSON/PPM/depth idênticos entre Debug e ASAN. Python
independente conferiu todas as faces/owners/estilos, IDs de sprites, paletas,
dimensões/recortes e posicionamento/depth usando as grades e overrides
independentemente verificados nos checkpoints anteriores. Releu/decodeou todos
os SPR# selecionados diretamente dos recursos originais. Nos três snapshots
House07 também recompôs todos os pixels de parede e objetos (inclusive alpha
parcial) sobre o passe terrain/floor/avatar previamente validado: RGB e depth
float32 finais coincidiram byte a byte, assim como contadores dos objetos.
Não houve empates wall/avatar nesses três casos. Não é um oracle independente
de toda a geometria de avatars/terreno/VM. House03 teve oracle de seleção/projeção
e igualdade entre builds, sem oracle independente do RGB final completo.
Snapshots sem --walls preservaram os pixels/depth dos cinco checkpoints prévios.
Onze inputs comerciais tiveram hashes iguais antes/depois.

| Snapshot | Camadas | Frames únicos | Pixels de cache | Bytes frames | Candidatos paredes | Escritos paredes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
|House07 r0 zoom3|69|24|443712|2218560|418560|255311|
|House07 r2 zoom3|69|24|443712|2218560|328000|146271|
|House07 r0 zoom1|69|24|27600|138000|75904|43549|
|House01 r0 zoom1|0|0|0|0|0|0|
|House03 r0 zoom1|180|29|29600|148000|192332|96709|

Nenhuma máscara ausente nesse corpus. House07 possui12 máscaras de objeto e6
flags door; House03 possui36 máscaras de objeto e8 flags door. Todas as máscaras
selecionadas nesses snapshots usaram fallback PALT preto, preservando alpha
decodificado do SPR#; os padrões tinham paletas explícitas. Vistas close House07
e wide House03 inspecionadas visualmente. Junções/tampas e paredes abaixadas
continuam pendentes; essa inspeção não é comparação com screenshot original.

House07 r0 zoom3 PPM SHA256:
`390920c09db98db581cfd46ba4e48eeefde157ed4006d61831201b1ddcb2eff8`;
depth: `ce0b10d0d8ee4005820a90544e2848b6b0c038c0e2239d6bbf6a6eb2a31f57d9`.
Relatórios ignorados: `reports/verify-saved-walls.py`,
`saved-wall-native-validation.json`, `house*-walls-r*-z*`, logs de build e
`walls-oracle-base.*`; oracle usa também frames SPR2 anteriormente verificados
em `object-oracle/`. Nenhum recurso/imagem comercial é versionado.
Próximo passo: junções standing/up, depois down/cutaway e topologia de rooms,
seguindo para VM salva/interação Linux. Meta integral permanece ativa.
