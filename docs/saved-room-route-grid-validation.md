# Grade candidata de rota por sala salva

`odh_saved_room_route_grid` rasteriza uma sala geométrica em coordenadas de
1/16 tile para alimentar o `RouteGrid` limitado. A área fica presa ao retângulo
da sala; posições cujo mapa de salas não aponta para a sala solicitada são
bloqueadas. RoomObs, WallObs fornecido pelo chamador e caixas de entidades
conhecidas também bloqueiam centros de avatar. Footprints de entidades recebem
a expansão de três unidades usada por `VMRoutingFrame.AttemptWalk`; entidades
incertas com caixa conhecida são obstáculos conservadores.

O gerador exige inventário de entidades, RoomObs e WallObs declarados completos.
Uma entidade da sala sem caixa ou sala conhecida, identidade/limites inválidos,
obstáculos incompletos ou orçamento excedido retornam `unsupported`. Registros
com o sentinela OBJM `(-16,-16)` são excluídos somente desta sala não-zero:
Simitone `GetRoomAt` os atribui à sala 0, que não aparece em
`VMRoutingFrame.AttemptWalk` para uma sala interna.

Os limites padrão são 262.144 células, 4.096 entidades, 16.384 retângulos e
4.194.304 células rasterizadas em soma. Assim, o trabalho do gerador tem teto
determinístico além do teto de alocação do `RouteGrid`.

`ready` significa somente que o candidato geométrico está formado. A unidade
não gera WallObs: o chamador ainda precisa fornecer exatamente os obstáculos
de parede da sala. Ela também não executa flags vivas, bilateral EntryPoint 5,
estado runtime de portas, validação de slot/goal, transições de portal ou
`VMRectRouter`. Não conectar esse resultado ao Routing45 nem rotulá-lo como
`routing_clearance` até completar essas condições.

`odh-saved-room-route-grid-tests` cobre área da sala, desvio do `RouteGrid` ao
redor de footprint conservador, bloqueio de RoomObs,
WallObs e entidades, margem de footprint, entidade incerta com shape conhecida,
sentinela fora da sala, entradas incompletas e tetos de células/trabalho. São
fixtures sintéticas, não prova de rota ou gameplay em House07.
