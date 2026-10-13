# World, playback e preview Linux — 2026-10-13

A cena agora usa posições/direções reais do World e a amostra de animação do
Playback, no mesmo processo que executa os BHAVs. Há export de frames e janela
X11 opcional com controle de ação, pausa e câmera. Trata-se do probe construído
da cadeira; não é ainda um jogo/lote completo, com queue geral ou persistência.
A ligação gráfica torna visível a lacuna de postura já registrada no Stand.

## Coordenadas e contenção

`graphics_entity` retorna ID/geração/kind, posição gráfica e direção. Perfil
plano da referência fixada: avatar livre(x/16,y/16,(level−1)*2.95); objeto
livre(x/16−.5,y/16−.5,(level−1)*2.95). Level é1-based; não se usa2.94 como altura.
N/NE/E/SE/S/SW/W/NW usam índice0..7, DGRP recebe flag1<<index.

Avatar scale1 em storage SLOT de objeto livre usa a posição gráfica do contêiner
mais offsetXY/16 rotacionado, mais(.5,.5,0); não soma novamente a posição lógica
copiada ao filho. Z do SLOT é ignorado para esse perfil de avatar, como na
referência. Seleção usa ordinal dos registros type0 na ordem de arquivo,
confirmando a relação bidirecional da ocupação. ObjectComponent/DGRPRenderer
usam ângulos cardinais; para diagonal a rotação de offsets é zero, embora a
seleção DGRP conserve a flag. O host reutilizável e CLI explícito seguem essa
política. `place_sprite` continua aceitando ângulo genérico fornecido pelo caller.

Resolver deve fornecer a SLOT da definição conhecida do contêiner. Falta de
estado/tabela/posição, nível inválido, offsetsXY não finitos, vínculo inconsistente,
contenção não posicionada, objetos contidos, contêiner aninhado, bones de avatar
ou outra escala produzem unsupported/FormatError. Revalida geração/posição após
callback do resolver. Não há terreno, interpolação entre ticks ou avatar scale
inferida de idade. Simulação permanece independente da visibilidade.

## Identidade de animação e recursos

`Playback::sample()` fornece shared_ptr<const AnimationHeader>, actor/actorGeneration,
vmGeneration, frame/frames/end/loop. É leitura, sem avançar ou alterar controlador;
recusa ator removido/reusado, run antigo ou wait/ticket inválido antes do próximo
tick. Um loop não bloqueante terminal continua válido enquanto o run é o mesmo.
A amostra retém o recurso após cancelamento, evitando usar apenas nome como identidade.
O host confere actor/geração contra a entidade gráfica antes de avaliar pose.

`SceneRenderer` é host reutilizável, separado do core e do backend da janela.
Carrega uma vez rig/appearances/BMF/texturas e frames únicos referidos pelo DGRP
base do objeto single-part. Não retém Views depois de fechar seus streams;
Animation.far possui stream/índice próprios vivos. CFP é decodificado somente
na primeira utilização de um xskill; seleção ASCII-casefold deve ser única,
preservando a grafia real da entrada. Cache confere counts do header, até64
recursos e1048576float32 cumulativos. Rawgeometry/texturas/sprites imutáveis;
pose/skinning/projeção/triângulos são avaliados a cada render.

Budgets anteriores de geometry65536(real+blend),131072triangles,16parts e1Mi
texels cumulativos continuam. Sprites únicos decodificados somam até1Mi pixels;
frames podem ser compartilhados por direction/zoom sem duplicar pixels.
Compositor limita novamente extents por draw. Não há descarte de entidades ou
ticks fora da tela; cache atingindo limite gera erro explícito.

## Probe e janela

`odh-bhav --view TEXTURES_FAR --view-skeleton ENTRY NAME --view-part ENTRY NAME
BMF TEXTURE` exige Main probe e suas dependências. Até16parts explícitas.
`--view-frames DIR` exporta PPMs; `--view-stride1..4096` controla observação.
`--view-window` abre viewport400×400/câmera fixa center(4,4,0), zoom3/rotation0.
Com janela, Space/Enter ou clique solicita Sit inicialmente e Stand após sentar;
P pausa relógio/primitivas; setas esquerda/direita giram, cima/baixo ou +/- mudam
zoom1..3; Escape fecha. Ações/P são por borda, sem autorepeat; câmera admite
repeat. Ação enquanto pausado só fica disponível depois de despausar.

`--view-auto` executa a sequência automaticamente na janela. Preview tem clock
nominal25Hz e deadline wall-clock; atrasos não pulam nem mesclam ticks. Pausa não
incrementa ticks, hold ou post. Defaultstride é1 com janela e8 sem janela;
observação não estabelece FPS medido ou temporização original. Headless corre
sem pacing wall-clock. Render pode ser observado em boundaries além do stride.

O preview limita4096ticks e8192 observações. Budget/event exhaustion não dispara
a próxima fase sem ação/condições válidas. Runtime render/readback/export failures
param a observação, finalizam JSON coerente com view.fault/diagnostic e exit2;
assets inválidos na construção falham antes do trace. Arquivos parciais podem
permanecer em reports. Tick budget gera scene_status=budget_exhausted/exit3.
User close gera quit=true/exit0 com status VM efetivamente alcançado, mesmo
incompleto; isso não é prova de sucesso da interação.

X11 é backend opcional e isolado, com pimpl, TrueColor e conversão de máscaras/
byte-order para XImage; core não inclui Xlib. Mapeamento da própria janela é
aguardado por até2s; frames/shapes/opacity são conferidos. Eventos bounded4096
por poll. Expose/configure redesenham, foco perdido limpa estado de teclas.
Readback consulta só a janela criada pelo backend; `--view-readback` compara
seus pixels com o framebuffer em cada observação (modo de validação).
Quantização em visuais RGB com menos bits pode diferir do RGBA8, não se afirma
igualdade universal de visuais. Window resizing não é interface suportada.

CMake procura X11 quando ODH_X11=ON no Linux; ausente, a ferramenta headless
continua compilável e pedido de janela dá diagnóstico. `-DODH_X11=OFF` remove
backend/link X11. Requer libX11 development para construir o preview; SDL não
é dependência desta etapa. Teste de janela pode retornar77/skipped sem display
acessível. O sandbox da sessão não acessava X11, então gates nativos foram
executados com acesso ao display, exclusivamente nas próprias janelas de teste.

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff --animation-probe '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf --age 21 --sound-probe --reset-probe a2o-sit-loop.cmx.bcf --post-ticks 240 --stand-after --animation-extra adult-chair-stand-new.cmx.bcf --main-probe --hold-ticks 120 --view '../referencias/The Sims/GameData/Textures/Textures.far' --view-skeleton adult-skeleton.cmx.bcf adult --view-part b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf ExtremeMlgt_01.bmp --view-part c001ma_ross.cmx.bcf c001ma_ross xskin-c001ma_ross-HEAD-HEADB.bmf C001MAlgt_Ross.bmp --view-window > reports/live-manual.json
# Acrescentar --view-auto para sequência automática;
# trocar janela por --view-frames reports/live-headless para observação sem display.
```

## Gates

CTest37/37 passou em Debug e ASan/UBSan, incluindo teste da própria janela X11.
Novos checks:261 graphics state e98 playback total, com recurso/geração/run,
contenção, slots/cardinais/diagonais, stale callback e snapshots. Host sintético
com animação produz RGB/depth byte-idênticos ao CLI de controle, não duplica
cache CFP, rejeita counts/nomes ambíguos/NaN/moved object e captura fault de
CFP ausente sem mudar World. Controller de preview testado em janela própria
com teclas sintéticas: ação sob pausa permanece indisponível, despausa libera,
segunda Space após release solicita nova ação, Escape propaga quit; não altera VM.
Backend conferiu todos12288pixels RGB de uma fixture, shapes/opacity, mappings
teclado/held-repeat. Esses são testes sintéticos dirigidos à própria janela,
não ações humanas nem gameplay do executável original.

Nativecheapchair16808/GUID26BFBB29/Base1128, body/head601vertices/704triangles,
mesmos assets/hashes das etapas anteriores. --hold-ticks120 +post240 termina
94dispatches/662ticks/comfort3, ambas fases true; JSON inteiro da simulação,
sem view, é exatamente igual ao controle sem render. Headless produz86frames;
Debug/ASan todos byte-idênticos, JSON view igual normalizando só outputs.
ODH_X11=OFF compilou/rodou o mesmo probe: sem libX11 no ldd,86frames idênticos
e mesmo JSON da simulação/view normalizando paths.

Execução automática nativa na janela:87 observações (um redraw adicional),
todos87 readbacks160000pixels iguais aos frames apresentados; mesma simulação
JSON do controle. Isso prova o backend/ligação desta cena no desktop atual,
sem equivalência ao renderer original, performance final ou port console.

Cache final:3 CFPs/50518float32/202072bytes,327680bytes RGBA texturas,
119244bytes em6 frames únicos de sprite RGBA/index/Z. Cor+depth1280000bytes por
framebuffer400×400; compositor mantém base e cópia durante draw. XImage e
readback acrescentam seus buffers. Isso não é RSS/VRAM; overhead, metadata,
meshes/BHAV/controladores/logs permanecem por medir. LSan desabilitado por ptrace,
`detect_leaks=0`; nenhuma validação de leaks é afirmada.

Cinco snapshots reais reconstruídos no CLI de controle com posições/direções,
clipe/frame extraídos da observação: RGB inteiro e counts de oclusão iguais.
Float frame é reconstruído como float32 exato para não introduzir diferença
na fração de pose. PNGs e sequência inspecionados; reports fora do Git.

| Tick | Postura | Clipe | SHA256 PPM |
|---|---|---|---|
|0|0|rest/sem clipe|23f1bff57bdc2d3bd36ee488a4c4f62d1f7a968e31f7a6a203a831045b35fbe3|
|169|1|a2o-sit-loop|89d1831e8dc07c6bd98763a1754a745e0d47e2e6a87530ef8e188e84840e1ccc|
|360|1|a2o-stand-left|a4df7d5f1a33bc2c43146f2924cbbc46e169e5750783a9b918f7d00170bf23e3|
|422|0|a2o-sit-loop|357acb5e8cfbeaa0d10b94857c6b40133310ffd58d415c333ddc03d0c88a7f51|
|662|0|a2o-sit-loop|e7803e800c36698c987f79a6f1c7032ef5cd5715492161c2c1950f4817cf7faa|

Teste native hold4096/stride4096 atingiu exatamente tick4096, retornou exit3 e
JSON scene_status=budget_exhausted/view.tick_limit_reached=true, com apenas a
fase Sit; Stand não foi despachado depois da exaustão.

## Lacunas observadas e próximo passo

Ao terminar Stand, World registra container0/posture0/position(48,64,1,dir6),
mas o reset feito antes da escrita de posture0 selecionou Sitloop. A imagem
mostra avatar sentado no ar fora da cadeira nos ticks422/662. É evidência visual
ODH da lacuna já documentada em [stand-validation.md](stand-validation.md),
não prova do comportamento original. Não foi substituído por pose de pé.
É necessário investigar encerramento/queue/Function20 e inicialização de idle,
com referência/original/Ghidra para a pergunta concreta, antes de afirmar
interação graficamente fiel.

O início sem clipe usa a pose base do skeleton, com avatar parcialmente fora do
viewport fixo. Inicialização standing/walk e interpolação de rota ainda faltam;
não se usa autofit por frame para esconder a diferença. Scale/terrain/bone slots,
OBJf/scheduler/queue geral, picking, piso/paredes/lote, RNG real/decay/áudio e
persistência delimitada continuam pendentes. A janela mostra um probe controlado;
a meta Linux funcional/testável permanece ativa.

## Referências

FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73, MPL2.0 declarada:
- [VMEntity.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMEntity.cs)
- [VMAvatar.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs)
- [VMGameObject.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMGameObject.cs)
- [LotTilePos.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Model/LotTilePos.cs)
- [EntityComponent.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Components/EntityComponent.cs)
- [ObjectComponent.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Components/ObjectComponent.cs)
- [AvatarComponent.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Components/AvatarComponent.cs)
- [DGRPRenderer.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Utils/DGRPRenderer.cs)

Código ODH próprio; não há execução do original, projeto Ghidra TS1 importado,
calibração completa contra screenshots originais ou hardware console validado.
Originais preservados e derivados/outputs locais fora do Git. Continuidade OMM
com hashes de fontes e revisão Git verificada.
