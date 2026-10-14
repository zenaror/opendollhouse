# Definições explícitas e estado comum das instâncias

O host pode indexar definições OBJD de fontes explícitas e lê o estado comum
de cada bloco salvo: frames da VM, relações, slots e flags de sprites dinâmicos.
Essa etapa termina antes das partes dependentes de recursos, sem restaurar
threads ou entidades em World. O [snapshot anterior](saved-prefix-validation.md)
continua fornecendo posições, bancos iniciais e a ligação ObjM→objt.

Referência: FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
consultado em `refs/upstream/FreeSO`: `OBJD.cs:382–440`,
`OBJM.cs:340–417,535–610` e `IffFieldEncode.cs`.

## Metadata OBJD e catálogo próprio

`read_object_definition` cobre somente versões136/138, cujo prefixo comum
foi conferido na referência. Lê32 bytes até GUID, preservando ObjectType18,
MasterID20, SubIndex22 signed16 e GUID28 uint32. `is_multitile` equivale
a MasterID não zero, inclusive SubIndex-1. Extent32 e cauda restante são
informados; não é validação completa do OBJD ou execução de seu construtor.
Outras versões são recusadas explicitamente neste reader.

DefinitionIndex possui candidatos e proveniência de fontes declaradas.
Limites padrão:8192 candidatos e256KiB cumulativos de nomes de fonte/asset,
conferidos antes da cópia. GUID ausente, único e ambíguo são estados distintos.
Duplicatas permanecem ordenadas e não escolhem uma precedência presumida
entre arquivos. O índice não carrega BHAV/SPR2/DGRP nem cria entidades.

O host aceita flags repetíveis `--object-definitions FAR` e
`--object-definition-file IFF`, até16 fontes/4096 bytes por caminho.
Lê todos os OBJD dessas fontes (não apenas o primeiro chunk). Tem limites
de8192 entradas FAR,4096 chunks IFF por asset,8192 candidatos e256KiB de
proveniência. Metadata inválida selecionada é erro de recurso, sem skip
silencioso. Sem fontes explícitas o catálogo permanece ausente.
JSON registra proveniência/chunk/layout para todos os candidatos de cada
GUID salvo, com status unique/missing/ambiguous, sem implicar restauração.

FAR agora também tem limite cumulativo de nomes, além do limite individual
prévio. Defaults gerais:4096 bytes por nome e1MiB cumulativo. O catálogo
usa256KiB cumulativo. Todas as entradas serializadas, inclusive nomes
duplicados, consomem esse budget antes de alocar a string.

## Continuação do cursor ObjM

`read_objm_common` exige offset, paridade e extents consistentes com o bloco
e retoma exatamente `prefix.consumed_bits`. Não arredonda para o próximo byte.
Cada campo verifica EOF; não replica o zero-fill da referência.

Sequência:

1. StackCount/StackFlags signed32 e frames;
2. RelationshipFlag signed32 negativo, count e registros;
3. SlotCount signed16, primeiro par como LinkedSlot e os demais como slots;
4. DynamicSpriteFlagCount signed16 e palavras.

Frame tem StackObjectID/TreeID/NodeID signed16, contagens de locals/params
byte, parâmetros antes de locals, PrimitiveState signed32 e CodeOwnerObjType
signed16. Byte usa larguras2/4/6/8, com extensão de sinal e cast uint8;
não usa as larguras de signed16. Relação preserva IsPresent não zero,
TargetID e valores signed32. IsPresent0 e RelationshipFlag não negativo
são recusados, como no perfil de referência consultado.

Counts negativos/excessivos, truncamento e budgets são recusados antes da
reserva de arrays. Defaults por bloco:8MiB,1024 frames,4096 relações,
4096 valores por relação,4096 slots incluindo LinkedSlot,4096 flags de sprite
e65536 campos decodificados. Cada scalar/count/byte consome um campo nesse
budget; isso não é uma contagem de bytes nem uma medição de RSS.
O host ainda impõe65536 campos cumulativos ao conjunto inteiro da casa.

Resultado possui todos os arrays e cursor exato final. JSON separa
`saved_common_records` do World construído e marca
`resource_dependent_state_decoded:false`/`world_entities_restored:false`.
Não retoma a VM nos frames salvos, não liga slots nem publica relações
salvas no backend de runtime nesta etapa.

## Recursos e dados nativos

Objects.far SHA256 `68cadf2f250a50a6227540ee9175cbb937016eed54770bed12ec8c55047ed0c5`
contém743 OBJD com GUIDs únicos:741 versão138 e2 versão136. No catálogo
com apenas essa fonte:

- House01:60 instâncias, todas resolvem GUID único;
- House03:267 instâncias, todas resolvem GUID único;
- House07:180 resolvem, duas faltam: ID105/GUID0x32aa2056 (Bob) e
  ID137/GUID0x3251685c (membro47).

Adicionar os dois IFF de personagem ao catálogo produz745 definições únicas
e resolve todas as182 instâncias de House07. Não houve recuperação por nome
ou substituição de GUID. A ligação usa recursos explícitos e IDs do save;
o caller1 continua apenas no World construído do probe.

Acrescentar ao comando residente de [family-restore-validation.md](family-restore-validation.md):

```sh
--object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
--object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
--object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff'
```

Estado comum comparado independentemente em Python e C++, todos os campos
e cursores das três casas, total509 blocos/14206 campos:

| Casa | Frames | Relações | Slots com LinkedSlot | Flags de sprites | Campos |
| --- | --- | --- | --- | --- | --- |
| House01 | 122 | 0 | 74 | 0 | 1462 |
| House07 | 398 | 96 | 242 | 42 | 5690 |
| House03 | 536 | 0 | 364 | 4 | 7054 |

O restante por instância é24..77 bits em House01,24..2872 em House07 e
24..84 em House03. Continua bruto: comprimentos diferentes exigem os branches
por OBJD/objt e dados de personagens. Auditor local ignorado
`reports/objm-common-audit.py`; `reports/verify-common-state.py` compara os
JSONs reais do host e os resultados completos Debug/ASan.

## Checks e gates Linux

87 checks de metadata OBJD,12 de índice e186 de estado comum sintéticos.
Estado comum com House07 produz916 checks totais (inclui730 nativos).
Formatos têm527 checks, incluindo novos budgets de nomes FAR. Cobertura inclui
signed/cast byte, contagens, truncamento, ownership, cursor/paridade divergente,
LinkedSlot ausente, limites cumulativos, GUID ausente/duplicado e proveniência.
CLI verifica fontes ausentes/únicas/ambíguas, metadata inválida, orçamento de
fontes e ligação de GUID sem mudar as entidades de runtime.

Debug e ASan/UBSan:69 testes passaram,1 X11 pulado (70 configurados).
LSan desativado por ptrace; nenhum teste de hardware ou do executável original.

Gates continuam iguais: residentes Bob/membro47 em House07 executam470
instruções/2 ticks e param em AnimateSim44; Bob visitante em House01/03
executa317/1 tick e para em GotoRoutingSlot45. Recursos resolvidos e estado
salvo decodificado ainda não são aplicados ao runtime.

Relatórios Debug/ASan idênticos byte a byte com somente Objects.far no catálogo,
ignorados em `reports/{bob,member47,house01,house03}-common-native[-asan].json`:

- Bob: `31956b25bf6aa2b2217d89ae1c0b3507ee840cb129e60ced6a88ca41e596eced`;
- membro47: `6c1a46d48b09fbcf869b68a2b8b37279a42b2fcf1326f47e32220b7ed085530a`;
- House01: `527c5ab5855387eb596420d600536e6c0f6871e07e2a5e9196ed3c767fc849bd`;
- House03: `e604b0530de53e7c786b1f713132b5ad103e4cdd712415d62fa61dfb4e27459c`.

Bob com as745 definições, relatórios `bob-common-complete-definitions[-asan].json`,
SHA256 `2ef0465d88e04bd31cc745cab6452df2f697937af10a43d98b9c6e366296da5b`.
Originais conferidos antes/depois e intactos: casas/bairro mantêm hashes do
checkpoint anterior; personagens User00000 SHA256
`900835b71807bac87ed0f431167850a5ed666b6008d7c37739042fd94610b7d5` e
User00001 `7b5fecc81f6101e14589fa94d89e1d3b3f1bb56f695d9bcc847a7ba05497bd50`.

## Próxima restauração

Continuar desde o cursor comum: multitile quando OBJD.MasterID não zero,
portais e personagens conforme OBJT.OBJDType, floats/strings com padding
correto, PersonData por versão e demais campos finais. Usar somente definição
única comprovada no perfil explícito; não adivinhar layout de GUID ausente ou
ambíguo. Depois restaurar identidades105/137, contenção, posições e threads
em World e integrar arquitetura/aparência/animação/render. Preservar distinção
entre dados salvos, adaptação de referência e comportamento original comprovado.
