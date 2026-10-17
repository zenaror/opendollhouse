# Catálogo de entradas e chamadas entre recursos HIT

Unidade de 2026-10-17 sobre `0a3faf5`. O `HitEntryCatalog` e a extensão de
`NativeHitController` representam o mapa nativo como chave → arquivo HIT e PC
local, sem concatenar arquivos. A [evidência estática do loader](hit-native-entry-map-evidence.md)
demonstra buffers independentes, mapa global e substituição por `TrackData`.
A ordem completa de descoberta dos grupos no original permanece pendente.

## Leitura e catálogo

`read_hot` mantém o padrão `HotReadProfile::simitone_local`. O caller do novo
catálogo deve solicitar explicitamente `HotReadProfile::native_entry_map`:

- Não exige um offset de entrada no próprio HOT nem usa a precedência local
  de hitlist/patch para definir `subroutine_offset`. A chave CSV7 é mantida
  para resolução global; demais metadados não resolvidos continuam marcados.
- `TrackData` aceita campos unsigned válidos em decimal, ou hexadecimal com
  prefixo `0x`/`0X` ou dígitos A–F/a–f, conforme o helper nativo estudado.
  Campos inválidos e overflow são recusados pelo ODH; falhas residuais do
  parser original não são reproduzidas.
- Definições posteriores da mesma chave substituem a anterior, com contagem
  em `track_data_replacements` e `duplicate_records`.

O campo de entrada CSV7 aceita `@símbolo` HSM, equate conhecido ou literal iniciado por dígito
no perfil nativo. O helper original `0x57b554` distingue esses casos; o corpus
contém dez linhas numéricas em três HOTs (`CClock`, `Moose`, `SlotMach`). O
perfil legado continua exigindo `@símbolo`. Não se usa zero como substituição
silenciosa para um literal inválido ou símbolo desconhecido.

O caminho `Patch` do perfil nativo lê os dois campos iniciais e encontra o
filename entre as primeiras aspas seguintes; não presume que o filename seja
sempre o terceiro campo CSV. O corpus contém 24 linhas de `GuineaPig.hot` com
prefixo extra antes das aspas. O leitor conserva esse prefixo em
`HotPatch::ignored_pre_filename`, sem interpretá-lo como parte do filename,
e lê loop/piano depois das aspas; nesse perfil, somente o valor1 é verdadeiro,
conforme o callback nativo. O perfil legado preserva seu layout anterior.
Esse formato foi conferido no callback original, sem deslocar campos por
tentativa ou excluir os recursos da validação.

O catálogo copia somente pares de ponteiros para HIT/HOT imutáveis fornecidos
na ordem explícita do caller. Os recursos devem permanecer vivos e em endereços
estáveis durante o uso do catálogo, de seus resultados e dos controllers.
Não há varredura de diretório, prioridade inferida pelo nome ou ordem nativa
automática. Cada `TrackData` final conserva o HIT pareado com o HOT que o
definiu; o grupo posterior substitui chaves anteriores e gera recibo limitado
de colisão. ENTP limita a região de código do parser ODH, mas não é fonte de
entradas nem fallback para o catálogo.

O preflight valida todos os pares e offsets antes de construir o índice.
Os padrões de orçamento são 64 grupos, 65.536 linhas incluindo substituições
internas, 16 MiB de extensão serializada HIT+HOT, 4.096 recibos de colisão e
256 bytes por nome de evento. Esses limites são política ODH; extensão
serializada não mede RSS ou toda a memória dos mapas C++.

`resolve_event` recebe explicitamente o grupo do evento e resolve eventos
tipo1 para seu track e chave CSV7 no mapa global. Metadados permanecem no HOT
do evento, mesmo que a entrada esteja em outro HIT. Essa API não executa
admissão, escolha de patch/hitlist, prioridade de eventos ou vínculo World.
Não usa o resolvedor de eventos legado do `SoundEventFactory` como contrato
nativo.

## Execução entre buffers

`NativeHitResourceResolver` devolve `optional<HitCodeTarget>`. Cada alvo retém
`const HitFile*` e PC local. O controller valida versão, região e destino antes
de empilhar `{arquivo de origem, PC pós-operando}`. Return8/End12 restauram
ambos; não substituem HOT, track, registradores ou binding de áudio.

O resolvedor anterior `NativeHitCallResolver` continua como perfil local.
Fornecer os dois simultaneamente é erro explícito. Alvos ausentes/nulos,
versões ou PCs inválidos e reentrada absorvida pelo callback fixam falha.
O cursor de retorno que chega ao fim da região falha apenas na próxima busca.

Stop limpa a pilha e restaura arquivo/PC iniciais. Restart ativo ID55 também
restaura a entrada, mas preserva os frames herdados, incluindo seus arquivos.
Pausa e retomada preservam a execução suspensa. O trace registra recurso de
origem e de destino de cada instrução; `current_hit()` expõe o arquivo ativo.
Pilha e trace são pré-alocados e limitados. Os padrões de 64 frames e 4.096
entradas de trace são diagnósticos por controller, sem constituir orçamento
global adequado para instanciar todos os tracks do jogo.

## Corpus e protocolo de validação

O levantamento estático de recursos comerciais encontrou 27 pares HIT/HOT,
95.189 bytes HIT, 2.360.620 bytes HOT, 4.363 linhas `TrackData` e 4.264 chaves
distintas. Não havia ENTP nesses HITs. As 99 chaves repetidas entre arquivos
pertencem a `xpack`/`gnome`, cujos HITs têm bytes idênticos; não se conclui
que a identidade de arquivo possa ser descartada. A varredura de bytes de
possíveis opcodes não é um disassembler nem prova limites de instrução.

`reports/hit-native-entry-map/catalog-probe.py` e `catalog-corpus.json` preservam
o levantamento. `catalog-runtime-probe.cpp` usa o leitor e catálogo C++ com
`catalog-inputs.tsv` em ordem lexical explicitamente diagnóstica. Resolve o
evento real `vox_blowout_candles`, chave1340/PC16, e executa seu HIT até Call7
chave1347/PC110, onde espera recusa do opcode69 ainda não implementado.
Zero efeitos de áudio são esperados, pois a patch inicial desse track é zero.
Esse protocolo deve ser aplicado separadamente às duas instalações externas.

O primeiro probe encontrou a restrição CSV7 numérica e recusou `Cuckoo.hot`;
os logs e o diagnóstico intermediários foram preservados em
`reports/hit-native-entry-map/before-numeric-csv7/`. A correção decorre da
leitura do helper original, sem remover esses arquivos da validação.
Depois dessa correção, o probe expôs a forma de filename com prefixo extra de
`GuineaPig.hot`; ela também foi rastreada no parser original antes de ampliar
o leitor. Essas falhas intermediárias não contam como sucesso do corpus.

Os resultados finais de execução e os hashes dos arquivos desta unidade são
registrados em `reports/hit-native-entry-map/validation.json` e nos logs
adjacentes. Os testes incluem HOT parseado de A, entrada em B, chamadas
B→A→C e retornos C→A→B, conservando os metadados e patch de A. Também cobrem
limites de recurso, ENTP separado, registros substituídos, mudança de ordem,
alvos inválidos, faults/reentrada e restart com frames de outro HIT.

Verificação final: 133 checks do controller, 101 do leitor e 26 do catálogo
integrado passaram. O catálogo também compilou diretamente com C++17 e
`-Wall -Wextra -Werror -Wconversion -Wsign-conversion -pedantic`. CTest Debug
e ASan/UBSan configuraram 116 casos cada: 115 passes, um skip X11, zero falhas
(14,83 s e 28,02 s); LSan desativado. O build sem SDL/X11 passou os três testes
do leitor, catálogo e controller (0,02 s).

Os quatro probes de corpus (duas instalações × Debug/ASan) passaram com
27 grupos, 4.363 linhas, 4.264 chaves, 99 colisões e 2.455.809 bytes
serializados. O evento e o Call alcançaram o limite esperado de opcode69,
sem efeitos de áudio. Foram reconferidos os hashes dos 81 HIT/HOT/HSM entre
edições. Recibos em `catalog-{classic,legacy}-{debug,asan}.json` e
`corpus-identities.json` no mesmo diretório. No host de 64 bits, cada entrada
de trace mede32 bytes; o probe usou64 entradas e8 frames, sem instanciar
controller por todo o catálogo ou inferir o consumo em consoles.

Nenhum executável original foi executado ou modificado. Interpretar recursos
comerciais no ODH não prova o evento completo, áudio físico ou compatibilidade
dos ports. O adapter/backend ainda depende das rotinas descritas na
[investigação de pausa do binding](hit-native-binding-pause-evidence.md).
