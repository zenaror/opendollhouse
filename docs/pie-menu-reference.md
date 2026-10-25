# Menu de ações da interface final

## Direção visual

O ODH agora organiza e mostra as ações elegíveis em botões ao redor do objeto
clicado, com categorias e retorno ao nível anterior. Essa primeira versão usa
retângulos e texto genéricos; moldura, fundo, cabeça do Sim e outros detalhes da
arte final ainda faltam. A meta continua sendo recuperar o menu circular do
The Sims 1 com nomes e regras dos dados instalados pelo jogador. O Simitone é a
referência técnica; ainda é preciso comparar a aparência com a instalação
original.

Uma revisão separada do Simitone encontrou que seu fundo e seus botões são
desenhados proceduralmente. Na instalação local original, os arquivos de tabela
de recursos apontam para imagens próprias do menu; elas existem no `UIGraphics.far`
selecionado. Isso oferece uma rota para reproduzir a apresentação com dados do
jogador, sem colocar imagens comerciais no código ou no Git. A janela de teste
agora carrega `PieBack.bmp` e `PieButt.bmp` diretamente do `UIGraphics.far` do
jogador. O fundo usa magenta como transparência, e o botão aparece junto do
rótulo. A captura `house07-fridge-menu-assets.png` em
`reports/pie-menu-window/` registra esse primeiro uso visual.

Isso ainda não é a interface final: os rótulos usam caixas genéricas, o HUD é
de diagnóstico e a posição/composição ainda não foi comparada com a instalação
original em execução. `PieFace1.bmp` foi encontrado na tabela, mas seus pixels
locais são uniformemente magenta; por isso não é desenhado nem tratado como arte
visível confirmada.

## Caminho observado no Simitone

1. Um clique no lote procura o objeto sob o cursor. Se não houver objeto, o
   Simitone pode abrir o menu para o chão caminhável. O objeto escolhido é
   normalizado para o líder do grupo de objetos; objetos desativados mostram
   uma explicação em vez do menu
   (`UILotControl.ShowPieMenu`, revisão Simitone indicada em
   [references.md](references.md)).
2. `VMEntity.GetPieMenu` percorre as ações da TTAB do objeto e, quando pedido,
   as ações globais. Para cada ação, `GetAction` localiza as rotinas de ação e
   teste. A lista só recebe ações cujo teste passou. Regras de idade, visitante
   e animal também podem remover uma ação antes de ela aparecer
   (`VMThread.CheckTS1Action` e `VMEntity.GetPieMenu`, no submódulo FreeSO da
   revisão Simitone).
3. O texto padrão vem da tabela de nomes associada à TTAB. Um BHAV pode trocar o
   nome enquanto o menu é calculado usando `ChangeActionString`; esse texto vem
   de uma tabela STR do escopo do objeto, semiglobal ou global. Assim, o rótulo
   não deve ser substituído por nomes escritos diretamente no código.
4. `UIPieMenu` agrupa nomes que contêm `/` em categorias. Até quatro itens são
   distribuídos em quatro posições; até dois usam duas; menus maiores usam oito
   posições e colocam itens adicionais acima/abaixo. Selecionar uma categoria
   abre seus filhos; selecionar uma ação envia à VM o índice, argumento e
   indicação global da ação.
5. O fundo circular, os botões com texto, o destaque de seleção e a cabeça do
   Sim no centro compõem a apresentação do Simitone. São detalhes de aparência
   observados na referência; a primeira versão ODH deve priorizar nomes corretos,
   categorias, seleção, fechamento e limites da tela antes de efeitos visuais.

O cálculo ODH agora usa o raio de 60 pixels e a mesma ordem de posições de
`UIPieMenu.RenderMenu` para as oito primeiras opções. Os itens excedentes
alternam acima e abaixo do centro, como na referência. O centro continua sendo
limitado ao quadro e cada botão é ajustado às bordas; o teste cobre os quatro
pontos cardeais, dez opções e uma âncora no canto de um quadro 320×240. Isso
aproxima a geometria, mas não confirma a aparência final dos botões, texto,
animação, cabeça do Sim ou composição no jogo original.

## Limite atual do ODH

O leitor ODH de TTAB já preserva índice do rótulo, flags e rotinas de ação/teste,
inclusive tabelas comprimidas da versão 9. `make_pie_menu_model` cria categorias
por `/`, conserva a linha TTAB e o idioma do rótulo, e `layout_pie_menu` coloca
as escolhas ao redor do ponto selecionado. A janela aceita clique, Tab e
Space/Enter; Backspace e o botão central voltam um nível. Isso é uma primeira
fatia de gameplay, não a arte final nem a elegibilidade completa.

No fluxo de House07 com `--window-input`, o clique na cadeira128 abriu a opção
`Sit`, executou a ação até `returned_true` e deixou o avatar105 anexado ao
slot0. O teste usa eventos de janela sem display; o Main continua esperando
quando o limite de 512 ticks termina. A geladeira112 conclui `Have A Snack` pelo
teste headless de interação direta, mas não estava visível no enquadramento
inicial do teste de clique. Ainda falta provar em um objeto real o clique em
categoria e o retorno, e capturar/comparar a apresentação visual em uma janela.

Uma inspeção controlada posterior identificou a geladeira 112 da House07
(GUID `1734088879`), ligada a `Objects.far:Fridges.iff`, TTAB128. A tabela tem
nove linhas localizadas, incluindo `Have A Snack`, `Have A Meal`, `Serve Meal`
e `Cast.../Enchant`. Com o morador e estado salvos usados neste teste, apenas
`Have A Snack` passou pela checagem atual; três testes ainda precisam de rotina
semiglobal 363, quatro ações de depuração retornaram falso e a ação de magia
precisa de leitura global não suportada. Isso mostra que uma tabela grande não
significa que todas as opções devem aparecer ao mesmo tempo.

Ao escolher `Have A Snack`, a execução headless de House07 agora enfileira a ação
Food4170/4175 e termina com `returned_true`, sem falha do host e com a fila vazia.
O avatar chega até (377,456), volta à animação de descanso, e o objeto Food é
removido conforme o ramo executado no BHAV. O fluxo inclui orçamento de família,
criação auxiliar OutOfWorld, `Refresh` de área sem cômodo e comparação de
atributos; mudanças de saldo fazem parte da cópia ODH e persistem no save de
sessão HOST v3/v4 sem alterar os arquivos originais do jogo.

O teste foi headless e parou no limite de 1.024 ticks enquanto a Main aguardava
outra ação. Isso valida a sequência da interação selecionada, mas não a
renderização/UI durante o movimento, nem todas as opções da geladeira, outros
objetos ou paridade completa com o jogo/Simitone. A criação auxiliar de uma
posição diferente continua explicitamente fora do perfil desta execução.

## Recursos visuais originais identificados

Os arquivos de texto de recurso locais ligam estes IDs às imagens do menu:

| ID | Símbolo | Caminho na tabela | Entrada real do FAR | Dimensões / formato |
|---:|---|---|---|---|
| 28 | `kSMSystemPieButton` | `shared/sys/PieButt.bmp` | `Shared\Sys\PieButt.bmp` | 17×17, BMP 24-bit |
| 29 | `kSMSystemPieBackground` | `shared/sys/PieBack.bmp` | `Shared\Sys\PieBack.bmp` | 195×195, BMP 24-bit |
| 800 | `kPieFaceBkg` | `CPanel/PieFace1.bmp` | `cpanel\pieFace1.bmp` | 201×201, BMP 24-bit |

As associações vieram de `SMCtrlMgrRes.h/.RT` e `Res_CPanel.h/.RT`; as
dimensões foram lidas pelo decodificador BMP do ODH sem exportar os pixels. O
SHA256 de `UIGraphics.far` é
`109d8234cf5d65a1af82593d85b4e62c62302fde574a67b8b8f66c3462b14239`;
`SMCtrlMgrRes.RT` é `045e57e3c502cf4e7f76606c754b95422dbf6d6b6b0784202783f8ed4da3d81b`
e `Res_CPanel.RT` é
`413c56a8d216461c537fef69e3e9a21753b9be3664e71e9844d697c3b085e034`. A imagem
de rosto central e o uso de cada recurso ainda precisam ser comparados com uma
captura do jogo original antes de definir sua composição.

## Captura da janela ODH

Depois de ajustar o raio para 60 px, `odh-person --saved-world-main
--render-window` abriu House07 em Xvfb 800×600; um clique na geladeira exibiu
`Have A Snack`. A captura
`reports/pie-menu-window/house07-fridge-menu-assets-radius60.png` (SHA256
`adfcabfa525f40d2b5d1f08de81eb33cfb0ccf75b322aa25ab6431ca2ceb75de`) mostra
PieBack/PieButt carregados do `UIGraphics.far` externo. Ela não demonstra
paridade visual; o launcher integrado ainda não chegou ao lote na tentativa
isolada registrada em [current-handoff.md](current-handoff.md). O wrapper
`xvfb-run` encerrou a tela ao final.

Uma janela X11 isolada com os dados House07 abriu a casa e o HUD de diagnóstico.
Clique real pela janela na geladeira mostrou `Have A Snack`; clicar na opção
fez o avatar se mover até a geladeira e o programa permaneceu sem falha. O
resultado final da interação não foi confirmado nesse percurso visual; a
conclusão `returned_true` continua provada apenas pelo teste headless separado.
O comando de captura precisou receber os arquivos locais `.flr` e `.wll` como
`--architecture-resource`; sem eles, a preparação de rota falhou em
`prepared_architecture_unavailable`. As duas capturas e seus hashes ficam em
`reports/pie-menu-window/`, fora do Git. O Xvfb foi encerrado após cada execução.

## Primeira reconstrução proposta

- Separar a descoberta de ações da tela: cada opção deve carregar texto já
  resolvido, ação, argumento, origem local/global e categoria. A interface
  apenas mostra as opções recebidas e devolve a opção escolhida.
- Confirmar pela execução original que os três IDs acima são o fundo, o botão e
  o centro do menu de lote nesta edição. Depois implementar carregamento local
  dessas imagens e comparar alinhamento/escala com o original.
- Testar a navegação de categorias em uma ação real elegível e comparar o mesmo
  objeto/idioma com o Simitone e o original. O menu visual de House07 ainda tem
  uma ação elegível, então não prova categorias ou voltar.
- Manter o texto ligado ao idioma selecionado e testar rótulos compridos,
  categorias vazias, menus com 1, 2, 4, 5, 8 e mais de 8 opções, e cliques perto
  das bordas. A expansão visual não pode esconder a última opção fora da tela.
- Gate antes de chamar de menu de jogo: um objeto real com diversas opções em
  pelo menos dois níveis de categoria, uma ação local e uma global identificadas
  pelos recursos, e a ação escolhida chegando à interação já validada na VM.
  Depois comparar o mesmo objeto e idioma com o Simitone e com a edição original
  instalada. Um menu de teste com rótulos inventados não passa esse gate.
- Antes de usar a geladeira como gate de interação, completar o destino e a rota
  de `GotoRelativePosition` da ação `Have A Snack`, ligar um gravador persistente
  para mudanças de orçamento e completar os donos de código ausentes das outras
  ações suportáveis. Reavaliar cada linha com regras e estado da família; nunca
  exibir ações apenas porque aparecem na TTAB.

## Fontes consultadas

- Simitone Client `UILotControl.cs`, `UIPieMenu.cs` e
  `UIInteractionQueue.cs`, revisão `4906db3156616bfaffc10ce0963cf08062975fef`.
- FreeSO submódulo do Simitone `VMEntity.cs`, `VMThread.cs` e
  `VMChangeActionString.cs`, revisão `f86bbd34b2112296ed786741f640a0535886d180`.
- ODH `include/odh/formats.hpp` e `src/ttab.cpp` para o limite do leitor atual.

As fontes completas, licenças e identificação das revisões estão em
[references.md](references.md). Comportamentos observados no código de terceiros
são pistas de implementação; a comparação com o jogo original continua
necessária para confirmar fidelidade.
