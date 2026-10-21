# Changelog

Mudanças relevantes do OpenDollhouse, em português e com os limites de
validação indicados por versão.

## Não lançado

### Adicionado

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
