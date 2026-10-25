# Retrato de cabeça no renderizador

## Estado

`SceneRenderer::render_head_portrait` fornece a saída CPU de 74×74 pixels para
um retrato focado no osso `HEAD`. A chamada usa uma projeção em perspectiva
separada da câmera isométrica de lote e aplica a pose head-only. A projeção não
muda `WorldProjection` nem a saída normal do lote. O rasterizador agora também
tem um modo opcional de profundidade recíproca e atributos corrigidos para
perspectiva; retratos ativam esse modo, os demais usos mantêm o modo affine.

Os valores iniciais de FOV π/3, zoom 13, distância 13,5, ângulos `atan(0,5)` e
`atan(-0,5)` e deslocamento vertical 0,1 foram consultados em
`refs/upstream/Simitone/Client/Simitone/Simitone.Client/UI/Model/UIIconCache.cs`,
em `GenHeadTex`. A pose foi conferida em `SimAvatar.StripAllButHead`. O
renderizador usa seu próprio cálculo de câmera e rasterização CPU; equivalência
com o pipeline gráfico do Simitone não foi demonstrada.

## Matriz de testes sintéticos

| Caso | Resultado |
|---|---|
| Saída com dimensões 74×74 e geometria visível | passou |
| Renderizações repetidas com pixels e profundidade idênticos | passou |
| Direção/posição do avatar no lote não altera o retrato | passou |
| Câmera com zoom inválido é recusada | passou |
| Triângulo que cruza o plano próximo é recortado e ainda desenha | passou |
| UV e profundidade corretos para perspectiva em pesos analíticos | passou |
| Saída normal do lote permanece igual | passou |

`scene_hand_gestures`: 86 verificações; teste `raster` cobre também
interpolação de perspectiva e a recusa de profundidade não positiva.
`scene_cfp_provider`, `reusable_scene_host`, `saved_scene_cli` e `raster`:
CTest 5/5 no total. Os cinco alvos
`odh-person`, `odh-scene-hand-gesture-tests`, `odh-scene-cfp-provider-tests`,
`odh-scene-renderer-driver` e `odh-saved-scene` compilaram em
`build/linux-checkpoint`; `odh-raster-tests` também compilou.

## Integração provisória no seletor

O seletor Linux fornece agora retratos para os cartões a partir do
`SceneRenderer` já montado para cada morador. Ele desenha uma miniatura de 48×48
e mantém apenas os pixels RGBA num cache limitado a oito moradores: no máximo
175.232 bytes de pixels, mais os metadados do mapa. A chave confere GUID,
identidade do renderer, descritores BODY/HEAD e lista exata de acessórios
ativos. Uma troca de renderer ou acessório invalida o retrato.

Execução com `tools/play_linux.py --mode 1 --house 7 --resident-id 48
--language 1`, instalação local original e Xvfb isolado: a captura mostra Bob e
Betty com retratos. O relatório marcou `host_failed=false`, 61 ticks e família
House07 com GUIDs 850010198 e 844195932. Captura ignorada pelo Git:
`reports/household-selector/household-portrait-integration.png`, SHA256
`d0b6fb6275e9d9846b9c184a8816b24d53cb8539b1c7865d17b1bce9ae71a28a`. O JSON
completo do processo está em `reports/household-selector/portrait-session-output.json`,
SHA256 `75972503ffb1a423273b2ed41873d83f9dc4acbfd254c9aafb647628d7b46d21`.
`xvfb-run` encerrou a tela e não restou processo do jogo. A integração de UI é
uma faixa de teste ainda provisória.

## Limites e próximos passos

A imagem é real, mas não foi comparada lado a lado com uma captura do
The Sims 1/Simitone. Os testes de interpolação e clipping continuam sintéticos;
não provam enquadramento correto para todos os tipos de corpo, idade, cabeça ou
expansão. Falta completar o cache para todos os caminhos de troca de aparência,
validar mais moradores e reconstruir categorias/painéis da interface. Não há
afirmação de paridade visual nem de UI jogável completa.
