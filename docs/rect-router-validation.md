# Diagnóstico de corredor VMRectRouter

`odh_rect_router` implementa uma fatia bounded do modelo de corredor do
Simitone fixado em `4906db3156616bfaffc10ce0963cf08062975fef`, nas fontes
`VMRectRouter.cs`, `VMFreeList.cs` e `VMObstacleSet.cs`. O módulo preserva a
ordem de inserção fornecida, colisão por interseção de área positiva, Contains
inclusivo, primeiro avanço guiado pelo heading, expansão subsequente na ordem
topo/direita/baixo/esquerda, empates A* estáveis e distâncias inteiras
truncadas. Orçamento excedido e aritmética fora do domínio suportado falham sem
corredor parcial.

O módulo não gera a ordem RoughBalanced do .NET, não executa OptimizeLines,
não atravessa portais e não move entidades. Logo, mesmo com as primitivas
alinhadas ao Simitone, caminhos empatados podem diferir se a ordem de entrada
não for a mesma da árvore viva. Não há alegação de equivalência com o
executável original.

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
e `main_movement_integrated` permanecem false. A soma foi 4.564 unidades de
trabalho.

## Validação

O teste focal `rect_router` cobre corredor simples, passagem por aresta,
destino na borda, início bloqueado, destino bloqueado, parede separadora,
heading oposto, determinismo da ordem, limites, coordenadas sem suporte e
entrada inválida. Compilação warning-enabled e CTest focal passaram. A build
completa e os 157 testes CTest também passaram; `linux_window_backend` foi
pulada pela ausência de display X11.
