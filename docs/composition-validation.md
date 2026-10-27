# Composição CPU de avatar e objeto — 2026-10-13

O pipeline agora coloca o avatar texturizado e a cadeira no mesmo espaço de
projeção/depth, selecionando DGRP por direção/zoom/rotação e compondo RGBA/Z.
Os recursos comerciais continuam externos. São cenas reproduzíveis exportadas,
com posições e frame explícitos; ainda não há ligação ao World/playback em tempo
real, piso/lote, janela ou input. Esta etapa não conclui a versão Linux funcional.

## Projeção e espaço compartilhado

`WorldProjection`, `project_world`, `project_tile` e `place_sprite` vivem no core
C++17. Tile axes são (X,Y planar,Z altura); world axes (3X,3Z,3Y), Y up.
A câmera recebe center_tile, viewport, zoom1..3 e rotation0..3. Width de tile
32/64/128, altura16/32/64, escala worldPixelsPerUnit=width/(3sqrt2).
Yaw315/225/135/45 e pitch30 vêm da referência fixada. Subtrai o centro em
world units; screenY cresce para baixo. Sendo dx,dy,dz relativos ao centro:

| Rotação | horizontal H | forward F |
|---|---|---|
|0|dx-dz|dx+dz|
|1|-dx-dz|dx-dz|
|2|-dx+dz|-dx-dz|
|3|dx+dz|-dx+dz|

```text
screenX = viewportWidth/2 + tileWidth*H/6
screenY = viewportHeight/2 + tileWidth*F/12 - tileWidth*sqrt(3/8)*dy/3
depth = -sqrt(3/8)*F - 0.5*dy
```

Depth é negativo de camera-space Z, sem normalização near/far. A comparação é
uma redução afim monotônica da projeção ortográfica da referência quando ambos
os pipelines usam esse mesmo espaço. Isso é inferência matemática das matrizes,
sem comparação executada contra GPU ou original. Não há clipping near/far,
packing24bit, perturbations shader1e-11 ou equivalência de arredondamento GPU.
Coeficientes planares simplificados mantêm bordas inteiras de tiles; um teste
1×1 em viewport16×16/center(.5,.5,0) garante placement(8,7), sem drift de um pixel.

Avatar usa RotationY(pi-direction*pi/4), direction0..7, seguido de translação
world da posição explícita; normals recebem a mesma rotação. O loader conserva
pose/skinning/texture policies anteriores. Não importa offsets de headline/SLOT
para a transformação da mesh. Resolver a conversão do World/VM para posições
gráficas, incluindo origem de tiles e contenção, ainda pertence à próxima etapa.

DGRP ObjectOffsetXYZ é interpretado de floatbits, escalado(1/16,1/16,1/5) e
rotacionado no plano pelo ângulo do objeto. Local rect:
trunc(CadgeHalf+DGRP.x+offsetScreenX),
trunc(CadgeBaseline-frameHeight+DGRP.y+offsetScreenY).
CadgeHalf17/34/68 e baseline87/174/348 são subtraídos só depois da truncagem.
Então soma projeção do objeto e anchor por rotação:
(0,+tileHeight/2),(-tileWidth/2,0),(0,-tileHeight/2),(+tileWidth/2,0).
A posição final é truncada para inteiro. SPR2.Position é metadata do crop e não
é somada novamente. Esta política CPU omite scroll offset arredondado e a
correção subpixel de CameraController2D da referência; original_calibrated=false.

## Z, alpha e budgets

O origin world do sprite inclui posição/offset DGRP. Back offsets (world XYZ)
por rotação: (.15,0,.15),(.15,0,2.85),(2.85,0,2.85),(2.85,0,.15).
Front directions: (3,0,3),(3,0,-3),(-3,0,-3),(-3,0,3).
Calcula back depth e delta de front em profundidade compartilhada:

```text
t = (1 - zByte/255)/0.4        # não limitar a [0,1]; range é [0,2.5]
spriteDepth = backDepth + t*frontDepthDelta
frontDepthDelta = -3*sqrt(3/2) # ≈ -3.674234614 em todas as rotações
```

`composite_sprites` copia o framebuffer base e compõe layers em ordem. Placement
inteiro1:1, nearest; espelhamento lê RGBA e Z do mesmo texel invertido. Alpha0
não escreve cor/depth; Z255 com alpha visível continua válido. Depth menor vence,
empate mantém o fragment existente. Alpha parcial sobrevivente escreve depth.
RGB usa source-over reto uma vez, `(src*a+dst*(255-a)+127)/255`, alpha output255.
Essa é política ODH explícita: a referência premultiplica no shader, mas há
paths com estados AlphaBlend/NonPremultiplied diferentes. Não se afirma igualdade
universal de blending ou independência de ordem de layers translúcidas.

Preflight antes de copiar/draw: base opaca, depth finito/+infinity, arrays exatos,
frames não nulos com pixels+Z e flags conhecidas, parâmetros finitos e todo range
Z representável em float. Mesmo layers fora da tela são validadas. Sem mutar
inputs ou retornar frame parcial em falha. Limites:2048 por eixo,1Mipixels base
e separadamente soma de extents dos sprites,4096layers,16Mi candidatos clipped.
Referências repetidas contam novamente. Índices da paleta são metadata ignorada
pelo compositor. Empty sprite extents são permitidos por esse API; placement
com dimensões zero é recusado no perfil do projetor.

Default nativo400×400:1280000bytes por buffer cor+depth,327680bytes de texturas,
41160bytes de sprite70×98 RGBA/index/Z. A cópia imutável exige base e result
ao mesmo tempo:2560000bytes dos dois framebuffers, além das texturas/sprites.
Loader, geometria, CFP, estruturas/overhead e temporários permanecem por medir;
o decode de um novo frame tem seu próprio limite antes do gate cumulativo.
Esses números são payloads de buffers, não RSS, VRAM ou resultados New3DS.

## Host e comandos

`odh-render` conserva o modo de avatar experimental anterior. `--scene FAR IFF
OBJD_ID` ativa câmera compartilhada e objeto. Aceita --world-zoom1..3,
--world-rotation0..3, --object-direction/--avatar-direction0..7, --graphic,
--avatar-position/--object-position/--center-tile X Y Z. Scene recusa --scale;
zoom fornece escala nominal. Graphic é índice relativo a BaseGraphicID, não ID
DGRP absoluto. Flags DGRP além de flip e sprite sem Z são erros explícitos.

Defaults scene:object(0,0,0),avatar(.5,.5,0),cameraCenter(.5,.5,0),direções0,
zoom3,rotation0. Essas posições são escolhidas explicitamente para o experimento;
não constituem transcrição do World da VM. JSON informa world_connected=false,
playback_connected=false e política de posição/alpha. Até16parts, geometry/
textures budgets anteriores e sum de sprites limitado a1Mi pixels.

PPM P6 obrigatório; `--depth-output FILE` exporta float32 IEEE little endian,
um valor por pixel na mesma ordem, +infinity no fundo. JSON só após write/close.
Color/depth destinos iguais, paths aliases, symlinks e hardlinks para o mesmo
arquivo são recusados antes de escrita; falha no depth pode deixar PPM pronto,
mas não emite sucesso JSON. Nenhum asset é distribuído ou versionado.

```sh
./build/odh-render '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult '../referencias/The Sims/GameData/Textures/Textures.far' --part b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf ExtremeMlgt_01.bmp --part c001ma_ross.cmx.bcf c001ma_ross xskin-c001ma_ross-HEAD-HEADB.bmf C001MAlgt_Ross.bmp --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 160.75 --scene '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 16808 --lit --output validation-output/scene-160_75-z3-r0.ppm --depth-output validation-output/scene-160_75-z3-r0.depth > validation-output/scene-160_75-z3-r0.json
```

## Gates e identidade

Identidade dos assets detalhada em [sprite-validation.md](sprite-validation.md),
[render-validation.md](render-validation.md), [mesh-validation.md](mesh-validation.md)
e [pose-validation.md](pose-validation.md). DiningChairs.iffSHA
cffff7b4e8b5d1b31351c32a2f8656f50dbf72a82217bf38b4828853fc6f90c7;
cheapchairOBJD16808/Base1128/SPR21128/PALT2, body+head601vertices/704triangles.
Quatorze cenas: frames0/80.5 perto/rotation0 e frame160.75 em todas12 combinações
zoom/rotation. Os exports selecionados também passaram no oracle raw sprite.
PNG dos frames e montage12 vistas inspecionados: postura/alinhamento/sobreposição
visíveis. Não há screenshot original como oracle.

`compare_scene.py BASE.json BASE.ppm BASE.depth SPRITE.json SPRITE_PREFIX
SCENE.json SCENE.ppm SCENE.depth` calcula independentemente placement, back/delta,
mirror, alpha/depth e cada RGB/depthfloat32 final. O base é uma execução da mesma
mesh/camera/frame com o objeto fora do viewport; seus parâmetros, ausência de
pixels do objeto e grafo de origem/OBJD/DGRP dos exports são conferidos. Essa
verificação depende do raster base para o avatar; não testa raster GPU. Checks
analíticos separados cobrem transform de mesh com faces não vazias, yaw0/2,
posição/centro trasladados e depth no pixel-center conhecido.

| Frame/zoom/rotação | Cobertos | Sprite writes/occluded | PPM SHA256 |
|---|---|---|---|
|0/3/0|6486|2699/0|2b34536aafb57434ae0ee161b08734e6e1a024e336cfabe96537239283f05abd|
|80.5/3/0|6665|2692/7|0eaf5f49ac8728bfaf515b6c5a96c926a740ebdc52c4a787717ce8e87c7da250|
|160.75/3/0|4608|2486/213|a47053f30b489bc3d970917631a2266e74d2a2f0f8d8c88a8b203d517bbeb1be|
|160.75/3/1|5079|2238/1099|5516fc2563019493f8a5b6875a464650e801aeff5b7f8eca9fa030d0ab6472e3|
|160.75/3/2|5509|1697/1640|bb6fd0509b6a239129e8566d2b60720f4cce5f87a0c816db2d806f455dfbbd70|
|160.75/3/3|5121|1882/817|a0acd379b9cf0109bb6b62211b6b520d586dc42d6cec0d499a7d14a3f9e9e5af|
|160.75/2/0|1214|700/52|1f41de341dadbf0746a26a0c00d191cfda53452e03d0ea2ad6a96bf079f9b36b|
|160.75/2/1|1348|646/291|c345dae97e2946e22e4a6482b28b82c3366c7432664b30e6dd1a9030e2faf0d5|
|160.75/2/2|1447|492/445|bd264c352d47a6e5790de6d7eac8095eb5b2258a16ebaa5ec1207ef9bd4fdf5f|
|160.75/2/3|1333|524/228|f5b9b39bf259c71c72b860b78fcead14aff9dd594a1177ffd1268f8ebcb09a01|
|160.75/1/0|334|211/13|361b9ae7345e94db178fb5bf3ffcc377148d8c3865c06a0139ff01edd53c0382|
|160.75/1/1|366|193/81|bf432c78c558d6a38ed1e963f32c06db27158e8b002e85e92b4a49c0ab09f0fd|
|160.75/1/2|391|148/126|61c24587176e467edc0ca04169c87faed9ac2def4e6c138359b174d9e3a6a804|
|160.75/1/3|357|157/67|b35192a556eeacecb3ba2dc7b7d3692f1efaddeb3d382341ca52f19d90270eff|

Todas as14 composições RGB e depth foram byte-idênticas ao oracle. Controles
negativos com byte RGB/depth alterado e identidade OBJD diferente foram recusados.
Cena sanitizada frame160.75/zoom3/rotation2: PPM/depth byte-idênticos ao Debug;
JSON igual só normalizando os dois caminhos de outputs; exit0/stderr vazio.

CTest34/34 Debug e ASan/UBSan; gates novos195 projeção/835 composição sintéticos,
incluindo todos256 valores alpha, depth ordering/tie/flip, pixels fora da tela,
budgets/overflow/não finitos e inputs imutáveis. CLI exercita seleção, layers,
source resolution, RGB parcial, defaultpixel, transform analítico não vazio,
export depth e aliases de output. Após revisão de aliases e ampliação do gate
analítico, o render_cli foi repetido e passou nos dois builds. LSan desabilitado
por ptrace (`detect_leaks=0`); nenhuma validação de leaks é afirmada.

## Referências e continuidade

FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73, MPL2.0 declarada.
Implementação ODH própria, sem shader/engine copiados como backend:
- [WorldCamera.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Utils/WorldCamera.cs)
- [WorldState.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/WorldState.cs)
- [DGRPRenderer.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Utils/DGRPRenderer.cs)
- [_2DWorldBatch.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Utils/_2DWorldBatch.cs)
- [2DWorldBatch.fx](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.content/ContentSrc/Effects/2DWorldBatch.fx)
- [Vitaboy.fx](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.content/ContentSrc/Effects/Vitaboy.fx)
- [AvatarComponent.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Components/AvatarComponent.cs)
- [CameraController2D.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Utils/Camera/CameraController2D.cs)
- [WorldEntities.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/WorldEntities.cs)
- [ObjectComponent.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Components/ObjectComponent.cs)

Próximo: ligar instâncias/direção/posição/contenção World e relógio de playback
à cena, resolver origem/offset das coordenadas VM com evidência específica,
carregar assets com cache limitado, piso/câmera e janela/input Linux.
Scheduler/queue, RNG/decay/áudio/lote/persistência continuam pendentes.
Não houve execução do original, calibração de câmera contra ele ou validação de
consoles; a meta permanece ativa.
