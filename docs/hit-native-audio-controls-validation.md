# Pedidos nativos de controle de áudio e flags de Patch

Unidade de 2026-10-17 sobre `cdb8369`. A investigação estática da imagem
identificada está em [fórmulas nativas](hit-native-control-formulas-evidence.md).
O runtime continua independente do executável; os dados comerciais permanecem
externos. Esta unidade calcula e encaminha argumentos na entrada do wrapper,
antes de suas transformações DSP e do despacho ao backend concreto.

## Dados preservados

`HotPatch` agora conserva os sufixos booleanos nativos #3/#4/#5 como
`native_id43`, `native_id26` e `native_id27`, depois de `looped`/ID44 e
`piano`/ID38. Os novos campos são `optional<bool>`: valor presente é verdadeiro
somente para inteiro1; ausência é desconhecida, sem presumir default.
O perfil `simitone_local` permanece com os novos campos ausentes.

O leitor nativo ODH ainda aceita as duas flags anteriormente exigidas;
campos adicionais presentes devem ser inteiros int32 válidos. Ausência,
vazio e valor falso são situações distintas. Essa é uma política explícita
ODH de leitura e validação, não reprodução de argumentos ausentes do parser
nativo. Campos após o quinto continuam ignorados. Duplicatas substituem o
payload inteiro: flags ausentes na última linha não herdam valores anteriores.
Não se deduz ID26 de loop/piano, de um overlay do controller ou do primeiro
binding. Patch.ID21/28/29 dependem de getters/bancos efetivos e podem mudar;
seus defaults históricos não constituem metadata imutável do HOT.

## Cálculos antes do wrapper

As funções `compose_native_hit_spatial_gain` e `compose_native_hit_pitch`
não alocam e são `noexcept`. Recebem valores efetivos explícitos dos getters18/19/25/26/30/31/32,
pan local20, `optional<int32_t>` para ganho do grupo e limiar ID123. O helper B
recebe o snapshot local do controller e Patch.ID21 explícito.
Não procuram grupos nem recursos nem resolvem herança de bancos. Grupo ausente significa bypass; grupo
presente com ganho1024 também pula a primeira multiplicação, como o original.
Um manager futuro ainda precisa reproduzir a criação e a consulta dos grupos.

O helper A conserva produtos de32 bits antes de cada divisão signed por1024,
com truncamento para zero. Primeiro aplica grupo ao ID19 quando necessário,
depois ID18 ao resultado. Quando ID123!=0 e ID25<ID123, comparação assinada,
divide o ganho por2 com truncamento para zero. Não clampa para faixa de áudio.
O caso ID19=2047,grupo600,ID18=900 produz1053, mantendo as duas divisões.

A seleção por ID26 escolhe pan local ID20 ou posição IDs30/31/32, nesta ordem.
O getter20 nativo tem retorno descartado; o pedido de pan vem do banco local.
A distância calculada e descartada pelo helper A não é atenuação aplicada
por esta função. O helper B produz o argumento de32 bits
`local21 + Patch21 - 3600`, com wrap, antes da transformação adicional do
wrapper. Não aplica pow, frequência, volume Q15, pan law ou conversão PCM.
Getters não equivalem aos campos locais quando há sentinela INT32_MIN:
eles podem seguir o parent. A revisão detectou essa diferença e a API foi
corrigida para exigir valores efetivos, sem tratar todos os locals como getters.
O teste de integração usa parent sintético explícito para defaults fresh e
confirma ID19 sentinel→ganho1024 enquanto pan local20 mantém INT32_MIN.
Providers compartilhados/mutáveis e seus efeitos ainda exigem contrato.

## Integração e ordem dos efeitos

No perfil global, `NativeHitAudio::apply_global_control_write(id,snapshot)`
é obrigatório. Escritas ID19/20 entregam cópia completa após store/flags,
substituindo o antigo callback numérico nesse perfil. O perfil local conserva
`raw_control(id,value)`. O callback global identifica um recálculo A, sem
copiar Patch.ID26 nem aplicar B/pitch. A pré/pós cópia de ID26 durante binding
continua usando os dois callbacks completos anteriores.

O registry encaminha pedidos tipados por geração: `apply_spatial_gain`
chama `set_pan` ou `set_position`, verifica reentrada e depois chama `set_gain`.
`apply_pitch` é independente. Falha na posição/pan ou reentrada interrompe
antes do ganho. Tipo de placement inválido é recusado antes dos efeitos;
views aposentadas continuam recusadas. Backends que não implementam esses
novos métodos falham explicitamente. Os valores são pedidos ao wrapper;
o nome da interface ODH `NativeHitPatchBackend` não afirma que todos os
backends nativos implementam a mesma operação física para cada slot.

A diferença entre controller.ID26 e Patch.ID26 na primeira fase é relevante:
o slot de posição pode ter outro despacho numa geração não espacial. O
adapter físico precisa conferir a classe concreta, sem presumir que qualquer
pedido de posição equivale a SetPosition. Também não se fundem posição e
ganho numa atualização física atômica: são chamadas ordenadas e a primeira
pode usar o ganho anterior. Não se reproduz acesso inválido ou loader por
assinatura incompatível no ODH.

O teste integrado usa Patch de outro HOT, com cinco flags explícitas, e retorna
seu ID26 preservado. Grupo ausente, limiar0, Patch.ID21=3600 e defaults fresh de parent
são providers sintéticos explícitos. O backend registra pedidos brutos. Confirma fase
controller26=7→Patch26=0, ordem position/gain/pitch→pan/gain/pitch e os dois
setters A-only seguintes; o segundo controller sobrescreve ganho/pan observados
na mesma geração. Isso não valida saída audível, DSP ou seleção automática
de providers. Falhas de callback e reentrada preservam o teardown existente.

## Validação e próximos gates

Recibos locais em registro arquivado não distribuído; auditoria independente
em registro arquivado não distribuído. Um oráculo Python separado,
com inteiros arbitrários, máscara32 e divisão signed truncada, comparou324
combinações de borda e20.000 entradas aleatórias determinísticas:20.324 casos,
zero divergências em ganho, placement, pan local, XYZ e pitch. Ele não executa
o jogo original. Os testes unitários do helper têm seis grupos de casos
conhecidos, incluindo bypass1024, divisões sequenciais, sinal e overflow.

Fonte final:119 checks de recursos,163 de controller,64 de registry e51 de
integração; seis grupos de casos no helper. CTest completo Debug:121
configurados,120 aprovados,1 skip X11,0 falhas (16,00 s). ASan/UBSan:
mesmos resultados (30,80 s), com `ASAN_OPTIONS=detect_leaks=0`; não se alega
validação LSan. Sem SDL/X11, cinco testes selecionados passaram; não é suíte
completa nessa configuração. A revisão independente auditou25 recortes e
2.131 bytes, incluindo PE/hash e comparação com objdump fresco.

Quatro probes Classic/Legacy × Debug/ASan+UBSan passaram. Cada caso processa
27 triplas/81 arquivos,4.494 mappings,4.168 candidatos admitidos (178 não tipo1,
38 não resolvidos,110 excluídos por metadata exigida). Outcomes:3.744 zero,
1 ausente,423 adquiridos; sem wrong-kind ou Patch de outro HOT neste corpus,
ambos cobertos sinteticamente. Cada aquisição passou pelas duas fases,
pedidos tipados, flags e pausa/retomada; falhas de binding/cleanup e recursos
vivos ao final de cada item foram zero. Os26 hashes de fonte por receipt
foram conferidos com a árvore final (104 conferências). Os inputs, binários,
logs e política sintética de providers ficam nos receipts.

Há17.515 registros de Patch e17.504 vencedores globais; as distribuições
abaixo usam o payload final por HOT, não metadata intermediária de duplicatas.
Entre vencedores, ID43 eID26 estão presentes/falsos em17.503 e ausentes em1;
ID27 está presente/falso em17.480 e ausente em24. Nenhum valor presente é
verdadeiro. No conjunto final por HOT:17.514 presentes para43/26 e17.491 para27,
respectivamente1/24 ausentes. `sizeof(HotPatch)=112` no host x86-64; não é
medida de RSS ou ABI dos ports. O helper não acrescenta heap por cálculo.

Birthday conserva ID26=7 quando a Patch é zero, resolve1340→PC16 e Call7→110,
e chega ao fault esperado69 emPC111, sem NoteOn; não conclui o aniversário.
A ordem lexical continua diagnóstica. Os resultados não observam o original
em execução e não validam emissão física de áudio.

Faltam adapter concreto do wrapper/backend, getters mutáveis da Patch,
grupos/duck/provider, carga/streaming, transformações DSP e PCM. O manager
persistente por TrackID, World e Clock continua necessário; opcode69 permanece
recusado. Há ainda a política de trace/instruções por lifetime a resolver antes
do catálogo persistente. Linux funcional e os gates posteriores dos ports
continuam pendentes; sucesso desta unidade não comprova compatibilidade geral.
