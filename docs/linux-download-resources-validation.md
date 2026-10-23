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
- A execução terminou com código 3 porque atingiu o limite de 256 ticks do
  modo de quadro único. O JSON mostrou `status=waiting`, `host_failed=false`,
  sem diagnóstico de falha de VM. Portanto, o quadro é evidência de composição
  com os dados reais, mas não valida uma sessão jogável ou interação completa.
- `git diff --check` passou.

## Limites

O plano e a composição agora aceitam Downloads da instalação Legacy. Ainda
faltam um ciclo de jogo contínuo sem esgotar o orçamento, controles de jogador,
interações completas, gravação e restauração de partida e validação visual da
janela interativa. Nenhum comportamento foi verificado em hardware portátil.
