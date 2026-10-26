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
Base publicada antes da etapa atual: `6dc587ec1b3420ef6460130991eeeef3c336b178`.
Consultar OMM `OpenDollhouse` e Git para estado posterior; ler antes de atualizar
handoff com CAS e SHA Git completo. Nunca usar o antigo checkout `/media`.

Preservar WIP anterior fora desta frente: `tests/routing.cpp`,
`tools/household_go_here_session.hpp`; também `Testing/`,
`tests/factory_tests_runner.py`, `.sh` e `tests/house07_snack_integration.py`.
As três linhas anteriores de `Vm::has_routine` e seus testes em `tests/vm.cpp`
foram incorporados como dependência direta da nova validação de PushInteraction.
Preservar também o WIP em `managed/OpenDollhouse.Simulation` e na referência de
trabalho ignorada; não continuar esses arquivos nesta frente.
O cache CMake da raiz é legado inválido; `build/linux-checkpoint` é o build nativo ativo.
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

## Checkpoint nativo validado (2026-10-26)

### Uma ação pode aguardar outra e depois continuar (etapa atual)

AllowPush dentro de uma ação pode iniciar outra de prioridade maior. A fila
preserva os pais na ordem, restaura a prioridade ao retornar e só encerra as
mudanças de necessidades e NonInterruptable quando a ação externa termina.
Retorno verdadeiro ou falso da filha não desvia a ação pai: ela volta à mesma
instrução no próximo tick. Os registradores compartilhados conservam os efeitos.
A identidade do frame usa rotina/ticket/caller/callee/owner; StackObjectID pode
mudar durante a execução. Fila legada não pode inventar pais protegidos.

Controller codec v5 conserva os pais suspensos e lê v1–v4 sem inventá-los; VM
codec continua v2. Novas esperas AllowPush=1 conferem fila a cada tick; waits Main
antigos mantêm o prazo salvo. Capturas após avanço do relógio, antes da observação
do retorno e entre retorno da filha e retomada do pai são restauráveis. A sessão
rejeita combinações inconsistentes de frame, prefixo, ticket e estado da VM.
Alvo reciclado durante despacho gera diagnóstico antes da saída de frame.

Build C++ e 15 CTests passaram em `reports/native-nested-actions/attempt-1/`.
O teste integrado compara a continuação sem interrupção com reaberturas repetidas,
para filha com retorno verdadeiro e falso, incluindo Sleep/Idle, World e TEMP/XL.
Isto é evidência sintética; cancelamento completo, callbacks e RunImmediately
seguem pendentes. Ver `native-queue-migration.md` para contrato e limites.

Regressões com o mesmo binário SHA256
`6df60219ced71276479606d2a9eeb2d824921bd902426c7a75254f7da20a0e14`:
`reports/native-active-interaction/attempt-22/result.json` confirma cortes256/512
com 12 seções iguais na reabertura e no final4096, save legado carregado e
25 originais intactos. `reports/native-menu-checks/seated-9/result.json` confirma
Sit/save/reopen0/Stand nas Casas05/07, bytes iguais ao reabrir e fim em pé, sem
vínculo ao assento; 26 originais intactos. Os 14 processos terminaram e perfis
isolados foram removidos. Nenhuma tela virtual criada. Esses cenários reais são
regressões de continuidade; não demonstram nesting dentro de uma interação do
jogo. Revisor independente aprovou o contrato estático sem bloqueadores.



### Save/reopen de ação em andamento (commit `4a6e0c2`)

O save nativo de família registra todos os bancos de código
instalados em cada VM, inclusive os ociosos necessários à ação seguinte. Corrige
o caso em que a geladeira cria comida e a VM do morador precisa desse código ao
retomar PushInteraction. Também foram corrigidos registros de VM ao remover
objetos, desfazer criações e rejeitar IDs duplicados durante restauração.

Gate House07 da tentativa 7 e nove CTests passaram; a tentativa 9 repetiu a
comparação depois da alteração da fila descrita abaixo. Dois saves, nos ticks
256 e 512, foram reabertos em processos novos; os 12 blocos do save e o estado
final coincidiram com a execução sem interrupção até tick 4096. Save legado
também carregou; 25 arquivos originais mantiveram seus hashes. Revisor independente
aprovou o rastreamento dos bancos instalados e seu ciclo de vida. Detalhes em
[native-active-session.md](native-active-session.md). Isto prova somente a
continuação headless da interação geladeira/comida na Casa 07. Não é paridade
Simitone nem build jogável. A mudança de C# existente e outros WIP citados abaixo
permanecem preservados e fora do commit.

### Prioridades e aviso de PushInteraction (commit `ecc7c6d`)

A etapa atual decodifica as sete prioridades TS1, guarda prioridade/modo/flags na
fila C++, aplica as regras de inserção suportadas e grava esses dados no formato
de save v2. Saves v1 são lidos como FIFO legado; ao chegar a primeira ação TS1
nova, a fila preserva a ordem antiga e passa para a ordenação por prioridade.
Seis CTests focados passaram. O VM pré-checa rotina, propriedade do código e
limite de pilha, e só publica o estado da ação depois de criar seu quadro. A cena
House07 foi repetida em attempt-15: os
saves dos ticks 256 e 512 reabriram com todas as 12 seções iguais; as duas
continuações coincidiram com a execução contínua até tick 4096. Save legado
carregou e 25 hashes originais permaneceram iguais. O novo teste nativo verifica
que uma prioridade maior e uma mudança da prioridade ativa para zero marcam a
ação para notificar; `IdleForInput` trata o aviso no seu próximo horário já
agendado. Detalhes e limites em
[native-queue-migration.md](native-queue-migration.md).

### Regras de disponibilidade antes de iniciar ação (commit `ee42558`)

PushInteraction agora mantém a rotina de check da TTAB junto à ação. Antes de
iniciá-la, o C++ executa o check com o dono de código correto e bancos TEMP/TempXL
compartilhados. Se retornar falso, remove só aquela candidata e tenta a seguinte;
os efeitos do check permanecem, como na fonte. O save v3 conserva essa identidade
e lê v1/v2 sem inventar checks. Há orçamento próprio de execução e recusa explícita
de primitivas ainda não suportadas nesse contexto. Filtros de permissão de UI,
cobertura completa de primitivas e ações aninhadas continuam pendentes.

Build nativo e seis CTests passaram; o teste final de PushInteraction entre
moradores também passou. Evidência: `reports/native-queue-checks/attempt-1/`.
House07 repetida em `reports/native-active-interaction/attempt-16/result.json`:
os dois cortes reabriram com 12 seções iguais e chegaram ao mesmo estado do
controle até tick 4096; save legado carregou, originais intactos, processos e
perfis encerrados. As fixtures provam os checks exercitados; a casa prova somente
continuidade daquele fluxo, sem equivalência ao jogo completo.

### Ações escolhidas no menu (commit `ef9dd70`)

A fila recebe flags TTAB, prioridade de jogador e rotina de disponibilidade.
Menu e despacho compartilham a regra TS1 de crianças/adultos, visitantes, cães,
gatos e debug. Antes de iniciar, a fila verifica novamente o estado atual.
Codec v4 registra essa exigência por ação; saves v1/v2/v3 não ganham regras novas
inventadas. O menu acompanha o UID nativo da requisição para distinguir uma ação
rejeitada antes de começar da conclusão de outra ação automática. Bancos de
código precisam conservar ação e checks exatos, inclusive ao reabrir saves.
Build nativo e oito CTests passaram (`reports/native-menu-checks/attempt-1/ctest-final.log`).
House07 geladeira repetida em `reports/native-active-interaction/attempt-17/result.json`:
12 blocos iguais ao reabrir nos ticks 256/512 e nas continuações até 4096; save legado
carregou, 25 arquivos originais intactos e processos/perfis encerrados.
O perfil antigo de um morador passou sentar/reabrir/levantar na House07 em
`reports/native-menu-checks/seated-2/result.json`. A falha House05 descoberta nesse
checkpoint foi resolvida pela etapa abaixo; os relatórios anteriores continuam
preservados como evidência das falhas, não como resultado do binário atual.
Esse perfil antigo não admite salvar qualquer ação manual pendente: seu suporte
a donos dinâmicos continua limitado a rotas e à continuação sentada dedicada.
O caminho de família usa o registro completo de bancos de código.

### Continuação do Sim sentado no banco (commit `6dc587e`)

RunTreeByName destino 2 reutiliza um banco já instalado na VM chamadora somente
quando source/asset correspondem ao objeto alvo e a rotina existe nesse owner.
Não exige que o banco tenha uma thread própria. Tuning privado e semiglobal é
carregado do recurso exato; ausência de link usa privado e chave ausente no
recurso carregado vale zero, conforme a referência. Dados desconhecidos não
viram aprovação nem valores fabricados.

A continuação sentada aceita subrotinas comuns do Main sob o owner Sit declarado,
incluindo espera de animação: valida contexto, frame, ticket, roster e fingerprint.
Não aceita interação ativa nem owner estrangeiro. Esses owners são gravados pelo
registro próprio de sentado, sem serem confundidos com Route45. Antes de salvar,
o host exige que as rotinas não-globais em execução pertençam ao banco que será
recarregado. Na restauração, registra os recursos antes de resolver a animação.

Build e três CTests focados passaram (`reports/native-tree-owner/attempt-1/`).
`reports/native-menu-checks/seated-8/result.json`: House05 banco173 e House07
cadeira128 completam Sit, save, reopen em processo novo (bytes iguais em tick0)
e Stand; terminam em pé e sem vínculo ao assento. 26 originais intactos e todos
processos/perfis encerrados. O gate original House05 também passou em idioma1,
com save512/reopen512/Stand até1024; o teste duplo usou idioma14 e save1024.
Binário SHA256 `2ea0d52dd5ae652917ed9118b0ae31e555388c26d2e9980f8466e198940a0142`.
House07 geladeira também passou em `reports/native-active-interaction/attempt-21/result.json`:
cortes256/512 com 12 seções idênticas ao reabrir e no final4096, save legado
carregado, 25 originais intactos e processos/perfis encerrados.
Revisão independente conferiu código e contratos; fixtures não substituem os
gates reais. Não é suporte genérico a qualquer ação pendente ou paridade de jogo.

Limites restantes de RunTreeByName: destino0, destino1 geral, instalação de
bancos arbitrários ainda não carregados, precedência de STR local semiglobal e
argumentos extras além dos quatro TEMP ainda precisam de cobertura própria.

Próximo passo: ampliar a cobertura real de checks e o estado de execução para o
prefixo protegido de ações, AllowPush aninhado, cancelamento e RunImmediately.
Não chamar a fila parcial de paridade de jogo.

A orientação mais recente exige não usar C#. A frente atual conecta o executor
compartilhado diretamente ao `Vm` C++ e conclui bancos TEMP/TempXL nativos.
O candidato .NET e seu WIP permanecem preservados, sem novos builds ou alterações
nesses arquivos nesta retomada. A antiga estratégia de ampliar a ponte para
validar cada componente não é a direção vigente.

Base publicada: `4a6e0c22eb1e3c86f644bf790f7d6275d05f69e0`. Consultar Git/OMM
para a publicação posterior. Detalhes: [native-state-integration.md](native-state-integration.md).
O restante deste documento descreve checkpoints anteriores e não autoriza
retomar desenvolvimento em C#.

Reforço humano posterior: a migração integral deveria ter sido o caminho
principal desde a decisão de escrever um motor nativo. Não interpretar o
trabalho atual como autorização para continuar o Simitone gerenciado ou limitar
o port a bibliotecas. Entregar sistemas integrados em C/C++, aproveitando o
código TS1 de referência e o C++ existente. A meta consultada permanece ativa.

`odh-person` C++ usa o executor compartilhado. A comparação inicial produziu
1.713 passos idênticos com o executor anterior e o novo, isolando uma falha
anterior de idioma. A busca de nomes agora segue a seleção de tabela TS1, e
PushInteraction foi extraído para C++ compartilhado com destinatário por morador,
identificador TTAB real e ordem correta de inserção na frente da fila.

Teste final `reports/native-vm-resident-continuation/`: Casa 07, idioma 14,
morador adicional 105, geladeira 112/ação 4103. Aos 4.096 ticks a ação retornou
verdadeiro, a fila estava vazia e os objetos temporários de execução removidos.
Salvou, reabriu sem avançar e continuou até tick 5.120; três processos terminaram
com código zero, sem falha do host. Dados originais preservados e perfis/PIDs
encerrados; nenhuma janela foi aberta. Dez CTests focados passaram. Binário
SHA256 `2425bf514ae00260e2499315b9af88922aa533117a02da4a6c4dfe75068bc9bd`.
Novos bancos de estado são componente separado testado, ainda não usados pelo
`Vm` existente. O save foi feito depois da ação, não durante sua execução.
Prioridades completas, modos especiais de fila, UI original e paridade de jogo
seguem incompletos. Não promover esse fluxo a versão jogável equivalente ao Simitone.

Próximo desenvolvimento: continuar portando sistemas TS1 para o mesmo executável
C++, incluindo filas/prioridades e ações ainda não suportadas, preservação de
ações em andamento nos saves, famílias/vizinhança/expansões e apresentação nativa.
Usar os limites documentados para escolher fluxos reais; não retomar a ponte C#.

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
