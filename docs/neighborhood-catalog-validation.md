# Catálogo e seleção de lotes da vizinhança

Unidade de entrada somente leitura, adicionada em 2026-10-22 sobre
`b2b5df9f02f3c0975d2a9baea4c832ff5cc54efb`.

```sh
odh-neighborhood --user-data '/caminho/The Sims/UserData' --mode 1
odh-neighborhood --user-data '/caminho/The Sims/UserData' --mode 1 --house 7
```

O catálogo combina `LotLocations.iff` STR# cujo ChunkID é o modo,
`Neighborhood.iff` FAMI e a presença de `Houses/HouseNN.iff`. Caminhos são
resolvidos a partir do diretório fornecido; a CLI não cria diretórios, importa,
copia ou salva recursos. O arquivo da casa é verificado como arquivo regular,
sem afirmar que seu conteúdo é um lote válido ou executável. Sem `--house`,
lista todos os lotes do modo, incluindo arquivo ausente (`file_exists=false`,
`selectable=false`). Selecionar casa sem arquivo ou fora do modo é erro, sem
JSON parcial. Casa sem FAMI continua selecionável, com `family=null`.

A biblioteca `read_lot_locations` reutiliza `read_strings`, exige STR# -3
com grupo de idioma explícito (padrão 1; sem fallback), e interpreta exatamente
três inteiros decimais separados por duas vírgulas. Espaços ASCII e tabulações
nas bordas dos campos são aceitos; `+`, sufixos, campos vazios, caracteres de
controle e vírgulas extras não são. Casas duplicadas falham, inclusive o
sentinela 99, validado e omitido como no frontend Simitone. Limites padrão:
1 MiB de payload, 4096 entradas STR# em todos os idiomas, 128 bytes por linha
do idioma escolhido, casas 1..99 e coordenadas -32767..32767. Esses limites
são o perfil conservador ODH, não uma afirmação de limites do jogo original.
Coordenadas são pixels de origem, sem transformação, escala, zoom ou tiles.
O tail desconhecido de STR# é preservado como extensão
`lot_locations_remaining_bytes`, sem interpretação ou exigência de zeros.

A família usa `FamilyContext::by_house`, incluindo a política existente de
último FAMI serializado para a chave signed-low16. O JSON distingue a casa
solicitada, HouseNumber bruto no FAMI, `fami_chunk_id`, `family_number`,
`member_count` e GUIDs unsigned. Não transforma FamilyNumber em ChunkID ou
identidade de runtime. A saída é evidência `catalog_selection_only`.

Modo 6 responde `status=unsupported` e lista vazia, inclusive quando uma casa
é informada; não precisa abrir dados do jogo. A configuração correspondente
não tem fundo/flow selecionável no Simitone. Modos fora de 1..7 são erros.
A CLI usa o perfil de casas selecionáveis 1..98; 99 é sentinela.

O catálogo/seleção resolve casa → família → lote, mas ainda não abre runtime
nem desenha overworld. Não inicia VM, host de cena, gameplay ou UI de seleção.

## Referências estáticas

Referências fixadas em [references.md](references.md), sob Ms-PL:
Simitone `4906db3156616bfaffc10ce0963cf08062975fef`,
`Client/Simitone/Simitone.Client/UI/Panels/UINeighbourhoodSelectionPanel.cs`
(configurações 1..7, STR# por modo, CSV e sentinela 99); seu FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.content/TS1/TS1NeighbourProvider.cs` (diretório UserData,
LotLocations, Neighborhood e `House` + PadLeft(2) + `.iff`). PadLeft não impõe
máximo 99: a faixa ODH é um limite explícito do perfil observado.
Implementação própria, sem importar o fluxo de UI ou execução dessas fontes.

## Matriz sintética

| Alvo | Casos verificados | Resultado |
| --- | --- | --- |
| `lot_locations` | Header ausente/parcial, formato -2, string truncada; CSV válido, espaços/tabulações, coordenadas nos extremos, ordem, sentinela; vírgulas faltantes/extras, vazio, sufixo, sinal +, newline; casa 0/100/32768, coordenadas fora da faixa, overflow int32; casa/sentinela duplicados; budgets de entradas/linha/casa/coordenada/payload; idioma ausente; tail preservado; modos 1..7 e inválidos 0/8 | 37 checks |
| `neighborhood_cli` | IDs 7/200/900 distintos e GUIDs altos; casa existente sem FAMI; casa sem arquivo listada e seleção rejeitada; seleção correta com coordenadas/caminho; casa fora do modo/sentinela/fora da faixa; modo inválido/negativo/sufixo/duplicado; CSV malformado/duplicado/fora da faixa; modo6 vazio e modo6 com casa sem arquivos de entrada | 23 checks |
| `family_resources` | Regressão do parser e FamilyContext existentes | 107 checks |
| `neighborhood_resources` | Regressão NBRS existente | 3087 checks |

Fixtures são geradas integralmente em memória/diretório temporário, sem bytes
comerciais. Build Debug GCC 13.3.0, C++17, Linux, SDL audio e X11 desativados:

```sh
cmake -S . -B /tmp/odh-neighborhood-build -DCMAKE_BUILD_TYPE=Debug -DODH_SDL_AUDIO=OFF -DODH_X11=OFF
cmake --build /tmp/odh-neighborhood-build --target odh-neighborhood odh-lot-locations-tests odh-family-tests odh-neighborhood-tests -j4
ctest --test-dir /tmp/odh-neighborhood-build -R '^(lot_locations|neighborhood_cli|family_resources|neighborhood_resources)$' --output-on-failure
```

Resultado do build focado: os quatro testes indicados passaram. Depois, o build
completo e toda a suíte CTest também passaram: **165/165**, sem falhas. O teste
foi sintético; não valida runtime, renderização, hardware ou ports. O build
preexistente não foi reutilizado pois seu CMakeCache ainda registra o caminho
antigo `/media/rafael/Dados/...`.

## Leitura da instalação local

Execução somente leitura em
`/home/rafael/_PROJETOS/TheSims1/referencias/The Sims/UserData`, 2026-10-22.
Quantidades são evidência de catálogo, sem reprodução de recursos comerciais.

| Modo | Estado | Lotes | Arquivos presentes | FAMI associados | Membros | Tail STR# (bytes) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | supported | 10 | 10 | 2 | 5 | 200 |
| 2 | supported | 10 | 10 | 0 | 0 | 10 |
| 3 | supported | 9 | 9 | 0 | 0 | 9 |
| 4 | supported | 41 | 41 | 2 | 5 | 820 |
| 5 | supported | 10 | 10 | 0 | 0 | 200 |
| 6 | unsupported | 0 | 0 | 0 | 0 | não lido |
| 7 | supported | 9 | 9 | 0 | 0 | 200 |

Modos podem conter a mesma casa; as linhas não são somadas como lotes únicos.
O sentinela 99 foi omitido. Presença de FAMI não prova carregamento da família.
Os resultados foram resumidos sem registrar GUIDs nem copiar bytes dos arquivos
comerciais. A execução foi somente leitura. O modo 6 não foi aberto porque o
Simitone não o oferece como bairro selecionável.
