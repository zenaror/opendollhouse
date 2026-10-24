# Menu de ações da interface final

## Direção visual

A lista retangular que hoje aparece durante os testes é provisória. A interface
final deve recuperar o menu circular de ações reconhecível do The Sims 1,
montado com os nomes e regras dos dados instalados pelo jogador. O Simitone é a
referência técnica para descobrir as ações e organizar o menu; este projeto terá
sua própria implementação e ainda precisa comparar a aparência com a instalação
original. Não há evidência aqui para afirmar que a aparência atual do Simitone é
uma cópia exata da interface original.

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

## Limite atual do ODH

O leitor ODH de TTAB já preserva índice do rótulo, flags e rotinas de ação/teste,
inclusive tabelas comprimidas da versão 9. Isso é leitura estrutural. O fluxo
comprovado de House07 ainda apresenta uma lista de depuração com uma ação (Sit),
sem categorias, ações dinâmicas ou comparação de vários idiomas. Portanto, ler
TTAB não significa que o ODH já decide corretamente quais ações aparecem.

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

## Primeira reconstrução proposta

- Separar a descoberta de ações da tela: cada opção deve carregar texto já
  resolvido, ação, argumento, origem local/global e categoria. A interface
  apenas mostra as opções recebidas e devolve a opção escolhida.
- Montar primeiro categorias e seleção navegáveis com mouse/teclado; depois
  desenhar o menu circular usando recursos de UI do jogo quando sua origem,
  formato e licença de uso local estiverem confirmados.
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
