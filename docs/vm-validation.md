# Núcleo inicial de execução BHAV — 2026-10-13

Este checkpoint registra o primeiro núcleo. O avanço posterior de entidades
e slots está documentado em `world-validation.md`.

`odh_vm` agora executa um perfil limitado de BHAV 8002 sem janela. A biblioteca
possui rotinas, frames, registradores e interfaces para memória do mundo;
não possui entidades, scheduler, filas, roteamento, animação ou persistência.
A meta Linux funcional continua aberta.

## Perfil implementado

- Metadata 8002: type, argumentos, locais e tree version; bytes desconhecidos
  preservados. Versões 8000/8001 continuam sem metadata serializada; execução
  usa o perfil legado limitado de quatro argumentos e zero locais, conforme o
  parser antigo do SimsLib no checkout Simitone/FreeSO. Isso habilita rotinas
  desse perfil, mas não estabelece equivalência completa dos formatos legados.
- Chamadas diretas opcode >= 256: global abaixo de 4096, privado abaixo de
  8192, semiglobal a partir de 8192. O conjunto de recursos é explícito para
  um code owner no checkpoint inicial. O avanço posterior de bancos por owner
  e invocações explícitas está em [code-owner-validation.md](code-owner-validation.md).
  Ainda não há função/OBJf ou
  precedência de arquivos. Global/privado/semiglobal não se misturam por ID.
- Quatro palavras de argumento; -1 usa temp correspondente quando ao menos
  um argumento 1..3 é diferente de zero, conforme a referência FreeSO.
  Argumentos adicionais usam esse perfil até o limite de 20 temps; outros
  casos falham. Locais são zerados por frame, no tamanho max(locals,args).
- Destinos FE/FF retornam true/false ao pai. FD segue a única aresta definida
  quando existe; duas arestas FD falham. Essa regra é de referência e ainda
  não foi comparada com o executável original.
- Primitiva 2: comparações assinadas, atribuição, soma, subtração, multiplicação,
  divisão não zero, módulo não zero, AND, OR/XOR TS1, flags 1..16 e incremento/
  decremento com comparação. Escritas têm truncamento explícito de 16 bits.
  RHS do incremento/decremento é lido após a escrita, preservando aliases.
- Scopes internos: literal 7, temps 8, parâmetros 9, stack object ID 10 e
  locais 25. Outros scopes precisam de callbacks do mundo; ausência não vira
  zero. Contexto conserva IDs caller/callee/stack object por frame.
- Trace antes de cada instrução: step, profundidade, scope/ID, PC, opcode,
  branches e oito operandos. Fault preserva frame/PC para diagnóstico.

Limites de proteção atuais: stack de 64 frames, até 4.096 variáveis de cada
classe por frame e 20 temps. São limites ODH, não limites comprovados do jogo.
O orçamento de instruções é resumível e não representa passagem de tempo.
Loops não descartam estado para caber no orçamento. Callbacks não podem reentrar
em start/run; exceções externas se propagam sem invalidar frames.

## Continuação externa de frame

Um serviço em `WAIT` pode agora usar `call_pending(ticket, VmCall)` para empilhar
uma BHAV filha sem perder o ticket de espera do pai. Esperas filhas mantêm seus
próprios tickets; ao retorno da rotina filha, a VM publica
`VmExternalFrameReturn`, restaura a espera-pai e cede ao host sem reexecutar a
instrução BHAV original. `resume_external_frame` retoma o frame de serviço em
estado waiting, que pode então agendar outra filha ou concluir seu ticket. As
chamadas são limitadas pelo mesmo teto de stack. Isso fornece um mecanismo de
continuação para futuros frames externos de rota; ainda não há invocação de
portal BHAV pelo `Routing`, nem integração de `Route45` com Main.

`odh-vm-tests` usa uma fixture sintética com Route45-like wait, BHAV filha que
também aguarda, uma segunda filha aninhada, restauração LIFO de tickets e
conclusão pela aresta original do pai. Chamadas com ticket stale ou rotina
ausente preservam o wait-pai.

## Importação de uma pilha parada

`Vm::restore_dormant_state` aceita uma pilha completa de `VmFrame` já resolvida
e vinte registradores. Antes de alterar a VM, confere cada rotina, dono de
código, posição, destinos de desvio, limites e variáveis. Locais e parâmetros
curtos são completados conforme os metadados da rotina. A VM fica no estado
`dormant`; `run()` não executa nada até o host chamar explicitamente
`activate_dormant_state()`.

O teste sintético carrega dois quadros, confirma que a VM não roda durante a
importação e, após ativação explícita, observa o quadro superior iniciar na
posição carregada e voltar ao pai. Uma pilha inválida é recusada sem mudar a
VM anterior. Isso prova o mecanismo de importar um snapshot já interpretado;
não liga ainda os dados OBJM salvos a essa API. `primitive_state`, resultados
especiais de retorno, ação/interação, filas, relógio e serviços ainda precisam
ser resolvidos antes de ativar uma partida salva.

## Diferenças e lacunas explícitas

Primitivas fora da 2, scopes externos ausentes, rotinas faltantes, índices
inválidos e frames legados falham com diagnóstico. Divisão/módulo por zero,
flags fora de 1..16 e sqrt (operator 20) aguardam semântica comprovada.
O perfil não executa os hacks FreeSO de comparação de Room, TempXL ou listas
TSO. O byte IsSigned da expressão é preservado no trace; a referência consultada
não o usa. O tratamento do original permanece por investigar.

Este perfil testa mecanismos compartilháveis, mas não comprova compatibilidade
SimAntics completa. Interfaces de memória não substituem os serviços do mundo.

## Checks e corpus

No checkpoint de 2026-10-21, a build Debug completa e CTest passaram com
158/158; `linux_window_backend` foi pulado por falta de X11. A suíte focal
`odh-vm-tests` possui 112 checks sintéticos atuais: parsing de metadata, sinais
e truncamento, flags, aliases, chamadas aninhadas nos três scopes, retorno
false, isolamento de locais, substituição de argumentos, loops retomados,
limites, diagnósticos, reentrada de callback e continuação de frames externos.
A integração CLI testa dados sintéticos de FAR/IFF/BHAV e JSON de trace.
Sanitizers cobrem checkpoints anteriores da VM; a continuação externa desta
revisão não foi executada com ASan/UBSan. LSan estava desativado por ptrace
(`detect_leaks=0`) naquela validação, então leaks não foram verificados.

Após ampliar a leitura de metadata, o inspector com sanitizadores percorreu
todo o corpus e produziu relatório idêntico ao checkpoint TTAB, com os 14
contadores anteriores preservados. Isso valida leitura, não execução de todas
as 28.778 rotinas.

## Execução limitada da cadeira real

Entrada `DiningChairs.iff` do `Objects.far`, SHA-256
`cffff7b4e8b5d1b31351c32a2f8656f50dbf72a82217bf38b4828853fc6f90c7`.
Sem serviços do mundo, ação `0x1001` chama `0x1004`; no PC 0 dessa rotina,
a expressão compara scope16/index0 com scope3/index11. O probe para com
`unsupported read scope 16 index 0`, após dois passos, com dois frames.
Isso é a VM ODH interpretando instruções reais, não execução do `Sims.exe.bak`.

Os probes Debug e ASan/UBSan produziram JSON idêntico e saída esperada 3.
O probe com sanitizadores forneceu também `Global.iff` explicitamente; nenhuma
rotina global foi chamada antes dessa falha. Relatórios locais ignorados:
`reports/chair-action-vm.json` e `reports/chair-action-vm-sanitize.json`.

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run
ASAN_OPTIONS=detect_leaks=0 ./build-sanitize/odh-bhav \
  '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

Próximo trabalho: modelo de entidades/slots/person data e contexto de interação,
seguido das primitivas de roteamento/animação usadas por `sit core` (0x1003).
Sprites/paletas e composição Linux seguem como frente necessária ao cenário
funcional, com testes contra o original ainda pendentes.
