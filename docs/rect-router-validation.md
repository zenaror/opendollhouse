# Diagnóstico de corredor VMRectRouter

`odh_rect_router` implementa uma fatia bounded do modelo de corredor do
Simitone fixado em `4906db3156616bfaffc10ce0963cf08062975fef`, nas fontes
`VMRectRouter.cs`, `VMFreeList.cs` e `VMObstacleSet.cs`. O módulo preserva a
ordem de inserção fornecida, colisão por interseção de área positiva, Contains
inclusivo, primeiro avanço guiado pelo heading, expansão subsequente na ordem
topo/direita/baixo/esquerda, empates A* estáveis e distâncias inteiras
truncadas. Orçamento excedido e aritmética fora do domínio suportado falham sem
corredor parcial.

`rough_balance_obstacle_prefix` reproduz o shuffle parcial `new Random(1)` de
`VMObstacleSet.RoughBalanced` para o prefixo arquitetônico. Um vetor golden
emitido por `System.Random(1)` no .NET 10 confirma a permutação de 16 índices;
o Simitone fixado mira .NET 9, e a reprodução usa o algoritmo legado de seed
explícito. A CLI aplica o shuffle somente a `wallObs + roomObs`, antes das
quatro bandas e das entidades, igual à ordem de construção observada.
O cálculo do corredor não aplica `OptimizeLines` automaticamente. A ordem das
entidades runtime ODH ainda não foi provada idêntica a
`roomInfo.Entities`; por isso nenhum resultado afirma equivalência integral ou
clearance do executável original.

## Otimização de linhas com `dirIn=null`

`optimize_rect_route_lines` é uma API separada para a geometria de
`VMRectRouter.OptimizeLines` da revisão acima, com o ramo de direção de entrada
omitido explicitamente (`incoming_direction_applied=false`). Ela remove nós
intermediários com entradas consecutivas iguais e tenta cortes progressivos,
exigindo que cada saída de raio HiP pertença ao próximo retângulo livre na
sequência. O primeiro corte que falha encerra a tentativa; o último aprovado
recalcula os crossings intermediários e os `LineID`. O vetor de entrada não é
alterado.

A saída preserva `entry_hip` em escala fixa 0x8000 e também a entrada inteira
truncada para zero como na referência; descartar a fração alteraria os futuros
segmentos. O raio usa divisão inteira, teste de borda inclusivo e desempate
estrito de t em y. Aritmética que excederia um resultado int32 da referência é
rejeitada como `unsupported_coordinates`; limites de entradas e trabalho
falham sem saída parcial. O chamador deve fornecer um corredor encontrado,
com entradas nos respectivos retângulos e destino no último retângulo. Uma
cadeia arbitrária com gap não é promovida a rota válida pelo otimizador.

Os goldens sintéticos são derivados da fórmula inteira, sem comparação
executada com o C# da referência nesta etapa. Para a linha `(1,1) -> (29,9)`,
os crossings nas bordas x=10 e x=20 são `(327680,117024)` e
`(655360,210648)` em HiP. Os testes cobrem no-op vazio/retângulo único,
duplicata intermediária, frações, último corte aprovado antes de uma falha,
gap, overflow e limites. O teste focal `rect_router` passou com compilação
warning-enabled.

Esta API ainda não é chamada pelos probes. O ajuste de entrada vindo de portal
(`dirIn`), geração de waypoints, Bezier, clearance e movimento continuam
pendentes; isto é uma implementação parcial de `OptimizeLines`, não uma
alegação de equivalência completa ao Simitone ou ao executável original.

## House07

O diagnóstico `--route-grid-diagnostic 105 128` converte os 153 retângulos do
snapshot AttemptWalk salvo para o router e submete os três destinos SLOT que
passaram pelos gates estáticos. A direção de Bob é convertida dos oito notches
para cardinal com o arredondamento ties-to-even usado pelo perfil Simitone.
Resultado na instalação local:

| Destino | Resultado | Retângulos expandidos | Entradas no corredor |
|---|---:|---:|---:|
| (392, 440) | found | 0 | 0 |
| (408, 456) | found | 3 | 2 |
| (376, 456) | found | 3 | 2 |

O primeiro destino coincide com a posição salva do ator; esse caso retorna
corredor vazio e não representa um deslocamento. Os demais resultados são
cálculos sobre snapshot salvo, incluindo caixas geométricas de quatro portas
sem estado runtime validado. `route_clearance`, `runtime_door_state_validated`
e `main_movement_integrated` permanecem false. Com RoughBalanced habilitado,
esta reexecução somou 4.402 unidades de trabalho. A ordem de entidades continua
sendo a sequência owner-ID do snapshot ODH.

## Validação

O teste focal `rect_router` cobre corredor simples, passagem por aresta,
destino na borda, início bloqueado, destino bloqueado, parede separadora,
heading oposto, determinismo da ordem, limites, coordenadas sem suporte,
entrada inválida e vetor golden do shuffle seed-1 comparado com .NET 10.
Compilação warning-enabled e CTest focal passaram. A build
completa e os 157 testes CTest também passaram; `linux_window_backend` foi
pulada pela ausência de display X11.
