# RNG compartilhado e Random Number

Checkpoint de 2026-10-13. O host `odh-person` tem um estado aleatório explícito
compartilhado entre opcode8 e o ciclo consumido por Sleep. O Init nativo chega
à rotina de traits depois de inicializar seus motivos. O algoritmo implementado
é o perfil da referência FreeSO; sua equivalência ao RNG do executável original
não foi demonstrada.

## Fonte e estado portátil

FreeSO fixado em `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2, consultado
localmente em `refs/upstream/FreeSO/TSOClient/`:

- `tso.simantics/VMContext.cs:524`: `NextRandom`, estado ulong e xorshift64*.
- `tso.simantics/Primitives/VMRandomNumber.cs:12`: leitura do range, geração,
  escrita e retorno true; linhas29–35 decodificam quatro palavras de operando.
- `tso.simantics/Primitives/VMSleep.cs:25`: `NextRandom(1)` ao completar Sleep.

`RandomState` mantém um uint64, com seed explícita, cópia e reset. Para range
não zero, aplica XOR com shifts de12,25,27 bits, multiplica por
2685821657736338717 com wrap unsigned64 e calcula módulo pelo range.
O estado salvo é o resultado dos XORs, não o produto. Range zero retorna zero
sem avançar estado. Range1 retorna zero e avança. Seed zero é absorvente,
conforme a referência; não é substituída silenciosamente por outra seed.
Não depende de `std::rand`, distribuição da biblioteca, relógio ou entropia.

## Opcode8 e publicação

Os oito bytes contêm DestinationData int16, DestinationScope uint16,
RangeData int16 e RangeScope uint16, little-endian. O range lido é convertido
para uint16, inclusive valores negativos da VM. O resultado volta a signed16.
Scopes maiores que255 são recusados, sem truncar o selector.

Leituras locais: literal7, temp8, argumento9, StackObjectID10 e local25.
Os demais scopes passam pelo `VmMemory` explícito do host. Destinos temp,
argumento e local usam os outputs validados da VM; destinos externos usam
o callback de escrita. Literal e destino StackObjectID são unsupported neste
adapter: o último requer publicação de contexto ainda não disponível.
Os recursos World e LotContext continuam impondo seus próprios limites.

O adapter lê o range antes de escrever, inclusive quando range e destino
coincidem. Calcula usando uma cópia candidata do RNG e só confirma o estado
depois de validar o destino ou receber sucesso da escrita externa. Falha de
range/destino/writer deixa RNG e outputs sem mudança. A referência gera antes
de escrever; preservação do RNG em writes rejeitados é o contrato de falha
do ODH, não uma alegação sobre falhas do jogo original.

Callbacks são seriais e não podem reentrar ou modificar o RNG. Writer que
devolve false deve deixar seu armazenamento intacto; observer deve não lançar.
São precondições documentadas, sem guard de reentrância em runtime. O host
atual usa World/LotContext e observer limitado pelo budget de instruções.
O snapshot ocupa um uint64; isso não constitui medição de orçamento de console.

## Host, seed e Sleep

`--seed UINT64` aceita decimal ou hexadecimal. O default do host de diagnóstico
é1, identificado como fixture no JSON. Nem a seed default nem uma seed fornecida
são tratadas como estado aleatório salvo pelo original. Sua recuperação exige
um contrato de persistência separado. Zero e uint64 máximo são aceitos;
overflow, sinal, texto extra e repetição da opção são recusados.

Sleep chama o mesmo `RandomState::next(1)` que alimenta opcode8. O JSON conserva
seed inicial/final, origem da seed, quantidade de ciclos de Sleep e cada chamada
opcode8 com owner, range, resultado, destino e seed antes/depois. Logs têm
limite10000, coerente com o budget do host. O ciclo de Sleep não é registrado
como uma chamada opcode8.

## Resultado nativo e próximos gates

Usar o comando de [lot-validation.md](lot-validation.md) com `--seed 1`.
Para Init, adicionar `--routine 4097`. Recursos comerciais permanecem externos
e somente lidos.

| Probe | Resultado com seed1 |
|---|---|
| Main4096 | 11 instruções/1 tick; Sleep avança seed1 para33554433; continua em Person8211 pc3, opcode31 unsupported |
| Init4097 | 58 instruções/0 ticks; oito calls RNG; chega a traits4100 pc17, write scope30/index0 unsupported |

Ranges do Init:50,1,1,1,50,1,15,11. Resultados:15,0,0,0,28,0,3,6.
Sete destinos são MotiveData14 e o último é argumento9/index0.
Seed final1206631074890568497. Todos os resultados e estados intermediários
coincidem com um oracle Python independente com inteiros arbitrários e máscaras
unsigned64. Seeds0,2,uint64 máximo chegam ao mesmo gate de scope30 em56,57,58
instruções respectivamente; não se escolheu seed para evitar dependências.
Isso ainda não executa entrada8 ou o pipeline posterior de BodySTR.

Scope30 na referência é MyPersonDataByTemp: o índice PersonData vem de
`TempRegisters[data]`, não do próprio `data`. Fonte:
`tso.simantics/Engine/VMMemory.cs:126–130,570–574`. Será ligado ao World com
validação dos dois índices em uma próxima unidade.

O opcode31 do Main foi identificado corretamente como **NeighborId5**,
não Person1: operando `00000000850a0000`, flags85, alvo StackObjectID10.
`VMSetToNext.cs:60–65,263–279,299–311` delega a
`TS1NeighbourProvider.cs:267–277`. O próximo dado necessário é NBRS do bairro.
Sucesso escreve uma identidade de vizinho que pode não representar entidade
instanciada; fim retorna false sem mudar o alvo. O provider usa Entries
ordenado por NeighborID signed e lookup ativo/RuntimeIndex, sem wrap.

Investigação estrutural do `UserData/Neighborhood.iff`, SHA256
`e585615fed1f9ddfb15ff8f5ebe7a09ea48a1369e9a3106f8331d3d9f5dde323`:
NBRS1 versão62 tem49 registros,33 ativos marker1 e16 placeholders marker0.
Placeholders consomem só seu marker, não um registro completo. Fonte de layout:
`tso.files/Formats/IFF/Chunks/NBRS.cs:29–53,122–155`. Leitor NBRS, scope32
NeighborPersonData e Relationship26 permanecem pendentes; Main ainda não
enumera esses dados no ODH. Carregar entidades salvas da casa é outra unidade.

## Verificação e limites

131 checks do core cobrem vetores independentes, unsigned wrap, ranges extremos,
seed zero, sequência/cópia/reset, scopes e alias, outputs reais da VM,
rejeição sem avanço, destino MotiveData e um estado compartilhado por owners.
Fixtures CLI cobrem seed64, range negativo/zero/um, Sleep entre duas chamadas,
selectors inválidos e preservação do estado em fault.

Debug e ASan/UBSan:46 testes CTest passaram,1 X11 foi pulado. LeakSanitizer
desativado porque o ambiente ptrace impede sua execução. JSONs nativos Main e
Init coincidem byte a byte entre Debug e ASan, com seed1. Relatórios ignorados:
`reports/random-person-main.json` e `reports/random-person-init.json`.
Sem execução do original, restauração completa de saves ou teste de hardware.
