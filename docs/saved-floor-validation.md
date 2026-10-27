# Pisos salvos e altitude do terreno

Checkpoint de 2026-10-14. `odh-saved-scene --floors FLOORS_IFF` compõe os pisos
pintados da casa com os avatares e, com `--objects`, móveis/objetos reais do World
salvo. Pisos e entidades usam o mesmo espaço de profundidade. O resultado é um
snapshot CPU Linux; as threads salvas continuam dormentes. Paredes, superfície
de grama, rooms, iluminação do lote e interação ainda não estão integradas.

## Contrato e procedência

A implementação própria usa a referência FreeSO
`4c6b3e8f5835b228723caea3c9f683c62f244f73` (MPL-2.0; ver
[referências](references.md)). Contratos observados: `VMTS1Activator` decode e
RemapWalls; `SPR2Frame.FloorCopy`; `3DFloorGeometry`; `Blueprint.InterpAltitude`;
`EntityComponent.Position`; `VM` restauração de MTOffset e operadores
`LotTilePos`. Esse perfil não prova comportamento do executável comercial.

`interpret_saved_architecture` interpreta duas camadas legacy (Arry1/101 de
stride1; paredes2/102 de stride8) ou advanced (11/111 stride2;12/112 stride14).
Preserva registros originais, campos desconhecidos, seis campos de paredes,
IDs de objetos3/103 e grids auxiliares. Arry3 não é piso. Dimensões, strides,
extents, duplicatas e orçamento são validados antes de criar a saída. A
interpretação usa row-major; a origem é column-major. Não aplica normalização
arbitrária de estilos, road flip ou remapeamento de materiais.

O caminho global usa floors.iff: SPR2v1000/frame0,
ID=pattern+(zoom-1)*256, PALT default low16. A API host também aceita
[catálogo externo e preparação FLRm/WALm](external-architecture-renderer-validation.md);
o CLI cru continua exigindo globais. FloorCopy completa a textura por deslocamento circular de meia largura
e meia altura, com padding transparente à direita em largura ímpar. O resultado
precisa ser opaco; não modifica SPR2, índices ou Z originais.

Geometria usa cantos TL/TR/BR/BL, UVs de diamante, troca U/V em rotações ímpares
e alinhamento de tela por arredondamento ties-even. A projeção usada no
alinhamento é achatada em Z=0. Story Z=(level-1)*2,95; cada canto recebe sua
altitude. Diagonal16: primeira metade TR/BR/BL (TopLeftPattern), segunda
TL/TR/BL (TopLeftStyle). Diagonal32: primeira TL/TR/BR, segunda BR/BL/TL.
Quando os dois campos são zero, ambos recebem o material do grid de piso.
Material0 não desenha: ainda não existe superfície de grama. Diagonal dupla,
piscina/água ativa, materiais fora do perfil e pisos sem quatro cantos dentro
da grade falham explicitamente.

Altura é Arry0 byte1*10, fator3/160 em unidades de tile. A interpolação preserva
clamps1..dimension-1, ceil, remainder e operações float32 da referência. BaseAlt0
é política observada no carregador FreeSO; não é uma base calibrada no original.
Objetos livres amostram coordenadas lógicas; membros multitile usam a média
inteira do grupo. O host rejeita overflow de soma short em vez de reproduzir
wraparound. Coordenadas dos membros são as salvas, inclusive membros sem posição
runtime; essa última situação não ocorreu nos grupos das três casas validadas.
Objetos contidos herdam a altitude do recipiente uma vez, além do SLOT já
interpretado. Avatares livres amostram sua posição. A câmera é centrada nas
posições elevadas. World, bancos, coordenadas lógicas, motivos e saves permanecem
inalterados. Nenhum reInit ou tick é executado para desenhar.

## Limites

Grids até64x64/duas camadas, até32 arrays e2MiB de armazenamento lógico retido
na interpretação. Leitura do host: até64 chunks selecionados e1MiB de payloads
codificados/decodificados, até4096 chunks por IFF. Cache até48 materiais
pattern/zoom e1Mi pixels; um desenho até16 materiais. Texturas copiadas para
raster são contadas separadamente do cache. Visitas repetidas de pixels para
validar alpha na geometria têm limite16Mi, mesmo para tiles fora da tela.
Viewport até1Mi pixels e limites existentes de triângulos/samples do raster.
Esses limites são de buffers/trabalho explícitos; não são medições de RSS nem
validação de desempenho em New3DS. Não remove entidades fora da tela.

## Validação

81 testes configurados:80 passaram e1 janela X11 pulada, em Debug e ASAN/UBSAN.
LSan desativado pela restrição ptrace do ambiente; não há alegação de leak check.
Compilação dos módulos sem avisos. Novos testes sintéticos:64 de arquitetura,
573 de FloorCopy/geometria,66 de altitude e21.592 de host/cache/diagonais/zoom/
rotação/limites. Legacy e advanced são cobertos sinteticamente; evidência nativa
abaixo é legacy.

Um decoder Python independente confirmou todos os bytes RGBA de27 materiais
(9 de House07 em3 zooms), incluindo FloorCopy, contra export da API C++.
Em cinco snapshots, confirmou cada desenho de piso/material/metade/altitude
contra os arrays originais e a altitude de cada avatar/objeto visível contra
interpolação separada, grupos e containment. Todas as propriedades anteriores
de aparência/objeto, exceto Z gráfico, permaneceram iguais. As três vistas de
House07 sem `--floors` conservam RGB e depth byte a byte do checkpoint anterior.
Debug e ASAN produzem JSON, PPM e depth idênticos nas cinco vistas novas.

| Casa/vista | Desenhos | Triângulos | Materiais | Bytes de textura | Pixels cobertos |
| --- | ---: | ---: | ---: | ---: | ---: |
|07 r0 z3 640x400|683|1365|9|294912|249065|
|07 r2 z3 640x400|683|1365|9|294912|249025|
|07 r0 z1 1024x768|683|1365|9|18432|165442|
|01 r0 z1 1024x768|432|864|3|6144|34922|
|03 r0 z1 1024x768|628|1254|9|18432|159294|

House07 tem682 células regulares e1 diagonal em(31,30): somente segunda metade
horizontal/material8; as683 células pintadas estão no platô de altura200.
Os avatares105/137 recebem Z3,750000238418579, com diferenças float32 explícitas.
House03 inclui pisos no segundo andar e as duas metades de uma diagonal;
628 desenhos não significam628 células distintas. As imagens foram inspecionadas
em close House07 e vista larga House03. As portas/janelas visíveis são sprites de
objetos, não paredes implementadas.

A comparação independente cobre bytes de materiais e interpretação/posições;
não é um segundo rasterizador completo de todos os pixels do snapshot com pisos,
nem comparação com screenshots do original. Geometria/raster têm testes
sintéticos; a composição final tem igualdade Debug/ASAN e inspeção visual.

floors.iff SHA256:
`c332b75b0452ea609850b4e9ee3b9db729bf581495a6af2287331c5eb9f3be47`.
Todos os dez arquivos comerciais lidos foram comparados por hash antes/depois,
sem mudanças. Os hashes de casas/arquivos anteriores constam no checkpoint de
[objetos e arrays](saved-object-architecture-validation.md).

House07r0z3 PPM:
`8cdf3b88e93038c71932777c676267ee55dd8f3cedaae21010cab3cfcd49cf74`;
depth:
`4a7eab5782482035c444c9638d00bb74ebc6f19f4f7a2293a00dfad67a010a3a`.
Relatórios locais ignorados: registro arquivado não distribuído,
registro arquivado não distribuído, registro arquivado não distribuído,
registro arquivado não distribuído e logs
`floor-{debug,asan}-tests.log`. Nenhum recurso comercial está versionado.

## Reprodução

Adicionar `--floors '../referencias/The Sims/GameData/floors.iff'` ao comando
`odh-saved-scene` do checkpoint de avatares/objetos. JSON expõe
`architecture_decoded`, `floors_rendered`, materiais, desenhos/cantos/altitudes,
triângulos, bytes e visitas de pixels da geometria. `architecture_loaded=false`
ainda identifica a ausência de arquitetura completa/rooms/simulação.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DODH_SANITIZERS=ON
cmake --build build-asan -j4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

Próximos passos: materiais/segmentos de paredes, superfície do terreno, rooms e
obstáculos/portais; continuação de threads/filas salvas e interação no Linux.
A meta integral permanece ativa. Não há lote jogável ou resultado em consoles.
