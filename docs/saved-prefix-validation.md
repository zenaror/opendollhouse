# Prefixos salvos, posições e tabela de tipos

O [checkpoint de estado comum](object-state-validation.md) posterior acrescenta
frames/relações/slots e catálogo explícito de definições, sem restaurar World.

O host agora decodifica o prefixo de cada instância ObjM e liga seu ObjectID
à entrada serializada de objt. O resultado é um snapshot próprio com
coordenadas, footprint, atributos e bancos brutos. Não publica entidades em
World: o restante variável da instância, recursos OBJD e arquitetura ainda
precisam ser interpretados antes da restauração completa do lote.

## Referências e prefixo ObjM

FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0,
consultado em `refs/upstream/FreeSO`: `OBJM.cs:484–534,729–746`,
`IffFieldEncode.cs`, `OBJT.cs` e `VMTS1ActivatorNew.cs:479–529,555–557`.

`read_objm_prefix` começa no primeiro bit do bloco, após o skip do contêiner.
Campos têm bit0→zero ou bit1→selector de dois bits e valor signed MSB-first.
Signed16 usa larguras5/8/13/16; signed32 usa6/11/21/32. São valores diretos,
sem deltas. A extensão de sinal usa intermediário64 para evitar overflow
em INT32_MIN e casts fora do domínio signed32.

Ordem: footprint MinY/MinX/MaxY/MaxX signed32, X/Y/Level signed32,
UnknownData signed16, attrCount signed16 e atributos,8 temps,68 ObjectData
e5 extras. O prefixo não contém branch por versão nem exige OBJD.
O reader conserva Level bruto; difere explicitamente da validação semântica
1/2 feita pela referência. Esses valores não são aceitos automaticamente
como posições de runtime. UnknownData é conservado sem inventar significado.

Limites padrão por bloco:8MiB e4096 atributos. Contagem negativa/excessiva
é recusada antes de alocar. O mínimo de bits dos atributos e81 palavras
fixas é conferido antes da reserva; todos os campos verificam truncamento.
A referência pode preencher EOF com zeros; ODH recusa bits ausentes.
Os resultados possuem dados independentes do bloco original.

O cursor `consumed_bits` permanece exato, sem arredondar após extras.
`remaining_bits` conserva a extensão ainda não interpretada. Offset relativo
ao byte12 e paridade são mantidos; oddOffset afeta padding de strings
posteriores, sem mudar o início binário do prefixo.

X/Y usam16 unidades por tile na rota de referência. Somente o par(-16,-16)
indica OUT_OF_WORLD. Não se aplica FromBigTile às coordenadas já serializadas.
O JSON informa esse sentinel, sem aplicar posição ou direção a World.

## objt e ligação por identidade

FourCC da tabela é `objt`, chunk0, enquanto instâncias usam `ObjM`, chunk1.
`read_objt` lê header12, depois entradas sem contagem até EOF. Cada GUID0
consome só quatro bytes e continua: não é terminador. Metadados de sentinela
ficam ausentes, sem campos decodificados inventados.

GUID não zero é seguido por seis palavras uint16: Unknown1A, InitTreeVersion,
Unknown2A, MainTreeVersion, TypeID e OBJDType; depois nome bruto terminado
em NUL, padding quando o comprimento do nome é par e um uint32 extra
quando a versão interpretada como signed32 é maior que2. Header/magic/versão,
padding e extra são conservados. Não se converte encoding nem se impõe
whitelist de versão/magic à leitura estrutural.

Limites padrão:1MiB payload,4096 entradas incluindo GUID0,4096 bytes por
nome e256KiB cumulativos de nomes. NUL, padding, extensão e budgets são
validados antes de alocar a string. Como a tabela não tem terminador ou
contagem, não há cauda desconhecida distinguível: leitura bem-sucedida
termina em EOF e restante0; entrada final truncada é recusada.

`entry_for_type` usa entries[type-1], incluindo sentinelas. Type0 ou índice
fora da tabela não resolve. Não busca pelo TypeID bruto do metadado.

`read_saved_object_prefixes` conserva ordem de blocos e resolve
ObjectData11 (signed→uint16) →IDToOBJT →entrada objt →GUID bruto.
Não associa bloco a registro de mapa pela posição. IDs0/duplicados/ausentes,
contagem de blocos divergente e type ausente/sentinela são recusados.
O snapshot só retorna depois de validar todos os blocos, sem resultado parcial.
Limites padrão:4096 objetos,65536 palavras decodificadas (attrs+81 por bloco),
4096 atributos por objeto. Orçamento restante limita atributos antes de ler
cada bloco; são limites configurados, não medição de RSS no New 3DS.

O host exige objt0 quando há ObjM1, independente da ordem no IFF; duplicação
é erro. Sem ObjM mantém fixtures SIMI estáticas e snapshot null. JSON separa
`saved_object_prefixes` com registros completos do World construído e mantém
`world_entities_restored:false`. GUID é a ligação bruta de objt, ainda sem
resolver o recurso OBJD. Não aplica a recuperação especial de personagens
da referência quando GUID inválido combina nome e NeighborID em NBRS.

## Evidência estrutural nativa Linux

| Casa UserData | objt versão | Entradas (com sentinela) | Prefixos |
| --- | --- | --- | --- |
| House01 | 2 | 20 | 60 |
| House07 | 2 | 83 | 182 |
| House03 | 2 | 54 | 267 |

Todos os IDs nativos consultados resolvem entradas não zero. Comparação
independente Python versus JSON C++ verificou todos os campos e bancos de
cada prefixo das três casas, incluindo GUID/type, offset/paridade,
consumed/remaining bits. Relatórios locais ignorados:
`reports/verify-saved-prefixes.py` e `reports/objm-prefix-audit.py`.
O primeiro roda hosts Debug/ASan e compara os resultados com o decoder Python.

House07: Level1/UnknownData1 em182 prefixos; oito OUT_OF_WORLD; X -16..648,
Y -16..744;98 offsets ímpares; prefixos316..451 bits; mínimo158 bits restantes.
Primeiro bloco é ID179/type10/GUID0xf3aaa62f, X280/Y360/Level1,
footprint[352,272,368,288],362 bits. Último é ID203/type41/GUID0x81e6bef9,
X440/Y744/Level1,354 bits. Dados estão decodificados em C++ agora; o restante
dessas instâncias continua bruto.

O comando residente de [family-restore-validation.md](family-restore-validation.md)
agora inclui o snapshot no JSON. Bob e membro47 continuam Init180+Main290=470
instruções/2 ticks, fault AnimateSim44 em8330 pc9 e RNG12162792540164244070.
Bob visitante em House01 e House03 continua317 instruções/1 tick, fault
GotoRoutingSlot45. Nenhum gate avança por posições meramente presentes no JSON.

Relatórios completos Debug/ASan idênticos byte a byte, ignorados em
`reports/{bob,member47,house01,house03}-saved-prefix-native[-asan].json`:

- Bob: `f936311a7fedd8667714d974e1290045f290ccfe88d5bfb34c4febcfb46e2590`;
- membro47: `af7f78255cd471066bcf10b2c127b883449af371df4974e334983b7d69fdcfe9`;
- House01: `78ab0b3e02608f4d1646d721499fe6e5b88855de56e9548ff3229a11890e060b`;
- House03: `54ee3ce6ec2801feff7d46f73bbfdeecbca101c394bb1db47dda63d66b001389`.

Originais conferidos antes/depois e intactos. House01 SHA256
`99b7a9cd585904f1a474f46e0c5f8df9089db57a16ff7535ea15b2d82b29607d`;
House07 `5b162c81135612acfa65628438df2246cd25ad8cb15e2646a28bb32e38de5133`;
House03 `bd7f32bdbc822c54ae5462619082e41d3076e65bd902621555d87288b19d9ce4`;
Neighborhood mantém hash do [checkpoint de inventário](inventory-validation.md).

## Verificação e próxima unidade

2314 checks sintéticos de prefixo,126 de objt e20 de ligação do snapshot.
Prefixos cobrem larguras/limites signed, truncamentos, contagens, ownership,
paridade e fronteira de byte. O teste com House07 acrescenta731 checks,
total3045; essa contagem não é a do CTest sintético. Ligação cobre ordem
diferente do mapa, ID65535, TypeID bruto divergente, cumulativo/per-object
budgets, IDs inválidos, type/sentinel e falha posterior sem publicação parcial.
CLI cobre snapshot e objt ausente/duplicado/sentinela.

Debug e ASan/UBSan:66 testes passaram e1 X11 foi pulado (67 configurados).
LeakSanitizer desativado por ptrace; nenhuma execução em consoles ou comparação
de comportamento com o executável original.

A próxima leitura precisa continuar do cursor exato: stack/flags, relações,
slots, sprites dinâmicos, partes multitile, portais e PersonData, com branches
por OBJD/objt e versão. Resolver GUID ao OBJD antes de interpretar layout
dependente de recursos. Só então restaurar identidade, contenção e posições
em World e integrar arquitetura/aparência/animação/Main/fila/render Linux.
