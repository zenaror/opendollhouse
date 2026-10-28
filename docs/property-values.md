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

Ainda faltam o total completo dos bens, a avaliação da construção, a precisão
do multiplicador e a publicação conjunta da casa e do dinheiro. A inspeção
não habilita venda, despejo ou alteração do patrimônio da família.
