# Changelog

Mudanças relevantes do OpenDollhouse, em português e com os limites de
validação indicados por versão.

## Não lançado

### Adicionado

- Manager HIT persistente por `TrackID`, com registro explícito de controllers,
  resolução global de eventos e agregação de fontes.
- Recusas de lookup e de capacidade antes de efeitos deixam controllers
  registrados disponíveis; falhas após callbacks continuam faultando o manager.

### Validação

- Debug e ASan/UBSan: 38/38 casos do manager e 98 checks por configuração.
  Cinco CTests companheiros por configuração foram reutilizados, sem rerun.
- Fixtures sintéticas não comprovam integração World, áudio físico, fidelidade
  completa ou uma build Linux jogável.
