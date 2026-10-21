# Changelog

Mudanças relevantes do OpenDollhouse, em português e com os limites de
validação indicados por versão.

## Não lançado

### Adicionado

- Probe offline de portas em saves: liga faces de porta, grupos multitile,
  salas geométricas e ação authored da entrada Portal 15 via OBJD/OBJf; o
  reporte deixa explícito que a BHAV e o clearance de rota não foram executados.
- Consultas de piso preparado e sala geométrica em posições de 1/16 tile,
  incluindo a seleção distinta das metades diagonais. As respostas espaciais
  não validam placement, portais ou aceitação de destinos.
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
- Seleção de objeto por clique no preview Main com lote: o título informa ID e
  GUID. O buffer de seleção respeita alpha/profundidade; selecionar ainda não
  executa interações TTAB ou altera o World.
- A identidade OBJD agora preserva `TreeTableID` e o preview/JSON o mostra para
  o objeto selecionado, preparando a resolução explícita da TTAB sem alegar
  elegibilidade.
- Manager HIT persistente por `TrackID`, com registro explícito de controllers,
  resolução global de eventos e agregação de fontes.
- Recusas de lookup e de capacidade antes de efeitos deixam controllers
  registrados disponíveis; falhas após callbacks continuam faultando o manager.

### Validação

- Probe Portal15 no House07 da instalação: arquitetura preparada com 23 fontes
  loose explícitas; seis portas/grupos apontam para OBJf action 4105, cujo BHAV
  está presente no asset Doors.iff, e seis salas geométricas foram geradas.
  Sem execução BHAV/runtime ou clearance; nenhum recurso comercial foi incluído.
- Consultas geométricas: build Linux e 139/139 CTests passaram; teste X11
  `linux_window_backend` foi pulado por ausência de DISPLAY. As consultas
  validam ambos os IDs packed da diagonal, bounds e ownership; isso é apenas
  geometria preparada, sem integração de roteamento ou prova de gameplay.
- Resolver FLRm/WALm: testes sintéticos de nomes, precedência, duplicatas,
  misses, ASCII suportado, imutabilidade e limites. Catálogo de instalação e
  equivalência com o jogo original não são afirmados.
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
