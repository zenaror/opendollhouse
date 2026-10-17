# Catálogo global de objetos HIT: Track e Patch

Unidade sobre `65d508e`, de 2026-10-17. O caminho nativo de evento tipo1
consulta um objeto por TrackID no manager; ele não procura o Track no HOT
que definiu o evento. Além disso, Track e Patch compartilham esse namespace.
Um índice separado apenas de tracks preservaria incorretamente um Track
quando uma Patch posterior substituísse a mesma chave.

O `NativeHitObjectCatalog` representa o estado final desses registros e passa
a integrar `HitResourceBank::objects()`. A [evidência dos callbacks, tipos e
lookup](hit-native-backend-ownership-evidence.md) documenta registro pela mesma
rotina `0x57d0ce`, mapa `manager+0x80`, substituição e verificação de tipo da
patch após lookup. `GlobalHitList` usa outro mapa, `manager+0xd8`.

## Ordem, identidade e resolução

No perfil `HotReadProfile::native_entry_map`, o leitor conserva
`Hot::native_object_order` com tipo, chave e linha local de cada registro
Track/Patch. O vetor inclui duplicatas e respeita seções intercaladas.
O catálogo percorre esses registros na ordem dos grupos fornecida pelo caller;
a última chave processada vence, mesmo quando o tipo muda. Os recibos de
colisão conservam tipo, grupo e linha anterior/substituta. Não conservam
metadados intermediários: os mapas HOT retêm somente o último valor por
tipo/chave. O índice é uma representação de recursos carregados e imutáveis,
sem reexecutar construtores, destrutores ou efeitos de reload do original.

Os callbacks nativos de Track e Patch ignoram chave zero antes de ler o
payload; o leitor agora faz isso nesse perfil e não gera registro. Suas
chaves usam o mesmo helper numérico já estudado em TrackData: decimal, ou
hexadecimal com prefixo0x ou dígitos A–F. GlobalHitList usa esse helper, mas
admite chave zero e não participa do vetor de objetos. Entradas malformadas
ou fora de uint32 são recusadas; não se emulam falhas do parser original.
O perfil Simitone-local anterior permanece sem vetor de registros.

`lookup_object` retorna o tipo vencedor e sua origem; `lookup_track` e
`lookup_patch` não atravessam uma colisão de tipo. `resolve_track` exige um
Track vencedor com chave CSV7 resolvida e busca essa chave no catálogo global
TrackData. Ausência ou tipo divergente falham sem usar Track local, PatchID
ou HIT ENTP como fallback. Um registro Track não resolvido ainda ocupa sua
chave e mascara definições anteriores; a falha ocorre ao tentar resolvê-lo.

`resolve_global_track_event` lê EventMapping apenas no grupo escolhido pelo
caller. Dali obtém o TrackID global e resolve Track e entrada em etapas:

```text
HOT A: EventMapping → TrackID
HOT B: objeto Track vencedor → chave CSV7
HOT C: TrackData → HIT C + PC local
HOT D: PatchID → objeto Patch vencedor, quando solicitado separadamente
```

Essas origens podem ser diferentes. O resultado mantém `event_group`,
`track.object.group`, ponteiros de metadados e `HitCodeTarget`. A API não
deduz um namespace global de nomes de evento, não resolve overrides de
argumentos automaticamente e não admite/reproduz o evento.
`HitEntryCatalog::resolve_event` permanece como lookup local ao HOT selecionado,
usado pelos diagnósticos anteriores; ele não é autoridade do namespace global
de objetos. Não usar o HOT de origem do Track como namespace geral de Patch.

## Ownership e orçamento

O catálogo recebe grupos HIT/HOT e `HitEntryCatalog` imutáveis e emprestados,
construídos com a mesma ordem. Todos precisam sobreviver a seus resultados.
No banco proprietário, os grupos têm endereços estáveis, o catálogo de entradas
é construído primeiro, e o catálogo de objetos é destruído antes dele.
Os streams de leitura podem ser fechados ao terminar a construção do banco.

Padrões ODH: 64 grupos, 65.536 registros brutos Track/Patch incluindo
substituições, 4.096 recibos de colisão e 256 bytes por nome de evento.
O preflight valida todos os registros e metadados antes de alocar o índice.
O banco também cobra o total bruto após cada HOT, antes de conservar o grupo.
As capacidades são políticas ODH e não medem RSS; overhead dos mapas,
metadados, símbolos e controllers exige orçamento global próprio.

O catálogo rejeita HOT sem ordem nativa registrada; um vetor nativo vazio
é distinto da ausência dessa informação no perfil legado. Tipo desconhecido,
chave zero indevida, ponteiro de recurso nulo ou registro sem metadados também
é erro. O marcador não autentica recursos nem autoriza mutá-los após indexação.

## Verificação

Os testes sintéticos cobrem evento A com Track local conflitante e também
sem Track local, Track global B e HIT C; Patch de outra origem; substituição
por ordem e colisão Track↔Patch dentro e entre HOTs; linhas brutas duplicadas;
ausências, ENTP sem fallback, registros inválidos e limites. O banco é usado
depois da destruição dos streams para resolver evento, Track e Patch globais.
Contagens focais: leitor111, catálogo27 e banco19 verificações estritas.

Recibos desta unidade ficam em `reports/hit-object-catalog/`, fora do Git.
As suítes completas tiveram 118 testes configurados por rodada, 117 aprovados,
um skip X11 e zero falhas: Debug14,80 s; ASan/UBSan26,22 s, LSan desabilitado
com `detect_leaks=0`. Após antecipar a cobrança de registros no banco, ele
foi recompilado e os cinco testes pertinentes passaram novamente em Debug
(0,03 s) e ASan/UBSan (0,08 s). Sem SDL/X11, os cinco targets/testes selecionados
passaram (0,02 s). Não há alegação de suíte completa nessa configuração.

Uma auditoria independente rederivou o PE e conferiu 50 recortes: 13 do
registry de grupos (1.374 bytes) e 37 de ownership/namespace (5.772 bytes),
incluindo os hashes, VA/RVA/offsets, strings e entradas de tabelas selecionadas.
`static-audit.json` e `recheck_manifests.py` conservam o recibo e o reprodutor.

Os quatro probes comerciais Classic/Legacy × Debug/ASan passaram com streams
fechados antes dos lookups. Os 81 arquivos HIT/HOT/HSM foram rechecados iguais
entre as edições; não se transferem VAs ou comportamento entre executáveis.
Na ordem diagnóstica do TSV, há 21.831 registros brutos: 4.316 Track e
17.515 Patch. O catálogo final tem 21.809 chaves, 4.305 Tracks e 17.504
Patches, com 19 substituições de mesmo tipo e três entre tipos. O scanner
HOT independente corroborou essas contagens, lendo LHS/seções/linhas sem
usar os mapas produzidos pelo leitor C++.
Também comparou individualmente todos os 21.809 vencedores `(ID,tipo,grupo,
linha)` contra os dumps C++ nas quatro rodadas. Os quatro dumps têm SHA256
`d21904566714ad9d55ed339555a2e601e68f3b37829b91bc6657dba31f62a20c`.
Os 22 recibos de colisão foram conferidos com o substituto de cada operação,
incluindo suporte a mais de duas definições da mesma chave no reprodutor.

Foram examinados 4.316 EventMappings tipo1 nos grupos selecionados. As duas
APIs resolveram 4.278; nove usam Track global de outro HOT e um muda HIT/PC.
Por exemplo, nessa ordem, `cplant` do HOT PlantCarnivore resolve TrackID6403
do HOT SC4Drinks e PC306, enquanto o lookup local escolhe PC36. Isso prova
a diferença entre as políticas nessa sequência explícita; não estabelece
qual sequência foi observada no jogo original.

O lookup global também encontrou dois casos ausentes, um tipo divergente
e 35 recusas por metadados não resolvidos pelo perfil ODH. Esses diagnósticos
não dizem que os recursos originais sejam defeituosos nem que o jogo falhe.
O evento de aniversário usa o resolver global, chega a key1340/PC16,
Call1347/PC110 e à falha esperada no opcode69, sem efeitos de áudio. Os
probes não completam esse evento. Fontes, scanner, hashes e recibos estão
em `corpus/validation.json` e `corpus/EVIDENCE.txt`.

## Próxima integração

O manager persistente por TrackID continua pendente. Ele precisará usar esse
namespace global, manter contribuições e fila separadas, aplicar admissão,
group gate e geração World, e fornecer Clock explícito. O perfil local do
controller valida patches no HOT recebido; não é correto ligá-lo a um resultado
global presumindo que todas as patches sejam locais. A continuação implementa
um perfil explícito de [binding global e registry compartilhado](hit-global-patch-binding-validation.md),
ainda separado do host.

A [pesquisa do registro de grupos](hit-native-group-registry-evidence.md)
identificou escritores69/71, leitor73 e opcode70 sem ação, mas ainda não
provou o domínio semântico dos índices ou todo seu lifetime. Opcode69 segue
recusado no controller. A [pesquisa de ownership](hit-native-backend-ownership-evidence.md)
também encontrou aliases de um backend com desequilíbrio estático de releases;
não se reproduz uso de memória liberada como política ODH. Nada nesta unidade
comprova runtime original, áudio físico, jogo completo ou funcionamento dos ports.
