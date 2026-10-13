# Núcleo inicial de execução BHAV — 2026-10-13

`odh_vm` agora executa um perfil limitado de BHAV 8002 sem janela. A biblioteca
possui rotinas, frames, registradores e interfaces para memória do mundo;
não possui entidades, scheduler, filas, roteamento, animação ou persistência.
A meta Linux funcional continua aberta.

## Perfil implementado

- Metadata 8002: type, argumentos, locais e tree version; bytes desconhecidos
  preservados. Versões 8000/8001 continuam legíveis, mas execução é recusada
  enquanto não houver perfil explícito de frames legados.
- Chamadas diretas opcode >= 256: global abaixo de 4096, privado abaixo de
  8192, semiglobal a partir de 8192. O conjunto de recursos é explícito para
  um code owner. Não há mudança dinâmica de proprietário, função/OBJf ou
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

CTest 6/6 passou em Debug e ASan/UBSan. A suíte da VM possui 53 checks
sintéticos: parsing de metadata, sinais e truncamento, flags, aliases, chamadas
aninhadas nos três scopes, retorno false, isolamento de locais, substituição
de argumentos, loops retomados, limites, diagnósticos e reentrada de callback.
A integração CLI testa dados sintéticos de FAR/IFF/BHAV e JSON de trace.
LSan continua desativado por ptrace (`detect_leaks=0`); leaks não validados.

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
./build/odh-bhav '../The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run
ASAN_OPTIONS=detect_leaks=0 ./build-sanitize/odh-bhav \
  '../The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run \
  --global '../The Sims/GameData/Global/Global.far' Global.iff
```

Próximo trabalho: modelo de entidades/slots/person data e contexto de interação,
seguido das primitivas de roteamento/animação usadas por `sit core` (0x1003).
Sprites/paletas e composição Linux seguem como frente necessária ao cenário
funcional, com testes contra o original ainda pendentes.
