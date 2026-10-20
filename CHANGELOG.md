# Changelog

Mudanças relevantes do OpenDollhouse, em português e com os limites de
validação indicados por versão.

## Não lançado

### Adicionado

- Composição CPU de terreno, pisos, paredes e avatar Main em House07 usando o
  mesmo World restaurado, com posição viva, resolução SLOT limitada e câmera
  acompanhando o avatar.
- Manager HIT persistente por `TrackID`, com registro explícito de controllers,
  resolução global de eventos e agregação de fontes.
- Recusas de lookup e de capacidade antes de efeitos deixam controllers
  registrados disponíveis; falhas após callbacks continuam faultando o manager.

### Validação

- Build Linux e CTest: 127/127 aprovados; X11 foi pulado sem DISPLAY. Um probe
  local House07 compôs o Main com o lote, sem objetos/outros avatares; não prova
  gameplay nem equivalência visual ao original.
- Debug e ASan/UBSan: 38/38 casos do manager e 98 checks por configuração.
  Cinco CTests companheiros por configuração foram reutilizados, sem rerun.
- Fixtures sintéticas não comprovam integração World, áudio físico, fidelidade
  completa ou uma build Linux jogável.
