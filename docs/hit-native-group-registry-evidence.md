# Registro de `seqgroup` no HIT nativo

Evidência estática de 2026-10-17, sobre ODH `65d508e`. O registro em
`manager+0x1a4` guarda TrackIDs por índice resolvido do bytecode. Os escritores
diretos demonstrados são `seqgroup_kill` (69) e `seqgroup_return` (71).
`seqgroup_wait` (70) vai direto ao epílogo nesta imagem; `seqgroup_trkid`
(73) lê uma entrada e escreve o valor em variável da thread. A parada de um
controller não apaga sua entrada. Ainda não há prova de um domínio semântico
de índices ou de uma invariante que garanta lookup para todo TrackID salvo.

## Fonte e método

Imagem externa `../referencias/The Sims/Sims.exe`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`,
PE32 x86, base `0x400000`. Nos trechos `.text` e na tabela selecionada, RVA e
offset bruto coincidem com `VA−0x400000`; `0x6f6a78` é endereço de global em
runtime, não offset bruto. O arquivo não foi executado ou modificado.

Foram consultados primeiro os exports existentes
registro arquivado não distribuído,
`0057dac6.{c,asm}.txt`, `0057dc59.{c,asm}.txt` e os documentos
[opcode 69](hit-native-group-kill-evidence.md),
[identidade de TrackID](hit-native-track-identity-evidence.md) e
[lifecycle](hit-native-controller-lifecycle-evidence.md). A seleção de
assembly reproduzível, com bytes, VAs, RVAs e offsets brutos, está em
registro arquivado não distribuído; seu gerador
`make_selected.py` exige o SHA256 acima. Nenhum projeto Ghidra foi alterado.
A inspeção não demonstra execução do jogo nem áudio físico.

## Alocação, capacidade física e índice

`0x571717` (RVA/raw `0x171717`) aloca `0x21a8` bytes para o manager. O
construtor `0x57ca50–0x57ca6d` (RVA/raw `0x17ca50–0x17ca6d`) zera
`0x80b` DWORDs de `manager+0x17c` até o fim da alocação. O primeiro
endereço de registro usado pelos handlers é `manager+0x1a4`, diferença de
dez DWORDs. Restam **2049 DWORDs fisicamente endereçáveis a partir daí**,
índices aritméticos não negativos `0..2048`, com a última palavra em
`+0x21a4`. Esse cálculo descreve a extensão do objeto: não prova que 2048
seja um índice de sequência válido ou que todas as palavras até lá pertençam
ao registro segundo o formato do jogo.

Nos handlers 69, 71 e 73, o byte operando localiza uma variável (1–127);
o getter vtable `+0x88` retorna seu valor de 32 bits, que passa diretamente
para `manager+0x1a4+4*valor`. Não há checagem local de sinal, máximo ou
overflow após o getter (`0x57f209–0x57f21f`, `0x57f303–0x57f31b`,
`0x57f37b–0x57f38b`). Valor negativo pode apontar para campos anteriores
do manager; valor alto pode sair do objeto ou dar wrap na aritmética x86.
Zero como **valor da variável** indexa a entrada zero; zero como **byte
operando** segue o erro do comando. O helper de leitura de byte >127 tem
o comportamento diagnóstico descrito em [opcode 69](hit-native-group-kill-evidence.md),
não uma validação segura do índice.

O HSM `SoundData/SimsGeneratedHitSource.hsm`, SHA256
`ca6bef035809b2cdacdc2c14a98582cd4c4702c53cc6640beceaa548c06d55b6`,
nomeia os opcodes e constantes `GroupMusic=1`, `GroupDialogMain=2` e
`GroupDialogOverlay=3`. Esses nomes não provam que o índice de `seqgroup`
seja o mesmo campo ou se limite a 1–3. A capacidade física tampouco é
limite de segurança nativo. Um provider ODH precisa declarar seu próprio
contrato e rejeitar acessos fora dele.

## Jump table, escritores e leitor

A tabela de dispatch em `0x57f708` (RVA/raw `0x17f708`) usa `opcode−1`.
As entradas foram verificadas pelos bytes da imagem:

| Opcode / nome HSM | Entrada VA (RVA/raw) | Destino VA | Efeito demonstrado |
|---|---:|---:|---|
| 69 `seqgroup_kill` | `0x57f818` (`0x17f818`) | `0x57f1ec` | Lê ID anterior, pode parar/fazer fade nele, sempre escreve ID corrente. |
| 70 `seqgroup_wait` | `0x57f81c` (`0x17f81c`) | `0x57f701` | Epílogo direto; não lê byte adicional nem registro. |
| 71 `seqgroup_return` | `0x57f820` (`0x17f820`) | `0x57f2ee` | Lê ID anterior, pode parar o caller, sempre escreve ID corrente. |
| 73 `seqgroup_trkid` | `0x57f828` (`0x17f828`) | `0x57f35b` | Lê a entrada e passa valor ao writer de variável. |

Opcode 69 grava em `0x57f2d1–0x57f2dc` (RVA/raw
`0x17f2d1–0x17f2dc`) tanto no ramo vazio/igual quanto no ramo de parada
imediata ou transição. Na transição, restaura o PC do **caller** ao início
da instrução e agenda `caller.deadline=manager.Clock+2` antes do store
(`0x57f2b9–0x57f2cb`). Sem outro escritor no intervalo, o retry encontra
o próprio TrackID e avança. A entrada é o último ID escrito; sua presença
não prova parada do alvo nem término do fade.

Em 71, `0x57f323–0x57f32c` testa se a entrada é zero ou igual ao TrackID
do caller. Se diferente, chama lookup `0x573466` com o ID antigo em
`0x57f32e–0x57f335`, **ignora o EAX resultante**, e chama slot `+0x38`
com `ECX=ESI`, a própria thread emissora, em `0x57f33a–0x57f33e`.
Não para o objeto obtido pelo lookup. Depois, `0x57f341–0x57f349`
grava o TrackID corrente mesmo quando o caller acabou de parar. `+0x38`
reseta PC/deadline e o tira da fila; o laço do poll reavalia deadline e
não avança naturalmente à próxima instrução nesse caso. Se a entrada era
vazia ou igual, o handler grava o mesmo ID e pode continuar no mesmo poll.

Em 73, há dois bytes operandos. O primeiro localiza variável cujo valor
indexa o registro; o segundo fica em `EBX` como localização de destino
(`0x57f35b–0x57f38b`). `0x57f38b` empilha a palavra do registro e
`0x57f4ac–0x57f4af` a escreve via vtable `+0x84` no destino, com
argumento auxiliar zero. O trecho não valida existência do TrackID salvo.
O destino zero/fora da faixa depende do writer e do diagnóstico de byte;
não presumimos efeito seguro para entrada malformada.

Uma varredura das referências diretas a `+0x1a4` no corpo HIT
`0x57xxxx` encontrou os acessos do PC da thread (`this+0x1a4`) e os
três handlers acima. Nenhum outro store direto a uma entrada
`manager+0x1a4+4*índice` apareceu nessa região. Isso não elimina alias
indireto fora dela.

## Vida do TrackID e limpeza

`manager+0x80` é um catálogo separado, chave TrackID → controller.
`0x573466` (RVA/raw `0x173466`) faz lookup sem criar e retorna zero
para chave ausente. `0x57d0ce` (RVA/raw `0x17d0ce`) insere/substitui
por chave; na substituição, o objeto antigo é destruído antes de
`node+8` receber o novo ponteiro (`0x57d10c–0x57d128`). O registro
`seqgroup` contém o **número TrackID**, não um ponteiro. Substituição
com a mesma chave muda o objeto ao qual esse número resolve.

A parada `+0x38` em `0x57e037–0x57e0dc` (RVA/raw
`0x17e037–0x17e0dc`) desativa o controller, libera binding, zera
fade/isplaying, restaura PC, zera deadline e o retira da fila. Não toca
nas palavras do registro do manager nem remove por si a chave TrackID.
O teardown `0x57ccc8–0x57cd6f` para os objetos, destrói controllers
e limpa catálogo; não faz limpeza individual das entradas antes da
liberação do manager. Encontrou-se um único call direto ao inicializador
que zera o registro, em `0x571743`, no caminho de criação do manager.
Não foi estabelecido reload/reconstrução parcial que preserve ou limpe
essas palavras.

Consequência condicional: a entrada pode permanecer com TrackID de
controller inativo. Opcode 69 ainda faz lookup e usa ID19/fade_on do
objeto; não testa ativo. Se a chave saiu do catálogo, lookup retorna
zero e `0x57f24e–0x57f254` dereferencia o resultado sem guard local.
Em 71 o lookup obsoleto é ignorado antes da parada do caller; em 73
o valor numérico é escrito no destino. Não declaramos que chave ausente
seja alcançável no fluxo ordinário de recursos válidos, nem uma invariante
de retenção comprovada. Entrada de grupo não equivale a owner World ou
geração de objeto no ODH.

## Continuidade e fronteira de implementação

O poll `0x57dac6` avança fade antes do bytecode, captura
`manager.Clock` de `manager+0x58`, e executa enquanto deadline não zero
e não superior ao Clock. O manager `0x57d230` incrementa Clock se flag
`+0x54` permite. Após cada handler, `0x57dbdc–0x57dc20` ignora AL
e volta a testar deadline. Em 69, vazio/igual ou parada imediata
podem continuar no mesmo poll; transição marca retry após `Clock+2`.
Em 71, vazio/igual pode continuar, enquanto parada do caller zera
deadline. Opcode 70 é epílogo sem espera. Não se demonstrou conversão de
`Clock+2` para World ticks, milissegundos ou tempo de dispositivo.

Para a unidade de runtime, o contrato precisa manter registro
compartilhado por índice, identidade TrackID, lookup separado de
presença/atividade e resultado explícito `continue`/`retry` de 69.
Requer ainda política própria de índice seguro, coexistência com
71/73, vida do catálogo e provider compartilhado do writer/getter.
Sem esses elementos, manter o gate de opcode69 é a decisão sustentada.
Faltam prova do domínio semântico dos índices nos recursos, invariantes
de remoção/reload do catálogo, efeitos de providers compartilhados e
validação dinâmica do executável/áudio.
