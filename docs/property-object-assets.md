# Soma dos bens da casa

`inspect_property_object_assets` é uma inspeção em C++ dos dois grupos de
valor usados pela rotina `00444cf9` da imagem de comparação. Lê o World atual,
sem executar ações, apagar objetos, alterar dinheiro ou publicar saves.
A identidade da imagem e a análise dos valores individuais estão em
[valores dos objetos](property-values.md).

## Regras de soma

Cada objeto simples ou conjunto completo de peças é contado uma única vez,
pelo seu líder. A classificação usa o tipo da definição OBJD atual, não o tipo
salvo na tabela OBJT nem a classificação genérica do motor.

1. Excluir conjuntos cujo líder tenha tipo OBJD7, antes de consultar preços.
2. Se o líder estiver na área aceita pelo original e satisfizer o predicado
   de saída dos objetos, somar seu valor ao grupo principal.
3. Nos demais casos, consultar a categoria efetiva de construção. Categorias
   zero e quatro não entram na soma; outras entram no grupo de objetos de
   construção.

O valor vem da palavra atual ObjectData41 do líder. Conserto não zero em
ObjectData15 de qualquer peça reduz esse valor à metade, descartando a fração
em direção a zero. As flags do líder escolhem o grupo; as das peças secundárias
não criam contribuições independentes. A retenção pode levar à segunda regra
de categoria, sem transformar automaticamente todo objeto retido em patrimônio.

O teste espacial `00422665` considera as próprias coordenadas do líder,
convertidas em posições de ladrilho e depois em bytes com sinal. Aceita somente
o interior de uma casa de dimensão 1–64, excluindo a borda; não consulta o
andar. Não usa a posição do recipiente como substituta. A conversão preserva
inclusive o comportamento de números negativos e grandes observado na rotina.

## Inventário completo

O chamador fornece uma partição completa do World, incluindo objetos simples.
A inspeção verifica IDs positivos, líderes, peças repetidas, sobreposição,
cobertura de todas as entidades e identidade dos recursos/definições principais,
com limite de 4096 entidades. A autoridade sobre quais peças pertencem a cada
objeto continua sendo do chamador; identidades iguais não descobrem sozinhas
uma peça omitida e apresentada como outro objeto.

As palavras atuais do World prevalecem sobre bancos salvos antigos. Uma
classificação não resolvida deixa **os dois totais ausentes**, evitando publicar
uma soma parcial como se fosse o patrimônio completo. As linhas individuais
explicam a falha, sem autorizar remoção ou reembolso.

## Uso

```sh
cmake --build build/native --target odh-saved-world
./build/native/odh-saved-world "$GAME_ROOT/UserData/Houses/House07.iff" \
  --object-definitions "$GAME_ROOT/GameData/Objects/Objects.far" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00000.iff" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00001.iff" \
  --property-assets
```

Forneça também os recursos necessários às expansões e aos personagens da casa.
A ferramenta deriva a dimensão da arquitetura salva e usa os grupos validados
da importação inativa. Ela não afirma inspecionar uma partida em execução.
Sem a opção, a saída anterior permanece igual. Com ela, uma inspeção não
resolvida retorna saída3 e totais JSON `null`.

Casas maiores podem exigir `--import-word-budget 262144`. A opção aceita
1–1048576 palavras lógicas e aplica o limite tanto ao estado conservado da
importação quanto ao World. Não muda os limites padrão do motor ou das outras
plataformas. Palavras lógicas não são uma medida de memória física utilizada.

## Arredondamento separado

`comparison_pc24_truncate_scale` implementa um perfil explícito: inteiro de
entrada exato, multiplicação pelo número binary32 com 24 bits de precisão e
empate para o par, seguida de descarte da fração. A implementação usa somente
inteiros, sem depender do modo de cálculo do computador ou alterá-lo.
Por exemplo, 10 multiplicado pelo valor binary32 próximo de 0,7 arredonda para
7 nesse perfil; converter uma conta intermediária mais precisa diretamente em
inteiro poderia produzir 6. Inteiros grandes também não podem ser convertidos
prematuramente em float32 antes da multiplicação.

A configuração desse perfil foi encontrada no construtor e em caminhos do
controlador do aplicativo analisado. Isso não prova a configuração em toda
chamada de avaliação ou em outra thread. Portanto, o chamador ainda precisa
comprovar esse contexto antes de usar o perfil para publicar dinheiro.
Multiplicadores não finitos e resultados fora de 32 bits são recusados.

Essa rotina não escolhe o multiplicador, não substitui silenciosamente o perfil
Simitone de `Simi::purchase_value` e não aplica o reembolso à família.
A construção, o valor base do terreno, os dois grupos de objetos e a publicação
conjunta da casa e do dinheiro continuam etapas distintas.

## Validação desta etapa

Os testes `property_object_assets` e `property_value_scale` passaram. A revisão
independente não encontrou defeitos concretos no alcance dessas rotinas.
Uma comparação isolada com instruções x87, configuradas explicitamente para
esse perfil, conferiu 200 mil casos do arredondamento sem divergências.
Esse teste não executa o jogo original e não altera o modo de cálculo do
processo que está usando o motor.

A inspeção dos objetos nas Casas01, 07, 21 e 91 resolveu, respectivamente,
23, 131, 530 e 102 conjuntos. Na Casa21 foi usado o limite explícito de 262144
palavras lógicas. Em todas, remover o campo opcional do diagnóstico deixou o
JSON idêntico ao da importação normal. Os 103 arquivos de entrada selecionados
conservaram seus SHA-256.

Esses resultados não demonstram despejo completo, remoção segura de referências,
valor final de revenda ou publicação conjunta de propriedade e dinheiro.

A [avaliação completa](property-valuation.md) combina esses grupos com terreno
e construção sob um perfil explícito, ainda sem efetuar a saída da família.
