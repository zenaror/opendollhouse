# Teste de tipo e pedidos de som — 2026-10-13

Esta etapa corrige a identificação dos opcodes e executa as branches reais
da cadeira após xevt100. O trace contém números decimais:32 é TestObjectType
(0x20),23 é PlaySound (0x17). CreateObjectInstance é42 (0x2A). A indicação
anterior de criação32/posicionamento23 estava errada; foi corrigida no README,
no documento anterior e na fonte OMM. O trace anterior continua válido.

## Identidade de entidade e primitiva32

World agora associa uma identidade de definição explícita a cada entidade:
GUID e master GUID opcional. Ausência de master significa que o chamador declarou
conhecida a identidade de uma peça única. A associação é idempotente e não pode
ser substituída por outro GUID numa entidade viva; remoção/reuso do ID descarta
a identidade. Isso não cria objetos nem inicializa automaticamente um lote.

O prefixo OBJD já lido também fornece MasterID (offset20) e SubIndex assinado
(offset22). O probe só declara a identidade da cadeira com MasterID0; peças
multitile exigem um recurso master ainda não fornecido nesse CLI.

`object_type_primitive` lê GUID uint32 LE, IdData int16 LE e IdOwner byte6;
byte7 não é interpretado. Resolve ID nas variáveis explícitas do frame/temps ou
World, e compara GUID direto ou master conhecido. Objeto, definição, scope ou
índice ausente gera diagnóstico, não false inventado. Não muda posição, slots,
StackObject, atributos ou ID. Não implementa uma factory.

No corpus externo, `Objects.far/DiningChairs.iff` contém:

| GUID | OBJD ID | Label |
|---|---|---|
| 59C389BC | 16812 | Chair - Office |
| 26BFBB29 | 16808 | Chair - Dining - Cheap |

Os dois payloads têm216 bytes, versão138, MasterID0/SubIndex0. A busca de GUID
em FARs foi exploratória, por leitura; a ligação runtime usa o OBJD explícito
selecionado, não precedência inferida. Identidade do IFF SHA256:
`cffff7b4e8b5d1b31351c32a2f8656f50dbf72a82217bf38b4828853fc6f90c7`.

## FWAV e primitiva23

FWAV armazena nome bruto de evento terminado em NUL. O leitor limita o texto a
65.536 bytes e reporta o tail; truncamento ou ausência do terminador é erro.
Não interpreta samples ou HIT. EventID é o ID do chunk FWAV, não índice STR#.
O CLI resolve primeiro no IFF configurado como CodeOwner e depois no recurso
global explícito, rejeitando IDs repetidos dentro de cada recurso.

O serviço23 lê EventID/sample rate uint16 LE, flags e volume; ignora os dois
bytes finais. Preserva rate/volume/AutoVary/SimSpeedAffects como metadata. Source
é StackObject quando flag2 está presente, Caller caso contrário; bits1/4/8
significam loop/NoZoom/NoPan. `piano_play` solicita loop. Flags64/128 são recusadas
como fora do perfil. O backend futuro receberá o pedido completo.

Pedidos são não bloqueantes. O retorno normal é true mesmo com dono/recurso
ausente, nenhum backend ou recusa do backend, conforme a referência consultada.
Cada caso é observado com status próprio: `missing_owner`, `missing_resource`,
`skipped_no_backend`, `backend_rejected` ou `submitted`. Submitted indica somente
aceitação do pedido pelo callback, não reprodução. O CLI usa backend `none`.
Não há mixing, sample decoding, deduplicação de sons ativos, HIT variables,
acústica/atenuação ou reprodução real. Não simular que um áudio foi tocado.

Em DiningChairs.iff, FWAV0 é `chair_sit_metal` (16 bytes incluindo NUL),
FWAV1 é `chair_sit_wood` (15 bytes). FWAV2..5 também foram vistos, sem uso
neste trace. A sequência nativa barata testa Office(false), Cheap(true),
e segue PC17 para EventID0/metal. A branch PC18/wood corresponde à falha dos
dois testes; não inferir o som apenas do label da cadeira.

## Validação e probe real

CTest18/18 passou em Debug e ASan/UBSan. Novas suítes:18 checks de tipo e357
checks de FWAV/pedidos de som, todos sintéticos. Cobrem GUID/master, scopes,
ausência/reuso da entidade, binding imutável, offsets OBJD, truncamento/tail,
64 combinações de flags conhecidas, metadata, backend aceito/recusado/ausente,
recurso/dono ausente e continuação não bloqueante. Integração CLI cobre
precedência privada/global, ausência, ambiguidades, FWAV malformado, requisitos
de opções e limite de4.096 pedidos. LSan ficou desativado pela restrição ptrace;
não se afirma validação de vazamentos.

Comando do probe anterior, acrescentando `--sound-probe`:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff --animation-probe '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf --age 21 --sound-probe
```

Resultado verificado: saída3/fault `animation reset requires posture and carry
resources`,27 instruções,3 frames VM,169 ticks de simulação. A sequência após
44/xevt100 é BHAV1003 PC15(32 Office),PC16(32 Cheap),PC17(23 EventID0),
PC2..5,PC8(44 reentrada),PC12(44 ID0/reset). O clipe foi finalizado e retirado;
`animation_progress:null`. Houve um pedido `chair_sit_metal`, source2,
pan/zoomtrue, loopfalse, rate0/volume0/flags2, `skipped_no_backend`.
Posição final(64,64,1,direção2), recipiente2/slot0, postura1 e33 movimentos
permanecem os da cena construída. Não houve criação ou posicionamento novo.

JSON Debug/sanitizers idêntico byte a byte, ambos saída3 e stderr vazio.
Relatórios ignorados em `reports/chair-object-sound.json` e versão sanitizada.
Os originais foram abertos somente para leitura. As identidades de Global,
BCF/CFP e demais inputs permanecem nos checkpoints anteriores.

## Referências e pendências

FreeSO fixo `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2.0 declarada:
[VMContext.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/VMContext.cs),
[VMTestObjectType.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMTestObjectType.cs),
[VMPlaySound.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMPlaySound.cs),
[FWAV.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/FWAV.cs),
[OBJD.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/OBJD.cs).
Nenhum runtime C# incorporado; sem prova de equivalência com o original.

Continuar pelo reset44: recursos de postura/carry/loops e estado persistente;
depois demais snaps e cadeia completa sentar/levantar. Skeleton, interpolação,
renderização isométrica Linux, áudio real, lote/tempo/necessidades/persistência
e comparação com o executável original continuam necessários à meta completa.
