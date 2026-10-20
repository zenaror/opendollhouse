# Materiais externos no render de lote salvo

A API host liga o catálogo explícito de `.flr`/`.wll`, aliases FLRm/WALm,
`PreparedArchitecture` e os passes de piso/parede de `SavedLotRenderer`.
Nenhum diretório é varrido pela API. Os arquivos comerciais continuam externos.

## Seleção dos mapas e preparação

`selected_architecture_map(resource, type)` seleciona o chunk ID 1 por
existência, inclusive quando seu vetor de entries está vazio. Só na ausência
do ID 1 consulta o ID 0. Outros IDs são preservados, mas não usados. A ordem
dos chunks no arquivo ou no vetor não altera essa escolha. Duplicatas de IDs
0/1 são rejeitadas; o leitor de arquitetura já rejeita todos os IDs duplicados.
Esse contrato é deliberadamente mais estrito que `IffFile.Get<T>(id)`, que
retorna a primeira instância do ID. Ele não combina mapas nem escolhe pelo
conteúdo.

`SavedLotMaterialOptions` fornece o catálogo, as opções de preparação (road
flip/crop explícitos) e limites de aliases/preparação. O construtor do lote
seleciona mapas, resolve nomes e prepara os grids 64x64. Aliases declarados de
piso sem recurso, ou de parede sem recurso quando paredes estão habilitadas,
rejeitam a construção: um miss não vira silenciosamente um material global
com o mesmo byte. Misses da preparação sem declaração de nome mantêm o valor
original como na API pura, e ficam nos diagnósticos. Os getters
`material_aliases()` e `prepared_architecture()` expõem ambos os resultados.

O snapshot bruto e seus registros permanecem intactos. Um snapshot de render
separado recebe os pisos/paredes preparados e o mesmo crop superior esquerdo
de altitude, grass, target grass, pools e water. As posições/estados do World
continuam vivos. Não há reconstrução de portais, rooms ou iluminação.
Sem options, permanece o caminho cru global anterior; mapas selecionados não
vazios exigem o resolvedor. O caminho `odh-person --saved-world-main --render-floors ...` aceita
`--architecture-resource FILE` repetido para cada fonte `.flr`, `.wll` ou `.far`,
com extensão case-insensitive. O parser permite 256 fontes, caminhos não vazios
de até 4096 bytes, e exige `--render-floors`. A ordem é passada diretamente ao
catalogador: fontes posteriores vencem por nome/família; não há recursão nem
varredura automática. Todas as fontes entram nas verificações de aliases do
arquivo `--render-frame`, incluindo symlinks/hardlinks. Sem o argumento novo,
a invocação usa o caminho global anterior. Os demais CLIs continuam exigindo
globais; a seleção de fontes/precedência de instalação cabe ao chamador.

## Recursos e máscaras

Os construtores `SavedFloorRenderer(path, catalog)` e
`SavedWallRenderer(path, definitions, catalog)` recebem o mesmo catálogo que
resolveu os aliases. Os IDs ODH de cada família são independentes:

| Família | IDs globais | IDs externos 256..65533, zoom 1 / 2 / 3 |
|---|---|---|
| piso SPR2 | pattern + (zoom-1)*256 | 1 / 257 / 513 |
| parede SPR# | 1536 + pattern + (zoom-1)*256 | 1 / 1793 / 2049 |

Piso zero permanece vazio. Sentinelas pool/water 65534/65535 continuam fora
do perfil de geometria pintada. SPR2 usa frame 0 e FloorCopy; paredes usam o
frame da orientação já existente. Cada sprite consulta seu PALT local. As
políticas anteriores de ausência de PALT/máscara continuam as mesmas.

A pintura externa não fornece máscaras: style global consulta walls.iff;
style de objeto consulta o DefinitionIndex e valida o OBJD concreto. Caps
continuam globais. Proveniência de piso e parede registra caminho, nome do
asset, sprite e palette usados; o nome é o basename loose ou o nome FAR original.

O loader reabre apenas o recurso solicitado. Para FAR, valida índice, nome,
offset e tamanho do manifesto contra o handle do catálogo, usando exatamente
esse registro. Isso permite que uma entrada posterior de nome duplicado ganhe
na catalogação sem voltar à seleção ambígua por nome. Mudança de localizador
falha; não há hash de conteúdo nem garantia contra alteração de mesmo tamanho.
Chunks SPR/PALT de mesma combinação tipo/ID são ambíguos e falham. Um runtime
ID repetido da mesma família no catálogo também falha antes do render.

A cópia de metadados do catálogo é limitada a 256 fontes, 65536 records,
1MiB de nomes/caminhos e 4096 bytes por string. Cada renderer conserva o teto
de 16 caminhos de fonte incluindo globais/máscaras, 16 assets externos e
65536 chunks totais, 4096 por asset; provenance de assets tem teto de 256KiB.
Piso mantém 48 materiais em cache, 16 por draw e 1Mi pixels; parede mantém
256 assets totais, 4096 frames, 1Mi pixels e os limites anteriores de desenho.
Não há prefetch de conteúdo nem distribuição de dados de instalação.

## Referência e evidência

Consulta técnica: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, com
FreeSO submódulo `f86bbd34b2112296ed786741f640a0535886d180` (MPL 2.0 em
`LICENSE.md`). `VMTS1ActivatorNew.cs:115-116` seleciona os mapas;
`WorldFloorProvider.cs:292-295` seleciona os três SPR2 externos;
`WorldWallProvider.cs:369-372` seleciona os três SPR# externos.
A implementação ODH é própria, sem dependência do runtime C#.

`tests/external_architecture_renderer.cpp` constrói IFF/FAR sintéticos com
palettes e sprites distintos por zoom. Cobre mapas IFF parseados, precedência
1/0/IDs irrelevantes, vazio/duplicatas, alias -> catálogo -> preparação ->
renderers e lote, loose/FAR, entrada FAR duplicada vencedora, cores de cada
zoom, preservação dos globais e do input, máscara OBJD separada, ausência e
ambiguidade de chunks/IDs, source budgets e limites de aliases/preparação.
São testes de decode/render sintético, sem prova de identidade ao jogo original
ou validação de consoles/hardware. Um smoke local de metadados desta sessão
registrado pelo agente coordenador resolveu House30 (6/6 pisos, 1/1 parede)
e House82 (12/12 pisos, 13/13 paredes), com 480 vencedores de 113 fontes
explicitamente enumeradas; isso comprova somente resolução de nomes/IDs.

```sh
cmake -S . -B build-architecture -DCMAKE_BUILD_TYPE=Debug -DODH_SANITIZERS=ON
cmake --build build-architecture -j6
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-architecture --output-on-failure
```

Neste ambiente, LeakSanitizer padrão rejeita ptrace do sandbox antes de reportar
resultados. O comando mantém ASan/UBSan ativos e desabilita apenas leak checking;
não fornece validação de leaks.

Validação desta unidade em 2026-10-20: build Debug completo com ASan/UBSan,
136 CTests registrados, zero falhas e um `linux_window_backend` pulado sem
DISPLAY (135 executados). O teste novo passou 31677 checks sintéticos; os
quatro testes de renderizadores também passaram depois da revisão final da
cache de frames. Os números não medem compatibilidade com dados comerciais.

O teste `person_architecture_cli` cobre o parser e a catalogação real de fontes
IFF/FAR sintéticas por subprocesso, com uma falha posterior deliberada de
OBJD/definition ausente. Valida ordem, famílias/extensões, arquivos ausentes ou
FAR inválido, falta de `--render-floors`, limite 256/257, tamanho de caminho e
preservação dos inputs quando o output é alias lexical/symlink/hardlink. Esse
teste verifica a fronteira CLI/catálogo e erros determinísticos; a prova de
pintura e composição continua no teste C++ sintético de render acima.

Follow-up CLI de 2026-10-20: build Debug com ASan/UBSan sem warnings,
`person_architecture_cli` com 87 checks e suíte completa com 137 CTests
registrados, 136 executados, zero falhas e o backend X11 pulado sem DISPLAY.
O mesmo limite de LeakSanitizer/ptrace acima permanece explícito.
