# Mudança imediata de necessidade por BHAV — 2026-10-24

O runtime Linux reconhece o BHAV opcode 29 (`Set Motive Change`) nas formas
imediata (`Once`) e contínua. Ambas leem delta e limite pelos escopos definidos
no BHAV e aplicam a escala TS1 de energia positiva (`180 / (24 - 16) = 22`).
A forma imediata mantém o wrap signed-16 e os limites direcionais do Simitone:
um Sim já além do limite não é corrigido; uma mudança que cruza o limite para
exatamente nele.

Cada avatar guarda até 16 registros contínuos. A taxa por hora avança a cada
tick TS1 de cinco segundos, acumula frações e continua ativa mesmo ao atingir
o máximo. A decay natural é suspensa para necessidades com taxa ativa, exceto
Energy, como no Simitone. `ClearAll` apaga as taxas e as interações concluídas
também limpam as alterações do avatar.

Os registros e suas frações entram no snapshot versionado do World. O campo
transitório `Ticked` não é salvo, como no marshalling do Simitone; snapshots
v1 continuam sendo lidos como avatares sem alterações contínuas. Os bancos
originais de saves continuam separados do estado de execução.

## Referência e validação

Referência de código: Simitone/FreeSO pinado em `docs/references.md`,
`FreeSO/TSOClient/tso.simantics/VMContext.cs` (registro do opcode 29),
`Primitives/VMSetMotiveChange.cs` (formato de operandos e variante `Once`) e
`Model/VMMotiveChange.cs` (escala de delta/limite). Os recursos comerciais não
são alterados.

O teste `motive_change` executa 28 verificações sintéticas, incluindo instalação
contínua, escala, limites, primeira atualização, `ClearAll` e substituição. Os
testes `interaction_queue`, `world_snapshot_codec` e `person_cli` também passaram.
Uma leitura estrutural de `GameData/Objects/Objects.far!Beds.iff`, BHAV 4153,
encontrou instruções reais de alteração contínua para Comfort e Energy. O código
de ODH já executa essa classe de instrução, mas a rotina comercial completa ainda
não foi executada em uma sessão de lote Linux.

Isso valida contratos isolados e persistência de estado do ODH; não prova
paridade com o executável original nem que uma ação comercial completa funcione.
Próximo passo: executar uma ação real de cama ou outro objeto no lote com os
recursos comerciais, registrar a mudança observada e investigar o teste de
janela em um ambiente Xvfb isolado.
