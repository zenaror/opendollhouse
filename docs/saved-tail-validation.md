# Esquema conhecido completo das instâncias salvas

Checkpoint de 2026-10-14. O reader agora continua do estado comum até o último
campo conhecido de ObjM, usando definições únicas: multitile, portais,
personagens e UnknownInt. Conserva o sufixo opaco restante. Isso completa a
leitura do esquema da referência consultada, sem provar interpretação integral
do formato original ou aplicar o estado à simulação.

Referência FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:
`OBJM.cs:11–38,118–314,612–649`, `IffFieldEncode.cs` e
`IoBuffer.cs:238–248`, consultados no clone local.

## Decoder e limites

`read_objm_tail` recebe bloco, cursor comum, versão ObjM e metadata OBJD/objt.
Valida offset/paridade/extents, GUIDs não zero e iguais, metadata presente,
OBJD136/138 e IsMultiTile consistente com MasterID. A unicidade do candidato
é comprovada pelo catálogo antes da chamada. O cursor inicial é exatamente
`common.consumed_bits`.

MasterID não zero seleciona seis coordenadas signed32 e parentID signed16
de multitile. Tipo objt8 seleciona count e valores de portal. Tipo objt2
seleciona personagem. A seleção usa o tipo **serializado de objt** para
portal/person, conforme a referência; não substitui pelo ObjectType do OBJD.
UnknownInt signed32 encerra o esquema conhecido. Não se inventam campos após
ele: o sufixo restante é copiado MSB-first, com contagem exata de bits e low
bits não utilizados do último byte zerados.

Personagem conserva eventos de animação, Engaged, referências body/head/hands,
texturas, strings desconhecidas, acessórios, animações e estado de rota;
9 floats iniciais,16 motivos antigos e16 atuais; PersonData80 para versão
até0x45 ou256 depois; routing frame count, interações ativa/anterior/fila,
ObjectUses e MotiveDeltas. Campos desconhecidos permanecem brutos.
Interações conservam o prefixo zero obrigatório e todos os14 campos;
TTABIndex-1 não é interpretado como interação ativa válida automaticamente.

Floats são mantidos como bits uint32, sem conversão para números JSON ou
normalização: NaNs, infinidades e signed zero sobrevivem à leitura.
As strings conservam bytes, offset inicial e byte de padding quando presente.
IoBuffer faz um char por byte, sem UTF-8; a contagem de padding corresponde
ao comprimento bruto em bytes.

A leitura de string descarta o restante do byte de campo parcial, ou desfaz
o prefetch quando o cursor está numa fronteira exata. Padding segue a
paridade da referência. `nextField=false` deixa o parser no modo de strings
com odd=false; `true` permite a próxima leitura de campo e reinicia odd=true.
EOF, NUL ou padding ausente é erro, sem zero-fill implícito.

Defaults:8MiB por bloco,65536 campos decodificados,256KiB de conteúdo de
strings cumulativo,4096 bytes por string,4096 valores de portal,1024 acessórios,
4096 interações em fila,4096 ObjectUses e4096 MotiveDeltas. O mínimo de campos,
contagens e budgets é conferido antes de reservar arrays/texto. Strings contam
um campo no budget de campos; conteúdo exclui NUL/padding no budget de bytes.
O host impõe65536 campos e256KiB de strings cumulativos à casa inteira.
São limites configurados, não medição de RSS no New 3DS.

## Integração do host

`--object-definitions FAR` e `--object-definition-file IFF` do
[checkpoint anterior](object-state-validation.md) fornecem o catálogo.
GUID sem definição única permanece `definition_unavailable`, com state null;
não se adivinha o layout nem se retorna um estado vazio fabricado.
Sem catálogo, `saved_tail_records` permanece null.

Quando todos os blocos têm cauda decodificada, o contêiner informa
`known_reference_schema_decoded:true`. Isso é distinto de
`instance_state_decoded:false` e `world_entities_restored:false`, que mantêm
explícito que a semântica completa e sua aplicação ao runtime não foram
concluídas. Cada registro informa ainda `saved_threads_restored:false`.
JSON conserva os campos, floats binários, strings/padding, cursor e sufixo.

## Evidência estrutural nativa

Todos os509 blocos de House01/03/07 foram comparados campo a campo entre
C++ e um decoder Python independente que modela prefetch/odd/bitPos:

| Casa | Multitile | Portal | Personagens | Campos incluindo strings |
| --- | --- | --- | --- | --- |
| House01 | 50 | 0 | 0 | 410 |
| House07 | 80 | 36 | 2 | 1214 |
| House03 | 215 | 97 | 0 | 1869 |

Depois de UnknownInt, todas as instâncias dessas casas deixam0..7 bits,
todos zero. Foram preservados, sem assumir que todo sufixo de outros recursos
será padding. A cauda do contêiner continua `000000000000a3`.

House07 possui personagens salvos105/Bob e137/membro47, ambos com PersonData80.
Há364 bytes cumulativos de strings. Acessórios, fila, ObjectUses e MotiveDeltas
estão vazios nesses dados nativos; versões256 e arrays populados têm evidência
sintética. Ambos possuem Animation
`a2o-aware1;6;1000;25;1000;0;0;0` e BaseAnimation
`a2o-standing-loop;-10;1000;75;1000;1;1;1`.
ID105 termina em3697 bits e deixa7; ID137 em3707 e deixa5.

Auditoria local ignorada: registro arquivado não distribuído e
registro arquivado não distribuído. A comparação verifica todos os scalars,
strings/padding/start_byte, bits dos floats, decoded_words/string_bytes,
cursores absolutos e sufixo. Fontes comerciais foram conferidas antes/depois
e mantêm os hashes do [checkpoint de estado comum](object-state-validation.md).

Comando: probe residente anterior com Objects.far e ambos os IFF de personagem
explícitos. Bob e membro47 continuam470 instruções/2 ticks e fault AnimateSim44.
Visitante Bob em House01/03 continua317/1 tick e fault GotoRoutingSlot45.
O Main executa no World construído anterior; a decodificação não altera seus
estados para atravessar um gate de simulação.

Relatórios completos Debug/ASan idênticos byte a byte, ignorados em
registro arquivado não distribuído:

- Bob: `a530f0fdd9374cfea7d48b3fa3eaac2b123f3932726350a56ddb6224337cbb2a`;
- membro47: `693bcb7f6a91607c19f4a29dd27608bc54ffdb5530178342d262d38f1882de38`;
- House01: `23ccc61766c3f7c697775a8a31eb2830d5d67ff8fb9495db1e71164a332b4ec1`;
- House03: `5732550f8c6883e5118fe457b203bc7f35067a52e8f31dd7cbd67b1306f57730`.

## Checks e importação futura

1481 checks sintéticos passam, cobrindo branches80/256, arrays populados,
8 alinhamentos e ambas as paridades, bytes altos/padding não zero, floats
especiais, budgets, ownership, metadata/cursor divergente e truncamentos.
O teste com House07 produz2216 checks totais (735 nativos adicionais), não a
contagem do CTest sintético. Debug e ASan/UBSan:70 testes passaram e1 X11
foi pulado (71 configurados); LSan desativado por ptrace. Sem comparação de
runtime original ou testes de console.

A próxima publicação de estado deve construir um World candidato inteiro,
com182 IDs originais, antes de ligar VMs. Usar factory com unique_ptr mantém
o endereço estável do World não movível. Validar recursos, níveis1/2, direção
notch0..7, referências, ciclos, budgets e toda a contenção antes de publicá-lo.
Manter(-16,-16) como out-of-world, sem multiplicar coordenadas por16.

Preservar o ObjectData bruto, cinco extras separados, footprint, LinkedSlot,
flags e toda a cauda. ObjectData2/3/11/67 têm getters derivados no World atual
e exigem importação explícita. LinkedSlot é o primeiro par salvo, e runtime
slot0 corresponde ao par seguinte. `attach` conserva posições dos filhos;
`place_in_slot` as troca pela posição do pai e não serve para importar o save.

Motivos atuais precisam de adaptação declarada e importação sem o clamp do
setter: a referência faz Math.Round ties-to-even antes de converter para
signed16. Conservar ambos os bancos brutos, validar finitude/faixa e não
inventar zeros. Relações salvas exigem importação atômica no backend, ligada
à geração real de cada entidade. Conservar offsets/grupo/parent de multitile
e resolver master no recurso, sem inferir GUID por aritmética.

Frames e filas salvos devem permanecer dormentes até existir uma continuação
de VM validada. A conversão de referência preenche temps8→20, ajusta args,
reconstrói Callee via ObjectUses e reinicia certos primitivos de rota; essas
são adaptações FreeSO, não fatos demonstrados do TS1 original. Arquitetura,
aparência, animação e Main/fila/render seguem necessários ao lote Linux funcional.
