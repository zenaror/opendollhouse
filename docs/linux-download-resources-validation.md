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
- Para repetir usando uma build local e a instalação do jogo:

  ```sh
  python3 tests/house07_interaction_integration.py \
    --binary build/odh-person --game-root "$GAME_ROOT" --ticks 512 \
    --include-installed-resources
  ```
- O teste anterior de quadro único chegou ao teto de 256 ticks. O JSON indicou
  `status=waiting`, `host_failed=false` e diagnóstico vazio. O código de saída
  informa que esse quadro não prova uma sessão contínua.
- `git diff --check` passou.

## Limites

O plano e o Main do lote agora aceitam os dados Downloads da instalação Legacy,
e a sequência Sentar/Levantar foi validada por agendamento de teste dentro do
runtime real. Ainda faltam uma sessão interativa contínua, controles de jogador
visuais, interação iniciada por clique na janela, gravação e recarga de uma
partida ODH e validação das outras cidades/expansões. Nenhum comportamento foi
verificado em hardware portátil.
