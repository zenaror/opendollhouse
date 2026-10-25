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
Base publicada antes desta etapa: `0e2f6c8881ce84774581fb9e05ae9bdc085c81b8`.
Consultar OMM `OpenDollhouse` e Git para estado posterior; ler antes de atualizar
handoff com CAS e SHA Git completo. Nunca usar o antigo checkout `/media`.

Preservar WIP anterior fora desta frente: `include/odh/vm.hpp`, `tests/routing.cpp`,
`tests/vm.cpp`, `tools/household_go_here_session.hpp`; também `Testing/`,
`tests/factory_tests_runner.py`, `.sh` e `tests/house07_snack_integration.py`.
O cache CMake da raiz é legado inválido; `build/linux-checkpoint` é antigo apoio.
Convergir para um único jogo nativo; o port integral do Simitone está autorizado
pela direção mais recente abaixo.

## Direção humana mais recente: port completo C/C++

O usuário esclareceu expressamente que TODO o jogo baseado no Simitone deve ser
portado para C/C++, incluindo interface, gráficos, áudio, controles e plataforma.
O executável final não terá C#/.NET/CLR. Isto supera a antiga restrição de não
iniciar tradução integral. `docs/native-port-plan.md` é o mapa completo; C# fica
como referência e ponte transitória, não arquitetura final. O trabalho acontece
por etapas, mas a conclusão exige o aplicativo nativo completo. A migração de
VMExpression abaixo foi implementada e validada; o port completo segue incompleto.

## Etapa nativa concluída: regras das ações

Todos os 21 operadores TS1 VMExpression agora executam em C++ usado pelo jogo.
Detalhes e reprodução em [native-expression-integration.md](native-expression-integration.md).
O acesso à memória/entidades ainda passa por VMMemory gerenciado; não é VM inteira.
Native ABI1 aditiva com dois exports, preservando os 14 exports da fila anterior.
792 checks nativos, CTest4/4 e ASan/UBSan4/4; 702 diferenciais da ponte contra
fonte original, também executados pelo revisor no CLR9 real. Política numérica
foi medida em CLR9/10 frio/aquecido; não é prova do executável original.
Zero alocações em 10.000 chamadas aquecidas, não nas primeiras/reentrantes.

Linux House07: compra80/saldo665→585, Bob105 SitUID2 na cadeira139, save/reopen
preservado. Amostras nativas81.920/16.384 chamadas, erros0; logs semVM/fatal.
SIMI62→64 único delta preEP2, pessoa/motivos conservados depois. Gate
`reports/simitone-linux-evaluation/house07-native-expression/` aprovado pelo
revisor. ARM compartilha a MESMA fonte e792checks: PASS real no emulador,147configs
iguais, perfil/processos encerrados; não jogo/hardware. Gates/source/binaries
preservados, sem original comercial modificado. Regressão de família concluída
em `reports/simitone-linux-evaluation/family-native-expression/attempt-2/`:
118 ações, sete processos; ODHExpr/Alex criado, House09 comprada (20.000→16.500),
andou/salvou/reabriu, saiu da casa e foi excluído, com reinícios entre etapas.
FAMI6/GUID1545731141, NBRS49/49→289/289. Dados do Sim preservados; 18 mudanças
postEP2 em outras entidades não foram ocultadas. Expression nativa registrada,
zero erros VM/fatais, originais iguais e todos os recursos isolados encerrados.
Save antigo da casa/NBRS/personagem ainda retidos após exclusão; reutilização
não testada. Aviso repetido da expansão e valor4.133 continuam pendentes.
Primeira tentativa falhou no controlador de teste; evidência separada preservada.
`final-review.json` independente aprova a regressão limitada da família.

Próxima frente: propriedade nativa do estado/memória de simulação, eliminando
as dependências da ponte aos poucos sem um estado autoritativo duplicado.
Não voltar a ampliar recursos permanentes em C#. Todos os demais sistemas do
plano integral continuam exigidos.

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

SO anterior da fila: a5c2a6e43c367fb74a500ede6c64c775da31c4ec6bba92a8e4c8d69f1aa3f85a.
SO atual, com Expression: 3787dea79d4df8e2970b80b5087e50b23c8f59f74c8f00f634206e93b0aded08.
Fontes, dois SOs e pontes gerenciadas arquivados; ambos os parâmetros
ODHNativeDataLibrary e ODHNativeSimulationLibrary são obrigatórios no publish.
Patch experimental atual contém 24 arquivos após0003/4/5, incluindo NBRS e Expression;
SHA 1fe785a5e03ac5a69310250ad1d5a0b8fe2020c8bd8def5fc44ff20e6e9bb112.
O checkpoint de fila acima usou a revisão anterior de 22 arquivos.

O fluxo limitado de família foi executado; evidência abaixo. Manter próximos cortes
compartilhados ligados a fluxos reais e continuar investigação dos runtimes/alvos,
como etapas do port integral, sem adiar portabilidade para o fim. O orçamento total
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
