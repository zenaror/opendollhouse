# Changelog

Mudanças relevantes do OpenDollhouse, em português e com os limites de
validação indicados por versão.

## Não lançado

### Adicionado

- Route45 agora pode suspender o ticket raiz para planejar salas em uma grade
  completa de portais, executar EntryPoint15 validado, confirmar a sala real
  após o retorno, atualizar a grade e retomar a rota local. O provider mantém
  estado por ticket, exclui arestas que retornaram falso e respeita
  `IgnoreRooms`. No House07 real, o Route45 do Portal26 agora alcança o SLOT
  `[408,408]` em 32 movimentos pela grade de obstáculos da sala atual, sem
  agendar Portal15. O leitor de `RoomByTemp0` agora usa o cômodo principal
  derivado das ligações do mapa preparado, e o Portal26 do House07 retorna
  `true`. O Sim continua no mesmo cômodo: isso não prova travessia pela porta
  nem uma sessão de jogo completa.

- `RoomByTemp0` lê o valor usado pelo Simitone para os índices 0 a 4 em mapas
  preparados com nomes de piso resolvidos. O índice `Temp0 + 1` é limitado ao
  mapa disponível; o valor externo vem do cômodo principal compartilhado pelos
  cômodos conectados. Mapas incompletos e índices desconhecidos continuam sem
  resposta. Testes cobrem limites, mapa incompleto e diferença entre o cômodo
  selecionado e seu cômodo principal.

- O retorno falso de uma BHAV externa de portal pode consultar um provider do
  host para tentar outra chamada EntryPoint, com limite de 32 tentativas e
  validação do ticket/CodeOwner em cada retorno. Se a tentativa falha após
  mover o avatar, exige refresh válido da grade antes do retry e replana o
  trecho original depois do sucesso. Sem provider ou alternativa, Route45
  falha de forma explícita. A ODH ainda não calcula automaticamente o caminho
  entre salas nem descobre/agenda Portal15 no Main.

- O diagnóstico AttemptWalk separa a equivalência do algoritmo de ordenação
  de `RoomInfo.Entities` da equivalência do conjunto runtime de entidades.
  Simitone e `World` percorrem IDs em ordem crescente; um teste cobre criação
  fora de ordem. A composição por sala, avatares considerados e estado dinâmico
  de portas seguem sem validação.

- O teste do snapshot live AttemptWalk cobre um bloqueador com footprint
  dinâmico antes e depois de mover a entidade. A nova montagem conserva o
  bloqueador e atualiza sua caixa; isso não valida a transição de estado de
  portas comerciais nem clearance runtime.

- `Routing::tick` é testado contra uma grade que o host atualiza após planejar:
  se o próximo passo ficou bloqueado, Route45 encerra sem mover o avatar. O
  `odh-person` já recompõe o snapshot e atualiza a grade antes de cada tick
  ativo quando muda a revisão espacial; isso não valida clearance completo.

- A integração Route45/Main de House07 foi executada com o Main sintético em
  arquivo temporário: avatar percorreu 32 passos; ao mover o objeto 106 para a
  segunda célula da rota no tick 2, o host refez o snapshot e encerrou com
  `returned_false` após apenas um passo, antes de entrar na célula ocupada.
  Isso valida o refresh/fail-closed no host, não interação natural ou travessia
  de portais.

- A inspeção estática da ação Portal15 `Doors.iff:4105` encontrou chamada à
  rotina privada 4113, que contém dois opcodes Route45. A árvore também chama
  outros BHAVs privados. O `GLOB128=DoorGlobals` resolve 8197 em
  `Global.far/DoorGlobals.iff`; a rotina antiga atribui literal zero ao
  argumento 0 no mapeamento atual do decoder, mas não tem metadata 0x8002 e não
  foi executada. A documentação também relaciona esse fluxo ao contrato
  Portal15/child-route do FreeSO pinado, sem afirmar equivalência runtime.
  O BHAV 4113 ramifica por 4115 e escolhe SLOT type-3 literal 0/1 do StackObject
  com `NoFailureTrees`; um teste sintético cobre essa seleção no kernel VM.
  `Routing` ainda recusa esse escopo e flag, e nenhum BHAV comercial foi executado.

- `build_world_room_route_grid` rasteriza um snapshot AttemptWalk completo de
  RoomInfo/RoomObs/WallObs/EP5 numa grade local bounded, incluindo as faixas de
  limite da sala e preservando o motivo de snapshots incompletos. Há uma
  fixture de geometrias e um teste integrado ao snapshot vivo. A grade ainda
  não alimenta o `Routing` nem substitui o VMRectRouter do Simitone.

- Após uma frame externa de rota mover o avatar, `Routing` permite ao host
  reconstruir a grade da sala/andar atual antes de continuar o caminho. `odh-person`
  recompõe o snapshot AttemptWalk e o scheduler replana na grade substituída;
  uma fixture sintética cobre a mudança de posição e de grade. A execução real
  de Portal15 e a navegação entre salas continuam pendentes.

- `Routing` agora preserva até 16 jobs Route45 aninhados quando uma BHAV
  externa em execução dispara outro Route45. O controller valida o vínculo
  entre frame VM, ticket e identidade do avatar, mantém o job pai suspenso,
  propaga `PortalTurns` pelo contexto parent-or-self e recalcula o trecho pai
  após o filho mover o avatar. Fixture sintética cobre a sequência e o retorno
  ordenado. A malha segue única e fornecida pelo host; não é ainda roteamento
  runtime entre salas nem integração de gameplay.

- O inventário de portal agora preserva arestas dirigidas por ID de objeto e
  sala destino, escolhendo a primeira outra parte Portal na ordem do grupo como
  faz `AddRoomPortal` no Simitone. `find_room_portal_route` busca uma sequência
  bounded com a métrica de distância do `VMRoutingFrame` e permite ignorar uma
  conexão que falhou para encontrar alternativa. Ainda não executa a BHAV nem
  monta caminhos geométricos por sala; veja
  `docs/room-portal-routing-validation.md`.

- `DynamicRoomPortalInventory` mantém um conjunto bounded de membros informados
  pelo host e recompõe/publica o inventário após registro, movimento ou remoção,
  incluindo substituição em lote para snapshots do host.
  Posição desconhecida publica estado incompleto para não reter uma abertura
  antiga; identidade duplicada, remoção desconhecida e excesso do limite não
  alteram a geração. Ainda exige notificações do host e não está ligado aos
  callbacks de `World` nem ao replanejamento. No perfil Simitone observado,
  `Portal15` é estrutural; o inventário não modela animação de porta. O probe
  AttemptWalk já consome o snapshot via substituição em lote, ainda sem refresh
  persistente entre ticks.
  Consulte `docs/room-obstacles-validation.md`.

- O host agora pode preparar uma chamada VM para `Portal15`: valida o registro
  salvo e a tabela ligada à entidade, preserva a proveniência do recurso,
  carrega a closure BHAV privada e constrói Caller/Callee/StackObject com um
  `CodeOwner` alocado explicitamente. Condições e chamadas semiglobais
  continuam fora do perfil; as dependências globais são reportadas ao host.
  A rotina ainda não é despachada pelo loop de rota. Consulte
  `docs/entry-points-validation.md`.

- `Entity` agora mantém `radian_direction` contínuo para avatares, enquanto
  `EntityPosition.direction` e ObjectData expõem o notch arredondado de oito
  direções. Movimentos que permanecem no mesmo notch preservam o ângulo; um
  notch explícito novo o canonicaliza. Isso prepara o dado necessário para
  `OptimizeLines`, mas ainda não o aplica à busca angular ou a estados de
  portal. Testes World/save-bank passaram; veja também
  `docs/world-attempt-walk-validation.md`.

- Jobs de `Routing` agora capturam e revalidam o ângulo contínuo do avatar,
  cancelando se ele muda durante uma busca pendente mesmo que o notch continue
  igual; passos do job atualizam esse snapshot e a chegada canonicaliza o
  facing final. O controller também expõe estados explícitos para o frame raiz,
  e helpers testados reproduzem a seleção de direção de entrada e o limite de
  viradas em portal. `odh-routing-tests` cobre esses contratos; a pilha de
  frames-pai e a máquina completa ainda faltam. Consulte
  `docs/routing-validation.md`.

- A VM pode empilhar uma BHAV filha a partir de um serviço em espera, manter os
  tickets aninhados de forma LIFO e retornar ao serviço sem reexecutar a
  instrução pai. `odh-vm-tests` cobre esperas e retornos aninhados; o
  `Routing::call_external_frame` usa o mecanismo para uma BHAV de portal
  resolvida explicitamente pelo host; descoberta de portal/EntryPoint 15 e
  replanejamento ainda faltam. Consulte
  `docs/vm-validation.md`.

- `optimize_rect_route_lines` adiciona a otimização parcial de corredores do
  Simitone: deduplicação e corner-cutting validado por retângulos livres, com
  crossings fixos HiP. O ajuste opcional `dirIn` usa o orçamento inicial de
  oito unidades e as conversões float32/Point da referência; o diagnóstico
  AttemptWalk de House07 preserva o corredor bruto e emite a saída otimizada.
  `dirIn` permanece omitido porque o estado da rota-pai não é reconstruído.
  Os casos sintéticos foram comparados com uma transcrição C#/.NET 10 das
  rotinas e MonoGame 3.8.4, não com o Simitone compilado. Consulte
  `docs/rect-router-validation.md`.

- `snapshot_world_room_info` expõe o inventário bounded e completo de
  footprints atuais por sala, com coleção estática/dinâmica e ordem por ID;
  `query_world_room_info` deriva a query candidata do mesmo contrato. O Main
  diagnóstico publica o inventário vivo junto do probe atual. House07 no tick
  214 retornou 19 footprints na sala 3. Isso ainda não compõe WallObs/RoomObs,
  EntryPoint5 ou clearance. Consulte
  `docs/world-room-info-validation.md`.
- Router bounded de corredores baseado nas primitivas observadas em
  `VMRectRouter`/`VMObstacleSet` do Simitone. O diagnóstico House07 rodou sobre
  o snapshot salvo de 153 retângulos e encontrou corredor para os três destinos
  que passaram pelos gates ODH; mantém clearance runtime e integração com Main
  explicitamente falsos. Não implementa `OptimizeLines`, RoomInfo/portais vivos
  nem movimento. Consulte `docs/rect-router-validation.md`.
- `rough_balance_obstacle_prefix` replica a permutação `new Random(1)` de
  Simitone para o prefixo arquitetônico WallObs+RoomObs, deixando bandas de
  limite e obstáculos de entidades depois dele. Um vetor golden com 16
  entradas corresponde ao runtime .NET 10 instalado; a CLI passou a ordenar
  esse prefixo antes da busca House07. `OptimizeLines` e a ordem exata de
  `roomInfo.Entities` seguem pendentes. Consulte
  `docs/rect-router-validation.md`.
- Snapshot bounded de obstáculos `AttemptWalk`: compõe WallObs/RoomObs, as
  quatro faixas externas de um tile, footprints com margem 3, flags de
  interseção e EP5 bilateral; inventário incerto ou fora do orçamento falha
  fechado. O diagnóstico House07 resolve as tabelas EP5 dos assets exatos e
  inclui portas com caixa salva e HasZeroExtent desligado sem afirmar estado
  runtime. O snapshot House07 fica pronto com 153 retângulos, mas nenhuma rota
  é calculada e `route_clearance` segue false. Consulte
  `docs/saved-attempt-walk-obstacles-validation.md`.
- Adaptador runtime liga o inventário RoomInfo do `World` a RoomObs/WallObs
  preparados e aos portais Portal15 do lote, aplicando flags e EP5 bilateral
  por meio da política AttemptWalk existente. House07 Main no tick 214 ficou
  `ready`: arquitetura e inventário de portais completos, 24 aberturas, 8
  retângulos RoomObs, 128 WallObs e 153 obstáculos; início sem interseção.
  Clearance, estado mutável de portas e movimento no Main continuam falsos.
  Consulte `docs/world-attempt-walk-validation.md`.
- `odh-person --attempt-walk-destination X Y` expõe uma busca de corredor
  diagnóstica no snapshot vivo para destinos na mesma sala. House07 Main,
  avatar 105 até `(408,456)`, retornou `found` com duas entradas. O perfil de
  avatar é uma raiz isolada com threads salvas dormentes; ordem de entidades,
  `OptimizeLines`, clearance, estado de portas e movimento continuam sem
  validação. Consulte `docs/world-attempt-walk-validation.md`.
- Adaptador de auditoria para política de sobreposição entre entidades de um
  `World` restaurado: resolve flags e EntryPoints vinculados, preserva
  inventário incompleto como unsupported e respeita a parada sequencial por
  EP26 sem consultar bloqueadores posteriores. O teste sintético cobre também
  esse curto-circuito. O CLI informa apenas uma auditoria de sobreposição de
  entidades, não uma decisão final de colocação; BHAVs EP5 fora da pequena
  allowlist read-only e obstáculos de arquitetura/portais continuam fora do
  escopo. Consulte
  `docs/world-avatar-place-audit-validation.md`.
- Executor isolado de uma forma BHAV EP5 read-only validada estruturalmente:
  `TestObjectType(StackObject)` seguido de igualdade entre
  `StackObjectAttributes[16]` e literal 1. Um World temporário e dois ticks
  limitados garantem que esse perfil não muta o Main; todas as outras formas
  retornam unsupported. House07 executou o Flamingo BHAV4108 para o candidato
  `(280,376,1,3)` e obteve false após uma instrução. Isso não prova isenção
  bilateral nem compatibilidade com o executável original.
- Módulo puro `odh_saved_avatar_place_overlap` modela uma regra limitada de
  sobreposição de `GetAvatarPlace`: grupo multitile, exceção/tri-state de
  EntryPoint 5, flags de interseção e preferência de parada de EntryPoint 26.
  Os 16 casos sintéticos preservam dados ausentes como unsupported. O módulo
  não executa BHAV nem descobre obstáculos dinâmicos; não prova colocação
  runtime. Consulte
  `docs/saved-avatar-place-overlap-validation.md`.
- Auditoria de bloqueadores agrega uma lista completa em ordem crescente de
  ObjectID, preserva o último bloqueador e a parada por EntryPoint 26. Falha
  fechada para inventário incompleto, IDs fora do domínio VM ou resultados
  ambíguos. São 14 casos sintéticos; não descobre geometria nem executa BHAV.
  Consulte `docs/saved-avatar-place-audit-validation.md`.
- Módulo puro `odh_slot_scoring` calcula e ordena scores do perfil SLOT
  House07 standing/círculo com RNG explícito e ordem de entrada fornecida.
  Preserva consumo de RNG para candidatos rejeitados após score e empates
  estáveis, e aceita penalidade conhecida de avatar/porta/reserva. Entradas
  incertas ou malformadas falham antes de alterar o seed; teto de 4.096.
  Ramos RandomScoring/FSOEqualProximityScore têm testes sintéticos. Não executa
  gates geométricos/PositionValid/EP5 nem liga seed ou ranking ao Main;
  equivalência ao executável original permanece não demonstrada.
- Routing aceita provider opcional de destinos SLOT previamente ordenados e
  tenta um por vez, avançando apenas após `no_path` definitivo. Empates mantêm
  a ordem de entrada; expansões acumulam entre tentativas e limites/entrada
  inválida interrompem sem fallback. A lista é limitada a 4.096 candidatos.
  Chegada preserva entry flags/facing do
  candidato escolhido. A política sequencial segue o Simitone fixado; score,
  RNG e elegibilidade continuam sob responsabilidade do chamador. Sem provider,
  mantém a seleção anterior por menor custo na malha construída.
- Seleção de destino SLOT sobre footprints salvos: o perfil v10
  standing=1/sitting=0 diferencia box livre, objeto sobreposto rejeitado e
  avatar sólido conhecido mantido com penalidade de score. O diagnóstico
  informa elegibilidade nullable e exige uma classificação suportada antes
  de enviar o destino à busca na grade. Portas, multipartes e candidatos
  incertos continuam unsupported; flags/EntryPoint5 e ocupação runtime não
  são executados. Isso não estabelece PositionValid ou clearance.
- Diagnóstico Route45 House07 agora exige a entrada/chunk SLOT explícitos,
  gera os oito candidatos do círculo type-3 de raio exato e publica máscara
  direcional, sala, bounds, piso FloorChangeValid, RaycastWall contra a fotografia
  salva e caminhabilidade. O círculo é um perfil ODH restrito; esses gates
  estáticos não cobrem GetAvatarPlace runtime, e resultado encontrado não prova
  clearance ou equivalência com o jogo.
- Probe offline de portas em saves: liga faces de porta, grupos multitile,
  salas geométricas e ação authored da entrada Portal 15 via OBJD/OBJf; o
  reporte deixa explícito que a BHAV e o clearance de rota não foram executados.
- Geração geométrica de obstáculos aceita pares explícitos tile/sala de portais
  e imita a interrupção de runs de `GenerateRoomObs`; a completude do snapshot
  continua condicionada à identidade inequívoca dos pisos usados e não equivale
  a clearance total.
- Probe de colisão por entidade une flags/OBJM, OBJD e a posição restaurada no
  World. Mantém `unsupported` para posição divergente, avatar sem estado
  `KillTimeout` e porta sem estado runtime; geometria possível não habilita
  clearance ou roteamento.
- Inventário Portal15 invalida a completude para porta/membro sem posição ou
  sala, fora do lote, identidade duplicada e grupos de portas sobrepostos no
  mesmo tile, refletindo a ambiguidade da seleção no Simitone.
- Auditoria estática por sala atribui candidatos a colisão pela sala geométrica
  do centro e compara caixas expandidas contra RoomObs e as quatro faixas
  externas de um tile de `VMRoutingFrame.AttemptWalk`. Os grupos são contados
  separadamente; interseção ou ausência de interseção não vira caminho livre
  ou clearance.
- Grade candidata por sala rasteriza pertencimento, RoomObs, WallObs
  e footprints salvos em coordenadas 1/16 tile, com tetos para memória e trabalho.
  Entradas incompletas falham fechadas. O adaptador ainda não cobre regras
  runtime nem habilita Routing45.
- Candidatos geométricos globais de WallObs usam as fórmulas de segmentos do
  Simitone e tratam faces de porta ausentes como sólidas. O inventário não é
  mesclado por sala como no runtime; consulte a cobertura e os limites em
  `docs/room-obstacles-validation.md`.
- Controller Route45 do ODH agora filtra destinos do perfil exact-radius pela
  máscara direcional usando os setores angulares, coordenadas floor em tiles
  e rotação do alvo de `VMSlotParser.GetSearchDirection` do Simitone fixado.
  Ainda não é a validação completa de destino, nem está ligado ao Main.
- O room map preparado distingue misses estritos de `TryGetValue` dos aliases
  FLRm declarados-sem-resolução efetivamente usados. Mantém o contador legado de
  misses, considera os bytes retidos por fallback como identidade e continua
  bloqueando referências a aliases declarados ambíguos. Segue
  `VMTS1Activator.RemapFloors/BuildFloorDict` do Simitone.
- Consultas de piso preparado e sala geométrica em posições de 1/16 tile,
  incluindo a seleção distinta das metades diagonais. As respostas espaciais
  não validam placement, portais ou aceitação de destinos.
- Tradução pura da regra `VMEntity.FloorChangeValid` do Simitone, com decisões
  distintas para terreno, água, piscina, primeiro nível e entidades comuns.
  O leitor OBJD136/138 também expõe `LevelOffset`; ainda sem integração com
  Main ou aceitação de destinos de rota.
- Adaptador de placement associa flags OBJM, `LevelOffset` OBJD e piso preciso
  da arquitetura preparada para avaliar apenas `FloorChangeValid`; dados,
  coordenadas ou identidade de alias insuficientes resultam em `unsupported`,
  e o runtime continua sem usar o resultado como clearance.
- Resolução dos aliases FLRm/WALm contra catálogos de nomes explícitos e
  ordenados, com IDs dentro da política ODH, diagnósticos de misses e duplicatas.
  A descoberta de arquivos comerciais e a precedência dos providers seguem
  externas à API.
- Preparo limitado de pisos/paredes salvos com aliases explícitos, road flip,
  flags e crop; o mapa de salas pode consumir os grids preparados sem repetir
  conversões. Descoberta de recursos e portas/portais vivos seguem pendentes.
- Composição CPU de terreno, pisos, paredes e avatar Main em House07 usando o
  mesmo World restaurado, com posição viva, resolução SLOT limitada e câmera
  acompanhando o avatar.
- Objetos salvos na mesma captura Main, com Graphic/Hidden/posição vivos,
  filtro de nível/contêiner e sprites alpha/Z sobre a cena opaca. Flags
  dinâmicas e centros multitile seguem o snapshot; novos objetos e reuso de
  identidade exigem atualização do renderer.
- Seleção de objeto por clique no preview Main com lote: o título informa ID,
  GUID e `TreeTableID`. O buffer de seleção respeita alpha/profundidade.
- Preview Main descobre linhas TTAB, testa funções elegíveis em VM isolada e
  permite solicitar a primeira ação privada suportada por Space/Return; no
  House07 a ação `Sit` é elegível e pode ser enfileirada. Em execução X11
  prolongada a VM consumiu a fila e chegou ao Routing opcode45, que faultou
  como não suportado; movimento e efeito de gameplay não foram demonstrados.
- Flags TTAB com `AllowVisitors` (`0x1`) agora são avaliadas contra o perfil do
  ator; bits desconhecidos, pets e perfil ausente seguem fora do suporte.
- Prévia X11 House07 foi aberta e capturada em desktop real; clique, seleção e
  pedido via Space foram validados. Em sessão curta ficou queued; em sessão
  prolongada a VM chegou ao opcode45 e faultou antes de mover o avatar.
- Manager HIT persistente por `TrackID`, com registro explícito de controllers,
  resolução global de eventos e agregação de fontes.
- Recusas de lookup e de capacidade antes de efeitos deixam controllers
  registrados disponíveis; falhas após callbacks continuam faultando o manager.

### Validação

- Probe Portal15 no House07 da instalação: arquitetura preparada com 23 fontes
  loose explícitas; seis portas/grupos apontam para OBJf action 4105, cujo BHAV
  está presente no asset Doors.iff, e seis salas geométricas foram geradas.
  Sem execução BHAV/runtime ou clearance; nenhum recurso comercial foi incluído.
- Classificador de colisões salvas: build Linux completa; CTest passou em 141
  testes e pulou `linux_window_backend` no sandbox, que passou ao ser repetido
  com display real. Os 142 testes passaram ao considerar essa repetição. No
  House07, 91/182 entidades geraram caixas candidatas, 69 foram ausentes e 22
  ficaram unsupported (8 sem posição, 2 avatares com `KillTimeout` incerto e
  12 portas com estado runtime desconhecido). As caixas ainda não são cruzadas
  com obstáculos de sala e não validam clearance.
- Candidatos room-obstacle: fixture sintética confirma abertura de tile Portal15
  e merge de retângulos igual ao perfil da referência. No House07, 24 pares
  tile/sala deram 8/7/8/6/0 retângulos para salas 1–5; com inventário completo,
  todos os candidatos têm `generation_complete=true`, sem declarar clearance
  nem uso pelo Routing45.
- Inventário de portais: 27 checks sintéticos cobrem abertura por sala-alvo,
  posição ausente/fora do lote, sala ausente, IDs duplicados e sobreposição de
  grupos. House07 mantém 24 pares, zero tiles sobrepostos e inventário completo;
  isso valida somente a montagem estática do snapshot.
- Auditoria estática ampliada: 25 checks focais cobrem as quatro faixas externas
  de `AttemptWalk`, limites inválidos e toque estrito, além das interseções com
  RoomObs. No House07, 50 caixas não cruzam RoomObs, 41 cruzam RoomObs, zero
  cruzam as faixas externas, 22 permanecem unsupported e 69 not_applicable.
  Ainda faltam `wallObs`, políticas dinâmicas, EntryPoint 5 e pathfinding.
- Consultas geométricas: build Linux e 139/139 CTests passaram; teste X11
  `linux_window_backend` foi pulado por ausência de DISPLAY. As consultas
  validam ambos os IDs packed da diagonal, bounds e ownership; isso é apenas
  geometria preparada, sem integração de roteamento ou prova de gameplay.
- Resolver FLRm/WALm: testes sintéticos de nomes, precedência, duplicatas,
  misses, ASCII suportado, imutabilidade e limites. Catálogo de instalação e
  equivalência com o jogo original não são afirmados.
- Completude de identidade do piso: testes cobrem fallback global com mapa vazio,
  alias declarado-sem-resolução usado/não usado e duplicatas com resultado final;
  probe House07 mantém 115 misses estritos, mas zero referências ambíguas, com
  candidatos geométricos completos quando o inventário de portais está completo.
  Clearance de rota permanece falso.
- Preparo de arquitetura: 88 checks sintéticos passaram; build Debug e suíte
  de 133 CTests sem falhas, com janela X11 pulada por ausência de DISPLAY.
  Catálogo externo, portas/portais e runtime do lote continuam pendentes.
- Picking: composição, renderer de objetos/lote e CLIs passaram em cinco CTests;
  o sexto gate de eventos X11 foi pulado sem DISPLAY. A janela ainda exige
  validação visual e de entrada em uma sessão gráfica ativa. Build e CTest
  completos também passaram (127 configurados, X11 pulado, zero falhas).
- Cinco CTests focais passaram para a integração de objetos: oclusão diante e
  atrás da parede, alpha, estado vivo, contenção/nível, remoção/reuso de ID e
  recusa de cutaway incompleto. Fixtures sintéticas, sem prova de gameplay.
- Build Linux e CTest: 127/127 aprovados; X11 foi pulado sem DISPLAY. O probe
  local House07 compôs Main, lote e 135 objetos em 166 camadas; não prova
  gameplay nem equivalência visual ao original.
- Debug e ASan/UBSan: 38/38 casos do manager e 98 checks por configuração.
  Cinco CTests companheiros por configuração foram reutilizados, sem rerun.
- Fixtures sintéticas não comprovam integração World, áudio físico, fidelidade
  completa ou uma build Linux jogável.
- Route45: `odh-routing-tests` passa 51 checks, incluindo a recusa de um
  destino caminável que não corresponde à máscara direcional do SLOT. Validação
  em mundo construído e na revisão Simitone fixada; sem movimento real,
  integração ao Main ou prova de compatibilidade com o jogo original.
