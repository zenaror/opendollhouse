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
`build/linux-checkpoint`.

## Limites e próximos passos

Os testes usam malha sintética. Ainda falta renderizar um morador real da
instalação do jogo, comparar enquadramento e cores com uma captura de
referência, ligar a imagem ao seletor e implementar cache com limite de memória.
Este checkpoint não demonstra retratos corretos para todos os corpos/cabeças,
paridade visual com The Sims 1/Simitone ou uma interface jogável completa.
