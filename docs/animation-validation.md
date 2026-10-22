# Recursos de animação — 2026-10-13

Extensão posterior: skeletons, conversão de eixos, interpolação e matrizes
da pose estão em [pose-validation.md](pose-validation.md). O perfil de
animação-only abaixo continua separado do novo leitor de skeletons.

O checkpoint seguinte ligou frames/eventos silenciosos à VM; consultar
[playback-validation.md](playback-validation.md). Abaixo está a evidência da
etapa de leitura de recursos, anterior a essa ligação.

Esta etapa identifica o clipe da cadeira e lê seus dados. Não executa a
primitiva 44, não renderiza ossos e não comprova comportamento do original.
O probe publicado anterior continua parando em 44, BHAV 0x1003 PC8.

## Perfil e validação

`read_strings` lê STR# 0 (Pascal), -1 (NUL), -2 (valor/comentário NUL) e -3
(idioma seguido de valor/comentário NUL). Preserva bytes, inclusive texto não
ASCII; JSON usa escapes byte a byte, sem alegar conversão do encoding original.
Idiomas 0/1 pertencem ao grupo US. `string_in_language` continua sendo uma busca exata, enquanto
`string_in_language_or_english` oferece fallback para textos visíveis. O fallback foi
ligado aos rótulos de interação. O preview Linux aceita `--language 1..20`, converte
o texto usando o idioma real da entrada escolhida (inclusive quando caiu no inglês)
e o mostra no título da janela. Se PangoCairo estiver disponível, também desenha
esse rótulo na faixa inferior da cena; esse painel é apenas uma prévia, não um menu
de idioma, e a escolha ainda não é salva entre execuções. Payload ausente, somente
cabeçalho e tabela decodificada são estados distintos.

`read_bcf_animations` aceita apenas BCF binário com zero skeletons e appearances.
Preserva nomes, bits de duração/distância, moving, índices, flags, propriedades e
eventos na ordem do arquivo. Verifica ranges ativos e budgets cumulativos.
`read_cfp` expande translation XYZ e rotation XYZW em bits float32: FF literal,
FE repetição uint16+1 e deltas 00..FC. Reinicia o acumulador por plano; FD, valores
não finitos e repetição além do plano são erros. Bytes restantes são reportados.
Não aplica a troca de handedness do FreeSO. Alocação e truncamento são limitados.

Os 15 testes CTest passaram em Debug e ASan/UBSan. A suíte nativa nova executou
227 checks sintéticos: formatos de STR#, agrupamento de idiomas, propriedades,
truncamentos, budgets, literal/repetição/delta CFP, FD e não finitos. A integração
CLI valida JSON, bytes não ASCII, seleção, ambiguidades e erros sem stdout.
Isso inclui regressão para o fechamento JSON corrigido durante desenvolvimento.
LeakSanitizer ficou desativado (`ASAN_OPTIONS=detect_leaks=0`) pela restrição de
ptrace do ambiente; não se afirma validação de vazamentos.

## Arquivos comerciais consultados em leitura

O corpus fica externo ao checkout, em `../referencias/The Sims`. Não foi copiado
nenhum recurso comercial para o Git.

- `Objects.far/DiningChairs.iff`, SHA256
  `cffff7b4e8b5d1b31351c32a2f8656f50dbf72a82217bf38b4828853fc6f90c7`:
  STR129 formato -3, nove entradas idioma1, nove bytes restantes;
  índice3 adulto `A2O-sit-left`. STR130 índice3 infantil `C2O-sit-left`.
- `Animation.far/adult-chair-sit-new.cmx.bcf`, 5.632 bytes, SHA256
  `6b66dd3b1afe24ebb553f082ccb4cf09083c441b87f60059456141f58da96415`:
  quatro clipes; selecionado `a2o-sit-left`, xskill `xskill-A2O-sit-left`.
  Duração float32 5333ms (bits1168549888), moving0/distância0, 27 motions,
  máximo161 frames, 322 translations e 4.347 rotations. Sem bytes restantes.
- `Animation.far/xskill-A2O-sit-left.cfp`, 11.571 bytes, SHA256
  `58a5488653645d22ebe858098159c9896a7eba0b745d5483f2098a9c0380074a`:
  planos 322/322/322/4347/4347/4347/4347. As 18.354 amostras, 73.416 bytes
  decodificados, coincidiram bit a bit com o decoder Python independente
  `tools/compare_cfp.py`, que arredonda delta e soma para float32 separadamente.
  SHA256 da concatenação de uint32 little endian dos sete planos:
  `b9c4785b581b67e649626e3f3944996bbab0107dc61e5c052f69ba767f8c091d`.
  Ambos consumiram 11.571 bytes. Essa comparação valida os dois decoders neste
  input; não é prova de equivalência com o executável original.

ROOT contém 15 eventos: 14 `footstep` nos IDs733,966,1200,1466,1700,1966,2200,
2466,2700,2966,3200,3466,3600,3966, e `xevt=100` em4466. O parser apenas
preserva esses IDs e strings. O significado temporal abaixo vem do FreeSO.

As consultas normais e sanitizadas da STR129 e do BCF/CFP selecionado produziram
JSON byte a byte idêntico, stderr vazio. Relatórios ficam em `reports/`, ignorado:
`chair-animation-name.json`, `chair-animation-cfp.json`, `chair-cfp-comparison.json`.

O nome de um clipe é interno à coleção BCF e pode diferir do filename.
`a2o-comfychair-sit.cmx.bcf` contém uma variante distinta; não foi usado como
substituto. Não se afirma cobertura de todos os BCFs da instalação.

## Contrato investigado para a próxima unidade

Foi usada a revisão FreeSO fixa `4c6b3e8f5835b228723caea3c9f683c62f244f73`,
por consulta direta e análise de subagente em leitura. Esse contrato não foi
implementado nesta etapa nem confirmado contra `Sims.exe.bak`.

Os operandos da cadeira `03 00 00 00 00 40 00 00` significam ID3, source Object,
modo0, hurryable, eventos esperados0 e destino padrão Args[0]. Bits0/4 selecionam
modo; bit1 backwards, bit2 IDFromParam, bit5 destino local, bit6 hurryable.
Object normalmente usa CodeOwner; IDFromParam muda a origem para StackObject.
`GetAnimation` usa tabela OBJD.AnimationTableID+child e, se ela falta, 129/130.
Child é `TS1 && PersonsAge < 18`, inclusive zero. O World construído anterior
inicializa os 88 person words com zeros: consultar adulto129 explicitamente não
prova que esse World já selecionaria adulto. A próxima unidade precisa tornar a
idade explícita e testar a seleção infantil também.

Modo0 inicia um estado e aguarda ticks. Speed é30/25, dobrado somente com
WalkStyle1 e hurryable. Cada avanço converte frame em tempo `frame*1000/30`,
consome eventos e verifica fim pelo frame convertido a int. A thread é executada
antes do avanço da animação. A duração declarada não basta como temporizador.
`xevt` enfileira short; valores<100 incrementam EventsRun. Cada chamada consome
um evento, grava o código em Args[0] ou no local indicado e retorna false.
Assim `xevt100` ocorre antes de true e não incrementa EventsRun. No fim, a
referência sintetiza eventos enquanto EventsRun<ExpectedEventCount; somente após
esvaziar a fila limpa o estado e retorna true. `footstep` não tem handler nessa
rotina de eventos. Sons, mãos e dress/undress exigem serviços ainda ausentes.

Reset ID0, carry, loops, backwards, substituição do clipe e cancelamento precisam
de contratos explícitos de limpeza de estado/tickets no ODH. A referência não
usa os tickets ODH. Há também uma peculiaridade a investigar: `OrderBy` em
GetTimeProps descarta o resultado; a fila mantém ordem do arquivo. Não converter
essa observação em compatibilidade sem teste contra o original.

Casos prioritários: evento final/simultâneo antes de true, xevt100 sem incremento,
Args versus locals, idade/CodeOwner, reset durante espera, ticket sem órfão em
cancelamento/substituição e limites de frames/eventos/ticks. A reprodução dos
ossos, loading de skeleton e interpolação permanece uma unidade separada.

## Fontes primárias

Todos os links abaixo fixam a revisão consultada:

- [STR.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/STR.cs).
- [BCF.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/BCF.cs),
  [Animation.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/Animation.cs),
  [CFP.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.model/CFP.cs).
- [BCFReadProxy.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Utils/BCFReadProxy.cs),
  [TS1BCFProvider.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.content/TS1/TS1BCFProvider.cs).
- [VMAnimateSim.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMAnimateSim.cs),
  [VMMemory.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMMemory.cs#L674).
- [VMAvatar.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs#L443),
  [VMAnimationState.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Model/VMAnimationState.cs),
  [Animator.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.engine/Animator.cs#L68).

FreeSO declara MPL2.0. Nenhum runtime C# foi incorporado; referências e diferenças
continuam registradas, sem afirmação de clean-room ou equivalência funcional.
