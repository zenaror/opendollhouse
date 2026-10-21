# Funções de entrada da entidade

O [checkpoint de família e ObjM](family-restore-validation.md) posterior
documenta o caminho residente em House07; os gates visitantes abaixo
permanecem evidência do perfil sem ativação.

O host agora liga uma tabela própria de funções à entidade e fornece a leitura
StackObjectFunction35. A seleção usa a entidade StackObject do frame, não
CodeOwner, Caller, Callee ou EntityKind. Uma ação0 válida difere de uma tabela
ausente e de um índice fora do banco; escrita scope35 continua unsupported.

## Resolução estática de Portal15

`saved_portal_function` seleciona o índice15 da tabela de EntryPoints do
recurso exato e conserva `source`, `asset` e `chunk_id` da definição para uso
posterior em um catálogo de `CodeOwner`; esses campos são proveniência do
asset, não IDs de entidade do `World`. `saved_portal_action` aceita apenas
ações privadas no intervalo 4096–8191 e lê/parsa a BHAV do mesmo asset.
Ação zero, BHAV ausente e IDs globais/semiglobais falham fechados até existir
um resolvedor de banco correspondente.

O teste focal usa um IFF sintético com `OBJf` e BHAV 5015: confirma a
proveniência, o parse da rotina e os casos zero/ausente. Isso não executa a
BHAV, não valida um portal do lote e não prova o dispatch do executável
original. O teste do backend X11 é independente deste reader.

`saved_portal_vm_call_plan` avança essa fronteira para um `SavedWorldEntity`
tipo portal: confere caller avatar, identidade do portal vivo, EntryPoint 15
copiado para a entidade e definição única no `DefinitionIndex`. Retorna a
closure privada alcançável, argumentos zerados segundo a metadata e um `VmCall`
com Caller=avatar e Callee/StackObject=portal. O caller fornece um `CodeOwner`
não zero; a função não o deriva do ID da entidade. Chamadas globais são
listadas em `global_dependencies` para validação pelo banco compartilhado do
host. ConditionFunction não zero e qualquer dependência semiglobal falham
fechados nesta fatia.

O plano ainda não instala a closure no VM nem chama
`Routing::call_external_frame`; não há descoberta automática de portais,
replanejamento entre salas ou navegação jogável. `odh-saved-entry-point-resource`
valida este contrato apenas com IFFs sintéticos.

## Referência e readers

FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:
`VMEntity.cs:224–228,444–480`, `VMMemory.cs:144–145,585–586`,
`OBJf.cs:24–31` e ordem dos campos em `OBJD.cs`.

UsesFnTable0 gera33 entradas com ConditionFunction0; entradas0..29 usam os
campos BHAV do OBJD, e30..32 têm ActionFunction0 explícito na referência.
O reader próprio cobre o prefixo OBJD138 até Repair(byte160),162 bytes.
Todos os33 índices são produzidos, incluindo Init0, Main1, Load2, Dynamic8,
Portal15, Sit26, Stand27. A cauda do OBJD é registrada como extent restante;
não se afirma validação integral de seus demais campos/versões.

UsesFnTable não zero escolhe OBJf com o mesmo ChunkID do OBJD, dentro de seu
recurso. O header tem reserved/version, assinatura `fJBO`, count uint32;
cada entrada tem ConditionFunction e ActionFunction uint16 LE.
Valores/version/reserved brutos e extensão restante são preservados.
A versão é metadado estrutural, sem whitelist ou transformação suposta.

Limites padrão:256 entradas,64KiB payload,1024 bytes decodificados.
Contagem/bytes/truncamento são validados antes de reservar o vetor.
Unsigned function IDs são preservados no recurso; scope35 retorna a mesma
representação signed16 que o cast da referência.

Não se aplicam overrides especiais do construtor para GUID0xa9bb3a76,
tuning ou live reload. O reader representa os campos authored; os avatares
dos gates nativos não têm esse GUID. Não é implementação completa do construtor.

## Armazenamento e dispatch do host

World exige entidade existente e tipada para definir sua tabela. Copia pares
condição/ação; no máximo256 pares, cada par consome duas palavras dos limites
por banco e compartilhado. Validação e cópia precedem publicação.
Repetir a mesma tabela é idempotente; trocar por outra é recusado. Tabela
explicitamente vazia é bound, mas não produz zeros para índices inexistentes.
Remoção devolve sua contabilidade; reuso do ID não herda a tabela antiga.

O host seleciona OBJf independentemente de sua posição relativa a OBJD no IFF.
Ausência/duplicação do OBJf selecionado é erro de recurso; não há fallback para
funções brutas do OBJD quando UsesFnTable pede OBJf. Um OBJf não usado pelo
perfil legado não é interpretado como override.

Main, validação de Init/Dynamic e execução das etapas usam ActionFunction1/0/8
da tabela selecionada. BodySTR continua vindo do OBJD. A referência
ExecuteGenericEntryPoint(`VMEntity.cs:567–620`) despacha ActionFunction nessas
etapas, sem executar ConditionFunction; os pares completos são conservados.
O JSON informa tabela, source/version/cauda, pares, Main selecionado e
`objd_main` bruto, que pode ser diferente em recursos OBJf.

O host mantém os limites anteriores: OBJD138/type2/adult BodySTR e Main privado,
recursos globais/semiglobais explícitos, startup síncrono e orçamento agregado.
Probes antigos com prefixo OBJD curto podem executar sua rotina, mas não recebem
uma tabela completa inventada. Ações opcionais recebem a própria tabela quando
ela é fornecida pelo recurso dentro do perfil. Outros OBJD legados continuam
com seu perfil parcial anterior, sem ampliar silenciosamente esta cobertura.

## Evidência nativa de recursos

Leitura estrutural de Objects.far encontrou OBJf version0/count30 em16 tabelas
e version0/count31 em438. HelpSystem.iff OBJD16807 tem UsesFnTable1 e usa
OBJf16807/version0/31 entradas, reserved0/tail0, Portal15 condition/action0.
Logo HelpSystem desta instalação usa OBJf, embora seus campos legados também
contenham Portal0. A marca Global1 continua indicando controlador na rota
antiga de importação, separada da presença no save e do World atual do host.

Personagens UserData consultados usam tabela gerada33. Bob e User00003 têm
UsesFnTable0, Portal15=0, Main4096, Init4097, Load4098, Cleanup8210,
Dynamic8=0 e Gardening16=2. Tail54 após o prefixo162.
Scope35 lê o campo Portal, não ObjectType2.

## Gates nativos Linux e verificação

Comando base de [avatar-neighbor-validation.md](avatar-neighbor-validation.md):
House01/zoning/initialize/NBRS/seed1, Bob/User00000 e vizinho45/User00003.
Continuam visitantes em World construído, sem família ativada ou entidades
restauradas da casa.

- Bob: Init180+Main137=317 instruções/1 tick, antes309.
- Vizinho45: Init180+Main138=318/1 tick, antes310.
- Portal15=0 lido por scope35; ambos ultrapassam esse gate.
- Param em PersonGlobals8286 pc44, primitive45 GotoRoutingSlot,
  operandos `[8,0,2,0,0,0,0,0]`. Caller e StackObject são actor1.
  Saída3, host_failed=false: rota ainda não suportada no host.
- Seed final16610512366814951206. Tabelas conservam seus pares iniciais.

Relatórios Debug/ASan completos coincidem byte a byte, arquivos ignorados
`reports/{bob,family3}-entry-points-native[-asan].json`:

- bob: `4abb1300e7d3e37afe33dd7df5adbb98c41e2410bda239a7e2fbb188a447d9be`.
- family3: `3437674f3a95cf61770b5a960a8c39ff9b8156a4a7c10c331202b49080cee57f`.

303 checks dos readers e68 de World passaram. Cobrem33 campos distintos,
truncamentos, versão/prefixo, signed/unsigned IDs, seleção Portal34 versusType18,
OBJf version/reserved/magic/tail/counts e limites, ausência/zero/empty, ownership,
rebind idempotente, orçamento/removal/reuse, contextos e Expression na VM real.
Fixtures CLI cobrem OBJf antes do OBJD, dispatch escolhido em vez de campos
OBJD deliberadamente diferentes, conditions preservadas, tabela ausente ou
duplicada/mismatching, índice inválido, escrita recusada e probes curtos.

Debug e ASan/UBSan:59 testes passaram e1 X11 foi pulado (60 configurados).
CLI foi repetido depois das correções de validação de startup e metadado Main.
LeakSanitizer ficou desativado por ptrace. Sem execução comparativa do
original, lote jogável ou console.

## Próxima dependência espacial

VMGotoRoutingSlot.cs:13–29,53–57 interpreta Data:uint16=8, Type:uint16=2
(Global), Flags:byte=0. VMMemory.cs:731–747 resolve Global.iff SLOT100,
o nono registro type3. Não é SLOT privado nem argumento da rotina.
Flags operand0 permitem failure trees; bytes5..7 não participam da leitura.

A leitura estrutural do registro nativo encontrou SLOT version10, offset0,
standing1/sitting0/ground0, flags0xa0ff, snap-1, proximity16/64/16, gradient0,
height0, facing-3 (FaceAnywhere), resolution8, maxSize0/unknownI10=0.
Flags do SLOT incluem oito direções, RandomScoring e AllowDifferentAlts.

O bridge atual em routing.cpp:41–52 cobre uma circunferência fixa com flags0x55,
proximity igual, gradient0x3e400000, facing-2 e resolution16, e exige ator/alvo
já posicionados no mesmo nível. Portanto não cobre SLOT8. As posições/grid
de tools/bhav --world-probe são fixtures, não restauração nativa da casa.

A referência VMSlotParser e VMRoutingFrame exige posições, salas/arquitetura,
obstáculos/portais, candidatos na grade e RNG. O estado avança por ticks,
movimento e animação. Falhas com trees habilitadas podem chamar global398
com failureCode/blocker, gravam PersonData.RouteResult62; chegada grava
RouteEntryFlags37 e aplica facing. Não se atribuem esses resultados para
recursos espaciais que o host ainda não inicializou.

A próxima integração precisa de estado espacial do lote e suporte real ao
perfil SLOT8, além da escolha explícita de residentes/FAMI e controladores.
Não substituir posições ausentes pelas coordenadas do probe para alegar que
o Main nativo passou a rotear no lote.
