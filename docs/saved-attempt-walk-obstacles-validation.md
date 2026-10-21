# Snapshot de obstáculos AttemptWalk

`odh_saved_attempt_walk_obstacles` monta uma lista limitada de obstáculos
equivalente às entradas estáticas observadas antes da busca em
`VMRoutingFrame.AttemptWalk`, no Simitone fixado. Ela não chama
`VMRectRouter`, não produz caminho e não equivale a clearance runtime.

## Contrato implementado

- Requer mapa e inventários completos de `RoomObs`, `WallObs` e entidades;
  IDs, salas ou footprints desconhecidos falham fechados.
- Compõe `WallObs + RoomObs`, depois quatro faixas de limite com um tile de
  largura. Os limites geométricos da sala são zero-based e inclusivos; o
  perfil fonte usa `(Bounds.X - 1) * 16`, largura `(Bounds.Width + 2) * 16` e
  a mesma transformação para Y/altura.
- Inclui objetos game e avatares explicitamente considerados. Uma rota raiz
  nova começa com `AvatarsToConsider` vazio; o adaptador salvo declara esse
  perfil em vez de inferir membros de uma rota já em andamento.
- Aplica `DisallowPersonIntersection` ou ausência de
  `AllowPersonIntersection`. EP5 do ator e do bloqueador são bilaterais e
  curtos: `true` do ator dispensa a segunda chamada. Action IDs <=255 retornam
  false; IDs maiores exigem um resolver de BHAV e mantêm unsupported quando a
  forma não é aceita.
- Expande cada footprint bloqueador por três unidades e testa a interseção do
  ponto inicial com limites estritos. Tetos padrão: 4.096 entidades, 16.384
  retângulos e 4.096 execuções EP5. Falhas não retornam uma lista parcial como
  pronta.

## Validação House07

A CLI `odh-wall-appearance --route-grid-diagnostic 105 128` lê o House07,
`Objects.far`, definições salvas de avatar, Neighborhood, fontes arquitetônicas
explícitas e `Global.far/Global.iff/SLOT100`. As tabelas EntryPoint vêm do asset
exato da definição selecionada; EP5 usa apenas o executor read-only já
allowlisted. Isso evita tratar ausência do cache de EntryPoints em `SavedWorld`
como action 0.

No estado validado em 2026-10-21, portas com caixa salva e HasZeroExtent
desligado entram no snapshot por sua geometria OBJD, mesmo quando o estado da
porta segue runtime e desconhecido. Outras entidades unsupported, como avatares
sem KillTimeout, não são convertidas em footprints. O snapshot ficou `ready`
com 153 retângulos e 4 footprints de porta. O diagnóstico de corredor passou a
consumir esse snapshot com o módulo bounded `odh_rect_router`, descrito em
`docs/rect-router-validation.md`. O snapshot em si não chama o router
(`router_invoked_by_snapshot_builder: false`); a seção irmã
`attempt_walk_rect_router_diagnostic` executa a busca salva e mantém
`runtime_door_state_validated: false`, `route_clearance: false` e
`main_movement_integrated: false`. A rota runtime ainda depende do World vivo,
da classificação de portas e dos obstáculos de portal produzidos por RoomInfo.

Verificação do checkpoint inicial: o teste focal `saved_attempt_walk_obstacles`
passou; build Debug completa e 156 testes CTest passaram, com
`linux_window_backend` pulado por falta do display X11. A validação posterior
do router e o total atual de 157 testes estão em
`docs/rect-router-validation.md`. O diagnóstico CLI real produziu JSON válido;
não houve validação visual nem de gameplay.

## Proveniência

Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, FreeSO pinado
`f86bbd34b2112296ed786741f640a0535886d180`:

- `FreeSO/TSOClient/tso.simantics/Engine/VMRoutingFrame.cs`,
  `AttemptWalk`: limites, composição, filtro de entidades, flags, EP5 bilateral,
  expansão e consulta ao router.
- `FreeSO/TSOClient/tso.simantics/Model/VMRoomMap.cs`,
  `VMRoom.RoutingObstacles`: concatenação de WallObs e RoomObs.

O caminho atual é uma fotografia salva com arquitetura inicial, não a lista
construída por `RoomInfo` vivo. O relato também não demonstra equivalência com o
executável original.
