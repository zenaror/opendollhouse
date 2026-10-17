# Call7 e pilha de retorno do controller HIT nativo

Contrato delimitado em 2026-10-17 para `NativeHitController`. A extensão executa
Call7 com um mapa fornecido pelo caller e permite que Return8 e End12 retornem
de chamadas aninhadas. Ela não resolve automaticamente o catálogo nativo do
manager nem amplia os opcodes de áudio e de grupo já suportados.

Extensão posterior nesta data: a investigação do loader demonstrou que o valor
nativo é um ponteiro para um dos buffers HIT independentes. O controller agora
também aceita `NativeHitResourceResolver` com `{HitFile*, PC}` e guarda essa
identidade nos retornos e no trace. O resolvedor local abaixo permanece como
perfil explícito compatível com os testes anteriores. A resolução global e o
novo perfil HOT estão em
[hit-native-entry-map-validation.md](hit-native-entry-map-validation.md).

## Proveniência e alcance

A evidência nativa é a leitura estática de `../referencias/The Sims/Sims.exe`,
SHA256 `6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`,
base PE `0x400000`. Os bytes selecionados estão em
`reports/hit-native-controller-execution-selected.asm.txt`, SHA256
`f0171402d4637498e9ada7449c7aec0303faa59e1d66eed97cd7c7f05048d9a8`;
a interpretação anterior está em
[hit-native-controller-execution-evidence.md](hit-native-controller-execution-evidence.md),
SHA256 `635e6f322f12efe922b3143872c5c4f0da58ca8573129d0005f6c8749707154f`.
A prova de stop e restart está em
[hit-native-controller-lifecycle-evidence.md](hit-native-controller-lifecycle-evidence.md)
e em `reports/hit-native-controller-lifecycle-selected.asm.txt` e
`reports/hit-native-retrigger-selected.asm.txt`. Nenhum resultado de execução
do `Sims.exe` decorre dessa análise.

Simitone `4906db3156616bfaffc10ce0963cf08062975fef` é a referência técnica
principal para TS1; seu frontend ativa `FSOEngineMode.TS1` e `HITVM` em
`refs/upstream/Simitone/Client/Simitone/Simitone.Client/SimitoneGame.cs`.
No submódulo FreeSO `f86bbd34b2112296ed786741f640a0535886d180`, o
`HITInterpreter.cs` remapeia Call pelo HOT `TrackData` no modo TS1. Ali,
Return8 encerra a thread, enquanto End12 pode desempilhar. Esse desvio não
substitui a jump table nativa: opcodes 8 e 12 apontam ambos para `0x57e6e7`.

## Chave, destino e retorno

O handler Call7 em VA `0x57e6bf`, offset de arquivo `0x17e6bf`, lê os quatro
bytes seguintes como DWORD little-endian completo, sem reduzir a chave a
8 ou 16 bits. Avança o PC em quatro bytes; o endereço imediatamente após o
operando é o retorno. `0x57d0a9` (offset `0x17d0a9`) consulta a chave no mapa
`manager+0x168` por `0x582457`, lê o DWORD associado e entrega esse ponteiro de
bytecode como destino. O handler só então coloca o retorno na lista `+0x1a8`
e instala o destino no PC. O operando não é endereço absoluto.

O HOT CSV7 `@symbol` do track é armazenado em `this+0x31c`; o initializer
acessa esse objeto através de `controller+0xc4`, inicializado como self no
construtor e passível de apontar para um owner compartilhado. O initializer
usa essa chave no mesmo mapa para definir PC e loop-PC. Isso não equivale a
escolher inicialmente uma patch ou hitlist: o binding inicial usa ID50.
`NativeHitController` continua recebendo `entry_pc` já resolvido pelo caller;
o novo resolvedor só atende às chamadas executadas pelo bytecode.

A API recebe `NativeHitCallResolver`, função de chave `uint32_t` para
`optional<uint32_t>` com offset de bytecode. Ausência de callback, chave sem
destino, destino fora da região de código e instrução truncada produzem falha
explícita no ODH, sem interpretar a chave como PC e sem inserir zero como
fallback. A leitura nativa do hash pode inserir uma chave ausente; não há
contrato suficiente para reproduzir com segurança seu estado residual.
O caller mantém vivo o mapa capturado e não reentra no controller durante o
callback. O ODH valida o destino antes de empilhar o retorno.

O parser ODH aceita PCs em `[16, primeiro ENTP)` quando há tabela ENTP, ou
`[16, tamanho do HIT)` sem tabela. Os limites de recurso padrão são 1 MiB de
HIT e 8192 entradas ENTP. O destino Call obedece à mesma região. O retorno
salvo é o cursor pós-operando; um retorno que chegue ao fim da região só se
torna inválido quando se tenta buscar outra instrução.

Return8 e End12 compartilham o handler nativo: com pilha não vazia, removem
a entrada mais recente, restauram o PC e continuam; com pilha vazia, pedem
parada pelo slot `+34`. A capacidade da pilha portátil é política ODH:
`NativeHitLimits::call_depth` vale 64 por padrão e aceita 1..256. Esgotar a
capacidade falha explicitamente. A evidência estática não estabelece um
limite de profundidade nativo.

Stop pelo slot `+38` limpa `+0x1a8`. No restart ativo com ID55 diferente de
zero, o initializer restaura PC/loop-PC e limpa as listas `+0x1b0/+0x1b4`,
mas preserva `+0x1a8`. Assim, um retorno posterior pode desempilhar um PC da
execução anterior. A implementação portátil deve preservar essa distinção;
`activate` inicial começa sem entradas de retorno, e `stop()` as descarta.

## Probe de recurso e verificação

`reports/hit-native-call/resource-probe.cpp` define um probe diagnóstico com
os recursos `Downloads/BirthdayCake/bcakesnd.{hsm,hot,HIT}` fornecidos pelo
usuário. Suas identidades estão em
`reports/hit-native-call/resource-identities.json`. O probe fornece
explicitamente o `TrackData` do HOT local ao resolvedor. A entrada sintética
do controller é PC16 com patch0, sem binding de áudio. Os bytes HIT em PC34
são `07 43 05 00 00`: Call7 consulta `0x543` (1347), salva PC39 e deve ir
ao PC110 pelo mapeamento HOT local `0x543=0x6e`. O byte em PC110 é opcode69,
ainda não suportado por este controller; o probe espera falha explícita nele,
com uma entrada na pilha e zero efeitos de áudio. Esse caminho não demonstra
que o manager nativo escolheria o mesmo mapa, nem execução de áudio real ou
compatibilidade do evento completo.

Os 101 checks do controller passaram, incluindo as 82 verificações anteriores,
chamadas aninhadas, Return8/End12, chave ausente, destino inválido, truncamento,
limite de pilha e reentrada absorvida pelo resolver. A regressão de restart
continua a execução e consome um retorno herdado; a recursão com capacidade128
alcança o diagnóstico nativo na instrução102 e confirma a limpeza da pilha.
O build direto passou com C++17, `-Wall -Wextra -Werror -Wconversion
-Wsign-conversion -pedantic` (`core-strict.log`).

As suítes CTest Debug e ASan/UBSan configuraram 115 testes cada: 114 passaram,
um skip X11 e zero falhas, em 66,77 s e 85,49 s respectivamente. LSan foi
desativado no ambiente de sanitizers. Logs completos estão em
`reports/hit-native-call/{debug,asan}-{build,ctest}.log`; a recompilação final
dos testes está em `{debug,asan}-target-final-build.log` no mesmo diretório.

O probe acima passou nas duas instalações externas (anterior e Legacy), em
Debug e ASan/UBSan: uma resolução de chave1347 para PC110, quatro instruções
concluídas no trace, pilha com uma entrada e falha esperada em opcode69,
sem efeitos de áudio. Os recibos são `resource-{classic,legacy}-{debug,asan}.json`
no mesmo diretório. Usar dados reais nesse diagnóstico não equivale a executar
o evento completo ou provar a política do manager. Nenhum `Sims.exe` foi
executado; não houve teste de áudio físico ou de consoles.

## Lacuna do manager

Na investigação inicial, faltava provar a origem do mapa. A
[pesquisa posterior do loader](hit-native-entry-map-evidence.md) demonstrou
`TrackData` com substituição da última definição processada e buffers HIT
independentes. Não foi observado merge ENTP nesse caminho; a ordem automática
dos grupos continua pendente. O parser ODH conserva `Hot::track_data` e
`HitFile::entry_points` separadamente. A escolha explícita da ordem pelo caller
evita atribuir ao executável original uma regra de descoberta não demonstrada. Faltam também
o teto nativo da pilha e a semântica completa de falha do hash quando a chave
não existe. Esses pontos limitam alegações de fidelidade do manager, mas não
impedem o subconjunto portátil com mapa explícito e limites ODH declarados.
