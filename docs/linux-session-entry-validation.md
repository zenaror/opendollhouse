# Entrada integrada da sessão Linux

## O que mudou

`odh-person --game-root DIR --mode N` agora tem um caminho integrado de entrada.
Sem `--house`, prepara e mostra o seletor da vizinhança; ao escolher uma casa,
carrega a família, abre a escolha do morador na mesma janela e passa essa janela
Linux para o runtime do lote. Com `--house N`, pula o seletor do mapa.
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
- CTest completo: 179/179 passaram; `linux_window_backend` foi pulado por falta
  de servidor X11 acessível. Os sete gates focados da entrada também passaram.
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

A seleção interativa ainda não recebeu smoke visual: `DISPLAY=:0` não aceita
conexão nesta sessão. Os testes acima cobrem configuração e entrada sem janela,
não a captura de teclas na tela de primeira execução.

## Limite desta evidência

O teste `linux_window_backend` foi pulado: `DISPLAY=:0` não aceita a consulta
X11 e não há Xvfb disponível nesta sessão. Por isso, ainda não foi confirmado
visualmente que a escolha do mapa redimensiona a mesma janela e entra no lote.
O plano headless comprova resolução de dados e montagem dos argumentos, não
renderização, controles, ações ao vivo, salvamento, retorno à vizinhança ou
equivalência jogável. A seleção de morador tem cobertura lógica sem display;
interação gráfica dessa tela e continuidade jogável ainda precisam de smoke
visual. Esta etapa não edita nem exclui famílias, não salva progresso e não
implementa retorno à vizinhança.
