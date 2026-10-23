# Recursos Downloads no plano de lote Linux

## O que mudou

O plano Linux agora inclui FARs e IFFs opcionais encontrados sob `Downloads/`
na identidade da sessão e os entrega aos leitores de definições; os FARs também
entram no catálogo de animações. A pasta pode estar ausente. Os limites de
fontes desses leitores passaram a 128 para acomodar a instalação Legacy medida
sem remover seus limites de caminho, nomes e índices.

## Verificações feitas

- CTest completo: 195 testes passaram e `linux_window_backend` foi pulado por
  falta de servidor gráfico (196 cadastrados).
- `linux_game_entry_cli` cobre arquivos FAR e IFF sintéticos em `Downloads/`,
  fingerprint dos arquivos, argumentos dos dois leitores, expansão opcional
  ausente e instalação sem a pasta `Downloads/`.
- `person_animation_cli` e `person_architecture_cli` verificam o limite de 128
  fontes sintéticas.
- `odh-person --plan-only` com House07 da instalação Legacy resolveu dois
  moradores e listou 132 arquivos, incluindo os 70 FARs de Downloads e os sete
  arquivos de expansão; `expansion_mask` foi `127`.
- O leitor de lotes com 79 fontes FAR (base, ExpansionShared, sete expansões e
  70 Downloads) importou House81: 327 entidades e 5.188 GUIDs no catálogo. A
  fronteira sintética de 128 fontes passou; 129 foi recusada por
  `saved_scene_cli`.
- House81 agora tem quadro estático completo de 640x400: pisos, paredes e
  objetos foram desenhados (`floors_rendered=true`, `walls_rendered=true`,
  273 camadas de objetos). O catálogo usou os mesmos 79 FARs de definição e
  106 fontes de arquitetura (arquivos `.flr`/`.wll` soltos e FARs de expansões
  e Downloads); os 7 aliases de piso e 8 de parede ficaram resolvidos. Cache:174
  frames,113.760 pixels/682.560 bytes RGBA/índice/Z. RSS máxima neste PC:
  16.232 KiB. `architecture_loaded=false` indica que isso é um retrato, não uma
  arquitetura carregada para simulação; ainda não é visita jogável nem medida de
  outro aparelho. PPM: `/tmp/odh-house81-full-97aee44.ppm`.
- O runtime leu esses recursos e escreveu um quadro PPM de 320×240 de House07,
  com o Sim, arquitetura e objetos visíveis. A medição deste computador foi
  30.752 KiB de RSS máxima para essa execução; ela não representa memória em
  outro aparelho.
- `tests/house07_interaction_integration.py --include-installed-resources`
  executou com os dados Legacy: carregar família e lote, agendar Sentar no
  objeto 128, retornar sucesso, depois Levantar no tick 270 e retornar sucesso.
  O ator terminou sem postura sentada nem vínculo ao objeto; 182 registros de
  restauração do lote ficaram consistentes. O script validou a interação apesar
  de o processo relatar código 3 ao atingir o limite deliberado de 512 ticks
  com Main ainda esperando. O código 3 marca a sessão limitada incompleta; não
  foi uma falha da interação.
- O mesmo teste, com `--window-input --window-stand`, percorreu o caminho de
  clique, seleção do objeto, Space e a tecla U sem uma tela física; Sentar e
  Levantar retornaram sucesso. Com `--window-neighborhood --neighborhood-house 7`,
  a seleção M suspendeu e retomou o mesmo lote: tick 260, VM, World, relações,
  postura sentada e vínculo ao objeto ficaram idênticos antes/depois.
- Para repetir usando uma build local e a instalação do jogo:

  ```sh
  python3 tests/house07_interaction_integration.py \
    --binary build/odh-person --game-root "$GAME_ROOT" --ticks 512 \
    --include-installed-resources --window-input --window-stand \
    --window-neighborhood --neighborhood-house 7
  ```
- O teste anterior de quadro único chegou ao teto de 256 ticks. O JSON indicou
  `status=waiting`, `host_failed=false` e diagnóstico vazio. O código de saída
  informa que esse quadro não prova uma sessão contínua.
- `git diff --check` passou.

## Limites

O plano e os leitores de lotes agora aceitam os dados Downloads da instalação
Legacy, House81 foi importada e agora renderiza estáticamente pisos, paredes e
objetos quando o catálogo inclui os FARs de arquitetura das expansões e
Downloads. Com só os 23 arquivos `.flr`/`.wll` soltos, o resolver ainda apontava
7 aliases de piso e 8 de parede sem provedor; o erro agora lista quantidades e
IDs para facilitar diagnósticos futuros. Isso ainda não abre o lote público como
visita. House21, House40 e House90 param no leitor em `truncated OBJM tail
fields`. Também faltam sessão interativa contínua, confirmação visual da janela e
dos controles, troca real de lote, gravação e recarga de uma partida ODH e
validação das outras cidades/expansões. Nenhum comportamento foi verificado em
hardware portátil.
