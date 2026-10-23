# Comparação do caminho de rota dentro do cômodo

Esta leitura compara o auditor de rota do ODH com a referência Simitone TS1
fixada em `4906db3156616bfaffc10ce0963cf08062975fef` e seu submódulo FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`. É uma comparação estática do
código. Ela não executa Simitone nem demonstra equivalência com o jogo original.

## Como o Simitone tenta andar

Em `TSOClient/tso.simantics/Engine/VMRoutingFrame.cs`, `AttemptWalk()` trata
primeiro o caso em que início e destino caem no mesmo ponto: o estado vira
`TURN_ONLY`. Caso contrário, exige uma sala diferente de zero e cria o conjunto
de obstáculos daquela sala. Ele acrescenta uma borda de um tile em torno dos
limites da sala, considera os objetos da sala e alguns avatares ativos, expande
as pegadas em três unidades e aplica as flags de interseção e EntryPoint 5 dos
dois lados. Se o início já estiver dentro de um obstáculo, a tentativa falha.

`VMRectRouter.Route()` em
`TSOClient/tso.simantics/Engine/Routing/VMRectRouter.cs` parte da direção
cardinal do avatar, constrói regiões livres e procura uma sequência de
retângulos. Depois, `OptimizeLines()` tenta encurtar os trechos dentro dessas
regiões. O chamador escolhe caminho Bezier ou linha conforme o tuning e avança
o primeiro waypoint. Portanto, encontrar uma rota de retângulos é só uma parte
do movimento que o jogador vê.

## O que o auditor ODH cobre

`build_runtime_attempt_walk_probe()` monta a lista de obstáculos de arquitetura,
paredes, portais e entidades do `World` restaurado. O `World` atual mantém as
threads salvas dormentes e inicia um único Main novo, então o adaptador informa
que não há outros avatares ativos a considerar nesse perfil. Grupos de portas,
salas e entidades sem inventário completo fazem o snapshot falhar fechado.

Há dois diagnósticos ODH separados. O `same_room_route_audit` do candidato
monta um snapshot novo da posição real do avatar, cria uma grade por células
com `build_world_room_route_grid()` e procura caminho com `find_route()`. Ele
só tenta isso quando início e destino estão no mesmo cômodo. Sala inválida ou
diferente continua sem avaliação e exige portal ou escada. O custo e a
quantidade de pontos desse JSON descrevem a busca na grade; não descrevem um
caminho de `VMRectRouter`. Mesmo quando início e destino são iguais, esse probe
não muda o estado do controlador para `TURN_ONLY` como faz o Simitone.

O diagnóstico independente `--attempt-walk-destination` em `tools/person.cpp`
chama `route_rectangles()` e `optimize_rect_route_lines()`. É essa rota que usa
o port C++ do roteador de retângulos e a conversão da direção de oito posições
para cardinal, arredondando empates para o cardinal par como `Math.Round` do
Simitone. `src/rect_router.cpp` acompanha estaticamente a busca por regiões
livres, a ordem inicial de expansão, os custos, a escolha do ponto mais próximo
e a otimização de linhas. Ainda falta comparar os resultados desse port com
casos executados pela referência; isso não valida o diagnóstico de grade do
candidato.

Nenhum dos dois diagnósticos movimenta o avatar nem executa waypoints. No JSON
do candidato, `route_clearance=false` e `placement_decision=not_evaluated`.
O ODH também não cria, nesses diagnósticos, o caminho Bezier/linha que o jogo
usa depois da busca.

## Amostra real sem abrir janela

Com os dados locais de House07, `odh-person` executou o Main por 32 ticks sem
janela. O avatar começou em `[392,440]` e a consulta até `[400,440]` encontrou
rota dentro da grade completa da sala: custo 80 e 9 pontos. O preflight da
célula candidata ficou `clear`; o processo terminou em `waiting` no tick 32
(código 3 do host). Esta amostra valida o caminho de leitura e cálculo em uma
casa real, mas não input, movimento, paridade visual nem equivalência ao jogo.

## Próximo passo

Antes de ligar qualquer das buscas ao movimento, falta comparar a grade por
células e o roteador de retângulos com execuções da referência em casos
controlados: rota direta, desvio, início bloqueado, ponto igual, empate na
direção e bordas da sala. Separadamente, a integração precisa manter os gates
de sala/portal/escada, validar o destino e só então atualizar posição e estado
do avatar. A cobertura CLI de EP5 verdadeiro/falso e parada por EP26 também
permanece pendente; os testes unitários atuais cobrem esses contratos em
separado.
