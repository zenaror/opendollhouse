# Candidatos geométricos de obstáculos por sala

`odh_room_obstacles` expõe `generate_room_obstacle_candidates` sobre o mapa
retornado por `generate_saved_room_map`. Ele reproduz a parte puramente
geométrica de `VMRoomMap.GenerateRoomObs`: escolhe a caixa da sala (ou o
interior expandido para a sala 0), examina uma borda de um tile, considera
bloqueado todo tile que não contém o ID da sala em nenhuma das metades
compactadas, cria retângulos em coordenadas de 1/16 tile com margem de 3
unidades e executa o merge ordenado de `OptimizeObstacles`.

Referência: Simitone submódulo FreeSO `4906db3156616bfaffc10ce0963cf08062975fef`,
FreeSO `f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.simantics/Model/VMRoomMap.cs` (`GenerateRoomObs`,
`OptimizeObstacles`). A função original consulta entidades de porta
arquitetônica e seus grupos multitile; se um objeto do grupo estiver no quarto e
marcado como portal, a linha de obstáculo é interrompida. O snapshot salvo e o
mapa geométrico atual não carregam estado suficiente para reproduzir essa
abertura. Este porte mantém esses tiles bloqueados e retorna sempre
`complete=false`; nunca interpreta evidência de portal ausente como passagem
livre.

Quando recebe o mapa cru de `generate_saved_room_map`, o resultado também herda
a ausência do remapeamento de pisos/flags. O caminho de
[arquitetura preparada](prepared-architecture-validation.md) trata essa etapa
com aliases explícitos; ainda assim, os retângulos são candidatos geométricos,
não obstáculos runtime completos, colisão autoritativa, espaço de rota livre ou
aceitação de destino. Esta API não está conectada a Routing45 nem a uma consulta
de movimento.

`tests/room_obstacles.cpp` cobre bounds expandidos, coordenadas/margens de
tile, sala 0, ambos os IDs de tile diagonal (incluindo a máscara do bit de
orientação), ordem/merge de retângulos, estado incompleto e entradas inválidas.
Os testes são sintéticos; não são um diferencial executável com FreeSO nem
prova do comportamento do TS1 original.
