# SPR# e recursos de paredes

Checkpoint de recursos de 2026-10-14. O core `read_spr_header/read_spr_frame` e o probe
`odh-spr` decodificam os sprites legados SPR# necessários aos padrões e máscaras
de paredes. O compositor da casa ainda não desenha paredes. Essa unidade prepara
recursos reais para a próxima integração; não substitui paredes por quads lisos
nem declara arquitetura/rooms completos.

## Contrato do leitor

Referência: FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
`SPR.cs`/`PALT.cs` e `WorldWallProvider`. Implementação própria em C++, com
validação estrita e fixtures sintéticas. Não há prova de execução/fidelidade ao
original a partir do decoder.

Perfil de tabela de offsets: versões502/504/505 observadas no corpus, mais1000
coberta sinteticamente. Endianness é identificada pelos primeiros dois bytes;
a versão uint32 inteira é validada. Paleta raw32 é retida, com referência low16.
Header possui contagem de frames e offsets. Todos os headers, inclusive os
não selecionados, precisam caber após a tabela e não se sobrepor quando os
offsets são distintos. Aliases de offset são aceitos e usam o mesmo intervalo
limitado até o próximo offset maior. Header com zero frames é válido; selecionar
um frame nesse header falha.

O frame contém reserved32/height16/width16 na endianness do chunk. RGBA inicia em
zero transparente e índices em0; alpha distingue pixel transparente de índice0
pintado. Comandos externos0/16 são marcadores iniciais com count opaco;4 contém
uma linha delimitada pelo tamanho count-2;5 termina;9 pula linhas. Comandos da
linha1 pulam pixels,2 repetem um índice com byte de padding,3 leem índices literais
com padding ímpar. End marker é obrigatório. Filas/linhas incompletas e bytes de
padding realmente precisam existir; não há preenchimento de EOF. Índices, linhas,
colunas e opcodes são verificados. Comando desconhecido falha.

A saída preserva reserved, tamanho, versão/endianness/paleta, índices, contador de
comandos, bytes consumidos e restantes no intervalo do frame. Não copia/modifica
o payload original. Paleta é argumento explícito com1..256 cores opacas. O core
não procura recursos nem inventa uma paleta ausente. No probe, o fallback preto
com256 cores só ocorre com `--missing-palette-black`, e é marcado no JSON. Ele
corresponde ao DEFAULT_PALT observado na referência, não a evidência sobre o
renderer do original. SPR# não possui os canais alpha parcial/Z de SPR2; nenhum
Z é sintetizado nesta unidade.

Limites padrão:4096 frames,2048 por eixo,1Mi pixels por frame,256 cores,
1Mi comandos. Checagens precedem alocações; apenas o frame selecionado é expandido.
O probe lê até4096 chunks IFF e aceita IFF direto ou entrada FAR1 de nome exato;
usa os limites existentes do leitor FAR. Exporta RGBA e índices depois do decode,
com proteção contra alias por path/symlink/hardlink do input e entre outputs.
Falha de escrita pode deixar exports parciais; nunca escreve o input. Prefixo
até4090 bytes e entrada FAR até4096.

Versões sequenciais1001 e outros layouts continuam explicitamente rejeitados.
Big-endian/1000 são evidência sintética; o corpus nativo abaixo é little-endian.

## Evidência

84 testes configurados:83 passaram e1 janela X11 pulada em Debug e ASAN/UBSAN.
LSan desativado por ptrace; não houve leak check. Compilação sem avisos. Core:
515 verificações sintéticas, incluindo comandos, padding, transparência, seleção,
aliases de offsets, endianness, limites e truncamento em cada byte. CLI verifica
IFF/FAR, fallback explícito, duplicatas e proteção contra hardlinks/path aliases.

Python independente leu os IFF originais e decodificou cada frame. Todos os bytes
RGBA/índices e todos os metadados comparados coincidiram com a API C++. Exports
agregados Debug e ASAN também são idênticos. Dois inputs comerciais tiveram
hashes iguais antes/depois, sem mudanças; a entrada Windows é lida dentro do FAR,
sem extrair um IFF comercial para o checkout.

| Corpus | SPR# | Frames | Pixels | Comandos | Frames com fallback preto explícito |
| --- | ---: | ---: | ---: | ---: | ---: |
|walls.iff|210|1218|7566628|313905|474|
|Objects.far/Windows.iff|48|192|1452416|91932|192|

Em walls.iff,804 frames têm versão504,276 versão502 e138 versão505. Todos os192
frames de Windows.iff são504. Doors.iff foi inspecionado: não contém SPR#; isso
não significa ausência de máscaras de portas, pois estilos globais também existem.
Foi inspecionado visualmente o padrão1 near/frame0 (SPR2049,64x271) exportado.
Export de máscara de janela usa SPR350/frame0 real, identificado no corpus,
16x67. Esses probes não desenham paredes no lote.

Hashes:

- walls.iff:`7d339ff62068895c5ace79983ce24fe2fbdaac6458395588f75cefeafe66ca40`.
- Objects.far:`68cadf2f250a50a6227540ee9175cbb937016eed54770bed12ec8c55047ed0c5`.
- Windows.iff interno:`9013075b1962b35fa3e0b1f2b52642e8fe27dcd93cf56a7e9e5485e1e291d72f`.
- Export agregado walls:`ce52d43fe68a7a8a8bfd992a3ef5502479619f8dfdfeb86a83a2c57dee916d5d`.
- Export agregado Windows:`6fcfbe85a025c55f5e3aa025deab1c8fba23db609637e682a49594fff160b449`.

Relatórios locais ignorados: `reports/spr-all-frames.cpp`,
`verify-spr-native.py`, `spr-native-validation.json`,
`spr-windows-native-validation.json`, dumps `.bin`, logs `spr-*.log` e exports
`wall-pattern-1-near-frame0.*`/`window-mask-native.*`. Nenhum recurso comercial é
versionado. O consumo agregado do oracle offline não representa o cache do jogo.

## Associação arquitetônica para a próxima unidade

Leitura do subagente foi conferida contra os trechos locais; não houve edits por
esse agente. Fontes: `WallComponent.cs:823–854`, `WallComponentRC.cs:112–292`,
`WorldWallProvider.cs:51–174`, `VMTS1Activator.cs:555–609`,
`VMGameObject.cs:403–434`, `VMEntity.cs:1515–1553` e
`2DWorldBatch.fx:450–453`. São contratos FreeSO, sem promoção a prova original.

WorldWallProvider: patternID usa SPR=pattern+1536+(zoom-1)*256. StyleID usa
SPR=style*2+(zoom-1)*512 para walls up e+1 para down. Quando o far-down
falta, a referência usa up nos três zooms. Orientações de frame:0 TL,1 TR,2 horizontal,3 vertical. O padrão e
mask são recursos diferentes; o shader substitui alpha pelo alpha da máscara.
Junções têm sprites4096/4097/4098. Fences conhecidos: estilos2/12/13/14.

Linha central local no tile(x,y), e padrões das faces:

| Bit | Segmento | Extremos | Padrão da face/oposta | Estilo |
| --- | --- | --- | --- | --- |
|1|TL|(0,1)→(0,0)|TL; oposta(x-1,y).BR|TLStyle|
|2|TR|(0,0)→(1,0)|TR; oposta(x,y-1).BL|TRStyle|
|4|BR|(1,0)→(1,1)|BR|vizinho(x+1,y).TLStyle|
|8|BL|(1,1)→(0,1)|BL|vizinho(x,y+1).TRStyle|
|16|horizontal|(0,1)→(1,0)|BR/BL|TRStyle|
|32|vertical|(0,0)→(1,1)|BL/BR|TRStyle|

Bits4/8 não devem duplicar a parede física grossa do vizinho. O RC usa altura2,95
por story, espessura total0,15 tile, altitude nos endpoints, e tampas. O renderer2D
usa altitude do centro e retângulos/depth de sprites próprios. Não misturar esses
contratos como se fossem uma única implementação.

DecodeWalls FreeSO normaliza estilos não diagonais para1, preservando2/12/13/14,
e zera TL/TRStyle quando o bit correspondente falta. O core de arquitetura ODH
continua preservando os campos raw; essa conversão ainda não é aplicada ao desenho.
Diagonais reutilizam TLPattern/TLStyle para meios pisos: não tratar TLStyle como
estilo de parede diagonal. Face vertical2D tem comentário de incerteza na fonte.

Recortes de objetos: FlagField2(word40) bits14/15 indicam janela/porta. WallStyle
22..255 carrega6 SPR do próprio recurso a partir de WallStyleSpriteID (up3 zooms,
down3 zooms). WallPlacementFlags(word13) e direção cardinal escolhem a parede;
N/E/S/W→0/1/2/3. SetWallStyle só modifica TR(side0) ou TL(side3); sides1/2 não
fazem alteração nessa referência. Overrides/flags Door são runtime, não grids
serializados. Reconstruí-los não deve executar reInit nem filtrar só sprites
visíveis. Renderer2D e RC têm precedência diferente entre style salvo e override,
e algumas guardas RC são inconsistentes: não copiar esses lapsos automaticamente.

Na data deste checkpoint, binding OBJD, composição e integração ao lote ainda
estavam pendentes. Foram implementados depois; consulte
[paredes no snapshot salvo](saved-wall-validation.md) para a validação atual
dessas etapas. O gargalo seguinte é topologia de cômodos, obstáculos/portais e
execução/interação Linux. Este decoder de recursos isolado não prova lote jogável
nem resultados de consoles.

## Reprodução

```sh
build/odh-spr '../referencias/The Sims/GameData/walls.iff' 2049 0 --output reports/pattern1
build/odh-spr '../referencias/The Sims/GameData/Objects/Objects.far' 350 0 --iff-entry Windows.iff --missing-palette-black --output reports/window-mask
ctest --test-dir build --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```
