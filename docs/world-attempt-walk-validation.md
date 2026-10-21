# Snapshot AttemptWalk a partir do World vivo

`build_world_attempt_walk_obstacles` reconstrói a lista RoomInfo do `World`
atual e adapta seus footprints/coleções para o compositor AttemptWalk já
existente. O probe `odh-person --saved-world-main` combina essa lista com
RoomObs/WallObs do mapa preparado e com a inventariação Portal15 das portas
salvas e posições atuais. Flags e consultas EP5 permanecem ligadas ao World
vivo; EP5 só executa a forma limitada e read-only já permitida.

O adapter revalida o inventário após callbacks EP5 e falha fechado se entidades
ou projeções espaciais mudarem. Uma porta ou outro objeto arquitetônico criado
no World sem grupo/estilo salvo identificável deixa o inventário incompleto.
Não são inventados dados para entidades, geometrias ou resultados EP5
desconhecidos. O conjunto `AvatarsToConsider` é explicitamente vazio para a
raiz isolada do `odh-person`: o host executa apenas o Main escolhido e mantém
as demais stacks e filas salvas dormentes. House07 tem um avatar adicional na
sala, mas não há thread de roteamento concorrente nesse host. Isso completa o
conjunto para este probe isolado; não reconstrói `WaitTime`/`VMRoutingFrame` de
um simulador multithread nem pode ser generalizado para um pedido futuro no
jogo.

## House07 no Main

Em 2026-10-21, `odh-person` foi compilado em Debug e executado com dados
Classic locais, House07, 23 recursos loose `.flr`/`.wll`, render CPU e o Main
restaurado até o limite de 214 ticks. O processo terminou com código 3 pelo
limite de ticks já documentado; o JSON e o frame foram produzidos e o JSON foi
validado. A sala do avatar 105 é a sala 3; o inventário RoomInfo contém 19
footprints. O novo `attempt_walk_snapshot` reportou:

- `state=ready`, `architecture_complete=true` e
  `portal_inventory_complete=true`;
- 24 aberturas Portal15, 8 retângulos RoomObs e 128 retângulos WallObs;
- 153 retângulos totais após faixas externas e obstáculos RoomInfo;
- 13 IDs de entidades presentes como obstáculos, zero chamadas EP5 nesta
  amostra e início sem interseção.
- Um diagnóstico solicitado para `(408,456)`, destino SLOT fora da posição
  inicial, percorreu o snapshot vivo com a ordem RoughBalanced seed-1 e
  retornou `found`: 7 retângulos gerados, 3 expandidos, 2.258 unidades de
  trabalho e duas entradas de corredor, `[392,440]` e `[399,449]`.

Os números demonstram que a composição bounded e o cálculo de corredor
funcionaram sobre o World construído pelo Main para esse estado e arquitetura
inicial. `ready` só indica que os dados usados para montar a lista estavam
completos. A ordem de inserção das entidades ODH por owner ID ainda não foi
provada igual a `roomInfo.Entities`; `OptimizeLines` também não foi aplicado.
O adaptador não atualiza a geometria ao abrir/fechar uma porta, não prova
equivalência com o executável original, não calcula clearance, não move o
avatar e não integra Route45 ao Main. Esses campos continuam explicitamente
falsos.

## Testes

`world_attempt_walk` usa um World sintético para confirmar caixas live,
aplicação da política/EP5 bilateral e invalidação quando uma callback muda o
World durante a montagem. O teste focal passou. Build Debug completa em
`/tmp/odh-attemptwalk-build` e CTest passaram com 158/158; o teste
`linux_window_backend` foi pulado por falta de X11 acessível.

## Proveniência

O contrato base é descrito em
[`saved-attempt-walk-obstacles-validation.md`](saved-attempt-walk-obstacles-validation.md)
e usa a referência Simitone fixada registrada ali. Esta integração adiciona
dados observados do World ODH; não afirma comportamento runtime do executável
original.
