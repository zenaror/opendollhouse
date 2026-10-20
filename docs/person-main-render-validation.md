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
