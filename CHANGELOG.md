# Changelog

Mudanças relevantes do OpenDollhouse, em português e com os limites de
validação indicados por versão.

## Não lançado

### Adicionado

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
