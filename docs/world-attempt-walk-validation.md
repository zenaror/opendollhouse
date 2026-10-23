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

`Entity` também mantém `radian_direction` contínuo para avatares. O notch de
oito direções em `EntityPosition`/ObjectData é derivado com arredondamento
midpoint-to-even, como na conversão usada pela referência Simitone; movimentos
no mesmo notch preservam o ângulo e uma mudança explícita de notch o
canonicaliza. Isso fornece o dado angular que `OptimizeLines` pode consumir,
mas o diagnóstico ainda não instancia `VMRoutingFrame`, não recupera
`PortalTurns`/direção de entrada e não passa esse ângulo por `Route45`.

## Footprints dinâmicos

O Simitone coloca um footprint em `DynamicObstacles` quando
`StaticFootprint=false`, mas `VMRoutingFrame.AttemptWalk` forma seus obstáculos
percorrendo `RoomInfo.Entities` e lendo `obj.Footprint` diretamente; portanto,
ser dinâmico não elimina o bloqueador dessa consulta. O ODH recalcula o
snapshot live a partir das posições atuais. Um teste agora confirma que um
objeto com footprint dinâmico continua sujeito ao EP5 bilateral e que um novo
snapshot após movimento contém sua caixa atualizada. Isso valida o adaptador
sintético e o mecanismo de revisão espacial; não demonstra porta comercial
abrindo/fechando, refresh de `RoomObs` por `RegenRoomObs` nem clearance runtime.

No host `odh-person`, `refresh_route_obstacles()` compara a revisão espacial e,
antes de cada tick de uma rota ativa, recompõe AttemptWalk, inventário Portal15,
RoomObs e a grade da sala quando o `World` mudou. `Routing::tick` lê essa mesma
grade mutável e confere o próximo passo antes de mover. Um teste novo bloqueia
o próximo ponto depois do planejamento e verifica que a rota falha sem mover o
avatar. Isto verifica o contrato de atualização da grade e parada fail-closed;
não é simulação de BHAV de porta nem prova de clearance completo do jogo.

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
- O mesmo corredor passou pela API bounded `optimize_rect_route_lines`; o
  diagnóstico JSON preserva o corredor bruto e emite o otimizado separadamente.
  Para `(408,456)`, a segunda entrada ajustou de `[399,449]` para `[401,449]`,
  com `entry_hip=[13139968,14712832]` e ambos os `line_id=-1`.
- `dirIn` não foi fornecido porque o contexto da rota-pai/`PortalTurns` não é
  reconstruído por este comando. O JSON marca
  `incoming_direction_context=not_reconstructed` e
  `incoming_direction_applied=false`.
- Quando o destino é a posição inicial `(392,440)`, o corredor fica vazio e o
  JSON marca `optimize_lines_status=not_applicable_turn_only`; não o confunde
  com uma otimização executada nem inventa um trecho de movimento.

Os números demonstram que a composição bounded e o cálculo de corredor
funcionaram sobre o World construído pelo Main para esse estado e arquitetura
inicial. `ready` só indica que os dados usados para montar a lista estavam
completos. O Simitone fixado insere `RoomInfo.Entities` em ordem crescente de
`ObjectID` por `VM.AddToObjList`; ODH captura o World vivo por ID crescente e o
snapshot de obstáculos preserva essa ordem. O JSON agora informa
`entity_obstacle_order_matches_reference=true` somente quando a sequência
serializada de IDs de obstáculos é estritamente crescente. O campo mais amplo
`entity_order_matches_reference` continua falso, pois esta checagem não inclui
entidades sem obstáculos. É uma comparação de ordenação baseada no código-fonte,
não prova de comportamento TS1 ou de equivalência da seleção de entidades.
O adaptador não atualiza a geometria ao abrir/fechar uma porta, não prova
equivalência com o executável original, não calcula clearance, não move o
avatar e não integra Route45 ao Main. Esses campos continuam explicitamente
falsos. Consulte [`rect-router-validation.md`](rect-router-validation.md) para
comparar os corredores bruto e otimizado.

## Legacy Collection House05, 2026-10-23

O probe Linux com dados da Legacy Collection isolados em `/tmp` encontrou duas
regras comerciais adicionais em `EntryPoint5`: `UrnStone.iff#4105` compara o
tipo do objeto com 2 e, para Sims, `PersonData[31]` com `Attribute[1]`; já
`DiningTables.iff#4122` confirma pelo `Global.iff#355` que o objeto é um avatar
ativo e testa as flags 2 e 3 de `PersonData[65]`. O avaliador aceita apenas
essas formas BHAV exatas, lê os bancos do World atual e continua retornando
`unsupported` quando algum dado necessário não está disponível. Testes
sintéticos cobrem resultados verdadeiro/falso, curto-circuitos, dados ausentes
e formatos alterados.

O World salvo agora também preserva o campo `OBJD Type` que esse BHAV consulta.
Com isso, o teste headless da House05 completou 4096 passos sem erro: o residente
16 saiu de `(440,360,1,0)` e terminou em `(472,392,1,5)`, com 54 atualizações
de movimento; o residente 18 permaneceu estável. A verificação oficial terminou
com `status=passed`, `host_failed=false` e confirmou que os 131 arquivos de jogo
usados mantiveram os mesmos hashes. Isso prova movimento nesse cenário real e
limitado; não prova que toda a casa, outras rotas, animações, interações ou a
interface estejam prontas. A viagem entre casas continua bloqueada pelo perfil
de SLOT ainda não implementado.

## Testes

`world_attempt_walk` usa um World sintético para confirmar caixas live,
aplicação da política/EP5 bilateral e invalidação quando uma callback muda o
World durante a montagem. Além dos retornos simulados, o caso de integração
constrói uma BHAV sintética no formato read-only permitido e a executa pelo
avaliador real: o EP5 do ator retorna false para o objeto, o EP5 do bloqueador
retorna true para o avatar e a pegada RoomInfo deixa de ser obstáculo. Outro
caso altera a forma da BHAV e confirma `unsupported` sem publicar obstáculos
parciais. Isto verifica o encadeamento adapter/política
evaluator→callback→RoomInfo→AttemptWalk para essa forma sintética. Não exercita
a recuperação/cache de BHAV comercial do callback de `odh-person`, não amplia o
conjunto de BHAVs aceitas e não substitui a consulta House07 com o asset original.
Os testes `odh-world-tests` e
`odh-world-saved-bank-tests` também cobrem persistência do ângulo contínuo,
derivação do notch e preservação dos bancos salvos. O teste focal passou. A
build Debug completa em `/tmp/odh-attemptwalk-build` e o CTest passaram com
158/158 após adicionar esses casos; `linux_window_backend` foi pulado por falta
de X11 acessível.

## Proveniência

O contrato base é descrito em
[`saved-attempt-walk-obstacles-validation.md`](saved-attempt-walk-obstacles-validation.md)
e usa a referência Simitone fixada registrada ali. Esta integração adiciona
dados observados do World ODH; não afirma comportamento runtime do executável
original.
caso de integração de adapter/política
