# Continuidade atual

Meta integral: primeira versão funcional e verificável no Linux; New 3DS, Vita
e Android são etapas futuras com validação própria. ODH exige dados do jogo
fornecidos pelo usuário, sem dependência do executável original ou checagem de
CD. Preservar os milestones de leitores, VM, cena/interação, tempo e save; os
resultados Linux não comprovam compatibilidade dos ports.

A OMM foi consultada no escopo `OpenDollhouse`; o resumo atual já referencia
`f5ab084`. O handoff anterior não trazia os metadados de revisão, portanto seu
`freshness` era `not_verified`. Nos checkpoints, registrar `repository`
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

Core direto passou 82 checks com `-Werror -Wconversion -Wsign-conversion` e
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

A extensão Call7 adiciona resolvedor explícito de chave DWORD para PC e pilha
limitada, compartilhada por Return8/End12. Stop limpa a pilha; restart ativo
ID55 preserva seus retornos. Isso não implementa população/precedência do mapa
do manager. Resultados e limites em
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
