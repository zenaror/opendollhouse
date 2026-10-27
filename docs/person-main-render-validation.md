# Headless frame do Main via Playback

## Sit/Stand após deslocamento Route45 em House07 (2026-10-21)

O harness `tests/house07_interaction_integration.py` aceita
`--move-avatar-to X Y LEVEL DIRECTION` para agendar uma posição inicial de
teste no tick 1 e exige callbacks de movimento quando essa opção é usada. Com
os recursos originais preservados em `../referencias/The Sims`, House07 e a
cadeira ObjectID128, o avatar 105 foi colocado em `(376,440,1,direction 3)`;
Route45 percorreu 16 posições até o SLOT, Sit/Action4097 retornou `true`, e
Stand/Action4102 retornou `true` no mesmo Main/World. A fila ficou vazia, o
avatar terminou em postura 0 e sem attachment; a conclusão de Stand foi no
tick551. O Main continuou `waiting`, sem diagnóstico.

Reprodução:

```sh
python3 tests/house07_interaction_integration.py \
  --binary ./odh-person --game-root '../referencias/The Sims' \
  --object-id 128 --move-avatar-to 376 440 1 3 \
  --stand-tick 320 --ticks 1024
```

O perfil de integração passou. Isso valida uma sequência headless controlada
com recursos do jogo e movimentação Route45 no mesmo cômodo. A posição inicial
foi agendada pelo harness; não comprova navegação acionada pelo jogador, clique
na janela, portas/portais ou conclusão de uma sessão geral jogável.

## Resultado

`odh-person` possui uma opção de diagnóstico para exportar um único frame PPM
do `PlaybackSample` vivo do Main. O sample é passado ao renderer com o mesmo
clip selecionado, frame e gestos de mão. `AnimationCatalog::decode` recebe o
`AnimationHeader` selecionado, preservando a origem multi-FAR sem lookup por
nome. A aparência é construída diretamente do `AvatarBodyStrings` efetivamente
aplicado; não há estado de save fabricado. A resolução mantém os limites de
ambiguidade, recursos e 16 partes.

Sem `--render-saved-avatar`, o render usa âncora diagnóstica `(0,0,0)`. Com
`--render-saved-avatar OBJECT_ID`, usa posição/direção já decodificadas dos
prefixos ObjM do lote selecionado. A opção só aceita um ID cujo GUID coincide
com o recurso de pessoa carregado e cujo tipo ObjT é avatar; rejeita posição
ausente e coordenadas, nível ou direção fora do perfil. O JSON mantém
`runtime_position_present:false`: o sample é posicionado apenas para
apresentação e a entidade salva não é restaurada no World vivo da VM. A saída
só é criada quando ainda não existe; continua sendo um caminho headless, sem
janela ou controles.

O modo `--render-window` apresenta samples sucessivos do mesmo Playback/VM em
uma janela X11 opcional. `P` pausa, as setas giram a câmera, `+`/`-` ajustam
zoom e Escape fecha. A taxa de 25 Hz serve apenas para apresentação e não está
calibrada ao tempo original. No preview House07, um clique selecionou a célula
do chão; Space iniciou Go Here e, após cerca de um minuto, o avatar andou até o
ponto escolhido. Consulte [interaction-validation.md](interaction-validation.md)
para os detalhes e limites do teste. Os eventos foram enviados diretamente à
janela; não houve movimento do cursor físico. A compilação sem X11 também foi
verificada e a opção retorna diagnóstico claro de backend ausente.

`--render-size WIDTH HEIGHT` define o tamanho do quadro e da janela, com limites
de 160..1024 por 120..1024 e até 1.048.576 pixels. O padrão continua 320x240,
inclusive nas capturas headless. O launcher Linux solicita 800x600, igual à
janela da vizinhança. Isso usa 6,25 vezes mais pixels que o padrão, mas fica
abaixo do limite já imposto ao viewport. O tamanho maior não altera coordenadas,
estado do World ou escala dos assets; o picking usa o mesmo quadro ampliado.

## Execução com dados originais

Usou os arquivos externos preservados em `../referencias/The Sims`:

- Personagem `UserData/Characters/User00000.iff` (OBJD 128).
- Lote `UserData/Houses/House07.iff` (lote 7), zoning e neighborhood.
- `GameData/Global/Global.far` para globals/semiglobals.
- `GameData/Animation/Animation.far` e `ExpansionShared/ExpansionShared.far`
  como fontes do catálogo de animações.
- `GameData/Textures/Textures.far` e `adult-skeleton.cmx.bcf` para a aparência.

Relatórios e imagens foram escritos em um diretório local de resultados e não alteram os dados do
jogo:

| Captura | Tick | Clip | Frame | CFP amostrado | Pixels sombreados |
| --- | ---: | --- | ---: | ---: | ---: |
| primeiro sample | 2 | `a2o-idle-neutral-trans-stand-lstand` | 0.0 | 5490 | 6860 |
| posterior | 214 | `a2o-idle-neutral-trans-lstand-lhips` | 1.2 | 2928 | 6924 |

Uma terceira execução selecionou o Sim salvo ObjectID 105 (GUID `850010198`),
cuja identidade confere com `User00000.iff`; seu ObjM fornece `(392,440)`, nível
1 e direção 3. O perfil gráfico projeta isso para `[24.5,27.5,0]` e centraliza a
câmera em `[24.5,27.5,0.75]`. O frame registra 7119 pixels sombreados. O JSON
é registro arquivado não distribuído e a imagem é
registro arquivado não distribuído (SHA256
`4ee6fb0d7c829c9e95a36e5c9529b01308c301ae6a73923cb9dd59eafa2ebe49`).

Ambos produziram 320×240, 684 vértices, 840 triângulos e 425.984 bytes de
textura. A execução de 214 ticks terminou com status VM `waiting` e código 3
porque o limite de ticks foi atingido; o JSON registra 604 passos de VM e
nenhum diagnóstico de falha. Isso não afirma que o Main terminou ou que a
simulação é jogável.

### Contagem de pixels do avatar no lote salvo (2026-10-23)

O modo `--saved-world-main --render-frame` exportava a imagem corretamente,
mas informava `actor_raster_pixels=0` e `actor_composited_pixels=0`: a coleta
estava habilitada apenas para `--headless-main`, e a contagem composta lia a
camada do chão em vez da camada própria dos avatares. A coleta agora abrange os
dois modos e conta o token do avatar em `avatar_pick_ids`.

Repetir House07 no tick 214 produziu 4.725 pixels do Sim antes da composição do
lote e 4.150 pixels identificados como visíveis depois dela. O PPM foi idêntico
ao anterior à correção (SHA256
`8e61cf69fac5b4235cd3f8736a4b93e8755d694863abe0347585f58d2408dd25`), mostrando
que o ajuste corrigiu a medição sem alterar a imagem. É evidência do caminho
CPU de captura; não valida a janela Linux nem a aparência no jogo original.

Arquivos:

- registro arquivado não distribuído
- registro arquivado não distribuído (SHA256
  `e65f626268b56d33ee1222fca0811e71e88319a74b4bdaa75ed4ed80c2940f24`)
- registro arquivado não distribuído
- registro arquivado não distribuído (SHA256
  `f8cd00c582465e87f9c448dc4a23d7c86d72e53c5db54fc71a45d0d824d81247`)

## Verificação

Build de `odh-person`, `odh-saved-appearance-tests` e
`odh-scene-cfp-provider-tests` completou. Os CTests
`saved_avatar_appearance`, `person_cli`, `person_animation_cli`,
`scene_cfp_provider`, `animation_catalog`, `scene_hand_gestures` e
`reusable_scene_host` passaram (7/7). `git diff --check` também passou.

## Limites e próximo passo

Isto prova o fluxo de um sample do Main até pixels do renderer com recursos da
instalação original. Não prova equivalência gráfica ao jogo, posição real no
lote, restauração completa do estado, continuidade sem falhas, áudio ou
gameplay. O próximo milestone é renderizar o avatar na posição do lote carregado
em uma janela Linux e introduzir entrada mínima sem criar outro scheduler nem
substituir o VM/Main/Playback permanente do `odh-person`.

## Main em World restaurado (2026-10-20)

`--saved-world-main` restores House07 through `make_saved_world`, selects the
saved avatar whose GUID matches `User00000.iff`, and starts a new Main entry
using that same World. Saved BHAV stacks and action queues remain dormant; this
is not continuation of the saved simulation. The body descriptor is derived
without writing saved PersonData/motives or consuming RNG.

The real-data probe includes `UserData/LotZoning.iff`, which Main needs for its
zone lookup. Original resource files remain outside the repository. At 214
ticks, Main is `waiting` with 308 VM steps and five active stack frames. The
tick-214 sample renders the restored avatar at World position
`[24.5,27.5,0]`, direction 3: clip `a2o-idle-neutral-mstand-look-1d`, frame
25.2, 684 vertices, 840 triangles, 3,172 catalog CFP samples and 7,368 shaded
samples. Its PPM is registro arquivado não distribuído; JSON:
registro arquivado não distribuído.

At the 4,096-tick limit, Main is still `waiting` with 4,358 VM steps and five
active frames. It executes Main through timed animation, sound requests and
head-seek state. Four FWAV requests resolved to `hungry_sated_vox`; the host
reports them as `skipped_no_backend`, so this is silent simulation. The receipt
is registro arquivado não distribuído. Exit code 3
means the requested tick budget ended; VM status is not faulted.

To permit this progression, opcode 23 resolves FWAV entries from explicit actor,
semiglobal and global resources. Opcode 22 mode 0 writes head-seek target/state
fields following pinned FreeSO TS1 commit
`4c6b3e8f5835b228723caea3c9f683c62f244f73`. Playback accepts carry-only
and carry-plus-play modes by retaining the resolved carry resource; the renderer
does not compose that carry pose yet. Body rotation/routing LookTowards modes
remain unsupported. These reference-based behaviors are not independently
confirmed against the original game.

The frame contains only the avatar over a flat background. Saved threads remain
dormant, sounds have no sink, the X11 preview has not been visually validated
in this environment, and there is no player input or object selection. The
4,096-tick run is stronger evidence of a live Main/VM path, not a playable build
or a claim of simulation fidelity.

## Composição do lote no mesmo World (2026-10-20)

O caminho `--saved-world-main` pode receber `--render-floors FLOORS_IFF` e,
opcionalmente, `--render-walls WALLS_IFF`, `--render-level 1|2`,
`--render-wall-mode up|down` e `--render-terrain-theme 0..7`. O nível padrão é
1 e as paredes começam em modo `up`; tema de terreno é explícito.
`--architecture-resource FILE` pode ser repetido para fornecer cada arquivo
`.flr`, `.wll` ou `.far` na ordem de precedência desejada. Exige `--render-floors`,
aceita até 256 fontes explícitas e caminhos não vazios de até 4096 bytes; não
percorre diretórios. A ordem chega a `discover_architecture_resources` e o
catálogo alimenta `SavedLotMaterialOptions` do lote. O output de frame é
verificado contra todos esses inputs por caminho canônico/equivalência,
incluindo symlinks/hardlinks. Sem essa opção, o caminho global é preservado.
Contrato de mapas, preparação e recursos por zoom está na
[validação de materiais externos](external-architecture-renderer-validation.md).

Para definições OBJD em FARs mistos, `--object-definition-entry FAR ENTRY`
seleciona explicitamente uma única entrada IFF por nome; entradas não IFF do
mesmo FAR não são abertas como IFF. A opção é repetível e compartilha o limite
de 16 fontes com `--object-definitions FAR` e `--object-definition-file IFF`;
caminhos e nome da entrada têm limite de 4096 bytes. Nome de entrada ausente
ou ambíguo falha. O caminho FAR continua protegido contra alias do frame de
saída. Isso permite selecionar definições sem extrair a entrada para disco.
Em `odh-person`, `--object-definitions FAR` indexa somente membros com sufixo
`.iff` (sem diferenciar maiúsculas/minúsculas) e ignora outros tipos de membro;
IFFs são validados normalmente. Isso permite usar FARs de expansão mistos sem
extrair seus conteúdos.
Essas opções
também valem para a janela Main. Requerem as fontes explícitas e mantêm as
recusas existentes de padrões customizados sem resolvedor, pools/água e
recursos fora dos perfis dos renderizadores.

`SavedLotRenderer` empresta o `SavedWorld` restaurado que já fornece a memória
da VM; nenhuma outra instância de World é construída para desenhar. O host
preserva a propriedade desse World em `SavedWorld`, em vez de retirar seu
ponteiro. Cada captura consulta novamente posição/direção e geração do ator;
o sample de Playback deve corresponder à identidade ainda viva. O terreno usa
a altitude interpolada na posição livre do avatar ou do seu contêiner efetivo.
As entidades fora da tela permanecem no World.

Avatares anexados usam o resolvedor explícito OBJD→SLOT do contêiner, também
no preview sem lote. O cache valida GUID/OBJD, SLOT único e os limites de 16
tabelas e 4.096 registros no total. A posição gráfica vem do SLOT; nível e
altitude usam o contêiner efetivo, sem somar a posição lógica copiada do avatar.
Quando o contêiner é membro de um grupo multitile salvo, a altitude usa a média
inteira X/Y dos membros serializados, como em `saved_scene`; os limites são
4.096 registros/membros e soma intermediária sem overflow de short. O centro é
do snapshot salvo, inclui membros fora do mundo e não implementa movimento de
grupo runtime. A fixture de declive distingue a média das altitudes individuais
e verifica truncamento inteiro, membros inválidos/duplicados e orçamento.
A câmera acompanha a posição gráfica a cada captura com o deslocamento de
enquadramento `Z + 0.75`. Fixtures cobrem avatar salvo anexado sem flag runtime
`positioned`, movimento do contêiner, nível/terreno herdados, recursos SLOT
inválidos e limites por cache/acumulados. Contêineres aninhados continuam fora
do perfil de `graphics_entity`.

A ordem de composição é terreno/floors, raster do avatar vivo, sprites de
paredes/junções e sprites dos objetos salvos. Todos usam câmera/profundidade
compartilhadas. O perfil atual de paredes só aceita alpha binário e produz
superfícies opacas; elas estabelecem a cor/profundidade antes dos objetos.
Assim um objeto translúcido mais próximo mistura sua cor sobre a parede,
enquanto um objeto atrás da parede é rejeitado pelo teste de profundidade.
Inverter esses dois passes perderia a cor da parede atrás de um objeto
translúcido. A composição mantém os perfis CPU já documentados em
`saved-floor-validation.md`, `saved-wall-validation.md` e
`terrain-surface-validation.md`; não estabelece equivalência GPU/original.

O JSON `render.lot` informa nível, tema, contagens de geometria/floors/paredes,
bytes de recursos e amostras dos sprites de paredes/objetos. `render.shaded_samples`
inclui os rasters de terreno, pisos e avatar; amostras de sprites ficam
nos campos próprios, seguindo o contrato do compositor. Outros avatares
continuam sem ser submetidos nesse caminho e aparecem explicitamente
como `false`, mesmo que continuem preservados na simulação restaurada.

Os objetos são extraídos dos registros do mesmo `SavedWorld`; Graphic e Hidden
são lidos dos bancos vivos em cada captura, junto da posição/direção e do
contêiner resolvido por SLOT. A seleção de story e altitude usa o contêiner
efetivo e o centro multitile salvo quando aplicável. Os recursos passam pelo
`SavedObjectRenderer`, conservando seus budgets e recusas SPR#/sem Z/DGRP
ausente ou ambíguo. Os counters discriminam objetos submetidos, camadas,
bytes de frames, amostras, ocultos, fora do mundo, acima do nível e removidos.
O `HideForCutaway` no nível cortado continua recusado até existir ocupação de
paredes runtime. Remoções deixam de contribuir à aparência viva de paredes;
o helper estático continua estrito por padrão. Reuso de um ID salvo com outra
geração é recusado antes de devolver um frame.

Limites deste incremento: as flags dinâmicas de sprites e os centros de grupos
multitile continuam sendo snapshots do save; criação de novos objetos e
movimento runtime de grupos exigem atualização do renderer. A ordem entre
objetos e camadas DGRP segue a submissão salva e o perfil imediato de alpha/Z
do compositor existente, sem ordenação global de transparência por fragmento.
Não se afirma equivalência com o pipeline GPU ou com o jogo original.

O CTest `saved_lot_renderer` usa IFFs sintéticos: valida consulta de posição e
direção após movimento no mesmo World, nível/altitude, recusa de ator/viewport
inválidos e oclusão de pixels à frente/atrás dos pisos e paredes. Os testes
CLI rejeitam combinações incompletas e valores de view fora do perfil. Isso
verifica a integração CPU e os limites declarados, sem provar gameplay,
seleção, continuidade das filas salvas, áudio ou aparência do jogo original.
O incremento de objetos acrescenta regressões de Graphic/Hidden/posição vivos,
nível herdado do contêiner, alpha diante e atrás da parede, remoção/reuso de
ID e recusa de cutaway incompleto. Build focal de `odh-person` e do teste de
lote e cinco CTests (`saved_lot_renderer`, `saved_object_renderer`,
`saved_wall_renderer`, `person_animation_cli`, `person_cli`) passaram.

### Seleção de objetos no preview Main

Com `--saved-world-main --render-floors ... --render-window`, Button1 seleciona
o objeto sob o pixel do último frame apresentado. O título mostra
ID/GUID/TreeTableID e a ação selecionada; Tab e Shift+Tab percorrem as opções
elegíveis, e Space/Enter solicita a opção indicada. A elegibilidade é recalculada
ao percorrer e novamente antes de enfileirar. O backend retém o último clique
drenado em cada `poll`, incluindo coordenadas fora do drawable; Space/Enter não
inventam uma posição. O clique é resolvido antes de aplicar rotação/zoom que
vieram no mesmo poll. Esse seletor pelo título e teclado ainda não é um pie menu.

Em 2026-10-21, a execução X11 atualizada no House07 mostrou a cadeira 128
(GUID 650099497, TTAB 128) com `action 1/1: Sit`. Tab manteve a única opção
elegível; Space mudou o recibo visível para `interaction queued BHAV4097`.
Isso valida clique, apresentação da opção, ciclo unitário e enfileiramento
pela janela. Não valida a escolha entre várias ações elegíveis nem a conclusão
da ação; a enumeração de várias linhas e o mapeamento Tab/Shift+Tab também têm
testes automatizados separados.

Em outra sessão X11, clicar em um pixel de piso selecionou `(23,26)`, level 1,
room 3; Space colocou Go Here na fila. Após manter a janela aberta por cerca de
um minuto, a ação terminou e o avatar chegou ao tile escolhido. A entrega do
clique e tecla foi sintética e direcionada à janela, percorrendo o código de
seleção e a fila do app.

`composite_sprites` pode registrar opcionalmente `owner_ids` uint16 no próprio
passe que compara depth e escreve cor. Alpha zero não grava ID, fragmentos
ocultos por avatar/piso/parede não selecionam, e empates retêm o primeiro
fragmento, como a imagem. Um fragmento parcialmente transparente aceito é
selecionável. O renderer de objetos fornece seu ID por camada; o lote ativa o
buffer somente quando solicitado pelo host. O teto é um uint16 por pixel de
viewport (1 Mi de pixels, 2 MiB; 153.600 bytes em 320x240). O headless padrão
não aloca esse buffer. A ordem intraobjetos conserva o perfil imediato atual.

`SavedLotRenderer::pick` rejeita buffer malformado e retorna vazio para cenário,
coordenadas externas, entidade removida/oculta, geração ou GUID desatualizados.
O JSON `render.object_selection` registra cliques e a identidade escolhida no
tick informado; `render.lot.object_pick_buffer_bytes` informa o custo da
captura. A seleção não altera World, VM ou RNG. A descoberta de TTAB e o
enfileiramento manual são descritos em
[interaction-validation.md](interaction-validation.md); ainda não há pie menu,
nem isso confirma a execução ou o efeito de gameplay.

Build focal sem warnings e cinco CTests executados passaram:
`sprite_composition`, `saved_object_renderer`, `saved_lot_renderer`,
`person_animation_cli` e `person_cli`. Fixtures verificam tokens de alpha zero,
sobreposição/profundidade/empates, ID do renderer, parede na frente, limites de
coordenadas, Hidden, GUID e geração. `linux_window_backend` contém eventos
Button1 sintéticos dirigidos à própria janela para coordenadas e último clique,
mas foi pulado sem DISPLAY naquela rodada headless. O build completo recompilou
todos os consumidores de `CompositeFrame` e passou; CTest completo: 127
configurados, zero falhas, somente `linux_window_backend` pulado por falta de
DISPLAY. O teste manual posterior em X11 está registrado abaixo.

Em 2026-10-20, a prévia foi aberta na sessão X11 do desktop com House07 e os
dados externos locais. A captura 320x240 mostrou terreno, pisos, paredes,
objetos e o avatar 105. Um clique real selecionou a cadeira salva 128
(GUID 650099497, TTAB 128); o título mostrou a opção `Sit`. Com foco explícito,
Space mudou o título para `interaction queued BHAV4097`. O receipt registrou
`interaction_requested:true`, ação 4097, owner privado 2147483648, alvo 128,
geração 55 e estado `queued`; após fechar a janela, `queue_size` era 1,
`queue_active:false` e não havia resultado/completion. A imagem continuou
mostrando o avatar em pé. Portanto X11, apresentação, seleção e entrada de
tecla foram validados nesta sessão; a fila ainda não prova que a VM iniciou a
ação, que routing completou ou que o avatar sentou. Capturas brutas ficaram em
`/tmp` e não foram adicionadas ao Git.

O primeiro envio de Space sem ativar a janela não solicitou a ação. A repetição
com `windowactivate --sync` e tecla após o clique funcionou, o que confirma que
o caminho de entrada depende do foco da janela.

Em uma segunda sessão, mantive a janela aberta depois de solicitar `Sit` até a
VM avançar pelo idle/animação atual. Aos 103 ticks, o receipt terminou com
`status=fault`, `diagnostic="unsupported primitive 45"`, e a pilha mostrou
owner privado `0x80000000`, rotinas `4097 → 4100 → 4099`, caller 105, callee e
StackObject 128. Isso prova que a ação enfileirada chegou à execução BHAV e
atingiu Routing; não prova movimento nem chegada ao SLOT. `queue_size=1` e
`queue_active=true` refletem que a ação permanece ativa quando o VM faulta.
O próximo bloqueio concreto é conectar um Routing45 alimentado por clearance
construído e validado; não se deve fornecer a malha sintética de teste ou
tratar candidatos geométricos incompletos como destinos seguros.

### Continuação de Sit: Routing45, Snap46 e animação do objeto

Em 2026-10-21, uma nova sessão X11 com o lote, catálogo de arquitetura,
Neighborhood, objetos e animações externos repetiu a seleção da cadeira 128 e
`Sit`. O trace entrou em `Routing45`, depois em `Snap46` com operandos
`[0,0,3,0,4,0,0,0]`, e prosseguiu até `Animation44`. O Main host agora liga o
opcode 46 ao controlador Snap usando o SLOT indicado pelo OBJD único da entidade
StackObject; para fonte 0 de animação, resolve também a tabela privada da
definição única do StackObject quando o owner do código é o sentinel privado.
O receipt mostra a posição do avatar de `[392,440,1,3]` para `[392,456,1,4]`
e inicia `a2o-sit-behind` (161 frames), sem falha de host. Uma captura durante
a transição mostra o avatar parcialmente encoberto pela cadeira.

A animação avançou até o frame 132 e a primeira execução encontrou `unsupported
primitive 32` em BHAV 4099, PC 15. Após ligar ObjectType32, a repetição gráfica
passou desse ponto e chegou a `NotifyOutOfIdle49`, que então também foi ligado
ao Main host. Os CTests de Snap, ObjectType, Notify e `person_cli` passaram.
Uma tentativa posterior com Notify ligado não reteve a sequência privada
4097→4100→4099 no trace, então ela não serve como prova adicional de execução.
O teste interativo mais forte ainda termina com a ação ativa durante a animação
`a2o-sit-behind`, sem falha até o opcode49; falta observar um receipt que
comprove a conclusão da fila e a pose final sentada.

### Probe com recursos de House07

O probe atualizado usou a instalação local com House07, LotZoning, Neighborhood,
`floors.iff`, `walls.iff`, Animation.far e ExpansionShared.far. Aos 214 ticks,
o Main ficou `waiting` sem falha de host: 308 instruções, cinco frames ativos e
sample do avatar 105/GUID 850010198. A posição de captura foi
`[24.5,27.5,3.75]`, direção 3; o Z inclui terreno, e a câmera ficou em
`[24.5,27.5,4.5]`. O frame mediu 320x240, com 684 vértices, 840 triângulos do
avatar, 425.984 bytes de textura e 84.168 shaded samples agregados.

O receipt registro arquivado não distribuído registra
6.323 triângulos/1.107.072 bytes reservados de terreno, 683 desenhos/1.365
triângulos de piso, e 69 camadas/49 junções de parede. O PPM final foi
inspecionado; SHA256:
`6003498c6479da998666153da13c85da3575f9e11901be08760499b669691288`.
PNG de consulta visual local:
registro arquivado não distribuído (derivado do PPM, também
ignorado pelo Git). O probe terminou com código 3 porque o limite explícito de
214 ticks foi alcançado; o receipt identifica status VM `waiting`, sem falha
do host.

Após integrar objetos, o mesmo comando capturou 135 objetos em 166 camadas,
6174198 bytes de frames decodificados, 130766 amostras testadas e 27588
sombreadas. O receipt e PPM ignorados são
registro arquivado não distribuído; SHA256 do PPM:
`c14edf44b16cdfe5d80fb9fc7b1251606f027dc6d5af8a4656fd2ba62bb88309`.
A imagem CPU foi inspecionada e mostra o avatar, objetos, paredes e lote
compostos. O limite de 214 ticks ainda termina com código 3 esperado; não valida
input, continuidade de interações ou fidelidade visual ao original.

A reprodução precisa incluir o contexto de bairro. Sem `--neighborhood`, o
Main falhou no primitive 31 de BHAV 8211 antes do sample; com Neighborhood e os
recursos acima, o caminho foi reproduzido. Os dois arquivos `User00000.iff` e
`User00001.iff` também entram como fontes de definição:

```sh
./odh-person --avatar-file '../referencias/The Sims/UserData/Characters/User00000.iff' 128 \
  --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff \
  --lot '../referencias/The Sims/UserData/Houses/House07.iff' 7 \
  --zoning '../referencias/The Sims/UserData/LotZoning.iff' \
  --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' \
  --animation-source '../referencias/The Sims/GameData/Animation/Animation.far' \
  --animation-source '../referencias/The Sims/ExpansionShared/ExpansionShared.far' \
  --render-animation-far '../referencias/The Sims/GameData/Animation/Animation.far' \
  --render-textures '../referencias/The Sims/GameData/Textures/Textures.far' \
  --render-skeleton adult-skeleton.cmx.bcf adult --render-saved-avatar 105 \
  --saved-world-main --render-tick 214 --ticks 214 \
  --render-frame /tmp/house07-main-lot-objects-tick214.ppm \
  --render-floors '../referencias/The Sims/GameData/floors.iff' \
  --render-walls '../referencias/The Sims/GameData/walls.iff' \
  --render-level 1 --render-wall-mode up --render-terrain-theme 0
```

Esta evidência comprova captura CPU do Main sobre camadas do lote e objetos
usando o mesmo World restaurado. Não mostra o segundo avatar; não valida
X11, input físico, interações, recursos remapeados ou aparência original.
