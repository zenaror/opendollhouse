# Continuidade atual

## Direção e estado

Consultar `get_goal` ao retomar: somente `active` permite continuar. Na retomada
atual está ativo; a pausa literal no objetivo é histórica. Objetivo integral
incompleto: experiência The Sims 1 original, sete expansões e conteúdo clássico.
Simitone é base e marco intermediário; sua interface é provisória, com interface
original obrigatória na etapa final. Linux é a primeira validação; código e
recursos já devem considerar New 3DS/Vita/Android, depois Switch/Windows/macOS
Intel e Apple Silicon; Wii U é alvo desejado. Não prometer ports sem evidência.

Checkout único: `/home/rafael/_PROJETOS/TheSims1/opendollhouse`.
Origin: `https://git.home.zsrv.com.br/zenaror/opendollhouse.git`, branch `main`.
Base publicada antes desta etapa: `492c4f57fb79886c35c7b2b1875c1ca77647c3ae`.
Consultar OMM `OpenDollhouse` e Git para estado posterior; ler antes de atualizar
handoff com CAS e SHA Git completo. Nunca usar o antigo checkout `/media`.

Preservar WIP anterior fora desta frente: `include/odh/vm.hpp`, `tests/routing.cpp`,
`tests/vm.cpp`, `tools/household_go_here_session.hpp`; também `Testing/`,
`tests/factory_tests_runner.py`, `.sh` e `tests/house07_snack_integration.py`.
O cache CMake da raiz é legado inválido; `build/linux-checkpoint` é antigo apoio.
Não desenvolver dois motores completos nem traduzir integralmente o Simitone.

## Prioridade mais recente: migrar o núcleo agora

Após esta validação, o usuário cobrou a reescrita para evitar retrabalho nos ports.
A próxima frente é migrar execução real para C++ em `native/simulation`; não
continuar ampliando funcionalidades permanentes em C#. Simitone é referência e
ponte temporária. Próximo corte: VMExpression (opcode BHAV2), decisão dos operadores
TS1 e ordem de leitura/escrita nativas; VMMemory fornece acesso temporário pelo
adaptador. Preservar aliases, overflow, exceções, regras TS1/Room1024 e resultados.
Comparar com fonte original, usar no jogo Linux e executar mesma fonte no ARM.
A fila e BCON atuais continuam usados; o restante ainda depende de .NET.
Não recomeçar outro jogo completo nem prometer port só por trocar a linguagem.

## Fila nativa compartilhada: implementada e validada

`native/simulation` fornece `odh_simulation`, ABI C1 com 14 exports. O verdadeiro
VMScheduler usa a ponte `managed/OpenDollhouse.Simulation`, sem fallback.
Identidade estável é distinta de ObjectID assinado mutável; referências por
ocorrência, listas vivas/reentrância, alias destacado, migração tick1, comparação
unsigned e remoção pelo tick atual conservam o algoritmo upstream. Entidades,
RNG, exclusão e efeitos ainda são executados pelo adaptador C#.

SafeHandle conserva callbacks/registro sem GCHandle global. Uma proteção de operação
permite passagem serializada entre threads (VM.LoadAsync), recusando concorrência
ou reentrada por callback. Tetos desktop explícitos: 65.536 listas, 1.048.576
ocorrências e 64 MiB nativos; capacidade falha sem descarte. Bytes não incluem
registro C#, gráficos, entidades, metadados do alocador ou pilha. Save reconstrói
agendamento por Reset/tick1, sem novo formato. Detalhes e receita em
[native-simulation-integration.md](native-simulation-integration.md).

- `reports/portability/native-simulation/`: 82 verificações nativas, CTest 2/2,
  ASan/UBSan/leaks 2/2; 19 cenários diferenciais e 13 grupos da ponte, incluindo
  64 ciclos de VM coletados, 128 disposals e passagem main→worker→main. Biblioteca
  ausente falha explicitamente. Revisão independente sem bloqueio de código.
- `reports/portability/3ds-native-wake-execution/`: mesma fonte em ARM, PASS SVC
  exato e captura, reconstrução/avanço SINTÉTICOS da fila. 147 configurações iguais,
  instância/perfil/Xvfb encerrados. 3DSX40feddef8f8d9a70a7106fdd888d2ab748ff5075f33898b3f37212d63dd95a9c.
  Snapshot de fontes/binários e revisão independente; não jogo/save real/hardware.
- `reports/simitone-linux-evaluation/house07-native-wake/`: publish real e74+40s
  em dois processos isolados. Comprou cadeira GUID26BFBB29/objeto139/preço80,
  saldo665→585. Bob105 executou SitUID3, callee139, postura1 em ticks600–720;
  captura `first-before-action-12.png` mostra sentado. Seis fases conservam
  objeto/posição/direção/preço/dinheiro; save/Load exatos salvo SIMI62→64; dados
  de pessoa/motivos também conservados após callbacks EP2. Zero erros, seis
  hashes originais iguais. Segundo processo continuou e mostrou diálogo normal
  do kit mágico; não é prova de todas as atividades da expansão ou segundo Sit.
  Resultado bruto intacto, `purchase-proof.json`/`lifecycle-summary.json` separados.
  `final-review.json` independente aprova o checkpoint limitado, sem bloqueios.
  Ambos mapearam e usaram as duas bibliotecas. Picos observados da fila:
  9.680/9.440 bytes nativos, 479/458 ocorrências, 246/229 tokens registrados.
  PIDs/perfil/cópia/Xsocket/lock ausentes ao concluir.

SO da fila: a5c2a6e43c367fb74a500ede6c64c775da31c4ec6bba92a8e4c8d69f1aa3f85a.
Fontes, dois SOs e pontes gerenciadas arquivados; ambos os parâmetros
ODHNativeDataLibrary e ODHNativeSimulationLibrary são obrigatórios no publish.
Patch experimental atual contém 23 arquivos após0003/4/5, incluindo correção NBRS;
SHA b465e061fab545dceb65e5ea430d33123b833ac3c949a6a45e76a4d92d8ce64c.
O checkpoint de fila acima usou a revisão anterior de 22 arquivos.

O fluxo limitado de família foi executado; evidência abaixo. Manter próximos cortes
compartilhados ligados a fluxos reais e continuar investigação dos runtimes/alvos,
sem traduzir o motor inteiro nem adiar portabilidade para o fim. O orçamento total
e os demais sistemas compartilhados permanecem abertos.

## Checkpoint BCON anterior, preservado

BCON: `native/data` chama `src/formats.cpp` sem alteração. API C1, dois símbolos,
buffers do chamador, no máximo255 constantes; sem exceções cruzando ABI. Ponte
`managed/OpenDollhouse.Data.Interop` netstandard2.0/C#7.3, sem fallback, usada pelo
BCON.Read verdadeiro; Write igual. Preserva consumo/trailer/stream/EOF; inválidos
não reproduzem campos parcialmente mutados do reader antigo.

- `reports/portability/native-data-abi/`: CTest2/2; ponte12 grupos e2052 comparações
  contra fontes antigas reais, incluindo65536 valores; revisão independente.
- `reports/simitone-linux-evaluation/house07-native-bcon/`: compra normal de cadeira
  GUID26BFBB29/objeto139/preço80/saldo585, Bob sentado, save/reabertura em dois
  processos. Seis fases conservam objeto/preço/posição/dinheiro; promoção SIMI62→64
  permitida, demais estados antes do Load iguais. Bancos de pessoa/motivos após
  Load conservados. Zero erros VM/fatais/diagnóstico, seis hashes originais iguais.
  Ambos os processos chamam ABI1 e mapeiam a biblioteca publicada. Sem alegação de
  total de chamadas pelo logger once. Resultado bruto e revisão separados.
- `reports/portability/3ds-native-abi-execution/`: fixture ARM executou MESMA ABI e
  leitor, PASS SVC e imagem. Não é jogo/fila/CLR/save de família/hardware.
  3DSX `c7a5154b76219eedb21ae1a234e7677d98352ae2e87f3eed9f2a63ae70b5c5a6`.
- SO BCON publicado/testado:
  `7e39611e8099144df46a33ea06b09cc478a5e003e4f59a3566bbe067fc8588f6`.
  Binários únicos pequenos estão nos subdiretórios `binary/` dos gates.

O probe anterior7c6b foi reconstruído de `eea8a5b`, bytes exatamente iguais,
rotulado `rebuilt-exact` em `3ds-native-execution-final/binary/`, e restaurado no
caminho antigo. Não sobrescrever artefatos aprovados; outputs novos separados.

## Base, reprodução e portabilidade

Clones de trabalho ignorados `refs/work/Simitone-linux`: frontend
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`, FreeSO
`7e76c1960252ddd033d8937d5edb47d326f5b69c`. Clones upstream são de consulta.
Aplicar frontend0001/0002/0006; FreeSO0003/0004/0005 e depois
`tools/simitone/experimental-saved-code-recovery.patch`. No checkpoint BCON anterior, experimental tinha 22 arquivos; a revisão atual
e o hash do patch estão na seção da fila acima.
Frontend0006 atual SHAfa5cb3d3144c74d8f9e38e3b14f04499db765e7f9ac94cc706537668b86514a8;
inclui gravação imediata do bairro após retirar uma família da casa.
As fontes managed/native do checkout e biblioteca correta para o destino são
obrigatórias. Receita [native-data-integration.md](native-data-integration.md).
Regerar/verificar replay experimental após alterações no clone de trabalho.

Runtime Linux `build/simitone-linux`, self-contained.NET9.0.21, avisos herdados;
host tem SDK/runtime10. Testes9 usam roll-forward explícito, sem instalar runtime.
.NET9 expira2026-11-10; atualizar host é separado de resolver consoles.
Não publicar binários sem revisão de dependências/licenças.

ReSharp3DS9ddfe53fc0900a0b561aebeec5ff1e66527edef6 não executa fila genérica
inalterada: tipos VAR/GENERICINST/MVAR ausentes, nano BCL1.9/metadata próprios.
Prévia nano2 tem genéricos, mas precisaria portar runtime/ferramentas/BCL.
Não afirmar impossibilidade universal C#. Vita Mono WIP legado não prova net9;
Switch oficial privado/licenciado não prova homebrew; Wii U não tem CLR provado.
Demais sistemas ainda sem build local validado. [portability.md](portability.md)
conserva as fontes e limites. Inventário estático não prova execução nem memória.

## Pendências do jogo completo

Fluxo de famílias em [simitone-family-lifecycle.md](simitone-family-lifecycle.md).
Baseline executada: criar adulto, reabrir seletor, comprar House09 por3500
(saldo20000→16500), andar, salvar e reabrir preservando família/avatar.
Evict sem save imediato foi demonstrado: reiniciar restaura a família na casa.
NBRS também omitia 16 marcadores conhecidos. Ambas as correções foram
reconstruídas e validadas em nova cópia: NBRS 289/289 conserva os 16 marcadores;
Evict grava HouseNumber 0 e o reinício mostra o terreno vazio. Delete remove
FAMI/FAMs e a família não reaparece ao reiniciar. Evidência em
`reports/simitone-linux-evaluation/family-lifecycle-fixed/`, sete processos
encerrados e originais iguais. FSOV antigo, refund ValueInArch e NBRS/personfiles
retidos após Delete continuam riscos separados; reuso ainda não testado.
Não ampliar esses fluxos em C# antes da frente de migração acima.

Continuam construção, outros objetos/grupos/rotações, viagens e atividades das sete
expansões, escolha de idioma, áudio, conteúdo personalizado e UI original final.
Não reduzir primeira jogável a uma casa/interação ou afirmar paridade completa.

SaveHouse/Neighborhood/Characters não são atômicos; weather globalcas.weather e
travelRuntimeSubset não provados. Fidelidade nativa: nestedUseCount, ordem de busca,
cache de paredes e GUID3c08965f permanecem desconhecidos. Contratos resolvidos de
assinatura/saved frames/SIMI/interesses/No conservados em
[simitone-save-code-signatures.md](simitone-save-code-signatures.md) e gates anteriores.
Não refazer investigação Ghidra sem mudança/evidência nova.

## Dados, testes e retenção

Dados comerciais em `../referencias/The Sims` nunca modificados. Runtime exige
pasta de dados, sem executável/disco inserido. Testes copiam dados e usam perfil
isolado, Xvfb próprio e `xwd -root -silent`; não usar tela/input físico.
Encerrar processo, instância própria Flatpak, perfil e Xvfb ao terminar.

Ferramentas3DS já instaladas devkitARM/libctru. RetroArch Flatpak existente;
core oficial Azahar2126.2 em `refs/tools/`, cache útil preservado. Flatpak redefine
XDG: configurar diretórios RA explicitamente, perfil real readonly, comparar seus
147 arquivos e matar apenas instância da tarefa. Hash do log somente após encerrar
filhos. Isso não prova isolamento de todo o estado interno do Flatpak.

Cleanup autorizado20temporários e2arquivos RetroArch concluído; reconfirmação
posterior não autoriza novos caminhos. Evidência em `reports/cleanup/` e
`3ds-native-execution/external-side-effects/`. Desconhecidos/builds compartilhados,
saves, evidência única e dados originais permanecem. Usar safe-checkpoint-cleanup.

Este handoff substitui a acumulação cronológica anterior. Histórico integral e
localizadores anteriores continuam no Git, revisão `e79955f:docs/current-handoff.md`;
evidência detalhada permanece nos documentos específicos e `reports/`.
