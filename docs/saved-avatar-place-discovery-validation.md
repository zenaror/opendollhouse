# Descoberta geométrica de overlaps de avatar

`discover_saved_avatar_place_blockers` compõe a geometria candidata do avatar,
as caixas de entidades do World salvo, a sala selecionada pelo mapa geométrico
e a agregação de regras já implementada. Entidades fora da sala do candidato
são excluídas, seguindo `GetObjectRoom`/`GetRoomAt`; posições em diagonais
usam a metade correta do tile. A filtragem só roda com o mapa preparado e
aliases de piso sem ambiguidade; mapas crus/malformados e posições sem sala
resolvida falham fechados.
O footprint usado é o retângulo fixo de raio três unidades do perfil Simitone;
interseção de caixas segue a comparação estrita da referência. Possíveis
overlaps são ordenados por `ObjectID`. Membros do mesmo grupo multitile são
preservados na lista para a regra de grupo do agregador.

A saída é uma auditoria do **snapshot de entidades e salas geométricas**. Ela
não demonstra que as entidades estavam registradas como obstáculos em
`RoomInfo`, não inclui os obstáculos estáticos de arquitetura e não reconstrói
a lista runtime dinâmica.
EntryPoint 5 dos dois lados, flags vivos, estado ghost e presença/função do
EntryPoint 26 continuam desconhecidos; um overlap real com essas regras ausentes
resulta em `unsupported`. Uma auditoria `allowed` só diz que a lista de entidades
salvas fornecida não encontrou um bloqueio conhecido. `runtime_obstacle_inventory_complete`
e `runtime_clearance` permanecem `false`.

Inventários incompletos, IDs inválidos ou duplicados, grupos inconsistentes,
geometria incerta no andar candidato e orçamento acima de 4096 falham fechados.
Os testes `saved_avatar_place_discovery` exercitam 24 verificações sintéticas
(incluindo a verificação do próprio contador), incluindo ordem dos IDs,
filtragem por sala/diagonal, overlap desconhecido, grupo multitile, caixas
desconhecidas e limites. Eles validam o adapter, não o jogo original.

## House07

Com o build Linux atual e dados externos em `../referencias/The Sims`, a
reprodução do diagnóstico House07 documentado em
[`door-portal-probe-validation.md`](door-portal-probe-validation.md) encontrou
seis salas e inventário de portal completo.
Na amostra, seis dos oito pontos circulares não tinham overlap com footprints
de entidades do snapshot e receberam `rule_audit_status=allowed`; dois tinham
overlap possível com os IDs 106 e 125 e ficaram `unsupported` porque os inputs
de BHAV/flags não estão disponíveis. A busca estática reportou `found` com três
goals, enquanto `runtime_occupancy_validated=false` e `route_clearance=false`.
Esse resultado conecta geometricamente a enumeração de SLOT ao classificador
GetAvatarPlace sem converter snapshot vazio em clearance runtime.

A comparação estática está pinada a Simitone
`4906db3156616bfaffc10ce0963cf08062975fef` e ao FreeSO que ele incorpora
`f86bbd34b2112296ed786741f640a0535886d180`, especialmente
`VMContext.cs:1190-1235`. No upstream, `GetAvatarPlace` combina obstáculos
estáticos e dinâmicos da sala, ordena por ObjectID quando há mais de um e roda
BHAV EntryPoint 5 antes de aplicar flags e parada por EntryPoint 26. A descoberta
ODH não reproduz essa montagem ou execução.
