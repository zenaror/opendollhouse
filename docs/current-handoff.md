# Continuidade atual

## Inventário RoomInfo completo do World vivo (2026-10-21)

`snapshot_world_room_info` reconstrói e valida o inventário completo de
footprints RoomInfo por sala, preservando coleção e ordenação por owner ID;
`query_world_room_info` agora filtra a caixa candidata dessa mesma fotografia,
mantendo a semântica de inclusão estática/exclusão dinâmica do alvo. O
`odh-person --saved-world-main` exporta o inventário junto do probe atual.
House07 no tick 214 retornou 19 footprints completos na sala 3, todos
estáticos nessa amostra. Build completa e 157 CTests passaram; o teste de
janela ficou pulado sem display X11. Próximo: combinar essa lista do World com
RoomObs/WallObs e regras de AttemptWalk no mesmo processo, incluindo portais e
EntryPoint5 antes de qualquer alegação de clearance ou uso no Main.

## Snapshot de obstáculos AttemptWalk no House07 (2026-10-21)

O módulo puro `odh_saved_attempt_walk_obstacles` compõe WallObs + RoomObs,
faixas externas de um tile (coordenadas inclusivas zero-based), footprints
elegíveis expandidos por 3 unidades, flags e EP5 bilateral. Action IDs <=255
resultam em false; acima disso o chamador precisa de um resolver restrito. O
curto-circuito do EP5 do ator evita exigir a tabela do bloqueador quando já
retorna true. Limites de entidades/retângulos/chamadas e dados desconhecidos
falham fechados, sem expor lista parcial. Nesta revisão a build completa passou
e CTest registrou 157 testes sem falhas, com `linux_window_backend` pulado
porque `DISPLAY=:0` está inacessível.

O diagnóstico House07 lê tabelas EP5 da definição exata e avalia apenas o
subconjunto read-only existente. Quatro portas com caixa salva e HasZeroExtent
desligado fornecem footprint candidato apesar do estado runtime desconhecido;
o snapshot ficou `ready` com 153 retângulos. O módulo bounded
`odh_rect_router` agora reproduz o corredor de `VMRectRouter`/`VMObstacleSet`
sobre ordem explícita de obstáculos, interseção de área estrita, Contains
inclusivo, expansão inicial por heading, ordenação A* estável e limites
fail-closed. `rough_balance_obstacle_prefix` agora reproduz o shuffle parcial
seed-1 aplicado por Simitone ao prefixo `wallObs+roomObs`; vetor de 16 itens
confere com .NET 10. A CLI reordena esse prefixo, mantendo bandas e entidades
depois dele. `OptimizeLines`, portais e movimento continuam fora dele; ordem
`roomInfo.Entities` ainda não foi provada contra owner-ID ODH. No probe House07,
os três destinos já aprovados pelos
gates estáticos deram `found`; dois tiveram corredores de 2 entradas e o
destino igual à posição salva do ator retornou corredor vazio. O heading
derivou da direção salva em oito notches, com arredondamento ties-to-even. A
busca com shuffle somou 4.402 unidades de trabalho e manteve `route_clearance=false`,
`runtime_door_state_validated=false` e `main_movement_integrated=false`. Isso
aplica o modelo Simitone à fotografia salva, não prova a rota runtime do jogo.
Próximo: resolver RoomInfo/portas e portais no mesmo World e testar destinos
fora da posição inicial antes de integrar Route45.

Validação real usou `House07.iff`, `Objects.far`, duas definições de avatar,
Neighborhood, 23 fontes arquitetônicas e `Global.far/Global.iff/SLOT100`.
`git diff --check` passou. Sem validação visual: a janela do CTest foi pulada
pelo display X11 inacessível.

## BHAV read-only EP5 e House07 bloqueadores (2026-10-21)

`odh-person --placement-candidate` agora passa um resolver de EP5 que só executa
BHAVs com formato validado: TestObjectType(StackObject), seguido de igualdade
de StackObjectAttributes[16] com literal 1. Cada execução usa VM e World
temporários, no máximo duas instruções, e não tem opcode de escrita permitido.
Todo formato, dado de atributo ou rotina fora desse perfil permanece
unsupported. Flamingos 155/156 do House07 fornecem EP5 4108 no asset exato
`Objects.far/flamingo.iff`; `(280,376,1,3)` intersectou o 155 e classificou
EP5(Bob 105)=false em uma instrução. `(456,504,1,3)` encontrou IDs 35/101 e
classificou a posição como bloqueada por 35, com dois pares e EP26=false.
RoomInfo completo descreve o inventário de entidades do World restaurado; essas
posições são probes, não movimento real. Todos deixam
`placement_decision=not_evaluated` e `route_clearance=false`.

Testes focais passam para o executor read-only, auditor de overlap e CLI. A
próxima etapa é clearance arquitetônico/portal e busca de rota limitada no World
atual; não conectar Routing45 ao Main até validar esse clearance e estados de
porta/ocupação runtime.

## Consulta RoomInfo a partir do World vivo (2026-10-21)

`query_world_room_info` enumera o World atual, deriva OBJD boxes, salas e
coleções static/dynamic e reconstrói o índice RoomInfo antes de cada query.
Isso mantém consultas frescas sem observers para toda mutação. O limite é 4096
entidades por consulta. O caller precisa fornecer a sala runtime e o
`KillTimeout` atual do avatar; dados desconhecidos, flags ausentes, definição
ambígua e geometria/sala sem suporte falham fechados. A API preserva a regra de
RoomInfo de incluir o alvo na coleção estática e excluir da dinâmica.

`SavedLotRenderer` agora guarda mapa de sala derivado de arquitetura preparada
com aliases de piso/parede não ambíguos. O saved-world preview adiciona ao JSON
`room_info_probe`, consulta diagnóstica do avatar selecionado contra obstáculos
live no mapa congelado. `World` inicia avatares com KillTimeout=-1 e expõe
setter validado; lifecycle também faz parte da checagem de snapshot.
`odh-world-room-info-tests` passou com fixtures sintéticas para movimento entre
salas, contenção, zero extent, classificação, ordem, entradas desconhecidas e
orçamento, incluindo mutação disparada por callback. Callback recebe cópias e a
API verifica a projeção do World antes de aceitar o índice. `World` sincroniza
ObjectData[1] com a direção espacial em escritas VM e mudanças de posição. O
probe não executa EntryPoint5/26, policy allow/disallow, nem clearance, rota ou
interação; a arquitetura ainda é imutável no host. Build/CTest: revalidar após
este incremento. Esse contrato ainda não alimenta
por Main e não valida clearances, EntryPoint5/26, portas/portais, movimento ou
Sit/Stand no lote. Próximo: fornecer um resolver de sala runtime derivado do
lote Main sem promover o mapa geométrico salvo a estado de porta/portal; depois
compor as regras restantes de GetAvatarPlace e conectar clearance a Route45 e
Snap46 no mesmo World da UI.

## Estado runtime do footprint durante Route45 (2026-10-21)

`World::Entity` agora carrega `static_footprint` como estado runtime, separado
do estado salvo. `Routing` mantém o ator estático enquanto planeja, torna-o
dinâmico no primeiro passo real e o restaura ao concluir, falhar ou cancelar
por restart do VM. A transição segue o perfil do Simitone fixado
(`VMRoutingFrame::StartWalkAnimation`/`PreExit`); ainda não foi confirmada no
executável original. O setter protege contra entidade removida ou geração
reutilizada.

Os testes `world` e `routing` passaram; rebuild completo e CTest passaram com
150/150 testes sem falhas, sendo `linux_window_backend` pulado por display
indisponível. `git diff --check` passou. Sem validação visual. Isso ainda não
atualiza o `RoomInfoObstacleIndex`: `World` não publica eventos de movimento,
reclassificação ou remoção para o índice, e Main não tem clearance runtime.
Próximo passo: construir adaptador owner-keyed entre eventos/lifecycle de
`World`, geometria de `ObjectDefinition` e o índice; invalidar consultas caso
algum evento ou footprint esteja incompleto.

## Seleção de destino SLOT sobre footprints salvos

`assess_saved_slot_goal_selection` separa seleção de destino da caminhabilidade
da grade. No perfil v10 type-3 standing=1/sitting=0, unsnapped, ele testa o box
do avatar na posição candidata contra os footprints salvos da mesma sala.
Box livre é `snapshot_clear`; objeto sólido sobreposto é
`object_overlap_rejected`; avatar sólido conhecido é
`avatar_overlap_score_penalty`, conservando a distinção da referência entre
rejeitar o destino e reduzir seu score. A API não calcula o score nem executa
GetAvatarPlace: trata os footprints conhecidos como sólidos conservadores.

Geometria ausente, inventário incompleto, avatar incerto sobreposto, porta
relevante e multipartes sobrepostos são `unsupported`, com elegibilidade
`null`. Auxiliares de identidade ocupam três bitsets fixos de 4 KiB e o
inventário padrão limita entidades/membros a 4.096. A CLI reporta estado,
motivo, elegibilidade e contagens por candidato; só envia à busca os destinos
que passam os gates estáticos anteriores **e** têm elegibilidade de snapshot
`true`. A grade continua bloqueando os mesmos footprints para o caminho.
Flags Allow/Disallow, EntryPoint 5 bilateral, registro dinâmico, consulta de
portas, política multitile e sitting>0/EntryPoint26 seguem não modelados.
`runtime_occupancy_validated` e `route_clearance` continuam false; Main não usa
o resultado.

Proveniência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, submódulo
FreeSO `f86bbd34b2112296ed786741f640a0535886d180`:
`Engine/VMSlotParser.cs:264-321` faz a seleção objeto/avatar/assento e penaliza
avatar/porta; `VMContext.cs:1190-1235` monta os obstáculos de GetAvatarPlace,
aplica EntryPoint5 e flags. O modelo atual usa apenas o ramo sitting=0 sobre
footprints conservadores; não afirma equivalência ao executável original.

No House07 local com as 23 fontes arquitetônicas externas, a CLI reportou
oito candidatos: seis `snapshot_clear`, dois `unsupported` por sobreposição
com multipartes (IDs 106 e 125), zero objetos simples rejeitados e zero
avatares conhecidos com penalidade. Permaneceram três destinos da busca,
com caminho de um ponto/custo zero pois o avatar já estava em um deles. Os
ramos objeto/avatar foram testados com fixtures sintéticas. O teste focal
passou 32 checks e o build completo concluiu. CTest passou 145/145 com
`DISPLAY=:0` acessível, incluindo `linux_window_backend`, sem skips.
`git diff --check` passou. Incremento revisado; ainda sem commit nesta etapa.

## Gates estáticos para candidatos SLOT Route45 (2026-10-21)

`odh-wall-appearance --route-grid-diagnostic` exige
`--route-slot-resource ARCHIVE ENTRY CHUNK_ID TYPE INDEX` e lê o SLOT
selecionado de forma exata e fail-closed. O perfil v10 type-3 isotrópico gera
oito posições circulares; cada uma reporta `RouteEntryFlags`, máscara, bounds,
sala, `FloorChangeValid`, `RaycastWall` e caminhabilidade. O raio/diagonal é
um perfil ODH restrito; o raycast usa a fotografia inicial salva das arestas.
House07 `Global.far/Global.iff/SLOT100`, type 3 index 1 (offsets zero) teve
três candidatos que passaram os gates estáticos; todos estão na sala 3, piso 6
e raycast limpo. Um coincide com a posição salva do avatar, resultando em
caminho de custo zero e um ponto. Continua sendo apenas
`candidate_grid_search_only`, com `route_clearance=false`. Não valida
`GetAvatarPlace` dinâmico, flags Allow/Disallow, EntryPoint 5, portas vivas ou
semântica do executável original.

`odh-routing-tests` passou 56 checks. Build completa e 143 testes passaram;
`linux_window_backend` foi pulado porque o X11 retornou `DISPLAY inaccessible`
mesmo com `DISPLAY=:0` e socket X11 presente. Não houve validação visual neste
checkpoint. A verificação CLI assertiva confirmou oito candidatos reportados,
três com todos os gates estáticos; todos os oito tiveram `FloorChangeValid`
allowed e raycast clear. `git diff --check` passou. O commit de candidatos
SLOT foi publicado em `origin/main` e confirmado por `git ls-remote`. O
incremento dos gates estáticos ainda precisa de revisão final e publicação.

## Máscara direcional do SLOT no Route45 (2026-10-20)

O `Routing` filtra os oito candidatos do perfil de raio exato pela máscara
direcional e grava no `RouteEntryFlags` o resultado de
`VMSlotParser.GetSearchDirection` do Simitone fixado. A regra usa floor de
coordenadas em 1/16 tile, rotação do objeto, setores angulares e interseção com
os bits baixos do SLOT. O dado comercial House07 confirma que Route45 usa
Global SLOT100, type3/data1, flags `0x55`, raio `16`, standing=1 e facing=-2;
isso foi lido, sem alterar ou copiar recursos comerciais.

`odh-routing-tests` passa 51 checks, inclusive uma célula walkable em um setor
que a máscara `0x55` rejeita. Build completa e CTest 144/144 passaram com o
teste `linux_window_backend` usando o display `:0`. Esse marco verifica apenas
o controller em mundos construídos e a semântica da referência Simitone; não
liga Routing45 ao Main e não prova movimento no lote. Próximo: substituir o
diagnóstico genérico ao redor da caixa do alvo por candidatos explícitos do
SLOT verificado, depois modelar validação de parede/ocupação/portal e integrar
somente quando a geometria do lote puder ser comprovada.

## Grade candidata por sala (2026-10-20)

Commits `28403d3` e `8bdb5d4` estão publicados em `origin/main`. O adapter
`odh_saved_room_route_grid` transforma uma sala pronta em grade 1/16 tile,
bloqueando áreas fora da sala, RoomObs, WallObs e caixas de entidade com margem
do AttemptWalk. O teste focal do adapter cobre 15 checks; build completa e
CTest 144/144 concluíram sem falhas (143 executados, `linux_window_backend`
pulou no sandbox); o gate X11 separado passou 1/1 em `:0`. A geração de
candidatos WallObs usa as fórmulas de `VMRoomMap` na
arquitetura salva; arestas sem face reconhecida são conservadas como sólidas.
O inventário atual é global e não mesclado por andar, enquanto Simitone coleta
e mescla `wallObs` por sala; ele não é byte a byte idêntico às listas runtime.
Limites: 262.144 células, 4.096 entidades, 16.384 retângulos e 4.194.304
células rasterizadas; o adapter falha fechado em entradas incompletas. WallObs
usa somente o snapshot inicial de faces e não modela estado runtime da porta.
A unidade ainda não executa política runtime, EntryPoint 5, validação de
SLOT/goal, portal ou VMRectRouter. `ready` ainda não significa clearance, e
Main continua sem usar o novo resultado.

O `odh-wall-appearance --route-grid-diagnostic 105 128` gera uma busca estática
opcional para Bob e o alvo 128 na sala 3 de House07. Na cópia local, a grade
ficou `ready` (10.240 células, 4.529 caminháveis), start caminhável e `find_route`
retornou 9 pontos/custo 80 até `[392,448]`. O probe procura a vizinhança da
caixa expandida do alvo, não lê SLOT, e não valida aceitação do jogo. Continua
com `route_clearance=false`; o resultado não conecta Main/Routing45. Os testes
de obstáculo agora cobrem as duas diagonais, wrap de portas vizinhas e flags
malformadas. Próximo passo: identificar com precisão as entradas SLOT e os
gates de AttemptWalk para a interação Sit, depois adaptar esse diagnóstico
para testar posições válidas sem rotular `find_route` como VMRectRouter.

## Estado verificado da interação e auditoria de limites (2026-10-20)

A nota abaixo sobre a fila parada foi superada: no teste posterior registrado em
`docs/interaction-validation.md`, a fila privada foi consumida e a VM chegou à
cadeia `4097→4100→4099` no opcode 45 aos 103 ticks. O resultado foi
`unsupported primitive 45`; isso confirma a execução BHAV até o serviço, não
movimento nem conclusão de gameplay. O Main segue sem integrar o controller
Routing ou fornecer uma malha de clearance.

A auditoria estática por entidade agora também compara a shape expandida com as
quatro faixas externas de um tile que `VMRoutingFrame.AttemptWalk` constrói ao
redor de `VMRoom.Bounds`, além de RoomObs. No House07: 50 não cruzam RoomObs,
41 cruzam RoomObs, zero cruzam as faixas externas, 22 permanecem unsupported e
69 not_applicable. São resultados de sobreposição estática por entidade;
`routing_clearance` continua false. A auditoria tem 25 checks focais; o build
completo e 143 CTests passaram. O gate X11 também passou em execução separada
com acesso ao display `:0`.

Próximo incremento: criar um adaptador de clearance local ao lote, com orçamento
explícito e estados fail-closed para chão, RoomObs, faixas externas, paredes e
entidades; antes de habilitar Routing ainda será necessário resolver elegibilidade
runtime, EntryPoint 5 bilateral, RouteEntryFlags/slot e caminho VMRectRouter.

## Ordem de restauração de portas nas paredes (2026-10-20)

O adapter `odh-wall-appearance` agora ordena uma cópia das entidades por
ObjectID ascendente para aplicar escritas de estilo, seguindo o caminho de
restauração documentado no Simitone: entidades ordenadas e replay sequencial de
`PositionChange(Context,true)`. A API genérica permanece caller-ordered. O
conflito sintético cobre uma escrita de porta seguida por clear em ObjectID
maior. Isso representa apenas o estado inicial no perfil dessa revisão do
Simitone; mudanças runtime posteriores e equivalência com o jogo original não
estão provadas. Documentação: [associações arquitetônicas salvas](wall-appearance-validation.md).

## Campos de colisão OBJD (2026-10-20)

O leitor OBJD136/138 preserva `TileWidth` e `FootprintMask` quando ambos estão
presentes nos offsets verificados pela ordem de campos Simitone/FreeSO; views
curtas continuam válidas e deixam ambos ausentes. Isso ainda não os converte
em obstáculos vivos nem habilita Routing45. Testes de offsets/truncamento e
build dos consumidores passaram; `odh-saved-world` também leu House07 original
com `Objects.far` e os dois IFFs de personagens sem erro. Veja
[campos de colisão OBJD](object-collision-validation.md).

O incremento local após `94c7dc7` adiciona `odh_collision`: cálculo dos bounds
do OBJD com rotação/fallback de referência, ausência para HasZeroExtent/objetos
contidos, box candidato do avatar ±3 e interseção estrita. `odh-collision-tests`
tem 16 checks de fórmula, não de colisão no lote. Build completa e CTest
129/129 passaram; a janela segue pulada sem DISPLAY. Ainda faltam validar floors,
doors, rooms, objetos dinâmicos e política do avatar; não conectar Routing45.
O avanço local atual adiciona `odh_route_geometry`: piso preciso por posição
1/16 tile e Bresenham TL/TR com solidez externa explícita. O novo CTest cobre
coordenadas fora de faixa, diagonais de piso, oito octantes, reversão e portas/
paredes sólidas, não sólidas ou sem resolução. A geometria continua sem vínculo
com o Main e não basta para aceitar destino: `FloorChangeValid`, rooms,
inclinações, obstáculos dinâmicos e construção autoritativa do estado de portas
seguem pendentes. Build limpa out-of-tree concluiu; CTest passou 130/130, sem
falhas, com `linux_window_backend` pulado por falta de DISPLAY. `git diff --check`
passou. Ver [geometria de rota](route-geometry-validation.md).

## Main em lote restaurado (2026-10-20)

`odh-person --saved-world-main` restaura House07 com `make_saved_world`,
seleciona o avatar 105 por GUID/User00000.iff e inicia um Main novo no mesmo
World da VM/Playback. Threads e filas salvas continuam dormentes; não é uma
retomada da simulação salva. `SavedLotRenderer` desenha terreno, pisos,
avatar Main, paredes/junções e objetos salvos nessa captura, sem construir
outro World. A câmera acompanha a posição gráfica viva. Outros avatares
continuam no World, mas ainda não são desenhados.

O incremento posterior a `c1df46d` liga `SavedObjectRenderer` à mesma cena,
lendo Graphic/Hidden/posição/direção vivos e SLOT do contêiner. Paredes
binárias precedem objetos alpha, com depth comum. Objetos removidos deixam
de desenhar e de fornecer aparência de parede; reuso de ID exige refresh do
renderer. Flags dinâmicas e centros multitile permanecem snapshots; criação
de objetos, movimento runtime de grupos e transparência global ordenada
continuam pendentes. Cinco CTests focais e o CTest completo (127/127) passaram;
`linux_window_backend` foi pulado sem DISPLAY.

Com House07, Neighborhood e recursos locais externos, aos 214 ticks o VM está
`waiting`, com 308 instruções e cinco frames ativos. A cena 320x240 usou 6.323
triângulos de terreno, 1.365 de piso, 69 camadas de parede e 49 junções; o
avatar usou 684 vértices/840 triângulos. O probe atualizado adiciona 135
objetos, 166 camadas, 6.174.198 bytes de frames de sprite, 130.766 amostras
testadas e 27.588 sombreadas. Recibo/PPM ignorados:
`reports/odh-person-main-lot-house07-objects-tick214.json/.ppm`; SHA256 do PPM
`c14edf44b16cdfe5d80fb9fc7b1251606f027dc6d5af8a4656fd2ba62bb88309`.
O frame CPU foi inspecionado; isso não valida gameplay ou aparência equivalente
ao original.

Os incrementos `bb2db4e` e `281a972`, posteriores a `e325706`, adicionam seleção
de objetos por Button1 no preview Main com lote e preservam `TreeTableID` do
OBJD136/138 no pick e JSON. House07 original confirma cadeira128/GUID
`0x26BFBB29`/TreeTableID128; sua TTAB128 em `DiningChairs.iff` decodifica uma
linha Action4097/Test4098. O clique usa o último frame apresentado, antes de
aplicar rotação/zoom agregados, e revalida geração, GUID e Hidden vivos. O
buffer opt-in usa o mesmo passe alpha/depth dos sprites e no máximo um uint16
por pixel (2 MiB no teto; 153.600 bytes em 320x240). Picking é coberto por
fixtures; a janela real ainda não foi exercitada.

No HEAD local `edd21fa`, `Vm::install_owner` permite anexar um banco
private/semiglobal em safe point, sem reiniciar Main nem alterar estado
suspenso. O commit `b888f63` liga Space/Return à primeira linha
comum elegível da TTAB: recalcula TestFunction contra o World atual, carrega a
closure privada da ActionFunction do `source`/`asset` exato, instala um
CodeOwner separado por recurso e ação e enfileira pela `InteractionQueue`.
JSON diferencia pedido, fila, rejeição, fault e retorno do BHAV. O limite padrão
da VM é 64 owners incluindo owner zero (até 63 anexados). Isso ainda não
apresenta o pie menu completo nem comprova sentar.

A ação House07 4097→4100→4099 alcança Routing45; o Main host ainda não liga esse
serviço nesse caminho. O controller roteia com Global.iff SLOT100/tipo3/data1,
mas não existe conversor de arquivos de arquitetura/objetos do lote para a
malha de clearance. House07 tem 64x64 tiles; a resolução de 1/16 tile do World
resultaria em 1024x1024 células para o lote todo, acima do limite padrão. O
checkpoint antigo usava 16 bytes/célula em Linux64: 4 MiB no limite 262144 e
16 MiB no lote inteiro. As alterações locais atuais reduzem os arrays fixos a
9 bytes/célula (2,25 MiB no limite e 9 MiB no lote inteiro), mais até 2 MiB de
frontier e memória do caminho no limite padrão. São contas de buffers máximos,
não RSS medido. Uma diagonal segment16 no piso1, x31/y30, deve resultar em
`unsupported_geometry` até o rasterizador diagonal existir. O atalho `start ==
goal` agora exige célula livre dentro da malha; o Simitone valida disponibilidade
do slot antes de entrar em `TURN_ONLY`, mas essa condição ainda não é fornecida
por um adaptador do lote no Main host. O opcode15 da
BHAV4099 só aparece após RouteEntryFlags fora de 1/4/16/64 ou Snap46 false;
payload do BHAV é idêntico entre The Sims original e Legacy, mas a semântica
desse opcode no executável original permanece desconhecida. Na referência
Simitone os ramos usam scope18/MyPersonData e index37/RouteEntryFlags; opcode15
está mapeado como breakpoint. O opcode49 é NotifyOutOfIdle.

Build completa sem warnings e CTest atual: 128/128 aprovados, sem falhas; o
gate `linux_window_backend` foi pulado pois `DISPLAY=:0` não tem servidor X
acessível. O teste sintético de descoberta agora tem 26 checks e o conjunto
focal 4/4 passa. A tentativa de ação real pela janela e a conversão de rota do
lote continuam pendentes. Outros avatares visíveis, stacks salvas, áudio audível,
serviços Main, save/restore ODH e validação X11 também estão pendentes; logo
ainda não é um build jogável. Detalhes e hashes em
[validação do Main restaurado](person-main-render-validation.md).

## Checkpoint anterior — Main construído e posição projetada (2026-10-20)

`odh-person` agora pode exportar um frame PPM headless usando o `PlaybackSample`
vivo do Main. O caminho usa `AvatarBodyStrings` diretamente para resolver corpo,
cabeça e seis variantes de mão, decodifica CFP pelo `AnimationHeader` exato
selecionado no catálogo multi-FAR e passa clip, frame e gestos ao renderer.
Não cria `ObjmPersonState` nem escolhe animação por nome. A opção
`--render-saved-avatar OBJECT_ID` valida GUID e tipo contra o recurso de pessoa
carregado e projeta a posição/direção de ObjM salvo; esse dado não é inserido no
World do VM. `--render-window` acrescenta preview X11 paced a 25 Hz (cadência de
apresentação, sem calibração de tempo original), usando o mesmo Playback/VM;
P pausa, setas giram, +/- ajustam zoom e Escape fecha. Cliques não disparam
interações.

Validação com os arquivos externos da instalação original: os relatórios
`reports/odh-person-main-first-sample-20261020.json`,
`reports/odh-person-main-tick214-20261020.json` e
`reports/odh-person-main-savedpos-verified-20261020.json` registram frames do
Main em ticks 2 e 214. No tick 214 o clip foi
`a2o-idle-neutral-trans-lstand-lhips`, frame 1.2; o frame usou 684 vértices,
840 triângulos e 425.984 bytes de textura. Os PPMs são preservados em
`reports/odh-person-main-*.ppm`. No frame posicionado, ObjectID 105/GUID
850010198 projeta para `[24.5,27.5,0]`, direção 3, com a câmera centrada nessa
posição. Sete CTests focais passam e
`git diff --check` passa. A execução encerra em `waiting` ao esgotar o limite de
214 ticks (código 3 esperado pelo host); não é falha de renderização nem prova
de que Main concluiu.

Limites atuais: a posição ainda é projeção de apresentação, não posição viva no
World; lote/arquitetura não são desenhados e não há entrada de usuário para
gameplay. O preview X11 compila, mas o ambiente de validação atual não tem
servidor DISPLAY acessível; a cena não é jogável e o Main ainda pode parar em opcode/comportamento não
implementado. Próximo passo de maior valor: unir essa posição com o desenho do
lote salvo, preservando o mesmo Playback/VM e sem clonar o World.
Só depois avançar para ações contínuas, tempo/necessidades e persistência ODH. Detalhes em
[validação do frame do Main](person-main-render-validation.md).

Meta integral: primeira versão funcional e verificável no Linux; New 3DS, Vita
e Android são etapas futuras com validação própria. ODH exige dados do jogo
fornecidos pelo usuário, sem dependência do executável original ou checagem de
CD. Preservar os milestones de leitores, VM, cena/interação, tempo e save; os
resultados Linux não comprovam compatibilidade dos ports.

## Incremento atual — manager HIT por `TrackID`

O trabalho posterior ao checkpoint de áudio adicionou `HitTrackManager` como
orquestrador sintético de controllers registrados explicitamente. Um controller
é criado na fronteira de registro, nunca por Play; Play resolve evento→Track→
entrada global, aplica overrides, gates e contribuições; refresh/remove aplica
agregados ou fade sem apagar o controller. Recusas de lookup/registro antes de
efeitos são recuperáveis. O ajuste de `service+0x114` permanece apenas em
refresh/remove; o Play inicial usa o agregado bruto.

Cobertura final: receipt
`reports/hit-track-manager-20261020/validation.json` SHA256
`4a5bff2329e0fb5ac706c648e437719c580b023f161292c5502b54eeff04ec92`.
Debug e ASan/UBSan executaram o CTest do manager, cada um com 38/38 casos e
98 checks, sem falhas ou skips. Cinco CTests companheiros por gate foram
reutilizados explicitamente, sem rerun; os logs/receipts pré-fix continuam
preservados. Revisão independente aprovou o recorte.

Limites: os providers são sintéticos; a unidade não liga o manager a World,
gerações, scheduler/Clock, áudio físico, DSP/streaming ou runtime jogável. O
contrato e a matriz estão em
[validação do manager HIT](hit-track-manager-validation.md). Próximo caminho
para Linux jogável: conectar o host permanente `odh-person` ao renderer e criar
uma cena Linux com diretório de dados configurável, seleção/restauração de lote,
janela e entrada. A triagem Simitone localizou o fluxo TS1 de seleção de casa e
inicialização em `TS1GameScreen.cs`; ele serve como referência de lifecycle,
não como prova de comportamento original nem licença para importar caminhos
herdados de FreeSO/TSO. Depois da primeira cena com Main animado, fechar
interação sentar/levantar, tempo/necessidades e persistência ODH.

A investigação da lacuna Stand→idle está em
[stand-continuation-investigation.md](stand-continuation-investigation.md).
`odh-person` já mantém Main, `Sleep`, `InteractionQueue` e Playback no mesmo
relógio e preserva conclusão/retomada de ação; não se deve duplicar esse
scheduler. A saída sentada é específica ao probe `odh-bhav`, que termina a VM
do avatar depois de Stand. O contrato CFP multi-FAR está integrado ao renderer
e ao export headless do `odh-person`, mas não há ainda uma cena Linux interativa
com lote vivo, rota, posição ou entrada de usuário.

## Checkpoint atual — `afc836a` (2026-10-20)

O commit publicado implementa a ordem externa do NoteOn global: getter efetivo
controller38, writer do wrapper `(38,1,0)` quando não zero, NoteOn, getter
Patch38 vivo e A/B condicionados pelo valor pós-play, mesmo com resultado de
play falso. Os seis providers ficam explícitos e o perfil local continua
separado. A matriz tem 34 casos em lote; Debug, ASan/UBSan e sem SDL/X11
passaram cada um com 6 CTests e 34/34 casos. LSan ficou desabilitado. A revisão
independente não encontrou problemas no escopo.

A investigação subsequente fechou o writer concreto: ele grava o banco local
do wrapper em `wrapper+0x8c`, sem chamar o backend ou alterar flags do
controller. Veja [a evidência do writer global NoteOn](hit-native-noteon-writer-evidence.md)
e o relatório estático em `reports/hit-native-noteon-providers-20261020/`.
Providers A/B, semântica global posterior de ID38, adapter físico e primeira
versão Linux jogável continuam pendentes. Os testes feitos até aqui não
demonstram áudio físico ou compatibilidade completa.

A OMM é consultada no escopo `OpenDollhouse`. Nos checkpoints, registrar `repository`
(`opendollhouse`), `branch` e `git_revision` no handoff, com o `expected_sha256`
vigente. Confirmar por `get_handoff` com `observed_repository` e
`observed_revision`. SHA do handoff e HEAD Git são identidades distintas;
`freshness.matches` não comprova testes, árvore limpa ou backup remoto.

## Referência adicional recebida em 2026-10-15

`../referencias/The Sims Legacy Collection` foi fornecida pelo usuário como
edição Steam de 2025. Após confirmação humana do fim da cópia, o inventário
registrou 10.406 arquivos e metadados estáveis antes/depois. Os 81 HIT/HOT/HSM
comuns têm SHA256 iguais; quatro dos cinco FAR amostrados diferem. Identidade
PE32 x86 do executável e limites da comparação estão em
[legacy-reference-validation.md](legacy-reference-validation.md).
Consultar [referências](references.md#referência-comercial-adicional-legacy-collection).
Ela complementa a instalação anterior; a prioridade de desenvolvimento e a
proveniência das evidências já registradas permanecem.

## Controller HIT nativo — base `f5ab084`

`NativeHitController` executa um subconjunto HIT portátil com bancos próprios
1–16, 17–47 e 50–62, Clock explícito e callbacks sobre o binding de áudio
corrente. O caller fornece `entry_pc` já resolvido; `hitlist_id` não zero da
HOT é aceito sem chooser e sem sobrescrever ID50. Contrato, limites e
verificação estão em
[hit-native-controller-validation.md](hit-native-controller-validation.md).
A evidência estática que orientou o recorte está em
[hit-native-controller-execution-evidence.md](hit-native-controller-execution-evidence.md);
a auditoria conferiu 535 linhas, 1.594 bytes, oito entradas de opcode e
mapeamento PE (nota SHA256 `635e6f322f12efe922b3143872c5c4f0da58ca8573129d0005f6c8749707154f`;
relatório SHA256 `f0171402d4637498e9ada7449c7aec0303faa59e1d66eed97cd7c7f05048d9a8`). Nenhum executável original foi rodado.

Na base histórica, o core direto passou 82 checks com `-Werror -Wconversion -Wsign-conversion` e
ASan/UBSan. CTest Debug: 115 configurados, 114 passaram, um skip X11, zero
falhas (6,97 s); ASan/UBSan: mesmos resultados (13,10 s), LSan desligado;
sem SDL/X11: 9/9 (0,66 s). Oráculo Python independente: 6.331 casos Debug e
6.331 ASan, incluindo aritmética/fade signed 32-bit, INT32_MIN, igualdade e
flags. Logs e recibos estão descritos na validação.

Não houve probe da cadeira nem teste de backend físico porque a classe ainda
não está integrada ao host de áudio. O controller não implementa vozes/DSP,
cadência externa, manager persistente HOTTrackID, integração World, provider
espacial nem admissão. Os casos novos são sintéticos/host; não reivindicam
áudio físico, compatibilidade com o original ou ports.

## Próximos passos

Unidade de PCM reutilizável: load move/charge uma vez, replay usa mesmohandle,
stop/completion retêm PCM até release; loop e taxa efetiva explícitos. Troca
de taxa ancora na fase consumida usando taxa antiga. Lookup XA agora aceita
NativeHitPatchRef global e não depende do HOT local. Contrato e limites em
[hit-reusable-pcm-validation.md](hit-reusable-pcm-validation.md).
A [pesquisa do backend comum](hit-native-common-backend-evidence.md) fechou
classificador físico vs recurso virtual, preload/dirty, perda de pan/frequência,
NoteOn pre-controller38/post-Patch38 e loop latch. O adapter/callback matrix
externa foi implementada em `afc836a`; writer interno está documentado em
[hit-native-noteon-writer-evidence.md](hit-native-noteon-writer-evidence.md).
Providers físicos, DSP e streaming continuam pendentes; não reaplicar controles
nem inferir streaming por tamanho FAR.
Fonte final: mixer101/transporte152/catalog46/integração13 checks. Debug e
ASan/UBSan122cfg/121pass+1skipX11/0fail (15,84/26,78s), LSanoff;
semSDL/X11 só4selecionados pass. Oracle10.392/0, auditoria20ranges/4.320bytes.
Quatro probes Classic/Legacy×Debug/ASanUBSan Patch3212:54.296frames,
108.592bytes PCM retidos em1handle, replay/fase/loop/release pass, inputs82
hashes preservados por edição. Sink sintético, sem áudio físico. Recibos
`reports/hit-reusable-pcm/validation.json` e `native-*.json`.



Nova unidade de controles pré-wrapper: helper A portátil calcula ganho com
mul32/divisões sequenciais, grupo/duck explícitos e seleção pan/posição; helper B
combina local21/Patch21 antes do DSP. Perfil global exige callback de snapshot
para escritas19/20, sem fallback numérico; elas recalculam A somente. Registry
encaminha placement antes gain e pitch separado. Parser nativo conserva flags
Patch43/26/27 opcionais, sem transformar ausência em falso conhecido.
Contrato e cobertura em [hit-native-audio-controls-validation.md](hit-native-audio-controls-validation.md).
O adapter de áudio físico e providers efetivos ainda faltam.
Fonte final:119 checks recursos/163 controller/64 registry/51 integração e6
grupos helper. Debug eASan/UBSan121cfg/120pass+1skipX11/0fail (16,00/30,80s),
LSanoff; noSDL/X11 só5selecionados pass. Oracle20.324 casos sem divergência;
auditoria PE25recortes/2.131bytes. Corpus4 Classic/Legacy×Debug/ASanUBSan:
423 aquisições por caso sem falhas,26hashesfonte porreceipt conferidos.
Os vencedoresPatch têm ID43/26 falsos17.503/ausentes1;ID27 falsos17.480/
ausentes24. Dados não comprovam valores true. O parsing desses valores tem testes
sintéticos; seus efeitos físicos de preload/backend continuam pendentes.
Recibos em `reports/hit-native-audio-controls/validation.json` e `corpus/`.



O controller já oferece perfil `NativeHitPatchSource::global_catalog`, com
Patch de outra origem e outcomes zero/adquirida/ausente/tipo incorreto. A
inicialização global libera o binding anterior antes do lookup e pode seguir
ativa sem wrapper; tipo incorreto faz stop diagnóstico seguro e depois o
initializer restaura entrada/deadline/ID46/ativo. Há dois callbacks de aplicação
de controles, antes e depois da cópia do ID26 efetivo da Patch. A escrita
pública ID50 e timed opcode1 continuam fora do subconjunto. Contrato em
[hit-global-patch-binding-validation.md](hit-global-patch-binding-validation.md)
e [evidência dos resultados](hit-native-binding-outcomes-evidence.md).

`NativeHitPatchRegistry` agora possui backend por Patch ativa/geração, com
BindingLease/ResourceLease separados, factory externa RAII e views protegidas
por geração. Pausa chama Stop, resume não faz áudio; recursos são pedidos
explicitamente. Último release aposenta geração; Stop tardio não toca geração
nova. Os limites padrão64/256/256 e hard cap65536 são política ODH. No host,
armazenamento fixo padrão21.176 bytes exclui backends/capturas/allocator.
Revisão corrigiu overflow de releases adiados por tokens aposentados; regressão
`{1,1,1}` confirma cleanup. Registry deve sobreviver a todos os leases.

Fonte final: controller154 checks, registry55, integração29; CTest Debug e
ASan/UBSan120 configurados,119 aprovados,1 skip X11,0 falhas (15,42/25,87 s),
LSan desligado. Sem SDL/X11 três testes selecionados passaram. Auditoria PE
independente conferiu25 recortes/4.070 bytes. Recibos em
`reports/hit-global-patch-binding/`. O driver integrado ainda é sintético;
não há adapter DSP/PCM físico nem manager persistente/World/admissão no host.

Quatro probes Classic/Legacy × Debug/ASan+UBSan passaram:4.168 candidatos
por caso,423 adquiridos,3.744 zero,1 ausente; sem falha de binding/cleanup
ou recursos vivos. Birthday continua na falha esperada69. Driver sintético
ID26=0 apenas quando adquire; sem áudio físico. Hashes e receipts em
`reports/hit-global-patch-binding/{validation.json,corpus/summary.json}`.

O `NativeHitObjectCatalog`, agora em `HitResourceBank::objects()`, resolve
TrackID pelo namespace global compartilhado de Track/Patch. O parser nativo
conserva registros na ordem literal das linhas, incluindo duplicatas, e
ignora chaves zero de Track/Patch antes do payload. GlobalHitList usa outro
mapa. Chaves desses três callbacks usam o helper numérico nativo; o perfil
legado fica inalterado. `resolve_global_track_event` mantém três origens:
EventMapping escolhido, Track vencedor e HIT da entrada. Patch também é
global e pode ter outra origem. O lookup anterior de `entries().resolve_event`
permanece local ao HOT; não o transportar para o manager como autoridade.
Contrato, testes e recibos em
[hit-object-catalog-validation.md](hit-object-catalog-validation.md).

Checkpoint anterior do catálogo: recursos111, objetos27, banco19 checks; suites Debug e
ASan/UBSan 118 configurados,117 aprovados,1skip X11,0falhas (14,80/26,22 s).
Após última mudança da cobrança no banco, cinco testes focais recompilados
passaram nas duas configs; sem SDL/X11 cinco selecionados também passaram.
Auditoria independente: 50 recortes,7.146 bytes. Corpus em quatro probes:
21.831 registros/21.809 chaves,19 substituições de mesmo tipo e3 entre tipos;
9 eventos usam Track de outro HOT,1 mudaHIT/PC na ordem diagnóstica. Recibos
`reports/hit-object-catalog/`; ordem não é prova de enumeração original.

Para integrar ao host, usar o perfil global e o registry compartilhado;
o perfil local padrão permanece para diagnósticos anteriores. A
[pesquisa de ownership](hit-native-backend-ownership-evidence.md)
demonstrou um contador para os dois aliases espaciais e desequilíbrio de
releases no caminho estático; não copiar esse uso potencial de memória
liberada para ODH. A [pesquisa do registry](hit-native-group-registry-evidence.md)
confirmou escritores69/71 e leitor73; opcode70 não espera. Restam domínio
semântico dos índices, provider compartilhado e invariantes de reload/remoção.
Nenhum desses opcodes foi habilitado nesta unidade.

O `HitEntryCatalog` agora conserva arquivo HIT e PC por chave global. Grupos
posteriores na ordem fornecida substituem os anteriores, com recibos limitados.
`NativeHitResourceResolver` e frames de retorno permitem chamadas entre HITs,
sem trocar o HOT/track/binding do evento. O leitor precisa de
`HotReadProfile::native_entry_map` explícito para esse fluxo; o padrão legado
permanece. A investigação provou buffers independentes e o caminho TrackData,
não concatenação nem merge ENTP. Contratos e resultados atuais em
[hit-native-entry-map-validation.md](hit-native-entry-map-validation.md),
com [bytes do loader](hit-native-entry-map-evidence.md).

A [ordem do loader](hit-native-load-order-evidence.md) foi rastreada: raízes
na ordem de chamada, arquivos na ordem devolvida por Win32, HSM/HIT/HOT por
grupo. Não há sort interno no caminho estudado; ordenar no Linux é política
ODH e pode alterar qual chave repetida vence. O namespace HSM global usa
nomes exatos e substituição pela última definição; HOT guarda números já
resolvidos. O `HitResourceBank` agora possui recursos e catálogo com endereços
estáveis e entrada em ordem explícita; os streams podem ser fechados após a
construção. Contrato, orçamento e recibos em
[hit-resource-bank-validation.md](hit-resource-bank-validation.md).

Unidade atual: leitor106 checks, banco16 checks estritos; revisão independente
sem defeito concreto. CTest Debug e ASan/UBSan: 117 configurados por rodada,
116 aprovados, um skip X11, zero falhas (13,99/27,31 s), LSan desligado.
Sem SDL/X11: quatro testes selecionados aprovados. Quatro probes com dados
Classic/Legacy em Debug/ASan passaram após fechar os streams: 27 grupos,
13.207 registros HSM/8.431 nomes/4.776 substituições. O evento de aniversário
continua chegando à falha esperada do opcode69, sem efeitos de áudio. Recibos
em `reports/hit-resource-bank/`; auditoria estática conferiu 75 intervalos e
31 hashes de recortes. Isso não valida áudio físico ou jogo completo.

A [pesquisa do backend concreto](hit-native-engine-backend-evidence.md)
supersede a hipótese de pausa por cursor: o slot de pausa para buffers, e o
de retomada retorna sucesso sem reiniciá-los. Wrappers da mesma patch podem
compartilhar backend. Nenhum adapter novo foi implementado nesta unidade;
callbacks, ownership completo, streaming, controles e PCM já enfileirado
ainda precisam de contrato. Não encaminhar pausa de um binding ao sink
compartilhado. Integrar catálogo/banco ao manager persistente por TrackID,
contribuições e World; opcode69 continua pendente. Preservar os gates Linux
savedframes/Clock/Main, filas/interação, lote e save.

Orientação humana de 2026-10-17: lacunas do Simitone exigem investigação dos
dados e executável originais; o roadmap de terceiros não limita o objetivo
ODH. A issue8 e o fork alexjyong, com clone e revisão próprios, estão em
[auditoria de referências](reference-audit-2026-10-17.md). O fork tem caminhos
Linux/Legacy e melhorias de frontend úteis para comparação; nenhuma alegação
de fidelidade decorre deles. Seu FreeSO ainda não foi materializado/auditado.

A extensão inicial Call7 adicionou resolvedor explícito de chave DWORD para PC e pilha
limitada, compartilhada por Return8/End12. Stop limpa a pilha; restart ativo
ID55 preserva seus retornos. Isso não implementa população/precedência do mapa
do manager. Foi ampliada pelo catálogo acima. Resultados históricos e limites em
[hit-native-call-validation.md](hit-native-call-validation.md).

Concluir integração com adapter do host de áudio e manager persistente por
HOTTrackID, mantendo geração de World e binding PCM separados. Preservar gates
de admissão, ownership por fonte e teardown; não deduplicar por nome nem criar
ticket por admissão. Fechar backend/provider antes de alegar lifecycle completo.
A meta Linux também inclui saved frames/queues/Clock (105/137), Main sem reinit,
câmera/picking, input, lote com SLOT/failure trees/needs/inventory/save e
SDK/CC. Consulte os documentos de validação e evidência anteriores para
histórico de contratos, testes e proveniência.

Preservar originais, saves, refs pinadas, relatórios, builds, runners antigos e
artefatos da raiz. Não usar `git add -A` nem limpeza genérica.

## AttemptWalk composto com o World vivo (2026-10-21)

O `odh-person` agora monta RoomObs/WallObs da arquitetura preparada, inventaria
portais Portal15 e aplica a política AttemptWalk sobre o inventário RoomInfo do
World após Main. House07 no tick 214 ficou `ready` na sala 3: 24 aberturas de
portal, 8 RoomObs, 128 WallObs e 153 retângulos totais; 13 entidades entraram
como obstáculos e o início não intersecta a lista. O teste sintético cobre
flags/caixas/EP5 e mutação durante callback. Debug completo e CTest 158/158
passaram; `linux_window_backend` foi pulado sem X11. A execução real do Main
terminou no limite documentado de ticks (código 3), com JSON válido e frame
CPU. `route_clearance`, estado mutável das portas e integração de movimento
permanecem falsos. Não declara jogabilidade. Evidência detalhada em
[`world-attempt-walk-validation.md`](world-attempt-walk-validation.md).

Próxima unidade: auditar lacunas entre a composição atual e o pedido Route45
real (lista de avatares, estado/footprints dinâmicos de portais e seleção de
obstáculos por sala), validar destinos de SLOT fora da posição inicial e então
conectar a busca sem chamar isso de movimentação até o Main consumir a rota.

## Corredor de destino sobre AttemptWalk vivo (2026-10-21)

`--attempt-walk-destination X Y` agora executa `route_rectangles` no snapshot
vivo de AttemptWalk quando a meta está na mesma sala. RoughBalanced seed-1
ordena o prefixo RoomObs/WallObs; bandas e entidades seguem depois. Em House07
Main tick 214, avatar 105 até o destino SLOT `(408,456)`, a busca encontrou
corredor com 7 retângulos gerados, 3 expandidos, 2.258 unidades de trabalho e
entradas `[392,440]`, `[399,449]`. O host declara raiz isolada e threads salvas
dormentes, embora exista outro avatar na sala; isso não modela as threads de
um runtime multissím completo. A referência Simitone mantém `RoomInfo.Entities`
ordenada por `ObjectID` em `VM.AddToObjList`; o snapshot vivo ODH enumera o
`World` e os obstáculos por ID crescente. O JSON agora confirma somente
`entity_obstacle_order_matches_reference` quando a sequência efetivamente
serializada é estritamente crescente. `entity_order_matches_reference` continua
falso: essa prova cobre a subsequência de obstáculos, não todas as entidades
da sala nem equivalência comportamental com TS1. Clearance, estado dinâmico de
portas e integração de movimento permanecem falsos. `optimize_rect_route_lines` agora
reproduz o corner-cutting e o ajuste opcional de entrada `dirIn` do
`OptimizeLines` Simitone, com HiP preservado e limites fail-closed. O
diagnóstico de rota House07 já chama a API e mantém os corredores bruto e
otimizado separados. O JSON informa
`incoming_direction_context=not_reconstructed`: o estado da rota-pai que
decide `dirIn` não está disponível neste probe. A origem foi confirmada:
`VMRoutingFrame.GetParentFrame()` procura outro `VMRoutingFrame` na pilha do
thread, enquanto o `odh-person` expõe frames BHAV do `Vm`; o destino solicitado
pelo probe também não veio de um Route45 ativo. Portanto, não há estado-pai ou
`PortalTurns` vivo que esse diagnóstico possa recuperar. Integrar um frame de
rota ao runtime real é pré-requisito para alimentar `dirIn` e validar
waypoints/movimento. Uma transcrição temporária
C#/.NET 10 das rotinas da referência confirmou
os casos reto, curva, gap e duplicata; não invocou Simitone compilado nem o jogo
original. Os casos de portal também coincidiram com harness C#/.NET 10 usando
MonoGame 3.8.4: projeção >=8, clamp com vários crossings, offsets positivos e
negativos e dois nós. ODH falha fechado no domínio em que as somas inteiras de
quadrados da referência transbordariam. Próxima unidade: investigar a ordem
implementação de frame de rota ligada ao Route45 real, com estado-pai,
`PortalTurns` e continuidade de execução. Não promover a saída geométrica a
clearance, execução de waypoints ou movimento.
