# Objetos salvos e arrays de arquitetura

Checkpoint de 2026-10-14. `odh-saved-scene --objects` agora compõe os objetos
visíveis do World salvo junto aos avatares reais. `odh-architecture` decodifica
as grades Arry e mapas FLRm/WALm das casas. Pisos, paredes, relevo, rooms e
cutaway ainda não estão ligados ao renderer/simulação. As threads permanecem
dormentes; esta etapa não constitui um lote jogável.

## Seleção gráfica e posições

O host conserva todas as entidades no World. Seleciona para desenho objetos
com posição e Hidden34=0, lendo Graphic0 do World. Cada membro multitile usa sua
definição e posição concreta; não se desenha apenas o leader/master.

`SavedObjectRenderer` resolve source/asset/OBJD do DefinitionIndex explícito.
Valida identidade da definição no arquivo e seleciona DGRP pela soma
`BaseGraphic + Graphic`, convertida para unsigned16, conforme RefreshGraphic da
referência. NumGraphics não limita esse índice. NumGraphics0 com DGRP selecionado
presente ainda desenha; com DGRP ausente produz registro `no_graphics=true` e
nenhuma camada. Isso permite conservar partes de footprint de escadas de House03
sem inventar imagens. Ausência de DGRP de uma definição que declara gráficos
continua sendo erro. Não há fallback anônimo ou busca em outras fontes.

Direction notch0..7 vira máscara `1<<direction`, girada dois bits por rotação da
câmera. Todas as camadas do DGRP são consideradas na ordem serializada. Flags
dinâmicas são indexadas por SpriteID menos DynamicSpriteBase, dentro do intervalo
NumDynamicSprites; qualquer valor salvo nãozero ativa a referência. Não são uma
máscara de índices de camadas. Frames vazios são conservados no cache e contados
em `omitted_empty_layers`, mas não emitem pixels.

O perfil suporta SPR2 versão1000 com pixel e Z. PALT usa o low16 do default
palette do SPR2, como o decoder anterior. SPR# ou sprites sem depth são erros
explícitos. Flip, offsets de imagem e ObjectOffset participam da colocação e do
depth compartilhado com os avatares. A composição aplica o perfil RGBA/depth
já documentado; não há prova de ordem de transparência, luz de rooms ou calibração
visual equivalentes ao executável original.

O adaptador `graphics_entity` agora aceita objetos contidos em objetos livres.
SLOT de armazenamento é ordinal entre registros type0, resolvido pelo SlotID da
OBJD concreta do container. XY usa /16 e rotação cardinal do container; diagonais
mantêm o perfil anterior da referência. A posição gráfica do objeto contido usa
a origem do container, sem o offset adicional de0,5 dos avatares. Z usa /5 ou a
classe de altura1..9; classes0/5 usam o Z bruto, as demais a tabela da referência.
Nested containers, bone slots de avatar, terreno e interpolação continuam
pendentes. Saved avatars com snapshot e attachment validado também podem usar
SLOT, mesmo sem a marca de posicionamento criada por uma ação nova.

Em House07, os cinco filhos contidos usam SLOT128/slot0 e altura gráfica0,8 tile:

| Filho | Container | Classe de altura | Posição gráfica XYZ |
|---|---|---:|---|
|133, lâmpada|132, mesa de canto|8|31,21,0.8|
|152, lâmpada|124, mesa|3|25,30,0.8|
|129, rádio|106, mesa|3|25,29,0.8|
|141, fogão|115, balcão|4|21,26,0.8|
|116, pia|113, balcão|4|21,29,0.8|

Essas posições foram calculadas novamente a partir da posição salva do container,
campos brutos de SLOT e tabela de alturas, comparando com os resultados do host.
As coordenadas lógicas do save não foram sobrescritas.

## Cache e orçamentos

Caches possuem streams, views de chunks e recursos decodificados com lifetime
próprio. O DefinitionIndex imutável deve sobreviver ao renderer. Falhas não
modificam a imagem-base nem inputs; podem popular o cache limitado.

Limites:16 fontes,256 assets,65.536 chunks cumulativos,4.096 por IFF,4.096
DGRPs/imagens/frames,65.536 referências DGRP e65.536 flags dinâmicas. FAR:
8.192 entradas e256KiB de nomes. Proveniência de assets e registros tem orçamento
256KiB cada. Cache de frames:2.097.152 pixels únicos, até12MiB RGBA/índice/Z.
Viewport:até2048 por eixo e1.048.576 pixels. Draw extents repetidos:4.194.304
pixels; samples do compositor:16.777.216. A soma de extents repetidos não duplica
pixels do cache: House07r0z3 usa1.865.647 extents e1.029.033 pixels únicos.

Em cada render, o cache de frames marca as imagens requeridas pela vista atual.
Ao atingir o limite, frames sem uso nesta vista são evictados antes de tentar
decodificar a imagem nova; após composição bem-sucedida, o cache retém apenas os
frames dessa vista. Testes sintéticos confirmam que dois quadros de1.048.576
pixels cabem juntos (12MiB para RGBA/índice/Z), e que um terceiro é recusado
sem manter cache acima do teto. O renderizador agora informa dimensões e limite
quando um SPR2 ultrapassa seu orçamento de alocação.
No preview X11 com House07, a cena passou por comandos de rotação e zoom e foi
fechada normalmente: o relatório terminou em `waiting` no tick 131, sem
diagnóstico, com 1.538.700 bytes de frames de objetos no último quadro. Isso
valida as mudanças ensaiadas, não RAM/velocidade máxima nem desempenho no New3DS.
Não se exclui simulação fora da tela para economizar recursos gráficos.

## Arquitetura: estrutura preservada

`read_architecture_array` preserva cabeçalho de cinco signed32 LE
(zero,width,height,stride,unknown), payload codificado, runs, padding e eventual
sufixo. O RLE é orientado a bytes independentemente do stride. Opcode low15
indica quantidade; highbit seleciona fill com valor+pad, caso contrário é literal
com padding se quantidade ímpar. Opcodezero não emite bytes. O leitor valida todo
o stream necessário e o orçamento antes de alocar o resultado, rejeita
truncamento, overrun, extent inválido e fill vazio. Não faz wrap ou zero-fill de
bytes ausentes. Bytes após o extent exato ficam como sufixo opaco.

O preenchimento terminal é aplicado inclusive até o fim. FreeSO ARRY.cs retorna
antes de consumir/aplicar alguns fills terminais, deixando zeros no resultado.
A leitura ODH conserva os valores declarados: pools/água9/10 são255 e certas
flags32 permanecem32. Trata-se de interpretação estrutural explícita; não é uma
execução do jogo original nem equivalência byte a byte a esse leitor FreeSO.

Transposição é uma operação separada: destino `(x*height+y)*stride`. Para as
grades64x64 do corpus coincide com a fórmula da referência; a forma retangular
é uma definição explícita do ODH. Não se aplica resize, road flip, remapping de
IDs ou normalização de estilos. Tipos/strides1..16 são limitados por extents,
sem transformar bytes de altitude em alturas do terreno nesta etapa.

FLRm e WALm compartilham formato: zero/version/signature/count, nome bruto NUL,
pad quando comprimento do nome é par, unknown signed32, ID byte e metadados de
`5 + 2*chunk_id` bytes. Aliases duplicados ficam em ordem, sem escolha de
precedência. EOF é estrito. Defaults:1MiB codificado/decodificado por array,
dimensão1024/stride16; maps até4.096 entradas,256KiB de nomes,4.096 bytes por
nome e1.024 de metadados por entrada. O CLI limita64 chunks e1MiB cumulativo de
payloads e dados expandidos, além de4.096 chunks IFF.

## Verificação

50 checks sintéticos do renderer de objetos;390 do adaptador gráfico (incluindo
alturas0..9, NaN, limites, invalidação do resolver e saved attachment);228 dos
arrays/maps. A seleção de soma low16 cobre valores positivos/negativos e
NumGraphics0 com/sem DGRP. Suíte final Debug e ASAN/UBSAN:76 passaram, zero
falharam, um teste X11 sem execução (77 configurados). LSan desabilitado pela
limitação ptrace; sem resultado de leaks.

House07:180 objetos+2 avatares;8 objetos fora do mundo e37 ocultos, restando135
registros gráficos. As quatro rotações no zoom3 resolveram seus recursos com
cache novo; a cena integrada foi conferida nas vistas abaixo. House01 zoom1
carrega60 entidades, sem avatares, e desenha51 registros visíveis/54 camadas.
House03 zoom1 carrega267 entidades, sem avatares:258 registros visíveis,258
camadas,61 frames/49.974 pixels em cache e cinco partes de escada sem gráfico.
As duas casas tiveram JSON/PPM/depth Debug/ASAN iguais; o oracle completo por
frame/composição acima foi aplicado a House07.

| Vista House07 | Camadas não vazias | Frames únicos | Cache pixels | Samples testados | Shadings |
|---|---:|---:|---:|---:|---:|
|r0-z3|166|113|1.029.033|462.875|172.147|
|r2-z3|165|113|1.023.093|357.571|158.297|
|r0-z1|166|113|64.039|109.255|44.694|

R0z3 considera179 referências, incluindo13 frames vazios. Cache28assets/
68DGRPs/113frames ocupa6.174.198 bytes. Os dois objetos Beds119/120 têm18 flags
com índices pares0..16 ativos. House07 também seleciona Graphic3 para rádio129 e
Graphic4 para fonte160, em vez de forçar o gráfico inicial.

JSON, PPM e depth Debug/ASAN coincidem byte a byte nas três vistas. Um controle
Python independente verificou RGBA/índices/Z dos frames únicos usados e DGRP
(inclusive versões20001/20003), colocação, Flip, alpha, comparação de depth e
composição das135 entidades sobre o raster de avatares já validado. Todos os
RGB e float32 de depth conferem exatamente; os contadores também. O oracle
`compare_sprite.py` passou a ler versões20000..20004. Esta evidência valida
integração ODH e dados estáticos, não comportamento da VM original.

SHA256 PPM/depth House07:

- r0-z3:`51c48ff23f78c52ae7890b0335d7ec1235fa456c5d9987c563d119a90ebfe28e` /
  `b6efd63ac7a70220fcf27b966294701a6535eba39c68ae4a026ae676afbdfa51`.
- r2-z3:`3c80a0d9cf6cf5ae089c7de977936322e828f21ad0924e1bbc5aefac332024b5` /
  `3234bbc8023692f5949a754365cec7a3714950f56b6ce3c4606798b34e48c61a`.
- r0-z1:`211f37116c84e3960af79c0700193e2184b6c9dadb40d995fb7d2a52007341c2` /
  `aa9ee8cc21b7d91f74cf9de5a185d0158c43427cbdbb4e6e8ddc33efc7c3ff57`.

Arrays:42 nativos (14/casa), todos64x64/sem sufixo, mais seis mapas vazios.
Dados e transposição conferem byte a byte com RLE Python independente. Cada
casa expande135.168 bytes. SHA256 agregado de `u16LE(chunkID)+data`, em ordem IFF:

- House01:`78b598afc713631a934d70fd46b4a00081ae2e1de35f410da015db97db01d3b8`.
- House07:`8207d1d56653c82401f26139b5d674a74fdab76d99c1ada8ac7dcd674ffee5a7`.
- House03:`569675856227e2ca7bb933a7c37b32851932574ab9000e8e45247b35f9e70153`.

Os inputs selecionados tiveram seus hashes conferidos antes/depois sem mudança.
Evidência local ignorada: `reports/saved-object-native-validation.json`,
`verify-saved-objects.py`, `object-oracle/`, `house07-objects-r{0,2}-z{1,3}.*`
(somente três combinações verificadas), `house{01,03}-objects.*`,
`architecture-native-validation.json`, `verify-architecture-arrays.py`,
`house{01,07,03}-architecture.json` e logs de build/CTest. Imagens próximas e
ampla foram inspecionadas; mostram avatares e mobiliário, mas sem piso/parede.

## Reproduzir e continuar

Use o comando de [saved-avatar-render-validation.md](saved-avatar-render-validation.md)
acrescentando `--objects`. Para visão ampla, use `--zoom 1 --width 1024 --height 768`.
`odh-architecture HOUSE_IFF` emite estruturas e bytes, sem renderizar arquitetura.
Ausência de gráficos declarada é distinta de erro de recurso; demais falhas
retornam2 sem JSON de sucesso.

Consultas locais FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73:
`ARRY.cs`, `WALm.cs`, `VMGameObject.cs:71–82`, `ObjectComponent.cs:177–187,501–516`,
`AvatarComponent.cs:91–100`, `SLOT.cs:15–26`, `DGRPRenderer.cs:160–211`,
`VMTS1ActivatorNew.cs:555–584`. Revisões/créditos em [references.md](references.md).
Próximo: interpretar grades de piso/parede/terreno, resolver materiais, rooms e
obstáculos, integrar arquitetura ao compositor e continuar VM/ticks/interação.
