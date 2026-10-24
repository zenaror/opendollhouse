# Mudança imediata de necessidade por BHAV — 2026-10-24

O runtime Linux agora reconhece o BHAV opcode 29 (`Set Motive Change`) na
variante `Once`. Ele lê delta e limite pelos escopos fornecidos pelo próprio
BHAV, aplica a escala TS1 de energia positiva (`180 / (24 - 16) = 22`) e a
conversão curta com wrap signed-16 do código C#. Segue as verificações
direcionais do Simitone: um Sim já além do limite não é
reparado/clampado; uma mudança que cruza o limite termina exatamente nele.
O setter do World mantém os limites TS1 `[-100, 100]` e preserva valores
importados acima de 100.

`ClearAll` retorna sucesso porque este runtime ainda não cria registros de
mudança contínua. A variante contínua retorna `unsupported` explicitamente:
ela depende de um controlador por Sim que acumule frações a cada passo e
participe do save/retomada. Flags desconhecidas, índices fora do banco, escopos
sem valor e caller sem banco de Sim também são recusados sem escrever motivo.

## Referência e validação

Referência de código: Simitone/FreeSO pinado em `docs/references.md`,
`FreeSO/TSOClient/tso.simantics/VMContext.cs` (registro do opcode 29),
`Primitives/VMSetMotiveChange.cs` (formato de operandos e variante `Once`) e
`Model/VMMotiveChange.cs` (escala de delta/limite). Os recursos comerciais não
são alterados.

O teste `motive_change` executa 17 verificações sintéticas: ganho e perda com
limite, mudança abaixo do limite, escala de energia, valor já além do limite,
`ClearAll`, variante contínua ainda não suportada e recusas de flag, índice e
entrada. `person_cli` também passou depois de ligar o opcode ao host
`odh-person`. Isso valida o código ODH nesses casos sintéticos; não prova que
uma ação de objeto comercial específica já percorra o opcode, nem paridade com
o executável original.

Próximo passo: localizar um objeto/ação TS1 que use opcode 29 `Once` e executar
essa ação com os próprios recursos comerciais em uma sessão Linux. Depois,
implementar a forma contínua com estado por avatar e persistência validada.
