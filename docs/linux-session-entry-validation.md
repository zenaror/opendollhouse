# Entrada integrada da sessão Linux

## O que mudou

`odh-person --game-root DIR --mode N` agora tem um caminho integrado de entrada.
O launcher `tools/play_linux.py` encaminha para esse mesmo caminho; não inicia
um seletor e um runtime de lote independentes nem reconstrói os argumentos de
recursos por fora do host integrado.
Para retomar uma sessão, o launcher aceita `--load-session PATH`; modo, idioma,
família, lote e morador são conferidos com o arquivo salvo. Nesse caso `--mode`
pode ser omitido. `--plan-only` requer `--house` e não combina com retomada. Por padrão, o
launcher usa `build/linux-checkpoint/odh-person`; outra pasta pode ser indicada
com `--binary-dir`.
Sem `--house`, prepara e mostra o seletor da vizinhança; ao escolher uma casa,
carrega a família, abre a escolha do morador na mesma janela e passa essa janela
Linux para o runtime do lote. Quando o preview do lote fecha normalmente, o
seletor da vizinhança abre novamente na mesma janela; Escape no mapa encerra o
launcher. Com `--house N`, pula o seletor do mapa e encerra depois que o lote
fecha.
`--resident-id N` escolhe o morador pela identificação usada na vizinhança sem
mostrar a tela. Sem essa opção, use Tab para percorrer e Enter para continuar;
Escape cancela a entrada. Com a sobreposição de texto opcional, a lista informa
ID de vizinhança e arquivo do personagem e aceita clique na linha. Sem ela, a
janela mostra uma barra para a posição selecionada e o título atualiza ID e
arquivo. Esta etapa ainda não decodifica nomes localizados. A janela é
redimensionada para o preview do lote e conserva sua identidade X11.
`--plan-only` continua sem UI e usa o primeiro morador, a menos que
`--resident-id` seja informado. Na primeira execução gráfica, se ainda não há
configuração e `--language` não foi informado, o launcher abre um seletor com
os 20 idiomas do jogo antes da vizinhança. Tab/Shift+Tab percorrem a lista,
Enter confirma e Esc cancela; quando Pango está disponível, também é possível
clicar numa linha. Sem Pango, o título da janela mostra o idioma atual enquanto
o jogador percorre a lista pelo teclado. `--language` permite escolher
diretamente pela linha de comando. Depois de carregar família e lote, a pasta
de dados e o idioma são lembrados antes de iniciar o runtime, em
`XDG_CONFIG_HOME/opendollhouse/config` (ou `~/.config/opendollhouse/config`).
`--plan-only` imprime o plano em JSON sem abrir janela, exige `--house` e não
grava configuração.

O runtime recebe recursos de uma única instalação: arquivos base de
`GameData`, personagens do `UserData/Characters`,
`ExpansionShared/ExpansionShared.far` e recursos de chão/parede limitados a
`GameData`. As posições vêm do grupo inglês de `LotLocations`: nas amostras dos
outros 19 grupos, a primeira entrada é opaca e não contém uma coordenada válida.
Isso não altera o idioma enviado ao runtime para os textos. Modo 6 é recusado
porque o carregador de vizinhança não o suporta.

## Verificações feitas

- Build Debug completa e build de `odh-person` com `-DODH_X11=OFF`.
- No checkpoint anterior, CTest completo: 179/179 passaram; o backend de janela
  foi pulado porque aquela sessão não tinha X11 acessível. Os sete gates focados
  da entrada também passaram.
- Build `odh-person` passou com X11 habilitado e com `-DODH_X11=OFF`; no build
  sem X11, `--plan-only` continua resolvendo os dados sem abrir janela.
- `--plan-only` com House07, na instalação original e na Legacy Collection,
  resolveu a família de dois moradores e manteve os 38 caminhos listados dentro
  da instalação indicada. Os moradores 47 e 48 resolveram respectivamente os
  GUIDs 844195932 e 850010198.
- Modo 6 foi recusado com a mensagem de uso, sem abrir janela.
- A configuração sintética passou 10 verificações de seleção XDG, idioma 1–20,
  caminho de dados, permissões privadas, substituição atômica e fallback para
  `~/.config`.
- A seleção estrutural de House07 passou para idioma de runtime 15 em fixture
  sintética e para todos os códigos 1–20 nas instalações original e Legacy.
  Os caminhos foram mantidos dentro da instalação selecionada.
- O novo `linux_game_entry_cli` passa em 21 checks sintéticos: resolve uma
  família/lote sem abrir janela, mantém todos os recursos dentro da instalação,
  permite substituir configuração inválida com opções explícitas e rejeita um
  arquivo que aponta por link para fora da pasta do jogo. O modo `--plan-only`
  deixa a configuração do usuário intacta.
- `git diff --check` passou.

Na validação X11 de 2026-10-24, PangoCairo 1.52.1 foi habilitado no build e os
testes `linux_text_renderer`, `linux_game_entry_cli`, `play_linux_cli`,
`neighborhood_window_cli` e `person_cli` passaram (5/5). Capturas sem automação
de mouse mostram a vizinhança modo 1 e House07 com conteúdo visível. O teste de
ciclo do launcher usa processos sintéticos e confirmou duas entradas em lotes,
duas reaberturas do fluxo e saída limpa ao cancelar no mapa.

## Limite desta evidência

As capturas X11 comprovam apresentação de modo 1 e House07 em janelas 800x600
num desktop 1600x1200, mas não validam todos os modos, menus, comandos ao vivo,
edição de famílias ou equivalência jogável. O teste sintético confirma o retorno
do fluxo do launcher quando o processo do lote termina normalmente; não comprova
que a família ou o progresso sejam salvos. Esta etapa não edita nem exclui
famílias e não salva progresso ao fechar o lote.

A tecla S grava uma sessão experimental em
`$XDG_DATA_HOME/opendollhouse/saves` (ou `~/.local/share/opendollhouse/saves`).
O título da janela e o log de sessão mostram o caminho; o launcher pode reabri-lo
com `--load-session PATH`. O carregador compara a família e os recursos do jogo
antes de retomar.
