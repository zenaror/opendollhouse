# Inspeção do valor atual de grupos de objetos

`odh-saved-world --property-values` acrescenta `property_value` aos grupos
listados em `multitile_groups`. A saída padrão continua igual quando a opção
não é usada. A ferramenta importa a casa sem executar suas ações e não grava
objetos, dinheiro ou saves.

## Regra implementada

A função C++ `inspect_property_group_value` recebe um grupo completo validado
e lê o valor atual do líder em ObjectData41. Se qualquer peça tiver estado de
conserto não zero em ObjectData15, divide esse valor por dois, descartando a
fração em direção a zero. Usa as palavras atuais do World recebido, não a
cópia antiga dos bancos salvos. Não soma o preço de catálogo das peças.

O chamador precisa comprovar que forneceu o grupo completo. A função rejeita
IDs inválidos ou repetidos, líder ausente, identidades incompatíveis, entidades
ou palavras necessárias ausentes e mais de 4096 membros. Um grupo incompleto
permanece não resolvido, sem estimar seu valor.

## Correspondência com o estado salvo

Na mesma imagem de comparação, a rotina comum `00434353` transfere 72 palavras
de 16 bits a partir de `objeto+0x4a` no ramo de versão 11 ou superior; o auxiliar
`00464973` avança duas posições de memória por palavra. A transferência vem
após os atributos e os oito temporários. Uma palavra separada segue esse banco.
Isso explica a divisão do leitor em 68 palavras ObjectData e cinco extras:
os quatro primeiros extras completam o banco de 72; o quinto é o campo seguinte.
Essa divisão não desloca os índices usados pela inspeção.

| Índice ObjectData | Deslocamento no objeto | Uso observado |
| --- | --- | --- |
| 11 | `0x60` | ID salvo da instância |
| 15 | `0x68` | Estado consultado para desconto por conserto |
| 41 | `0x9c` | Valor atual consultado pela rotina de avaliação |
| 43 | `0xa0` | Flags consultadas pelo predicado de retenção |

A relação é `0x4a + 2 × índice`. Os ramos antigos transferem quantidades
menores e diferem também em atributos e temporários; esta constatação não
amplia automaticamente o suporte do leitor para versões antigas. Os cabeçalhos
inspecionados da Casa01/Casa07 eram versão 62 e da Casa91, versão 73.
Trata-se de evidência estática do layout, sem teste executando o original e
sem comprovação das regras completas de saída da família.

## Uso

```sh
cmake --build build/native --target odh-saved-world
./build/native/odh-saved-world "$GAME_ROOT/UserData/Houses/House07.iff" \
  --object-definitions "$GAME_ROOT/GameData/Objects/Objects.far" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00000.iff" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00001.iff" \
  --property-values
```

Forneça as fontes exatas necessárias à casa. Cada resultado declara
`inspection_only=true` e `refund_authorized=false`. A opção cobre somente os
grupos expostos pelo importador, não um inventário completo do valor da casa.
O World inspecionado é a importação inativa, não uma partida em execução.

## Evidência e limites

A regra deriva de análise estática da rotina `00432249` da imagem PE de
comparação com SHA-256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Não constitui validação executando o jogo original. As rotinas `00444cf9` e
`0041607b` separam categorias de valor e acrescentam outros componentes;
portanto esta inspeção não equivale ao reembolso ao desocupar a casa.

O teste `property_value` verifica valores positivos e negativos, frações,
conserto em peças secundárias, leitura do estado atual, identidades e limites.
`property_disposition` verifica a ordem de classificação corrigida pela análise
original. Ambos passaram. Na importação da Casa07, os 29 grupos foram
resolvidos; fora do campo opcional, a saída foi idêntica à saída padrão. Os
quatro arquivos de entrada conservaram seus hashes.

A avaliação da construção tem uma [regra própria](property-architecture-value.md),
separada desta inspeção de objetos. A leitura dos preços dos recursos está disponível como inspeção; ainda
faltam a cobertura completa dos estados de catálogo, o total dos bens, a
precisão do multiplicador e a publicação conjunta
da casa e do dinheiro. A inspeção
não habilita venda, despejo ou alteração do patrimônio da família.

## Valor base do terreno

A rotina `0046d0cf` transfere três valores de 32 bits consecutivos para os
campos de estado `+0x9c`, `+0xa0` e `+0xa4`; a atualização `0041607b` recalcula
somente os dois últimos. O primeiro componente da soma financeira é, portanto,
um valor armazenado separadamente, compatível com `Simi::lot_value`, e não o
subtotal das paredes. O ramo legado anterior à versão interna 37 usa um valor
inicial 4500; isso não autoriza fixar 4500 para casas modernas. A correspondência
com os campos SIMI existentes não substitui a validação do ciclo de carga e
salvamento nem resolve a precisão do multiplicador.

## Soma dos objetos e precisão

A [soma dos bens](property-object-assets.md) acrescenta a classificação completa
de grupos e objetos simples, consultando o World atual. Também documenta o
perfil explícito de arredondamento em inteiros; nenhum desses diagnósticos
publica dinheiro ou habilita o despejo.
