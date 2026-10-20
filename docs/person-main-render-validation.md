# Headless frame do Main via Playback

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
calibrada ao tempo original; clique não aciona jogo. Este host não tem servidor
X acessível: a tentativa terminou com `X11: cannot open DISPLAY`. Portanto o
caminho compila, mas a janela ainda precisa de validação visual em uma sessão
X11 ativa. A compilação sem X11 também foi verificada e a opção retorna
diagnóstico claro de backend ausente.

## Execução com dados originais

Usou os arquivos externos preservados em `../referencias/The Sims`:

- Personagem `UserData/Characters/User00000.iff` (OBJD 128).
- Lote `UserData/Houses/House07.iff` (lote 7), zoning e neighborhood.
- `GameData/Global/Global.far` para globals/semiglobals.
- `GameData/Animation/Animation.far` e `ExpansionShared/ExpansionShared.far`
  como fontes do catálogo de animações.
- `GameData/Textures/Textures.far` e `adult-skeleton.cmx.bcf` para a aparência.

Relatórios e imagens foram escritos em `reports/` e não alteram os dados do
jogo:

| Captura | Tick | Clip | Frame | CFP amostrado | Pixels sombreados |
| --- | ---: | --- | ---: | ---: | ---: |
| primeiro sample | 2 | `a2o-idle-neutral-trans-stand-lstand` | 0.0 | 5490 | 6860 |
| posterior | 214 | `a2o-idle-neutral-trans-lstand-lhips` | 1.2 | 2928 | 6924 |

Uma terceira execução selecionou o Sim salvo ObjectID 105 (GUID `850010198`),
cuja identidade confere com `User00000.iff`; seu ObjM fornece `(392,440)`, nível
1 e direção 3. O perfil gráfico projeta isso para `[24.5,27.5,0]` e centraliza a
câmera em `[24.5,27.5,0.75]`. O frame registra 7119 pixels sombreados. O JSON
é `reports/odh-person-main-savedpos-verified-20261020.json` e a imagem é
`reports/odh-person-main-savedpos-verified-20261020.ppm` (SHA256
`4ee6fb0d7c829c9e95a36e5c9529b01308c301ae6a73923cb9dd59eafa2ebe49`).

Ambos produziram 320×240, 684 vértices, 840 triângulos e 425.984 bytes de
textura. A execução de 214 ticks terminou com status VM `waiting` e código 3
porque o limite de ticks foi atingido; o JSON registra 604 passos de VM e
nenhum diagnóstico de falha. Isso não afirma que o Main terminou ou que a
simulação é jogável.

Arquivos:

- `reports/odh-person-main-first-sample-20261020.json`
- `reports/odh-person-main-first-sample-20261020.ppm` (SHA256
  `e65f626268b56d33ee1222fca0811e71e88319a74b4bdaa75ed4ed80c2940f24`)
- `reports/odh-person-main-tick214-20261020.json`
- `reports/odh-person-main-tick214-20261020.ppm` (SHA256
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
samples. Its PPM is `reports/odh-person-restored-main-tick214.ppm`; JSON:
`reports/odh-person-restored-main-tick214.json`.

At the 4,096-tick limit, Main is still `waiting` with 4,358 VM steps and five
active frames. It executes Main through timed animation, sound requests and
head-seek state. Four FWAV requests resolved to `hungry_sated_vox`; the host
reports them as `skipped_no_backend`, so this is silent simulation. The receipt
is `reports/odh-person-restored-main-tick4096-looktowards.json`. Exit code 3
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
1 e as paredes começam em modo `up`; tema de terreno é explícito. Essas opções
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

A ordem de composição é terreno/floors, raster do avatar vivo e sprites de
paredes/junções. Todos usam câmera/profundidade compartilhadas; a comparação
de profundidade mantém a superfície mais próxima. Submeter paredes depois do
avatar permite que alpha de paredes mais próximas use a cor do avatar como
fundo. A composição mantém os perfis CPU já documentados em
`saved-floor-validation.md`, `saved-wall-validation.md` e
`terrain-surface-validation.md`; não estabelece equivalência GPU/original.

O JSON `render.lot` informa nível, tema, contagens de geometria/floors/paredes,
bytes de recursos e amostras dos sprites de paredes. `render.shaded_samples`
agora inclui os rasters de terreno, pisos e avatar; amostras de parede ficam
nos campos próprios, seguindo o contrato do compositor. Outros avatares e
objetos ainda não são submetidos nesse caminho e aparecem explicitamente
como `false`, mesmo que continuem preservados na simulação restaurada.

O CTest `saved_lot_renderer` usa IFFs sintéticos: valida consulta de posição e
direção após movimento no mesmo World, nível/altitude, recusa de ator/viewport
inválidos e oclusão de pixels à frente/atrás dos pisos e paredes. Os testes
CLI rejeitam combinações incompletas e valores de view fora do perfil. Isso
verifica a integração CPU e os limites declarados, sem provar gameplay,
seleção, continuidade das filas salvas, áudio ou aparência do jogo original.

### Probe com recursos de House07

O probe final usou a instalação local com House07, LotZoning, Neighborhood,
`floors.iff`, `walls.iff`, Animation.far e ExpansionShared.far. Aos 214 ticks,
o Main ficou `waiting` sem falha de host: 308 instruções, cinco frames ativos e
sample do avatar 105/GUID 850010198. A posição de captura foi
`[24.5,27.5,3.75]`, direção 3; o Z inclui terreno, e a câmera ficou em
`[24.5,27.5,4.5]`. O frame mediu 320x240, com 684 vértices, 840 triângulos do
avatar, 425.984 bytes de textura e 84.168 shaded samples agregados.

O receipt `reports/odh-person-main-lot-house07-tick214.json` registra
6.323 triângulos/1.107.072 bytes reservados de terreno, 683 desenhos/1.365
triângulos de piso, e 69 camadas/49 junções de parede. O PPM final foi
inspecionado; SHA256:
`6003498c6479da998666153da13c85da3575f9e11901be08760499b669691288`.
PNG de consulta visual local:
`reports/odh-person-main-lot-house07-tick214.png` (derivado do PPM, também
ignorado pelo Git). O probe terminou com código 3 porque o limite explícito de
214 ticks foi alcançado; o receipt identifica status VM `waiting`, sem falha
do host.

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
  --render-frame /tmp/house07-main-lot-tick214.ppm \
  --render-floors '../referencias/The Sims/GameData/floors.iff' \
  --render-walls '../referencias/The Sims/GameData/walls.iff' \
  --render-level 1 --render-wall-mode up --render-terrain-theme 0
```

Esta evidência comprova captura CPU do Main sobre camadas do lote usando o
mesmo World restaurado. Não mostra objetos nem o segundo avatar; não valida
X11, input físico, interações, recursos remapeados ou aparência original.
