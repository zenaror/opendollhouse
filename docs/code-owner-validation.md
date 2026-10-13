# Bancos de código e chamadas entre proprietários

Checkpoint de 2026-10-13. `odh_vm` distingue recursos privados e semiglobais
por proprietário do código. Isso remove a colisão de IDs entre avatar e cadeira
na biblioteca e permite uma chamada explícita entre esses recursos, preservando
o frame que receberá seu resultado. A queue/AllowPush e a retomada do Main do
avatar no probe Linux ainda não estão integradas.

## Contrato e API

`CodeOwner` é um handle uint32 de recurso, independente de IDs de entidades,
GUIDs de OBJD e gerações do World. `VmContext::code_owner` pertence a cada
frame. O owner0 é o conjunto padrão, preservando chamadas e contextos existentes.
O último argumento opcional do construtor fornece `RoutineOwners`: bancos
privados/semiglobais adicionais imutáveis. Rotinas globais ficam no conjunto
padrão e são compartilhadas. Não há busca automática ou precedência de arquivos.

A seleção ordinária é:

| Tipo de rotina | Banco consultado | Owner do filho |
|---|---|---|
| Global, ID<4096 | Globais do conjunto padrão | Mesmo owner do pai |
| Privada, 4096..8191 | Banco do owner do frame | Mesmo owner do pai |
| Semiglobal, ID>=8192 | Banco semiglobal do owner do frame | Mesmo owner do pai |

Assim, quando uma rotina global chama uma privada, consulta o owner preservado,
e não necessariamente o banco padrão. Mudar StackObject ou Callee não troca
esse owner. Um owner ausente, mesmo ao iniciar uma rotina global, falha;
rotinas privadas/semiglobais ausentes não usam outro banco como fallback.

A origem dessa regra é a referência FreeSO fixa
`4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2:
[VMThread.cs:490](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMThread.cs#L490)
e [VMStackFrame.cs:71](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMStackFrame.cs#L71).
`ScopeResource` é o recurso de CodeOwner; as referências CallerPrivate,
CalleePrivate e StackObjPrivate são acessos distintos.

Uma primitiva pode retornar `PrimitiveDisposition::call` e um `VmCall` com
rotina, contexto e argumentos explícitos. O kernel valida e instala o filho,
conservando o pai na instrução da primitiva. O retorno FE/FF do filho percorre
a branch true/false daquela instrução do pai. Não há um segundo dispatch da
primitiva só para entregar seu resultado. O filho pode conter chamadas
ordinárias, consumir orçamentos sucessivos e esperar por serviços com tickets.
A conclusão da espera pode desempilhar o filho sem alterar a identidade do pai.

Esse limite de invocação explícito prepara as árvores funcionais, cuja
referência usa o recurso do objeto-alvo e atribui o owner do novo frame:
[VMEntity.cs:661](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMEntity.cs#L661)
e [VMRunFunctionalTree.cs:23](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMRunFunctionalTree.cs#L23).
O resultado `call` é um mecanismo do kernel; não implementa sozinho opcode20,
OBJf, avaliação de condições, interação da queue ou retorno no próximo tick.

`VmTrace` e a instrução salva em `VmPending` incluem o owner; callbacks de
memória e serviços recebem o contexto completo. O JSON do CLI existente
continua com seu esquema anterior, porque seu carregador ainda fornece um
único owner padrão. Registrar bancos adicionais na biblioteca não altera
resolvers de animação, tuning ou outros serviços que o host deve fornecer.

## Limites e falhas

- Máximo padrão64 owners, incluindo owner0; configurável por `VmLimits`.
  Bancos são carregados na construção e não crescem durante a execução.
- Owners adicionais não substituem0 ou globais; o scope de suas chaves deve
  corresponder à faixa numérica. Registros inválidos falham na construção.
- Cada chamada conserva os limites existentes:64 frames por padrão, até4096
  variáveis em cada banco do frame; chamadas ordinárias continuam limitadas
  ao mapeamento de20 temps. Não há afirmação de orçamento real de New3DS/RSS.
- `call` exige payload de filho e proíbe escrita simultânea em banco do pai;
  outros resultados não aceitam payload de chamada. Contexto/rotina/argumentos
  e limites são validados antes de inserir um frame.
- Uma chamada inválida mantém o pai e seus argumentos, mas gera fault após o
  dispatch; não é rollback de efeitos externos feitos por um callback.
- Tickets stale/duplicados continuam sem efeito. Recursos e registros de owner
  são cópias de propriedade da VM; alterações posteriores do chamador não os
  modificam. Não há troca de conteúdo ou registro durante a execução.

## Gates

`tests/vm_owners.cpp` contém41 checks sintéticos independentes:

- Três owners com os mesmos IDs privados/semiglobais produzem valores distintos;
  chamadas globais retornam aos bancos corretos, com owner no trace.
- StackObject/Callee independentes não mudam a resolução de código; bancos
  ausentes não fazem fallback e owners desconhecidos falham.
- Uma primitiva do owner0 invoca4096 do owner22, que chama um global e retorna
  ao owner0. Verifica contexto, argumentos, orçamento resumível, registradores
  compartilhados e branches true/false diferentes.
- Um filho de outro owner aguarda serviço; pending/trace preservam owner22,
  conclusão stale/duplicada não avança o pai, conclusão válida restaura sua
  identidade/PC e ele executa a instrução seguinte uma vez.
- Payload ausente, chamada com escrita ou disposição divergente, owner ausente,
  argumentos excedentes e stack cheio falham antes de inserir o filho.
- Registry rejeita owner0 duplicado, globais locais, chave de scope inválido e
  limite de owners; aceita o limite exato e preserva cópias imutáveis.

Debug e ASan/UBSan:38 testes passam; o teste X11 é skipped neste sandbox. LSan está desligado (`detect_leaks=0`), sem validação
de vazamentos. A revisão independente do diff não identificou falha bloqueante
na resolução, no retorno ou na conclusão de espera; isso não substitui os gates.

O cenário externo já existente cadeira/Main/Sit/hold120/Stand/post240 foi
reexecutado:94 dispatches,662 ticks,Comfort3. Todos os campos JSON da simulação
são iguais à baseline anterior. É uma regressão do probe ODH com recursos
nativos, não execução do programa original ou prova de idle correto.

## Pendências para integração

O [rastreio do Main/idle](person-lifecycle-validation.md) continua válido:
retomar uma interação exige o frame Main preservado em AllowPush, com
semântica própria de saída/tempo e pedidos de animação. Esta unidade fornece
bancos e chamada de filho, mas não cria a queue, não reinicializa o avatar e
não força um loop em pé após Stand.

Os próximos gates devem ligar resolução por owner ao host, implementar a
continuação de interação e ampliar as dependências realmente alcançadas pelo
Main do avatar. Execução completa de TS1, lotes/saves, comparação com o
executável original e ports continuam não demonstrados.
