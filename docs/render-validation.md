# BMP e rasterização texturizada CPU — 2026-10-13

ODH agora desenha as meshes texturizadas em C++, com profundidade por pixel,
filtro e iluminação opcionais. O pipeline usa os recursos externos reais,
skeleton/pose/skinning anteriores e retorna um framebuffer portátil.
Não há ainda janela/input, instâncias colocadas pelo World, cadeira/lote,
sprites com alpha/Z ou integração ao relógio de playback. Este renderer de
avatar isolado é progresso para o Linux funcional, não sua conclusão.

## BMP e identidade das texturas

`read_bmp` aceita BMP com DIB40, planes1, BI_RGB e8/24bpp. Produz RGBA opaco
top-to-bottom. A paleta usa BGR e ignora o byte reservado; 24bpp lê BGR direto.
Altura positiva inverte as linhas, negativa mantém top-down. Stride é alinhado
a4 bytes; clrUsed0 em8bpp significa256 entradas. SizeImage0 é permitido.

O perfil ODH recusa variantes comprimidas, outros DIBs/bpp, reserved de arquivo
não zero, tabela de cores24bpp, dimensões inválidas, índice fora da paleta e
SizeImage não zero diferente do extent calculado. Confere offset/header/paleta/
pixels e bfSize antes de alocar. Gaps são permitidos; tail é reportado.
Limites padrão2048×2048 e1048576pixels por imagem; checked uint64 arithmetic.
Não implementa color key ou alpha presumido para skins.

`odh-bitmap FAR BMP_ENTRY [RGBA_OUTPUT]` permite conferir a decodificação;
`compare_bitmap.py` lê o BMP do FAR com Pillow e compara todos os bytes RGBA.
Essa dependência é somente ferramenta de validação do host, não do núcleo.

| Recurso de Textures.far | Bytes / dimensão | SHA256 do input | SHA256 RGBA |
|---|---|---|---|
| ExtremeMlgt_01.bmp |66616 /256×256|3a78fcb353f77cc47fec4b7cea152f387963d6e88c36deafa7f51a3b855e44e7|38779f541fb3e70edd3dc3635f6e6115a6c0d172ca0743bb25de3bec1dbd9e3a|
| C001MAlgt_Ross.bmp |17464 /128×128|c5f74ce609e7c6c3a438841edca860a211f0cda7cc0d6dd77897b33b09a3bb6c|1e6b0133d589385c7d737594158003dba6a55d8bb3df640521464cbe0cb65c31|

Ambos são8bpp, bottom-up, BI_RGB, DIB40, pixeloffset1078, clrUsed/SizeImage0,
sem tail. O gate Pillow conferiu262144+65536 bytes RGBA exatamente. Controle
negativo com um byte adulterado foi recusado. Builds Debug/ASan produziram
RGBA e JSON nativos byte a byte iguais, exit0/stderr vazio.

## Rasterizador e políticas

`rasterize` recebe screen vertices x/y/depth, UV e normal, triangles e texturas
imutáveis. Valida todos os inputs e budgets antes de alocar/draw, retornando
frame novo; não altera inputs nem retorna frame parcial em falha.
Usa centros x+0.5/y+0.5, regra top-left para compartilhar edges e as duas
windings, sem backface culling. Triângulos degenerados são ignorados; bounding
boxes são recortados ao viewport antes de converter para inteiros, inclusive
coordenadas enormes. Edge/bary math em double, sem quantização de subpixel GPU.

Depth float começa em+infinity, menor vence, empate conserva o primeiro
fragment. UVs/normals/depth têm interpolação afim ortográfica; drift mínimo de
depth é limitado ao range convexo dos vértices. Não há perspectiva/near-plane.
Texcoords wrap nos dois eixos sem V flip. Nearest usa texel inteiro; bilinear
usa centros de texels e quatro vizinhos wrapped. Reduz a parte fracionária antes
de escalar UVs, evitando conversão perigosa de valores finitos muito grandes.
RGB é arredondado/clamped ao byte, alpha255. Sem alpha blending, gamma ou mips.

Iluminação é opt-in; default é unlit. Quando habilitada, normal interpola e
normaliza por fragment e aplica `sqrt((dot(n,up)+1)/2)*0.5+0.5`, perfil simples
do shader de referência. Normal zero escolhe up por política ODH. Não é prova
de equivalência aos pixels GPU, câmera/lighting ou renderer original.

Limites: dimensões2048,1048576pixels de framebuffer,131072triangles,16texturas,
1048576texels cumulativos e16777216 bbox candidates. Cada candidate do bbox
clipped conta, inclusive fora das edges. tested_samples mede esse orçamento;
shaded_samples mede writes que passaram depth, podendo incluir overdraw.
Todas as texturas, mesmo não usadas, precisam ter shape exato e alpha opaco;
nonfinite/índices/budgets inválidos causam FormatError.

## Host, câmera e export

`odh-render` carrega um skeleton/pose e até16 partes explícitas. Cada parte
seleciona appearance/binding/BMF do Animation.far e um BMP do Textures.far.
Mantém as regras de identidade/ambiguidade de [mesh-validation.md](mesh-validation.md).
Não infere a textura da cabeça a partir de TextureName='x', outfit ou tom de
pele. A textura body foi selecionada explicitamente conforme o nome nominal
da mesh; isso não implementa o resolver completo de roupas do jogo.

Câmera experimental fixa: yaw45°, elevation30°, target(0,3,0), scale55 e
viewport400×400 por default. Não faz autofit por frame. Projection:

```text
screenX = width/2 + scale*(x-z)/sqrt(2)
screenY = height/2 + scale*((x+z)/(2*sqrt(2))-sqrt(3)/2*(y-3))
depth   = -(sqrt(3/8)*x + 0.5*y + sqrt(3/8)*z)
```

JSON declara original_calibrated=false. Nenhuma direção/posição de World é
aplicada ainda. --width/height/scale permitem testes, --lit liga iluminação,
--nearest troca filtro, --frame exige animação. CFPressource ainda deve bater
com xskill; selectors são exatos, --part é a única opção repetível.
O host também limita geometry real+blend65536 cumulativa, triangles/texels
cumulativos. Output obrigatório PPM P6, sem PNG/GPU/Windows dependências;
JSON só sai após escrita e close bem-sucedidos.

```sh
./build/odh-render '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult '../referencias/The Sims/GameData/Textures/Textures.far' --part b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf ExtremeMlgt_01.bmp --part c001ma_ross.cmx.bcf c001ma_ross xskin-c001ma_ross-HEAD-HEADB.bmf C001MAlgt_Ross.bmp --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 160.75 --lit --output validation-output/render-native-160_75.ppm > validation-output/render-native-160_75.json
```

## Gates e resultados

CTest29/29 passou em Debug e ASan/UBSan. BMP768 checks sintéticos incluem
truncamentos, orientações, paleta/BGR/padding/gaps/tail, budgets e variantes
recusadas. Raster80 checks analíticos incluem UV/filter/wrap, top-left sem
dupla ownership/cracks, depth/order/ties, winding/giant clipping, lighting,
finite/budgets/shape/alpha. CLI cobre multi-part depth em ambas ordens,
cores/flags/animação/seleção/erros/output e todos os campos estatísticos.
LSan permanece desativado por ptrace; não se afirma validação de leaks.

Native body/head:601vertices/704triangles, BMPs acima, adult-skeleton e
a2o-sit-left/CFP com hashes anteriores preservados. Default400×400, scale55,
bilinear wrap e --lit; exit0/stderr vazio em frames0/80.5/160.75.

| Frame | bbox tests | writes depth | pixels cobertos | SHA256 PPM |
|---:|---:|---:|---:|---|
|0|88111|21850|12891|ec72bd361425142aadd1096a61c4de18173a3cb6455cfd4b6d768ea8dce8c32e|
|80.5|86842|22226|13811|5ab0b1f9bd5892d873c73dcd0ba9ab3a087595256939b9df39e40f847a860c6b|
|160.75|104974|22487|12998|f8187deac617e3e4f04aa7954b2fbe8fb18e7a392c86a2ddcd52d39bedec9cd5|

PPMs foram convertidos localmente para PNG e os três frames inspecionados:
texturas de corpo/cabeça visíveis e mudança de postura coerente com a pose.
Não foi comparado screenshot original, nem feito oracle pixel a pixel contra
GPU; gates independentes anteriores cobrem estrutura, CFP/pose/skinning e
Pillow cobre os texels. Renderer usa checks analíticos e comparação de builds.
Frame160.75 sanitizado: PPM byte-idêntico ao Debug, JSON igual após normalizar
somente o caminho de output diferente. Todas as execuções exit0/stderr vazio.

Buffers default:1280000bytes de cor+depth,327680bytes de texels.
Teste400×240/scale32/frame160.75:4395pixels cobertos,768000bytes cor+depth,
mesmos327680bytes de texels, PPM SHA
6b18cfb8e762b95af92f4e62ddf8aa2fb0c0ab68ff280e72712897d801b5e900.
São tamanhos de payloads de buffers, não RSS total, VRAM ou validação New3DS.
Geometria, índices/caches, alocações do loader/export e overhead permanecem por
medir no runtime final. registro arquivado não distribuído e bitmap-* ficam ignorados;
nenhum asset ou imagem derivada foi versionado.

## Referências e continuidade

Formato BMP conferido nas fontes oficiais
[BITMAPINFOHEADER](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader) e
[Bitmap Storage](https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-storage).
Sampler/lighting consultados no FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73,
MPL2.0 declarada:
[Vitaboy.fx](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.content/ContentSrc/Effects/Vitaboy.fx).
Código ODH próprio; não importa shader/engine terceiros como backend.

Próximo: projetor/instâncias World e playback ligados ao renderer, sprites
SPR2/paleta/DGRP de cadeira com Z/alpha, piso/compositor/câmera calibrada e
janela/input Linux. Manter simulação independente da visibilidade e caches
gráficos limitados. Scheduler geral/queue/RNG/decay/áudio/lote/persistência
continuam pendentes. Sem original executado ou hardware validado; meta ativa.
