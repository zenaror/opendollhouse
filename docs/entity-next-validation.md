# Enumeração de entidades do World

SetToNext31 agora consulta entidades do World nos modos0 (todas),1 (avatares),
2 (objetos comuns) e4 (GUID exato da definição). A enumeração de vizinhos5
continua usando NBRS, como um provider separado. Outros modos não são
substituídos por uma busca genérica.

## Referência e seleção

Referência: FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`,
MPL-2.0, `VMSetToNext.cs:72–85,105–173,263–307`,
`VMObjectQueries.cs:182–193,268–272` e `VM.cs:487–501,526–545`.

O cursor é um ObjectID signed16 e não precisa existir como entidade.
A busca retorna o menor ID elegível estritamente maior que o cursor,
sem wrap nos quatro modos. Sem candidato, retorna false e conserva o destino.
Cada chamada consulta o estado atual do World, incluindo remoção e reuso de ID.

Modo4 compara `Entity.definition.guid` exatamente. Não usa `master_guid`
como alias e não expande um mestre em partes multipart. Isso difere do contrato
de IsType32, que tem sua própria política de GUID mestre. Definições ausentes
não entram na busca por GUID; os modos0/1/2 incluem entidades sem definição,
filtradas apenas por kind. Na restauração real do lote, cada definição deverá
ser ligada aos recursos para não perder informação de identidade.

Avatares também são elegíveis em modo4 quando seu GUID corresponde.
Ghost images da referência são representações temporárias separadas,
não modeladas por World nesta etapa. PersonData.IsGhost não é usado para
excluir um avatar lógico da simulação. Entidades fora da tela não são descartadas.

World usa o mapa ordenado já existente: upper_bound(cursor) e varredura até
a primeira correspondência. Não há cache/índice adicional nem alocação na
consulta. O limite padrão4096 entidades continua; a cobertura do teste
32767 é um limite sintético configurado, não medida de consumo do New3DS.

## Operandos e memória

GUID vem dos bytes0..3 em little-endian. Flags byte4: bit7 indica formato novo;
tipo é Flags&127. Novo formato usa TargetOwner byte5 e TargetData byte7,
interpretado como índice byte não negativo. Local byte6 não participa nestes
quatro modos. No legado, destino é sempre StackObjectID10/index0, mesmo que
os demais bytes contenham valores inválidos. GUID só filtra no modo4.

Targets suportados: literal7 como cursor somente, Temps8, Parameters9,
StackObjectID10, Locals25 e callbacks externos explícitos. Scopes30/31
resolvem PersonData usando o temp selecionado. Outputs de frame/temp/Stack
são publicados pela VM; callbacks externos de escrita devem recusar sem mutação.

Target indisponível é erro. Sem candidato, a busca retorna false antes de
tentar escrever, inclusive para um cursor literal. Encontrar candidato para
um destino literal/não gravável dá diagnóstico; nenhuma identidade é inventada.
A referência ignora o booleano de SetVariable; ODH conserva erro explícito para
writes não suportados/recusados. Esse é um limite deliberado de cobertura.

World e dados capturados pelos callbacks precisam viver mais que a VM.
Callbacks/observer não podem reentrar a VM; writes recusados preservam backend.
Observações de sucesso/false só são emitidas para dispatch concluído; outputs
nativos ainda são staged até a VM validar/publicar.

## Host e verificação

O host liga a enumeração de entidades independentemente de `--neighborhood`.
Seu JSON inclui `entity_population` declarando actor/action construídos e
ausência de restauração de entidades salvas. `entity_next` registra contexto,
GUID, modo, cursor, resultado opcional, sucesso e destino. `neighbor_next`
continua separado; um record NBRS de identidade48 não vira World entity48.

Fixtures CLI verificam consulta do avatar por GUID, ausência/preservação,
cursor inexistente, no-wrap, formato legado, destino temp e erro de índice,
isolamento da identidade NBRS e modos0/1/2.

## Gates nativos Linux

Comando base de [avatar-neighbor-validation.md](avatar-neighbor-validation.md),
House01/zoning/initialize/NBRS/seed1; User00000.iff (Bob) e User00003.iff
(vizinho45). Família ativada continua ausente: são visitantes no World
construído, não a restauração do lote ou de seus residentes.

- Bob: Init180+Main129=309 instruções/1 tick, antes290.
- Vizinho45: Init180+Main130=310/1 tick, antes291.
- Help System GUID0x8963d7c1, cursor0/StackObjectID: false.
- Outra consulta GUID0xef122974, cursor0/Local8: false.
- Person1 seleciona actorID1 e depois retorna false no cursor1, sem wrap.
- Object0, depois de novo cursor0, seleciona actorID1.
- Os dois param em PersonGlobals8286 pc29, Expression read scope35/index15,
  operandos `[15,0,0,0,0,2,35,7]`, ainda unsupported. É leitura da
  função de entrada15 da entidade encontrada (StackObjectFunction), não
  novo fault na enumeração nem scope21 StackObjectDefinition.
- Seed final16610512366814951206; saída3, host_failed=false.

Ausências acima são válidas para o World construído do host. Não se conclui
que os objetos estariam ausentes num lote runtime restaurado. Não foram
adicionados recursos ou entidades inventadas para forçar outra branch.

Relatórios Debug/ASan completos coincidem byte a byte, arquivos ignorados
`reports/{bob,family3}-entity-next-native[-asan].json`:

- bob: `fe1e46afd1e21929958f3c871e15e744d1571cb095044fa072e5cc36d4b2ebb2`.
- family3: `0b8ddbd8e1d3379b3f17d076f3704525b0b0f0a6f547146fce48d64a0eb4735f`.

248 checks sintéticos do helper passaram, incluindo alvo byte255, GUID0 explícito,
ordem/live remoção/reuso, definições ausentes, filtros, memória/PersonData indireto,
callbacks próprios, rejeição sem mutação, contexto de code owner e VM real.
Debug e ASan/UBSan:57 testes passaram e1 X11 foi pulado (58 configurados).
LeakSanitizer ficou desativado por ptrace. Não houve teste de console,
execução comparativa do original ou validação de lote jogável.

Próxima dependência: ligar scope35 à tabela EntryPoints da entidade selecionada,
com ActionFunction15 proveniente de OBJD/OBJf conforme UsesFnTable.
VMMemory.cs:144–145 lê EntryPoints[data].ActionFunction; setter de scope35
retorna false. Não é um lookup de code owner nem acesso direto a uma palavra OBJD. Restaurar residentes,
objetos, posições e estado do lote permanece necessário para a versão Linux.


Contrato seguinte localizado: `VMEntity.cs:224–229` usa
GenerateFunctionTable(OBJD) quando UsesFnTable=0; senão, usa o OBJf com o
mesmo ChunkID do OBJD. `GenerateFunctionTable` em444–478 cria33 entradas;
a entrada15 recebe BHAV_Portal. A consulta pertence à entidade StackObject,
não ao owner de código da rotina em execução. O parser atual de ODH ainda
não fornece a tabela completa de funções nem OBJf. Esse suporte continua
pendente; não se retorna um zero genérico para funções desconhecidas.

Para Bob e User00003, a leitura estática confirmou UsesFnTable(byte44)=0 e
BHAV_Portal(byte34)=0: a entrada15 gerada vale0. Não foi derivada de EntityKind.

Há uma dependência de bootstrap além dos dados salvos: HelpSystem OBJD16807
tem Global(byte84)=1 e ObjectType(byte18)=7. TS1ObjectProvider.cs:68,103–105
inclui globals que não são GiftToken em ControllerObjects. O importador
antigo VMTS1Activator.cs:217–220 cria esses controladores OUT_OF_WORLD,
incluindo HelpSystem. No VMTS1ActivatorNew consultado não há loop equivalente
ControllerObjects; são rotas diferentes. A escolha/restauração do importador
e dos controladores precisa ser explícita, sem promover ausência serializada
a ausência no runtime completo. VM.Init sozinho não cria esses controladores.

A auditoria de FAR1 local não encontrou OBJD para0xef122974. Sua identidade
continua desconhecida; não foram inventariados standalone/downloads.
