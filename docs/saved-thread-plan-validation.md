# Plano de leitura das pilhas salvas

`odh-saved-world` agora mostra como os frames de cada personagem salvo podem
ser representados seguindo a conversão do ativador TS1 do Simitone. O campo
`simitone_thread_plan` é diagnóstico: `executable` permanece `false`, e a
resposta geral continua com `saved_threads_restored:false`.

Para cada frame, o plano resolve o índice de tipo do dono pela tabela OBJT e
retém o GUID, árvore, instrução, StackObject, parâmetros, locais e estado
primitivo bruto. Parâmetros são completados até quatro palavras, e os oito
registradores salvos são colocados nos primeiros oito de um banco de vinte;
os demais começam em zero. O dono e o chamado começam como o próprio Sim.
`ObjectUses` troca o chamado dos frames a partir da profundidade indicada,
como faz o adaptador do Simitone. Um dono sem GUID e uma profundidade inválida
fazem o plano falhar, em vez de inventar um valor.

## Referência e limites

Consulta local ao submódulo Simitone fixado em
`f86bbd34b2112296ed786741f640a0535886d180`:
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Utils/VMTS1ActivatorNew.cs`,
`ConvertThread` (linhas 212–323). A rotina usa o índice OBJT para obter o GUID,
faz padding dos parâmetros, cria fila/frames e reconstrói parte dos chamados
com `ObjectUses`. As notas dessa implementação marcam essas regras como
adaptações próprias do FreeSO/Simitone. Elas orientam o plano, mas não provam
que o executável original usa os mesmos detalhes.

Por padrão, o plano não verifica se a rotina e o endereço existem nos arquivos
BHAV. A opção `--verify-saved-threads`, junto das fontes explícitas
`--semiglobal FAR ENTRY` e `--global FAR ENTRY`, faz essa conferência antes de
imprimir o resultado: confirma que cada rotina está no recurso esperado e que
o endereço salvo aponta para uma instrução existente. Se faltar uma rotina, o
recurso não corresponder ao link do objeto ou o endereço estiver fora da
rotina, o comando falha sem imprimir um JSON parcial.

Com a mesma opção, o plano também lê a instrução do quadro chamador apontada
por cada `ObjectUses` salvo. No mapeamento do Simitone, GotoRelative e GotoSlot
marcam o quadro seguinte para tentar novamente; Gosub e IdleForInput marcam a
volta como parte de uma interação e estendem essa marca aos quadros acima.
Outras instruções deixam a volta normal. O JSON mostra `return_state` e
`action_tree`; sem a conferência de código, `return_state` fica
`unresolved`. Isso é uma reconstrução do adaptador Simitone, não uma regra já
confirmada no executável original.

Depois dessa conferência, o plano pode ser convertido em `VmFrame` e carregado
na VM parada se todos os retornos forem normais. A própria VM verifica se as
rotinas e posições existem nos bancos de código que recebeu. Quem chama essa
ponte ainda precisa carregar os recursos certos; ela não confere se o banco
semiglobal corresponde ao GLOB esperado. Quadros que precisam de repetição ou
interação são recusados até o kernel e o scheduler ODH suportarem esses estados.
Isso não executa a VM nem substitui a restauração das filas e do relógio.

`odh-saved-world --verify-saved-threads --import-saved-threads-dormant` repete
a auditoria e carrega, de cada fonte exata aprovada, somente as BHAVs citadas
pela pilha. A sessão possui o World restaurado e mantém uma VM por Sim salvo,
ligada ao ID e à geração da entidade. O banco de leitura/escrita da VM aponta
para esse mesmo World. A admissão exige avatar salvo, pilha não vazia, quadros
com o mesmo chamador e estado `dormant` sem instruções executadas; o limite é
64 VMs. Se a entidade for substituída sob o mesmo ID, a consulta à VM falha
pela geração diferente. O JSON informa quantas VMs continuam guardadas,
`all_retained_bindings_valid:true` e `executed:false`; `saved_threads_restored`
continua `false`. Nenhuma VM é ativada. O carregador não traz as chamadas
futuras dessas BHAVs nem fornece controladores de rota, animação ou fila.

Essa conferência não executa os quadros. Ainda não restaura o estado especial
de retorno entre frames nem retoma o relógio, a fila de interações ou os
serviços de rota. Portanto esta etapa prepara e confere dados; não é
continuação da partida nem teste de gameplay.

## Busca estática de ações em casas Legacy

Uma leitura estática de 648 arquivos `House*.iff` nas pastas `UserData` até
`UserData8` da Legacy foi validada primeiro contra a House07 original: o leitor
encontrou exatamente os Sims 105 e 137, ambos com seis quadros salvos. A busca
não encontrou erros de leitura nos 648 arquivos. Quatro casas continham ação
ativa ou fila não vazia; as cópias de cada casa nas oito pastas de usuário têm
o mesmo SHA-256, portanto contam como um único exemplo independente por casa:

| Casa | SHA-256 do `House*.iff` | Estado salvo relevante |
|---|---|---|
| House56 | `8bb39d8e613bf6e3902e4a405bc26420d5d1f00b01e3f0301e471ec51497e800` | Sim 292: UID ativo 4237, alvo 333, dois `ObjectUses` |
| House63 | `c42f3b71799d509e0854b3285c4ff440a7237cdca72db56ca43df9b143d65d3f` | Sim 323: UID ativo 59, alvo 121, dois `ObjectUses`; último UID 58 |
| House76 | `5df499faea1864eb3fb046221f743fca1ad54b11fb6b0884627e6bac6070d865` | Sim 35: UID ativo 2339, alvo 266, dois `ObjectUses`; último UID 2332 |
| House77 | `1c95d02c85a5b8d32fbaebe979c69b7edf1a12d4b33f8aa1a2590c3456268c9c` | Hecuba (ID 11): fila com UID 4051, chamador/alvo 11, TTA 33, prioridade 50 e árvore 8236; 1 quadro de rota e 4 quadros de VM |

Essa busca decodificou a tabela OBJT e os blocos OBJM para localizar o ramo de
Person e ler os campos já suportados por `objm_tail`. Ela só informa o conteúdo
salvo; não prova quando ou como o jogo executaria uma ação. A importação
completa da House77 ainda falha ao procurar a definição do objeto 649, então
seus vínculos com o restante do World não foram validados pelo importador ODH.
Os campos de fila continuam apenas dados dormentes: nenhuma ação foi ativada.

A busca também mostrou que o limite padrão de 65.536 palavras dos prefixos não
comportava os bancos fixos dos 847 objetos da House77. O limite cumulativo subiu
para 131.072 e o teste de prefixos agora inclui um lote sintético de 847
entidades. Isso permite avançar na leitura dos prefixos, mas não resolve a
definição ausente nem representa a importação completa da casa.

## House05 da instalação local

O mesmo caminho de importação foi exercitado em House05 com `Objects.far`,
`Global.far` e os recursos de personagem `User00009.iff`, `User00010.iff` e
`User00011.iff`. O World contém 245 entidades; os Sims 16, 18 e 21 têm três
quadros cada, todos conferidos nos recursos e guardados em três VMs dormentes.
Cada VM permanece com zero instruções executadas e vinculada à geração atual
do Sim correspondente. O hash SHA-256 de `House05.iff` foi
`a6d18d81c5072c507b118b68598ee12ea57403634d6507c39b7a04ad4c6cdd99`;
os arquivos comerciais lidos preservaram seus hashes antes/depois do probe.
A fila salva desses três Sims está vazia. Esses resultados são leitura e
importação inativa de dados nativos, não retomada de uma partida.

Reprodução, após compilar `odh-saved-world`:

```sh
./build/bridge-validation/odh-saved-world \
  '../referencias/The Sims/UserData/Houses/House05.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00009.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00010.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00011.iff' \
  --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff \
  --verify-saved-threads --import-saved-threads-dormant
```

## House07 da instalação local

Com `../referencias/The Sims/UserData/Houses/House07.iff`, `Objects.far` e os
dois recursos de personagem externos, o leitor restaurou estruturalmente 182
entidades e gerou:

| Sim salvo | ID | Quadros | Dono de rotina | Fila salva |
|---|---:|---:|---|---|
| Bob | 105 | 6 | OBJT type 81, GUID 850010198 | vazia |
| membro47 | 137 | 6 | OBJT type 82, GUID 844195932 | vazia |

Cada pilha contém rotinas 4096, 8193, 8283, 8227, 8216 e 8222, com endereços
salvos preservados; nenhum `ObjectUses` estava presente. Os dois Sims mantêm
registradores salvos zerados. O resultado real é uma inspeção do save e dos
recursos comerciais, não uma execução do original ou do Simitone.

Reprodução a partir do checkout, sem escrever nos dados comerciais:

```sh
cmake -S . -B /tmp/odh-saved-thread-plan-build -DBUILD_TESTING=ON
cmake --build /tmp/odh-saved-thread-plan-build --target odh-saved-world odh-saved-thread-plan-tests -j2
ctest --test-dir /tmp/odh-saved-thread-plan-build -R saved_thread_restore_plan --output-on-failure
/tmp/odh-saved-thread-plan-build/odh-saved-world \
  '../referencias/The Sims/UserData/Houses/House07.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff'
```

Para também conferir os quadros contra as rotinas reais do jogo:

```sh
/tmp/odh-saved-thread-plan-build/odh-saved-world \
  '../referencias/The Sims/UserData/Houses/House07.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' \
  --verify-saved-threads \
  --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

Para também importar as doze frames reais, sem ativar nenhuma delas, acrescente
`--import-saved-threads-dormant` ao comando acima. No checkpoint local, Bob
(ID105) e membro47 (ID137) foram importados em duas VMs mantidas pela sessão;
cada VM ficou parada com seis frames. Todas as doze rotinas vieram dos recursos
identificados na auditoria. Isso confirma o caminho de leitura e importação,
mas não a continuação da partida, a semântica do executável original ou o
funcionamento de interações.

No House07 local, as doze rotinas dos dois Sims foram encontradas e todos os
endereços salvos ficaram dentro de suas rotinas. Isso confirma apenas que os
dados apontam para código existente; não confirma que seja seguro ou correto
executá-lo. Esse save não contém `ObjectUses`, então nele não houve volta
especial a classificar; todos os frames ficam com estado `normal` pelo padrão
do adaptador.

Os testes sintéticos cobrem resolução do dono, padding, `ObjectUses`, os quatro
grupos de opcode usados para classificar a volta, extensão de `action_tree` e
recusa de opcode chamador ausente. Neste checkpoint de retenção, a build Linux de `odh-saved-world` e
`odh-saved-thread-plan-tests` concluiu, e os três testes focados passaram.
A conferência do diff (`git diff --check`) também passou. Esses testes
não executam as pilhas salvas nem provam gameplay.
