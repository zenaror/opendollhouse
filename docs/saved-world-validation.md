# Importação de entidades salvas para o World

Checkpoint de 2026-10-14. `make_saved_world` agora cria um World próprio a partir
 dos registros ObjM já decodificados. `odh-saved-world` exercita essa importação
com recursos comerciais externos, sem executar os scripts salvos. O lote ainda
não é jogável: arquitetura, renderização integrada e continuação das threads
seguem pendentes.

## Contrato implementado

`SavedWorld` possui o World, o estado de relacionamentos, os registros completos
 e os grupos multitile. Não guarda ponteiros para dados do chamador. A fábrica
valida o snapshot antes de construir um candidato; qualquer falha impede o
retorno de um World parcial. Alocações que falham também não publicam entidades.

A importação preserva os IDs positivos originais, GUIDs, atributos, PersonData,
motivos, posições, direção, slots e contenção. Os 68 ObjectData originais e cinco
extras ficam em um snapshot próprio, além dos dados de execução. O World
contabiliza 73 palavras desse snapshot e 64 palavras comuns de execução por
entidade; os índices derivados 2/3/11/67 continuam usando seus getters. Alterações
posteriores no World não reescrevem o snapshot. Remover uma entidade libera seu
orçamento, e IDs reutilizados recebem uma nova geração.

Coordenadas salvas já usam unidades de 1/16 de tile. O par `(-16,-16)` deixa a
posição de execução ausente, preservando a entidade e seu estado. A direção
aceita oito posições. Contenção exige reciprocidade entre os slots do pai e os
campos ContainerId/SlotNumber do filho; ciclos, filhos duplicados e referências
inválidas são rejeitados. Ao religar filhos, suas coordenadas salvas permanecem.
LinkedSlot é conservado no registro; não é convertido em slot0.

O ramo person selecionado pelo tipo serializado OBJT2 determina um avatar; ambos
OBJT e OBJD permanecem disponíveis quando seus tipos diferem. Os motivos são
adaptados de binary32 para signed16 com arredondamento para o mais próximo e
empates para o inteiro par, usando operações inteiras independentes do modo de
arredondamento do host. Valores não finitos ou fora da faixa são rejeitados.
Não se aplica o clamp do setter de motivos durante a importação. As palavras
originais de ponto flutuante, inclusive o banco anterior, ficam no registro.

`DefinitionIndex::unique_master` procura MasterID/SubIndex=-1 no mesmo arquivo e
fonte explícitos. Um GUID globalmente ambíguo também impede resolver o master.
Ausência ou ambiguidade produz `master_guid=null`, sem inferência aritmética. Os
grupos exigem um líder direto compatível e mantêm seus IDs de membros.

Os relacionamentos de objetos são importados em um lote atômico, ligados à
geração atual de cada fonte. Target e values usam seus 16 bits baixos, com
valores signed16. Fontes vazias também contam e ficam ligadas à geração; não se
sobrescreve uma importação anterior. Limites incluem NBRS e objetos juntos.
A API de relacionamentos permite chaves de alvo ausentes, conforme a estrutura
de dicionário da referência. A fábrica atual aplica um perfil mais estrito:
referências salvas, inclusive esses alvos, devem identificar entidades do mesmo
snapshot. Isso é um limite explícito, não prova de aceitação de todos os saves.

Stacks, temps, extras, footprint, flags de sprites, strings de aparência,
animações, filas e sufixos opacos ficam conservados nos registros. Stacks e filas
não são retomados. Não se reaplica Init ou herança de um personagem novo sobre
os dados salvos. Os probes anteriores `odh-person` continuam usando sua entidade
construída separadamente; não passam automaticamente a executar este World.

## Limites e consulta de referência

Defaults da fábrica: até 4.096 entidades, 65.536 palavras lógicas no World,
4.096 por banco; 65.536 campos retidos, 256 KiB de strings e 8 MiB de sufixos
opacos. O orçamento dos registros é calculado sobre os vetores reais, não sobre
contadores fornecidos pelo chamador. Esses números não medem RSS nem garantem
que a implementação atual caiba no New 3DS.

O host exige fontes explícitas: até 16 caminhos de 4.096 bytes, 8.192 candidatos
OBJD e 256 KiB de proveniência; FAR com até 8.192 entradas e 256 KiB de nomes;
IFF de definições com até 4.096 chunks. GUIDs necessários ausentes ou ambíguos
fazem a importação falhar. Não há busca automática nem decisão de precedência.

Consulta local: FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`,
`VMTS1ActivatorNew.cs` linhas 379–386 (motivos), 548–565 (identidade, posições e
contenção) e 590–599 (relacionamentos). Revisões e licenças em
[references.md](references.md). Essa implementação de terceiros fundamenta a
adaptação; não substitui evidência de comportamento do executável original.

## Validação

Suítes sintéticas: 104 checks de bancos World, 63 de importação de relacionamentos,
139 da fábrica e 17 de DefinitionIndex. Cobrem limites, cópias próprias,
referências/ciclos, frames dormentes, grupos, tipos divergentes, arredondamento,
gerações e falhas de alocação. Debug e ASAN/UBSAN: 73 testes passaram, zero
falharam, um teste X11 sem execução (74 configurados). LSan foi desabilitado
porque o ambiente não suporta sua inspeção sob ptrace; não há resultado de LSan.
O simulador de falhas de alocação do teste de relacionamentos passou a tratar
`new(nothrow)` junto ao par malloc/free após ASAN detectar um par incompatível.

Importações nativas, com Objects.far e NBRS1 de Neighborhood.iff; House07 também
usa User00000.iff e User00001.iff como fontes de OBJD:

| Campo | House01 | House07 | House03 |
|---|---:|---:|---:|
| Entidades | 60 | 182 | 267 |
| Avatares | 0 | 2 | 0 |
| Com posição | 56 | 174 | 263 |
| Contenções | 0 | 5 | 0 |
| Grupos multitile | 13 | 29 | 75 |
| Masters não resolvidos | 0 | 0 | 0 |
| Palavras no World | 8.726 | 26.885 | 38.824 |
| Fontes de relacionamentos, incluindo NBRS | 93 | 215 | 300 |
| Pares, incluindo NBRS | 22 | 118 | 22 |
| Valores, incluindo NBRS | 27 | 186 | 27 |
| Pares de objetos | 0 | 96 | 0 |
| Valores de objetos | 0 | 159 | 0 |

Os relatórios Debug/ASAN são iguais byte a byte. Comparação com os relatórios de
prefixo/common/tail do checkpoint anterior conferiu os 509 registros: todos os
68 ObjectData lidos pelo World, bancos originais, extras, atributos, slots,
identidade, posições/direções, contenções, profundidade dos stacks e tails
completos. Nos dois avatares, PersonData e motivos adaptados também conferem.
Todos os 159 valores de relacionamento de objetos foram lidos pelo backend e
comparados com a conversão independente para signed16. Os decoders anteriores
foram comparados com um leitor Python independente; ver
[saved-tail-validation.md](saved-tail-validation.md).

SHA256 dos JSON completos (os caminhos explícitos fazem parte do JSON):

- House01: `6dba7401918698e6f55d26e77053393d6769eb14d53c9a3d7376818e3782800b`.
- House07: `f5cfbc29297249ac6a39e0e2ef08f5fd4571cf15349579de7cc19bc656ecc605`.
- House03: `6e77d2a9ccbf7f7e3eacba94eb65bd62fc1cac7f6faf9235ddbb5fa21ff81e4c`.

Evidência local ignorada: `reports/house{01,07,03}-world-native[-asan].json`,
`reports/verify-saved-world.py`, `reports/saved-world-native-validation.json` e
`reports/saved-world-ctest-{debug,asan}.txt`. Os sete inputs selecionados tiveram
seus hashes conferidos antes e depois, sem alteração. Os hashes e caminhos estão
no relatório de validação; os hashes de referência estão nos checkpoints dos
formatos anteriores. Sem argumentos, sem definições, com opção desconhecida ou
com as definições dos personagens ausentes, o CLI retornou 2 e não emitiu JSON
parcial.

## Reproduzir com a instalação externa

```sh
./build/odh-saved-world '../referencias/The Sims/UserData/Houses/House07.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' \
  --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff' \
  > reports/house07-world-native.json
```

Saída 0 indica importação no perfil descrito; saída 2 indica falha. Os campos
`saved_threads_restored`, `architecture_loaded` e `rendered` continuam false.
O próximo trabalho deve ligar recursos de aparência e arquitetura a essas
entidades reais, implementar a continuação das threads com owners/ticks corretos
 e integrar renderização/interação. Este checkpoint não valida o original em
execução, fidelidade completa ou hardware dos ports.
