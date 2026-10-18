# Recursos PCM reutilizáveis e resolução XA global

Unidade sobre `b4a364e`, voltada à retenção necessária para um backend por
Patch/geração. Não é ainda o adapter físico nativo. A [evidência do backend
comum](hit-native-common-backend-evidence.md) delimita carga, classificação,
NoteOn e controles; as escolhas do perfil PCM ODH continuam explícitas.

## Recurso, reprodução e liberação

`PcmMixer` e `PcmTransport` oferecem `load_reusable(XaAudio&&,PcmControls)`,
`replay(handle,PcmReplayOptions{frequency_hz,loop})` e
`set_frequency(handle,frequency_hz)`. Load move uma vez o PCM para um handle
inicialmente parado. Cobra a capacidade do vector, não só os samples úteis,
no orçamento agregado até `release`. Rejeição de admissão conserva a entrada.
Stop e conclusão reconhecida pelo consumo conservam esse recurso; replay
reinicia o mesmo handle em sample zero sem novo decode ou cópia do PCM.

O caminho existente `add` continua one-shot: stop/conclusão liberam PCM e
`release` invalida seu handle. Operações de replay/frequência exigem handle
reutilizável. Handles não são reciclados. Slots permanecem pré-alocados e
limitados (padrão64, máximo256); PCM retido, inclusive parado/completo, ocupa
orçamento. Render/replay não criam novos vetores de samples no mixer.
O sink e o decoder possuem seus próprios custos e obrigações; isso não mede
RSS nem o consumo total de um port.

`frequency_hz` é taxa efetiva explícita de frames de origem, entre100 e192000.
Não é o argumento bruto de pitch HIT. O futuro adapter precisa aplicar seus
clamps e conversões nativas antes de usar essa API. Interpolação racional
linear, ganho Q15, balance linear e saturação permanecem o perfil ODH próprio.
Em loop, a interpolação da última amostra usa a primeira como vizinha.
PCM vazio conclui imediatamente, inclusive quando loop é pedido.

O flag de loop de cada replay é uma escolha explícita desse perfil PCM.
O loop latch observado no backend nativo não é implementado pelo mixer:
um adapter deve conservar seu estado por source/geração e não sobrescrevê-lo
com um novo Patch.ID44 falso como se o original o limpasse.

## Fila pendente e mudança de frequência

O transporte pausa o sink, reconhece o consumo, limpa somente o output ainda
não consumido e rebobina as notas sobreviventes antes de replay. Isso preserva
outras fontes; frames já consumidos não podem ser retirados. Replay é admitido
mesmo quando o alvo estava parado/completo e outra nota tem output pendente.
O sink continua sob um único owner; callbacks externos não podem manipulá-lo
concorrentemente. Erros do sink mantêm a política fail-closed existente.

Ao trocar frequência, primeiro restaura a fase no horizonte consumido usando
a taxa antiga. Essa fase vira a âncora da nova taxa. Recalcular o intervalo
inteiro com a taxa nova causaria salto. Rewind de loops usa multiplicação
modular sem overflow; duração finita é recalculada para o cursor corrente.

A revisão corrigiu dois casos de nota `rendered` ainda não consumida:
preflight também limita sua duração antes de operações do sink, e o mixer
direto recusa alterar a taxa sem a fronteira consumida. A regressão renderiza
um recurso inteiro, reconhece1/4, rejeita a troca direta e confirma samples
{500,1000} após rewind na taxa antiga e mudança da taxa. O preflight usa um
limite conservador de replay inteiro contra o horizonte renderizado; perto
do limite absoluto uint64 pode recusar um caso cuja duração restante caberia.
O overflow desse contador não foi alcançado em fixture; há checagem estática,
sem test hook que o apresente como uma execução real.

## Patch global e carregamento XA

`XaPatchCatalog::load(const NativeHitPatchRef&,allowance)` usa o filename da
Patch indicada pelo catálogo global, sem procurar uma Patch no HOT do Track.
Exige ID não zero e ponteiro presente; os recursos emprestados precisam
permanecer válidos/imutáveis. A referência não é um token criptográfico de
membership; o caller obtém a origem correta do `NativeHitObjectCatalog`.
O overload antigo `load(Hot,id)` permanece e os dois usam o mesmo caminho
privado de índice/abertura/decodificação.

Basename, case ASCII, rejeição de ambiguidades, fontes explícitas, detecção de
mudança de tamanho e allowance permanecem conforme
[catálogo XA](xa-patch-catalog-validation.md). O filename da Patch não é usado
como caminho arbitrário de filesystem. Tamanho da entrada FAR é limite do
decoder; não é substituto do OpenFile/GetFileSize físico usado pelo classificador
de streaming nativo. Este overload não decide streaming nem política DSP.

## Integração e validação

`native_patch_pcm` liga dois controllers ao mesmo catálogo global e registry,
com Patch local conflitante, loader XA e recurso PCM reutilizável. Um backend
DE TESTE escolhe explicitamente unity-Q15, pan central, taxa do cabeçalho e
sem loop; ignora pedidos HIT de controle nesse perfil documentado. Não é uma
implementação de conversão nativa nem callback matrix completa de NoteOn.
Confere uma decodificação/handle para os dois bindings, samples exatos, replay
ativo, pausa compartilhada sem liberar PCM, resume sem replay, consumo do fim,
liberação final e falha de segunda Patch acima do orçamento agregado.

A revisão independente executou um oráculo de fase racional separado:
10.392 casos, zero divergências, incluindo loop circular, replay, render,
commit/ack parcial, rewind e troca de taxa. Isso valida o mixer isolado,
sem demonstrar consumo ou áudio físico. Auditoria estática independente
conferiu20 intervalos e4.320 bytes da imagem identificada; EXE apenas lido.

Fonte final: mixer101, transporte152, catálogo46 e integração13 checks.
Os testes isolados de mixer/transporte passaram em C++17 com `-Werror` e
ASan/UBSan. A integração também passou em compilação estrita com `-Wconversion`.
CTest completo Debug:122 configurados,121 aprovados,1 skip X11,0 falhas
(15,84s). ASan/UBSan: mesmos resultados (26,78s), com LSan desabilitado por
`ASAN_OPTIONS=detect_leaks=0`. Sem SDL/X11: quatro testes selecionados passaram
(1,00s); não foi rodada a suíte completa nessa configuração.

Quatro probes Classic/Legacy × Debug/ASan+UBSan carregaram27 triplas de recursos
em ordem lexical diagnóstica, fecharam os streams e resolveram Patch3212 pelo
catálogo global (grupo25,linha3699 nessa ordem). O loader abriu a entrada
`Voice\mood\VOX_COMFORT_SIGHM4.XA` de SoundData.far: offset109753498,
29.124 bytes encoded, mono22050Hz/54.296 frames, capacidade PCM108.592 bytes.
Esses valores coincidiram nos quatro casos; isso não prova identidade das
edições completas ou equivalência do runtime original.

Cada probe conferiu prefixos PCM digitais exatos antes/depois de replay no
mesmo handle, troca de frequência após consumo parcial na fase esperada,
stop/completion retendo PCM, loop cruzando o fim do recurso e release zerando
handles/bytes. O sink é sintético, com unity-Q15 explícito e saída22050Hz;
não é mixer/DSP/streaming original. O checksum FNV do PCM é só um recibo de
regressão, não hash criptográfico. Há SHA256 dos81 recursos HIT/HOT/HSM mais
SoundData.far, fontes e binários nos receipts; os inputs foram conferidos
antes/depois e preservados. Relatórios em `reports/hit-reusable-pcm/`, com
`validation.json`, logs finais, runner e probes locais ignorados pelo Git.

## Próximos contratos necessários

A evidência agora confirma que pan/frequência sem buffer podem ser perdidos,
preload não limpa dirty e o primeiro play pode recriar o buffer. NoteOn consulta
controller38 antes do play, pode escrever wrapper38, e consulta Patch38 vivo
DEPOIS para decidir A/B, independente de sucesso do play. O perfil global atual
ainda não representa essa matriz completa; não anunciar opcode2/streaming
completos ou aplicar pan/pitch guardados automaticamente após todo load.
Getters mutáveis, buffers lógicos/dirty, source classifier, loop latch,
transformações DSP, streaming e adapter concreto continuam pendentes.
O manager persistente por TrackID, Clock/World e a política de trace/instruções
por lifetime também precisam ser fechados antes de integrar o catálogo todo.
A meta Linux completa e os gates dos ports permanecem ativos.
