# Avaliação completa da propriedade

`inspect_property_valuation` junta os componentes nativos já inspecionados:
valor base do terreno, objetos e construção. Recalcula os dois últimos a
partir do World e da arquitetura fornecidos, sem executar ações, remover
objetos, alterar dinheiro ou gravar saves.

## Cálculo e autoridade

O perfil explícito `comparison_pc24` aplica esta ordem:

1. Somar o valor da geometria aos objetos classificados como construção.
2. Aplicar o multiplicador binary32 e o arredondamento PC24 escolhido.
3. Acrescentar o valor base do terreno e o grupo principal de objetos.

A sequência acompanha as rotinas `0041607b` e `0042730f` da imagem de comparação
identificada em [valores dos objetos](property-values.md). A função não acrescenta
o dinheiro da família. Esse valor de propriedade não autoriza uma compra,
uma venda ou um reembolso, nem transforma um lote comunitário em residência.

O chamador precisa fixar um único estado atual, seus grupos completos e a
identidade dos recursos e preços. Referências constantes não comprovam que
outra thread deixou de alterar o estado nem que os dados vieram da mesma casa.
Essa responsabilidade continua sendo da preparação da operação persistente.

O perfil é obrigatório; não existe escolha implícita do modo de cálculo do
computador. O multiplicador também é fornecido explicitamente. A composição
não muda `Simi::purchase_value`, cujo perfil Simitone continua identificado
separadamente. A prova de precisão em todos os caminhos do aplicativo original
permanece distinta da implementação desse perfil nativo.

Se qualquer componente, operação ou limite não for resolvido, os três campos
compostos — construção combinada, construção ajustada e valor da propriedade —
ficam ausentes. Não há substituição pelos valores antigos de objetos ou
construção gravados no SIMI. As razões dos componentes permanecem disponíveis
para diagnóstico.

## Uso da ferramenta

```sh
cmake --build build/native --target odh-saved-world
./build/native/odh-saved-world "$GAME_ROOT/UserData/Houses/House07.iff" \
  --object-definitions "$GAME_ROOT/GameData/Objects/Objects.far" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00000.iff" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00001.iff" \
  --property-valuation-pc24 --architecture-multiplier-bits 3f333333 \
  --build-iff "$GAME_ROOT/GameData/Build.iff" \
  --floor-globals "$GAME_ROOT/GameData/floors.iff" \
  --wall-globals "$GAME_ROOT/GameData/walls.iff"
```

`3f333333` são os bits binary32 do multiplicador próximo de 0,7 encontrado
como padrão na imagem analisada; não é uma afirmação sobre configurações
personalizadas. A opção exige exatamente oito dígitos hexadecimais. Valores
não finitos deixam a avaliação não resolvida.

Forneça os recursos adicionais das expansões com `--object-definitions`,
`--object-definition-file`, `--resource-far` e `--resource-file`, conforme
[preços da arquitetura](property-architecture-value.md). Para casas maiores,
o limite explícito de importação está descrito em
[soma dos bens](property-object-assets.md).

A ferramenta exige um único SIMI1 e usa somente seu valor base do terreno.
O World importado permanece inativo. A resolução de preços é compartilhada
com `odh-property-architecture-value`, evitando duas regras diferentes para
os mesmos arquivos. A saída anterior não muda quando a opção não é usada.

Saída0 indica resolução deste perfil; saída3 indica avaliação não resolvida,
com valores compostos JSON `null`; saída2 indica erro de formato ou recursos.
A saída sempre declara `inspection_only=true`, `refund_authorized=false` e
`original_runtime_verified=false`.

## Verificação e limites

O teste `property_valuation` passou com estados nativos reais de teste,
incluindo arredondamento, dados ausentes, limite numérico, perfil inválido e
preservação dos dados. A revisão independente não encontrou defeitos concretos
nesse alcance. A extração do leitor compartilhado preservou a saída anterior
da ferramenta de construção nas quatro casas inspecionadas.

Com os recursos selecionados e o multiplicador explícito acima:

| Casa | Valor calculado da propriedade |
| --- | ---: |
| 01 | 10500 |
| 07 | 17225 |
| 21 | 157184 |
| 91 | 14311 |

Os 131 arquivos de entrada selecionados conservaram seus SHA-256. Em cópias
privadas da Casa07, alterar somente os totais antigos de objetos/construção
não mudou o resultado; retirar ou duplicar SIMI1 foi recusado. Multiplicador
não finito e materiais ausentes produziram valores compostos ausentes.
Esses testes não executaram o jogo original.

Ainda faltam a transformação segura da casa após a saída, a limpeza de
referências e a publicação conjunta da propriedade e do dinheiro. Também
continuam pendentes os fluxos completos de compra, venda, depreciação,
salvamento e reentrada; esta inspeção não comprova que esses fluxos funcionam.
