# Continuidade atual

Meta integral: primeira versão funcional e verificável no Linux; New 3DS, Vita
e Android são etapas futuras com validação própria. ODH exige dados do jogo
fornecidos pelo usuário, sem dependência do executável original ou checagem de
CD. Preservar os milestones de leitores, VM, cena/interação, tempo e save; os
resultados Linux não comprovam compatibilidade dos ports.

## Checkpoint atual — `afc836a` (2026-10-20)

O commit publicado implementa a ordem externa do NoteOn global: getter efetivo
controller38, writer do wrapper `(38,1,0)` quando não zero, NoteOn, getter
Patch38 vivo e A/B condicionados pelo valor pós-play, mesmo com resultado de
play falso. Os seis providers ficam explícitos e o perfil local continua
separado. A matriz tem 34 casos em lote; Debug, ASan/UBSan e sem SDL/X11
passaram cada um com 6 CTests e 34/34 casos. LSan ficou desabilitado. A revisão
independente não encontrou problemas no escopo.

A investigação subsequente fechou o writer concreto: ele grava o banco local
do wrapper em `wrapper+0x8c`, sem chamar o backend ou alterar flags do
controller. Veja [a evidência do writer global NoteOn](hit-native-noteon-writer-evidence.md)
e o relatório estático em `reports/hit-native-noteon-providers-20261020/`.
Providers A/B, semântica global posterior de ID38, adapter físico e primeira
versão Linux jogável continuam pendentes. Os testes feitos até aqui não
demonstram áudio físico ou compatibilidade completa.

A OMM é consultada no escopo `OpenDollhouse`. Nos checkpoints, registrar `repository`
(`opendollhouse`), `branch` e `git_revision` no handoff, com o `expected_sha256`
vigente. Confirmar por `get_handoff` com `observed_repository` e
`observed_revision`. SHA do handoff e HEAD Git são identidades distintas;
`freshness.matches` não comprova testes, árvore limpa ou backup remoto.

## Referência adicional recebida em 2026-10-15

`../referencias/The Sims Legacy Collection` foi fornecida pelo usuário como
edição Steam de 2025. Após confirmação humana do fim da cópia, o inventário
registrou 10.406 arquivos e metadados estáveis antes/depois. Os 81 HIT/HOT/HSM
comuns têm SHA256 iguais; quatro dos cinco FAR amostrados diferem. Identidade
PE32 x86 do executável e limites da comparação estão em
[legacy-reference-validation.md](legacy-reference-validation.md).
Consultar [referências](references.md#referência-comercial-adicional-legacy-collection).
Ela complementa a instalação anterior; a prioridade de desenvolvimento e a
proveniência das evidências já registradas permanecem.

## Controller HIT nativo — base `f5ab084`

`NativeHitController` executa um subconjunto HIT portátil com bancos próprios
1–16, 17–47 e 50–62, Clock explícito e callbacks sobre o binding de áudio
corrente. O caller fornece `entry_pc` já resolvido; `hitlist_id` não zero da
HOT é aceito sem chooser e sem sobrescrever ID50. Contrato, limites e
verificação estão em
[hit-native-controller-validation.md](hit-native-controller-validation.md).
A evidência estática que orientou o recorte está em
[hit-native-controller-execution-evidence.md](hit-native-controller-execution-evidence.md);
a auditoria conferiu 535 linhas, 1.594 bytes, oito entradas de opcode e
mapeamento PE (nota SHA256 `635e6f322f12efe922b3143872c5c4f0da58ca8573129d0005f6c8749707154f`;
relatório SHA256 `f0171402d4637498e9ada7449c7aec0303faa59e1d66eed97cd7c7f05048d9a8`). Nenhum executável original foi rodado.

Na base histórica, o core direto passou 82 checks com `-Werror -Wconversion -Wsign-conversion` e
ASan/UBSan. CTest Debug: 115 configurados, 114 passaram, um skip X11, zero
falhas (6,97 s); ASan/UBSan: mesmos resultados (13,10 s), LSan desligado;
sem SDL/X11: 9/9 (0,66 s). Oráculo Python independente: 6.331 casos Debug e
6.331 ASan, incluindo aritmética/fade signed 32-bit, INT32_MIN, igualdade e
flags. Logs e recibos estão descritos na validação.

Não houve probe da cadeira nem teste de backend físico porque a classe ainda
não está integrada ao host de áudio. O controller não implementa vozes/DSP,
cadência externa, manager persistente HOTTrackID, integração World, provider
espacial nem admissão. Os casos novos são sintéticos/host; não reivindicam
áudio físico, compatibilidade com o original ou ports.

## Próximos passos

Unidade de PCM reutilizável: load move/charge uma vez, replay usa mesmohandle,
stop/completion retêm PCM até release; loop e taxa efetiva explícitos. Troca
de taxa ancora na fase consumida usando taxa antiga. Lookup XA agora aceita
NativeHitPatchRef global e não depende do HOT local. Contrato e limites em
[hit-reusable-pcm-validation.md](hit-reusable-pcm-validation.md).
A [pesquisa do backend comum](hit-native-common-backend-evidence.md) fechou
classificador físico vs recurso virtual, preload/dirty, perda de pan/frequência,
NoteOn pre-controller38/post-Patch38 e loop latch. O adapter/callback matrix
externa foi implementada em `afc836a`; writer interno está documentado em
[hit-native-noteon-writer-evidence.md](hit-native-noteon-writer-evidence.md).
Providers físicos, DSP e streaming continuam pendentes; não reaplicar controles
nem inferir streaming por tamanho FAR.
Fonte final: mixer101/transporte152/catalog46/integração13 checks. Debug e
ASan/UBSan122cfg/121pass+1skipX11/0fail (15,84/26,78s), LSanoff;
semSDL/X11 só4selecionados pass. Oracle10.392/0, auditoria20ranges/4.320bytes.
Quatro probes Classic/Legacy×Debug/ASanUBSan Patch3212:54.296frames,
108.592bytes PCM retidos em1handle, replay/fase/loop/release pass, inputs82
hashes preservados por edição. Sink sintético, sem áudio físico. Recibos
`reports/hit-reusable-pcm/validation.json` e `native-*.json`.



Nova unidade de controles pré-wrapper: helper A portátil calcula ganho com
mul32/divisões sequenciais, grupo/duck explícitos e seleção pan/posição; helper B
combina local21/Patch21 antes do DSP. Perfil global exige callback de snapshot
para escritas19/20, sem fallback numérico; elas recalculam A somente. Registry
encaminha placement antes gain e pitch separado. Parser nativo conserva flags
Patch43/26/27 opcionais, sem transformar ausência em falso conhecido.
Contrato e cobertura em [hit-native-audio-controls-validation.md](hit-native-audio-controls-validation.md).
O adapter de áudio físico e providers efetivos ainda faltam.
Fonte final:119 checks recursos/163 controller/64 registry/51 integração e6
grupos helper. Debug eASan/UBSan121cfg/120pass+1skipX11/0fail (16,00/30,80s),
LSanoff; noSDL/X11 só5selecionados pass. Oracle20.324 casos sem divergência;
auditoria PE25recortes/2.131bytes. Corpus4 Classic/Legacy×Debug/ASanUBSan:
423 aquisições por caso sem falhas,26hashesfonte porreceipt conferidos.
Os vencedoresPatch têm ID43/26 falsos17.503/ausentes1;ID27 falsos17.480/
ausentes24. Dados não comprovam valores true. O parsing desses valores tem testes
sintéticos; seus efeitos físicos de preload/backend continuam pendentes.
Recibos em `reports/hit-native-audio-controls/validation.json` e `corpus/`.



O controller já oferece perfil `NativeHitPatchSource::global_catalog`, com
Patch de outra origem e outcomes zero/adquirida/ausente/tipo incorreto. A
inicialização global libera o binding anterior antes do lookup e pode seguir
ativa sem wrapper; tipo incorreto faz stop diagnóstico seguro e depois o
initializer restaura entrada/deadline/ID46/ativo. Há dois callbacks de aplicação
de controles, antes e depois da cópia do ID26 efetivo da Patch. A escrita
pública ID50 e timed opcode1 continuam fora do subconjunto. Contrato em
[hit-global-patch-binding-validation.md](hit-global-patch-binding-validation.md)
e [evidência dos resultados](hit-native-binding-outcomes-evidence.md).

`NativeHitPatchRegistry` agora possui backend por Patch ativa/geração, com
BindingLease/ResourceLease separados, factory externa RAII e views protegidas
por geração. Pausa chama Stop, resume não faz áudio; recursos são pedidos
explicitamente. Último release aposenta geração; Stop tardio não toca geração
nova. Os limites padrão64/256/256 e hard cap65536 são política ODH. No host,
armazenamento fixo padrão21.176 bytes exclui backends/capturas/allocator.
Revisão corrigiu overflow de releases adiados por tokens aposentados; regressão
`{1,1,1}` confirma cleanup. Registry deve sobreviver a todos os leases.

Fonte final: controller154 checks, registry55, integração29; CTest Debug e
ASan/UBSan120 configurados,119 aprovados,1 skip X11,0 falhas (15,42/25,87 s),
LSan desligado. Sem SDL/X11 três testes selecionados passaram. Auditoria PE
independente conferiu25 recortes/4.070 bytes. Recibos em
`reports/hit-global-patch-binding/`. O driver integrado ainda é sintético;
não há adapter DSP/PCM físico nem manager persistente/World/admissão no host.

Quatro probes Classic/Legacy × Debug/ASan+UBSan passaram:4.168 candidatos
por caso,423 adquiridos,3.744 zero,1 ausente; sem falha de binding/cleanup
ou recursos vivos. Birthday continua na falha esperada69. Driver sintético
ID26=0 apenas quando adquire; sem áudio físico. Hashes e receipts em
`reports/hit-global-patch-binding/{validation.json,corpus/summary.json}`.

O `NativeHitObjectCatalog`, agora em `HitResourceBank::objects()`, resolve
TrackID pelo namespace global compartilhado de Track/Patch. O parser nativo
conserva registros na ordem literal das linhas, incluindo duplicatas, e
ignora chaves zero de Track/Patch antes do payload. GlobalHitList usa outro
mapa. Chaves desses três callbacks usam o helper numérico nativo; o perfil
legado fica inalterado. `resolve_global_track_event` mantém três origens:
EventMapping escolhido, Track vencedor e HIT da entrada. Patch também é
global e pode ter outra origem. O lookup anterior de `entries().resolve_event`
permanece local ao HOT; não o transportar para o manager como autoridade.
Contrato, testes e recibos em
[hit-object-catalog-validation.md](hit-object-catalog-validation.md).

Checkpoint anterior do catálogo: recursos111, objetos27, banco19 checks; suites Debug e
ASan/UBSan 118 configurados,117 aprovados,1skip X11,0falhas (14,80/26,22 s).
Após última mudança da cobrança no banco, cinco testes focais recompilados
passaram nas duas configs; sem SDL/X11 cinco selecionados também passaram.
Auditoria independente: 50 recortes,7.146 bytes. Corpus em quatro probes:
21.831 registros/21.809 chaves,19 substituições de mesmo tipo e3 entre tipos;
9 eventos usam Track de outro HOT,1 mudaHIT/PC na ordem diagnóstica. Recibos
`reports/hit-object-catalog/`; ordem não é prova de enumeração original.

Para integrar ao host, usar o perfil global e o registry compartilhado;
o perfil local padrão permanece para diagnósticos anteriores. A
[pesquisa de ownership](hit-native-backend-ownership-evidence.md)
demonstrou um contador para os dois aliases espaciais e desequilíbrio de
releases no caminho estático; não copiar esse uso potencial de memória
liberada para ODH. A [pesquisa do registry](hit-native-group-registry-evidence.md)
confirmou escritores69/71 e leitor73; opcode70 não espera. Restam domínio
semântico dos índices, provider compartilhado e invariantes de reload/remoção.
Nenhum desses opcodes foi habilitado nesta unidade.

O `HitEntryCatalog` agora conserva arquivo HIT e PC por chave global. Grupos
posteriores na ordem fornecida substituem os anteriores, com recibos limitados.
`NativeHitResourceResolver` e frames de retorno permitem chamadas entre HITs,
sem trocar o HOT/track/binding do evento. O leitor precisa de
`HotReadProfile::native_entry_map` explícito para esse fluxo; o padrão legado
permanece. A investigação provou buffers independentes e o caminho TrackData,
não concatenação nem merge ENTP. Contratos e resultados atuais em
[hit-native-entry-map-validation.md](hit-native-entry-map-validation.md),
com [bytes do loader](hit-native-entry-map-evidence.md).

A [ordem do loader](hit-native-load-order-evidence.md) foi rastreada: raízes
na ordem de chamada, arquivos na ordem devolvida por Win32, HSM/HIT/HOT por
grupo. Não há sort interno no caminho estudado; ordenar no Linux é política
ODH e pode alterar qual chave repetida vence. O namespace HSM global usa
nomes exatos e substituição pela última definição; HOT guarda números já
resolvidos. O `HitResourceBank` agora possui recursos e catálogo com endereços
estáveis e entrada em ordem explícita; os streams podem ser fechados após a
construção. Contrato, orçamento e recibos em
[hit-resource-bank-validation.md](hit-resource-bank-validation.md).

Unidade atual: leitor106 checks, banco16 checks estritos; revisão independente
sem defeito concreto. CTest Debug e ASan/UBSan: 117 configurados por rodada,
116 aprovados, um skip X11, zero falhas (13,99/27,31 s), LSan desligado.
Sem SDL/X11: quatro testes selecionados aprovados. Quatro probes com dados
Classic/Legacy em Debug/ASan passaram após fechar os streams: 27 grupos,
13.207 registros HSM/8.431 nomes/4.776 substituições. O evento de aniversário
continua chegando à falha esperada do opcode69, sem efeitos de áudio. Recibos
em `reports/hit-resource-bank/`; auditoria estática conferiu 75 intervalos e
31 hashes de recortes. Isso não valida áudio físico ou jogo completo.

A [pesquisa do backend concreto](hit-native-engine-backend-evidence.md)
supersede a hipótese de pausa por cursor: o slot de pausa para buffers, e o
de retomada retorna sucesso sem reiniciá-los. Wrappers da mesma patch podem
compartilhar backend. Nenhum adapter novo foi implementado nesta unidade;
callbacks, ownership completo, streaming, controles e PCM já enfileirado
ainda precisam de contrato. Não encaminhar pausa de um binding ao sink
compartilhado. Integrar catálogo/banco ao manager persistente por TrackID,
contribuições e World; opcode69 continua pendente. Preservar os gates Linux
savedframes/Clock/Main, filas/interação, lote e save.

Orientação humana de 2026-10-17: lacunas do Simitone exigem investigação dos
dados e executável originais; o roadmap de terceiros não limita o objetivo
ODH. A issue8 e o fork alexjyong, com clone e revisão próprios, estão em
[auditoria de referências](reference-audit-2026-10-17.md). O fork tem caminhos
Linux/Legacy e melhorias de frontend úteis para comparação; nenhuma alegação
de fidelidade decorre deles. Seu FreeSO ainda não foi materializado/auditado.

A extensão inicial Call7 adicionou resolvedor explícito de chave DWORD para PC e pilha
limitada, compartilhada por Return8/End12. Stop limpa a pilha; restart ativo
ID55 preserva seus retornos. Isso não implementa população/precedência do mapa
do manager. Foi ampliada pelo catálogo acima. Resultados históricos e limites em
[hit-native-call-validation.md](hit-native-call-validation.md).

Concluir integração com adapter do host de áudio e manager persistente por
HOTTrackID, mantendo geração de World e binding PCM separados. Preservar gates
de admissão, ownership por fonte e teardown; não deduplicar por nome nem criar
ticket por admissão. Fechar backend/provider antes de alegar lifecycle completo.
A meta Linux também inclui saved frames/queues/Clock (105/137), Main sem reinit,
câmera/picking, input, lote com SLOT/failure trees/needs/inventory/save e
SDK/CC. Consulte os documentos de validação e evidência anteriores para
histórico de contratos, testes e proveniência.

Preservar originais, saves, refs pinadas, relatórios, builds, runners antigos e
artefatos da raiz. Não usar `git add -A` nem limpeza genérica.
