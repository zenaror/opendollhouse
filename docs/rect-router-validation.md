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

## Otimização de linhas e direção de entrada

`optimize_rect_route_lines` é uma API separada para a geometria de
`VMRectRouter.OptimizeLines` da revisão acima. Ela remove nós
intermediários com entradas consecutivas iguais e tenta cortes progressivos,
exigindo que cada saída de raio HiP pertença ao próximo retângulo livre na
sequência. O primeiro corte que falha encerra a tentativa; o último aprovado
recalcula os crossings intermediários e os `LineID`. O vetor de entrada não é
alterado.

O quarto argumento opcional é a direção de entrada `float` em radianos. Quando
há pelo menos dois nós, o ramo de portal calcula a projeção inicial, limita o
deslocamento inicial a oito unidades, desloca os crossings intermediários e
os limita à aresta compartilhada na ordem direita/esquerda/topo/baixo. O último
nó é preservado por esse ajuste; o corner-cutting começa no segundo nó e pode
recalcular os crossings seguintes. `incoming_direction_applied` indica que o
ramo foi executado, mesmo se a projeção inicial já consumiu as oito unidades.
Sem direção, os resultados anteriores são preservados.

O frontend Simitone fixado referencia MonoGame 3.8.4. Uma execução direta de
`Vector2.ToPoint()` desse pacote confirmou truncamento para zero inclusive em
valores negativos. O ajuste usa produtos/soma float32 separados, trigonometria
double com cast para float e a conversão do offset antes da soma inteira do
`Point`. A distância restante usa `Math.Sqrt` sobre a soma inteira dos
quadrados do `Point`, com cast direto para float, sem truncamento intermediário
para int. Se a aritmética int32 da referência transbordaria, ODH retorna
`unsupported_coordinates` em vez de reproduzir wraparound/NaN.

A saída preserva `entry_hip` em escala fixa 0x8000 e também a entrada inteira
truncada para zero como na referência; descartar a fração alteraria os futuros
segmentos. O raio usa divisão inteira, teste de borda inclusivo e desempate
estrito de t em y. Aritmética que excederia um resultado int32 da referência é
rejeitada como `unsupported_coordinates`; limites de entradas e trabalho
falham sem saída parcial. O chamador deve fornecer um corredor encontrado,
com entradas nos respectivos retângulos e destino no último retângulo. Uma
cadeia arbitrária com gap não é promovida a rota válida pelo otimizador.

Os goldens sintéticos foram comparados com um harness temporário C#/.NET 10,
transcrito das rotinas `OptimizeLines` (`VMRectRouter.cs:101-226`) e
`RaycastLineHiP` (`VMObstacle.cs:186-234`) na revisão fixada. Para a linha
`(1,1) -> (29,9)`, ambos produziram crossings em x=10 e x=20 de
`(327680,117024)` e `(655360,210648)` HiP; também coincidiram nos casos de
curva, gap e duplicata. O harness não invoca o Simitone compilado nem o jogo
original. Os testes C++ cobrem no-op vazio/retângulo único, duplicata
intermediária, frações, último corte aprovado antes de uma falha, gap, overflow
e limites. O teste focal e a suíte CTest completa passaram com compilação
warning-enabled.

O harness C#/.NET 10 com MonoGame 3.8.4 também coincidiu com a saída C++ nos
casos de projeção inicial maior que oito, clamp com múltiplos crossings,
offsets `+0.3f`/`-0.3f` e rota de dois nós. Nos offsets, a entrada ajustada foi
respectivamente `(7,10)` e `(3,10)`, e o crossing seguinte foi
`(185686,655360)` e `(141994,655360)` HiP. O teste de clamp produziu crossings
`(327680,0)`, `(655360,65535)` e `(983040,131070)` HiP. São fixtures
sintéticas; não houve execução do Simitone compilado nem do jogo original.

O diagnóstico de rota AttemptWalk de `odh-person` chama esta API quando o
corredor bruto retorna `found` e mantém os dois corredores em campos separados.
Para esse probe, `dirIn` fica omitido porque o estado da rota-pai e
`PortalTurns` não são reconstruídos; o JSON expõe essa limitação. Geração de
waypoints, Bezier, clearance e movimento continuam pendentes. Esta é uma
implementação bounded de geometria `OptimizeLines`, não uma alegação de
equivalência completa ao Simitone ou ao executável original.

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

Na amostra viva House07, Main tick 214, o corredor para `(408,456)` passou
pelo otimizador. A busca bruta gerou sete retângulos, expandiu três e produziu
as entradas `(392,440)` e `(399,449)`. A saída otimizada preservou a primeira
entrada e ajustou a segunda para `(401,449)` (`entry_hip` igual a
`[13139968,14712832]`); ambos os `line_id` são `-1`. O JSON mantém o corredor
bruto para comparação, indica `optimize_lines_applied=true` e
`incoming_direction_context=not_reconstructed`. A execução terminou no limite
de 214 ticks (código 3), como esperado para essa amostra.

O primeiro destino coincide com a posição salva do ator; esse caso retorna
corredor vazio e não representa um deslocamento. No JSON vivo, ele retorna
`optimize_lines_status=not_applicable_turn_only` e
`optimized_corridor=null`. Os demais resultados são
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
