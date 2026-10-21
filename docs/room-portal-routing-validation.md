# Grafo de portais entre salas

`RoomPortalInventory` agora carrega arestas dirigidas junto das aberturas
geométricas usadas por `RoomObs`. Cada `RoomPortalEdge` preserva sala de origem,
sala de destino, ID do objeto Portal e sua posição viva. A construção percorre
os membros na ordem do grupo e, para cada membro `Portal15`, liga sua sala à
primeira outra parte Portal encontrada em uma sala diferente. Isso segue
`VMContext.AddRoomPortal` no Simitone (`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/VMContext.cs:869-881`);
entradas recíprocas são produzidas pela avaliação do outro membro. Portais
contidos em um grupo sem porta arquitetônica ainda produzem arestas, pois o
registro de RoomInfo depende da propriedade estrutural Portal, não da face
visual da porta.

`DynamicRoomPortalInventory::replace_members` preserva a ordem recebida em
lote; os métodos individuais preservam ordem de registro e não reordenam o
grupo quando uma parte se move. A ordem importa: `AddRoomPortal` escolhe a
primeira outra parte Portal válida. O host continua responsável por fornecer a
ordem real do grupo e enviar posições/grupos atuais. O probe AttemptWalk ainda
reconstrói um snapshot por consulta; não mantém uma RoomInfo dinâmica entre
ticks.

`find_room_portal_route` implementa uma busca A* bounded sobre essas arestas
dirigidas. O custo é a distância Euclidiana XY em tiles mais 10 por andar,
usada por `VMRoutingFrame.GetDist`; o score inclui a distância heurística até o
destino e nós são processados em ordem estável. O planejador recusa inventário
incompleto, arestas ambíguas e posições inválidas; os limites independentes de
arestas, expansões e frontier são 4.096 por padrão. `IgnoredRoomPortal`
identifica a conexão por `{portal_id, target_room}`, a mesma informação que o
Simitone adiciona a `IgnoredRooms` quando uma ação Portal falha
(`Engine/VMRoutingFrame.cs:578-599`). Assim, o planejador pode selecionar uma
alternativa sem remover as demais arestas do mesmo lote.

`tests/room_obstacles.cpp` cobre a direção e a primeira correspondência no
grupo, ausência de aresta com somente um membro Portal, menor caminho entre
salas, fallback após ignorar uma aresta, ausência de rota quando alternativas
se esgotam, destino na mesma sala, inventário incompleto, duplicata ambígua e
orçamentos de arestas/frontier.
Os dados são sintéticos. O planejador não executa Portal15, não valida
condições BHAV, não constrói a rota geométrica dentro de cada sala, não altera
RoomInfo/RoomObs em runtime e não move avatares. A integração com o job de
`Routing` ainda precisa consumir uma sequência após cada retorno de BHAV.
