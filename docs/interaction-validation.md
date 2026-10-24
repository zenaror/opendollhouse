# AllowPush, fila limitada e retorno ao Main

## TS1 Budget — primeiro perfil de orçamento familiar

Opcode25 no runtime TS1 do Simitone é `VMTS1Budget` (no TSO, o mesmo número
significa TransferFunds; não confundir os perfis). A referência lê o valor
indicado pelo owner/escopo, usa orçamento da família ativa ou zero se não houver
família, retorna false quando o saldo anterior é negativo e, sem `JustTest`,
grava `saldo - valor`; a flag `Subtract` inverte o sinal. O tipo da despesa e o
tipo de transferência não alteram esse handler TS1.

`src/ts1_budget.cpp` implementa esse contrato com leituras de palavra já
disponíveis no `VmMemory`, inclusive Tuning, argumentos, locals e temps. O host
Linux fornece o saldo FAMI efetivo da família ativa. Para `Have A Snack` de
House07, os operandos `[3,26,0,0,1,0,3,0]` usam `JustTest`; o probe passou por
essa instrução e avançou até `GotoRelativePosition` opcode27. CTests focados
`ts1_budget`, `vm`, `vm_interaction_return` e `saved_interaction_discovery`
passaram 4/4.

O writer está deliberadamente ausente no host até existir persistência durável
para alterações de FAMI; uma chamada que tente mudar o saldo falha em vez de
perder a mudança ao salvar. O escopo 42 (`TempXL`, valor de 32 bits) também segue
unsupported, pois essa memória ainda não existe na VM ODH. Esse checkpoint não
prova que a refeição começou ou terminou.

Este documento reúne checkpoints em ordem histórica. A descrição de 2026-10-13
abaixo é apenas o estado inicial da fila; ela foi superada por integração
posterior do Main com ações de objetos no host Linux e pelo teste House07
Sit/Stand no fim deste documento. Essa integração ainda não prova a escolha da
ação por clique real na janela, nem uma sessão completa jogável.

## Regra de referência

Na revisão FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2,
[VMThread.Pop/HandleResult](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMThread.cs)
remove o frame especial Interaction, conserva o resultado para diagnóstico,
encerra a ação da queue e substitui o retorno por CONTINUE_NEXT_TICK.
HandleResult agenda +1 tick e não muda o instruction pointer do pai.
Assim, true/false do filho não escolhe uma branch do Main: ele reexecuta
IdleForInput. RunImmediately é uma exceção da referência, não implementada aqui.

[VMIdleForInput](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMIdleForInput.cs)
primeiro decrementa o argumento indicado por um índice LE16 assinado. Quando
AllowPush=1 e uma ação pode ser empilhada, entrega o controle sem mudar o PC.
Sem ação empilhada, uma queue pendente pode notificar o caller; interrupt ou
argumento negativo retorna true. Caso contrário, AllowPush1 agenda o próximo
tick e AllowPush0 pode esperar até o deadline. ScheduleIdleStart=0 tem o
sentinela da referência: nesse caso o decremento é1, mesmo quando o relógio
avançou mais de1. O arithmetic wrap de16 bits permanece explícito.

A notificação usa `NotifiedByIdleForInput = 1 << 6` em Flags/ObjectData8,
conforme [VMEntity](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMEntity.cs).
O wrapper nativo global281 confirma scope3/index8/flag7, com teste e limpeza
na primitiva Expression (índice de flag contado a partir de1).

## Kernel

`VmCallReturnMode::interaction_next_tick` distingue uma chamada de ação da
chamada ordinária. O frame guarda modo e ticket de invocação. Ao devolver
FE/FF, o kernel remove o frame da ação, publica `VmInteractionReturn` com
resultado/ticket/owner/generation, e fica em `VmStatus::yielded`. O PC e o
contexto do pai permanecem iguais; run não avança enquanto estiver yielded.

`resume_interaction(ticket)` aceita só a fronteira atual e restaura running.
O kernel não possui relógio: o adapter é responsável por permitir esse resume
apenas no próximo tick. Reset de start invalida a fronteira; tickets stale ou
duplicados não a consomem. Interações aninhadas são recusadas; chamadas
ordinárias dentro da ação continuam permitidas.

`retry_pending(ticket, optional_write)` reativa a mesma instrução de uma espera,
sem percorrer sua branch. Isso permite reavaliar IdleForInput quando chega uma
ação ou vence seu deadline. Reentry, tickets e índices inválidos são rejeitados.

`Vm::install_owner(owner, routines)` permite anexar código privado/semiglobal
de um recurso selecionado depois da criação da VM Main. A operação só ocorre
fora de `run()`; owner zero, duplicatas, escopos inválidos e limite de owners
são recusados. Ela nunca substitui uma rotina nem altera frames, PC, temporários,
tickets, status ou geração. Os owners existentes permanecem imutáveis. O teste
sintético estaciona Main em uma espera, instala código, completa o ticket
original e executa uma chamada de ação em outro owner. Isso valida o mecanismo
da VM; o host Linux ainda não carrega owners de objetos nem enfileira ações.

Calls agora podem publicar uma escrita validada no frame do pai: IdleForInput
precisa decrementar o argumento antes de instalar a ação. O kernel valida a
escrita e o filho antes de publicar ambos. Usa o índice do pai depois de push,
sem manter um ponteiro que a realocação da stack poderia invalidar. Esse avanço
substitui a restrição inicial de call sem write no checkpoint de code owners.

## Adapter InteractionQueue

`odh_interaction` depende de World e VM, sem APIs de plataforma. Cada controller
liga um avatar/generation a uma VM estável; recebe uma FIFO de VmCall com
caller, callee, StackObject, code owner e argumentos explícitos. A capacidade
padrão32 inclui a ação ativa na frente. Não há descoberta ou seleção automática
de TTAB. UID/tickets são monotônicos; resultado recente é um registro limitado,
sem histórico crescente.

`enqueue` captura a geração do alvo e rejeita caller/alvo ausente, identidade
fora do perfil, excesso de argumentos/capacidade e Main terminal. No dispatch
17, AllowPush1 entrega a frente ao kernel com modo interaction_next_tick.
AllowPush0 com queue pendente seta o bit de notificação em Flags8 conhecido e
retorna true; não inventa o banco quando ele não existe.

O host deve chamar `observe()` antes de cada run ou continuação de orçamento e
depois de execução/conclusão de serviço, e deve respeitar false antes de executar
mais instruções. Isso valida o avatar e o alvo ativo por geração, incluindo
remoção/recriação com o mesmo ID. Após um retorno válido, observe remove a
frente no mesmo tick do host, registra o resultado e prepara resume para +1.
Sem esse gate, chamar run diretamente após mutação de World pode acessar uma
entidade substituta: o kernel resolve código, não governa a vida de entidades.

`tick` exige passos consecutivos dentro de uint32, sem duplicação/pulos. Ele
retoma a fronteira de interação na data correta e reativa o ticket de idle ao
vencer o deadline, chegar uma ação ou ocorrer interrupt. Pause do host não deve
chamar tick. Reset da VM limpa queue/ativo/wait/fronteira; caller reciclado
invalida o controller. Falha ao instalar ação mantém a solicitação e o fault
para diagnóstico, sem registrar completion falsa.

Ações com espera usam os serviços normais da VM. O controller não executa ação
diretamente nem interpreta animação/postura. A remoção da queue ocorre no host
após observar o resultado; não instala standing-loop no retorno.

## Gates

- `vm_interaction_return`:54 checks sintéticos de PC/contexto preservado,
  resultado true/false ignorado pelo pai, chamadas ordinárias dentro da ação,
  orçamento, espera, eventos/generation/tickets, reentry e call+write atômico.
- `interaction_queue`:47 checks sintéticos de FIFO/capacidade, próximo tick,
  duas ações com resultados diferentes, countdown, deadline/sentinela, chegada
  durante espera, interrupt, Flags, reset/rebind, reciclagem de caller/alvo
  antes e durante ação, e faults sem falsa completion.
- `odh-vm-owner-tests`:52 checks sintéticos, incluindo registro aditivo em Main
  suspensa, rejeição de substituição/reentrada/escopo inválido/limite e execução
  da cadeia privada 4097→4100 no owner instalado.
- A revisão independente identificou a lacuna de geração do alvo ativo. Ela foi
  corrigida no gate de validação e coberta pelo teste que suspende antes do
  primeiro dispatch da ação, remove/recria o alvo e recusa mais execução.
- Leitura externa opcional executa o BHAV281 real de Global.iff, entre um Main
  fixture e ação fixture owner22. São6 checks: entrega por AllowPush1, retorno
  yielded com wrapper281/PC4 preservado, resume no tick1, retorno pela própria
  lógica de flags do wrapper e preservação de bits não relacionados. Passou
  em Debug e ASan/UBSan; não é execução do interpretador/executável original.

```sh
./build/odh-interaction-tests --native-global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

O teste nativo usa os recursos externos explicitamente; nenhum BHAV comercial
é versionado. Hash do Global.iff: `bdfd8456abceaa3e44f0a9754815248ea1e80c7b0df4fbaff53843fae3731478`.
A execução normal de CTest só usa fixtures sintéticas.

Debug e ASan/UBSan:40 testes passam, um teste X11 é skipped por acesso ao display
no sandbox. LSan desligado (`detect_leaks=0`), sem validação de vazamentos.
O probe nativo de cadeira anterior continua94 dispatches/662 ticks/Comfort3;
todos os campos JSON da simulação permanecem iguais à baseline.

## Cobertura ainda aberta

Essa FIFO é um perfil de ações já fornecidas para execução, não a queue completa
da referência. Prioridades/autonomia/condições TTAB, modes/flags especiais,
RunImmediately, nested interactions, cancelamento, eventos NotifyIdle da ação,
limpeza administrativa completa (NonInterruptable, motive changes e flags de
cancelamento), eligibility/routing, callback de conclusão e persistência seguem
pendentes. Interrupt desta API só acorda Main idle, não cancela ação ativa.

O host Linux deve carregar os recursos exatos do objeto selecionado, chamar
`install_owner` antes de enfileirar e usar esse fluxo em vez de iniciar cada
ação com `start`. As dependências alcançadas pelo Main real
(Sleep, RNG, scopes, MiscSTR156 e resources do avatar) ainda precisam de gates.
O idle em pé, o lote mínimo funcional e a fidelidade visual continuam não
validados. Não há evidência de consoles ou comparação com execução original.
# Avaliação isolada de TestFunction

Após a descoberta estática do TTAB/TTAs do objeto selecionado, `tools/saved_interactions.hpp`
avalia os TestFunctions privados do mesmo recurso numa VM isolada. A VM recebe o
`World` atual através de um callback de leitura; não recebe callback de escrita
nem primitivas de serviço. Rotinas de outros owners, primitivas não tratadas,
falhas de leitura e limites de execução resultam em `unsupported`, nunca em
elegibilidade. O probe usa até 32 linhas por seleção, 128 instruções por teste e
4.096 instruções no total; o argumento inicial é zero para a chamada manual.

O teste 4098 da cadeira `DiningChairs.iff` foi executado pelo helper sobre um
mundo sintético mínimo: cadeira vazia retornou elegível em 3 instruções; cadeira
com o chamador no slot 0 também retornou elegível. Testes sintéticos verificam
retorno falso, primitiva sem handler e orçamento zero. O harness `odh-bhav`
separadamente confirma as ramificações do mesmo TestFunction com estados
construídos. Estas são evidências da VM ODH e de um cenário construído, não uma
prova de comportamento do executável original.

O avaliador isolado também reconhece agora o opcode 32 (`Test Object Type`),
usando a identidade explícita do objeto e, quando disponível, do seu mestre.
Isso corrige uma lacuna de avaliação: antes, o teste BHAV 4166 de `Beds.iff`
aparecia como primitiva não suportada. No House07 comercial, o mesmo teste
passou a retornar falso para o morador usado na tentativa; os demais testes da
cama também retornaram falso e nenhuma ação foi enfileirada. A instrução
estática de BHAV 4166 primeiro compara o tipo do próprio Sim com um GUID de
personagem específico, então esse resultado é uma recusa normal para este Sim,
não prova de uma ação de cama executada. Build completa e CTest passaram
229/229; os três testes que exigem janela ficaram pulados pelo display isolado.

Desde 2026-10-24, quando um TestFunction retorna falso, o avaliador registra a
última instrução executada (PC e opcode) no diagnóstico. Um teste sintético
confirma esse registro. Em uma nova execução headless do House07, morador 137,
cama 117, todos os cinco testes da cama ainda retornaram falso; agora o motivo
localizado foi: BHAV 4107 no PC 0/opcode 2, BHAV 4109 no PC 0/opcode 2, BHAV
4141 no PC 2/opcode 2, BHAV 4145 no PC 0/opcode 2 e BHAV 4166 no PC 0/opcode
32. A ação não foi enfileirada e o roteador não planejou nem executou uma
passagem entre quartos. Os SHA-256 de `UserData/Houses/House07.iff`,
`UserData/Neighborhood.iff` e `UserData/LotZoning.iff` permaneceram idênticos
antes e depois. Um build Debug limpo em `/tmp` passou `saved_interaction_discovery`
e `linux_household_session_resume_cli` (2/2). Isto localiza a recusa no perfil
de avaliação ODH antes do roteamento; não demonstra a semântica do executável
original nem uma interação de cama funcional.

Na janela Main, um clique agora relata no JSON a elegibilidade calculada por
essa VM isolada junto de owner, ação, teste, rótulo, instruções e diagnóstico.
No preview de lote salvo, Tab/Shift+Tab percorre as linhas elegíveis com perfil
de flags suportado e ActionFunction privado; Space/Return solicita a linha
indicada no título. A elegibilidade é reavaliada ao percorrer e imediatamente
antes de enfileirar. Flags zero são aceitas; `AllowVisitors` (bit `0x1`) é
aceito depois de conferir o perfil do ator. Um visitor actor
(`PersonType==1 && GreetStatus<2`) requer esse bit.
Se a linha selecionada deixou de ser elegível quando a lista é recalculada,
Tab/Shift+Tab seleciona a primeira linha ainda elegível; o envio também revalida
a linha selecionada antes de enfileirar.
Pets, perfil de ator ausente e bits desconhecidos continuam fora do perfil.
O recurso exato
`source`/`asset` fornece o banco BHAV privado; sua identidade recebe um CodeOwner
de alta faixa, instalado na VM Main já viva. A fila valida novamente actor,
target e geração antes de aceitar caller/target/StackObject. Não reinicia Main.
O preview oferece um seletor mínimo por teclado e título, ainda sem pie menu.
O JSON diferencia `not_requested`, `queued`, rejeição, falha da VM e
retorno true/false. Um retorno BHAV apenas informa o resultado do filho; não
prova que o avatar sentou nem que o objeto atualizou os slots corretamente.
Os owners da VM são append-only e o limite padrão é 64 incluindo o owner zero;
uma sessão pode anexar no máximo 63 owners antes de novas ações serem recusadas
por capacidade. O preview instala um owner por recurso/ação, não por clique.

O recurso da ação ainda não ganha handlers implícitos. A cadeia observada
4097→4100→4099 entra em Routing opcode45; o Main host atual ainda não integra
esse controller nesse caminho, então uma tentativa pode parar como `vm_fault`.

No teste posterior de X11 com House07, a fila foi consumida e a VM alcançou
essa cadeia aos 103 ticks. O receipt terminou em `unsupported primitive 45`,
com owner privado `0x80000000`, caller 105, callee/StackObject 128 e fila
ativa. Isso confirma execução BHAV até Routing no perfil ODH; não confirma
movimento, chegada ao SLOT ou comportamento do executável original.
Leitura somente estrutural de BHAV4099 em `Objects.far` nas instalações The Sims
e Legacy Collection dá instruções idênticas; o payload SHA256 é
`c6ab9b5eed558746c2997bd0f29d741ba9907e38062bbcfdb1cd2da86ea815f6`. Em PC2–5
as comparações usam MyPersonData[37] contra Literals 1, 4, 16 e 64; falha em
todas chega ao opcode15 em PC6. O PC14 opcode15 só é alcançado quando Snap46
em PC13 retorna false. A referência Simitone mapeia scope18 como MyPersonData,
scope7 como Literal e índice37 como `RouteEntryFlags` em
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Engine/Scopes/VMVariableScope.cs`
e `Model/VMPersonDataVariable.cs`; nessa referência opcode15 é breakpoint.
Isso identifica ramos e nomes de campos da referência, mas não executa nem
estabelece a semântica do opcode15 no executável original. Comando reproduzível
para a decodificação estrutural:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 4099
```

O PC normal após Route45 ainda requer interpretar corretamente RouteEntryFlags,
executar Animation44 e avaliar Snap46. Ações adicionais, serviços e o retorno
stand continuam em aberto.
Prioridades, flags/modes TTAB, outras ações, cancelamento e o retorno ao ficar
de pé continuam em aberto.

Uma sessão comercial isolada posterior usou `--headless-resident-interaction
137 128` na House07. A linha foi enfileirada (`action=4097`, retorno true) e,
após 512 ticks, o morador 137 estava anexado ao SLOT 0 da cadeira 128, com a
animação `a2o-idle-sit-3`. A execução usou diretórios XDG temporários e não
gravou no save original. Uma sessão baseline com os mesmos dados e ticks teve
os mesmos valores finais para todas as necessidades. Isso valida esse caso
headless de sentar numa cadeira comercial; não demonstra mudança contínua de
necessidade, a cadeia completa de roteamento da janela nem comportamento do
executável original.

O harness também reproduziu o clique/pick/Space e o pedido de levantar pela
janela, usando entradas artificiais sem abrir display:

```sh
python3 tests/house07_interaction_integration.py \
  --binary build/linux-checkpoint/odh-person \
  --game-root '../referencias/The Sims' \
  --window-input --window-stand --ticks 512 --stand-tick 270
```

O harness passou: a ação 4097 foi enfileirada e concluída; no tick 270, a
entrada 27 executou a ação 4102 e deixou o morador de pé, fora do SLOT. A
captura headless de 320×240 mostrou a cena e o morador. Isso valida os caminhos
de seleção, entrada simulada, sentar/levantar e renderização dessa amostra; não
valida teclas físicas na janela X11, cujo teste separado continua falhando.

## Ação Go Here e destino temporário

`Destination.iff` das instalações The Sims e Legacy Collection declara o
marcador GUID `0x7C4`; sua TTAB v9 inclui Go Here (ação BHAV4096, teste
BHAV4109). O começo da ação chama a rotina global272 e usa GetDistanceTo
(opcode11) com o destino no StackObject e o caller lido por MyObject[11]. O
adapter aceita agora esse formato exato de operandos, além do perfil anterior;
`distance_to` testa que o resultado compara posições distintas corretamente.
As duas instalações têm a mesma tabela/rotina Go Here nessa leitura estrutural.

O caminho de execução agora cria um destino temporário 0x7C4 com os bancos
iniciais que a rotina precisa, enfileira a BHAV4096 real e remove o destino ao
terminar ou quando o host interrompe a sessão. No jogo, clicar no chão escolhe
o ponto e Space/Enter solicita a caminhada; clicar em um objeto mantém a
interação normal. Backspace pede para cancelar Go Here; a janela deixa a ação
salva encerrar pelo seu caminho de falha antes de limpar a fila e o marcador.

Validação reproduzível com os arquivos locais do jogo:

```sh
python3 tests/house07_route45_integration.py \
  --binary /tmp/odh-build-go-here/odh-person \
  --game-root '/home/rafael/_PROJETOS/TheSims1/referencias/The Sims' \
  --go-here 27 26 1 --ticks 214
```

No modo sem janela, BHAV4096 retornou `true`; o avatar saiu de
`[392,440]` e chegou ao centro do tile escolhido `[440,424]` após 48 passos.
O destino temporário foi removido ao final. Em uma janela X11 do próprio
OpenDollhouse, um clique sintético enviado diretamente ao app em (190,125)
selecionou o chão `(23,26)`, cômodo 3. Space enfileirou o pedido. A janela
precisou ficar aberta por cerca de um minuto para o Main concluir a ação: o
avatar saiu de `[392,440,1,3]`, andou 16 passos até `[376,424,1,7]`, BHAV4096
retornou `true`, a fila ficou vazia e o destino temporário foi removido. Duas
tentativas anteriores foram encerradas após cerca de nove segundos, cedo demais
para esse teste lento. Os eventos foram enviados diretamente à janela, sem
mover o cursor físico.

Esse teste da janela revelou que o mapa de seleção do chão era perdido quando
a imagem era transferida para a tela. O mapa agora fica guardado à parte e os
testes de seleção confirmam que sobrevive à transferência.

### Go Here atravessando cômodos

O Go Here salvo agora pode aceitar um destino no mesmo piso em outro cômodo. A
rota espera o planejador de portas, executa a BHAV Portal15 da porta salva,
atualiza o mapa de obstáculos do cômodo novo e só então continua até o ponto
escolhido. Antes deste ajuste, o destino distante era recusado porque ficava
fora do mapa do cômodo inicial. O suporte continua limitado a cômodos
conectados por uma sequência de portas que o inventário consegue validar; não
cobre mudança de piso nem garante que todo estado de porta seja fiel ao jogo
original.

Em House07, o avatar começou em `[392,440,1,3]`, no cômodo 3. A ação real
`Destination.iff:4096` levou-o ao tile `(20,20)` do cômodo 1, chegando em
`[328,328,1,7]` após 126 movimentos. O host planejou e executou uma Portal15,
confirmou uma mudança de cômodo e atualizou o mapa quatro vezes. Um teste de
unidade limita esse caminho ao formato exato do Go Here salvo; os outros perfis
de GotoRelativePosition continuam na rota local. Reprodução com dados externos:

```sh
python3 tests/house07_route45_integration.py \
  --binary /tmp/odh-race-check/odh-person \
  --game-root '/home/rafael/_PROJETOS/TheSims1/referencias/The Sims' \
  --go-here 20 20 1 --go-here-expect-cross-room --ticks 4096
```

Também passaram um destino na sala inicial (16 movimentos, sem porta) e o
destino ocupado pela mesa 106 (zero movimentos, falha de caminhada preservada).
São testes headless que usam a Main e recursos salvos. Uma validação posterior
no preview X11 confirmou a seleção e o passeio entre cômodos por eventos
sintéticos enviados diretamente à janela, sem mover o cursor. Para que o clique
seja associado a um cômodo, carreguei explicitamente os 23 arquivos externos
`.flr`/`.wll`; a tentativa anterior sem esse catálogo não tinha mapa de cômodos
e seus 377 cliques não selecionaram chão. Também omiti `--ticks 214`, que havia
congelado a janela no limite estático. Com o preview interativo, o pixel do tile
`(20,20)` foi reconhecido como level 1, room 1; Space iniciou Go Here, a janela
mostrou `Go Here arrived`, e o JSON confirmou `[392,440,1,3]` → `[328,328,1,7]`,
126 movimentos, uma Portal15 e quatro atualizações do mapa.

Em outra execução, enviei Backspace três segundos após iniciar Go Here para o
mesmo tile. A janela mostrou `Go Here cancelled by player`; o resultado final
registrou 42 movimentos, uma Portal15 planejada/retornada/transição, status de
rota `cancelled` e posição `[408,398,1,0]`. A BHAV4096 retornou `true`, como no
teste headless de cancelamento; a rota separada indica que o movimento foi
cancelado. Isso confirma o cancelamento pela interface ODH, não fidelidade de
tempo ou de animação ao jogo original. A interface no segundo andar e outras
escadas seguem sem validação.

Um chão bloqueado no mesmo cômodo, `(24,28)`, foi testado pela BHAV4096 salva.
O avatar não se moveu, a fila terminou vazia e o marcador sumiu. O jogo chamou
Global BHAV398, embora BHAV4096 tenha retornado `true`; por isso o host informa
separadamente que a caminhada falhou.

Também foi testado um destino ocupado por mobília. O objeto salvo 106 é uma
mesa de jantar (`DiningTables.iff`), localizada no centro do tile `(25,29)`.
O teste confere a posição e o arquivo do objeto e que seu ID consta na lista de
obstáculos usada pelo passeio. Ao pedir Go Here para esse mesmo tile, o avatar
não se moveu, a caminhada falhou, Global BHAV398 foi chamada, a fila terminou
vazia e o marcador sumiu. BHAV4096 ainda retornou `true`. Reprodução:

```sh
python3 tests/house07_route45_integration.py \
  --binary /tmp/odh-build-release/odh-person \
  --game-root '/home/rafael/_PROJETOS/TheSims1/referencias/The Sims' \
  --ticks 256 --go-here 25 29 1 \
  --go-here-expect-route-failure \
  --go-here-blocker-id 106 --go-here-blocker-asset DiningTables.iff
```

Também foi testada uma mudança no mundo enquanto Go Here caminhava. O teste
move uma entidade salva para perto do caminho no passo 40. O avatar já havia
andado dois passos, para `[394,440,1,2]`; a rota parou ali, chamou Global
BHAV398, esvaziou a fila e removeu o marcador. O retorno `true` de BHAV4096 não
foi tratado como chegada. Isso valida a parada causada por uma mudança de
obstáculo durante a caminhada. A reprodução é:

```sh
python3 tests/house07_route45_integration.py \
  --binary /tmp/odh-build-release/odh-person \
  --game-root '/home/rafael/_PROJETOS/TheSims1/referencias/The Sims' \
  --ticks 256 --go-here 27 26 1 \
  --go-here-mutation 106 405 440 1 2 40 \
  --go-here-expect-interruption
```

Também foi testado o cancelamento do jogador antes do primeiro passo. O pedido
entra na BHAV398 salva, o avatar fica em `[392,440,1,3]`, a fila esvazia e o
marcador some. A referência Simitone chama o código de falha `4` de
`Interrupted`; ainda falta confirmar esse número diretamente no executável
original. Esse teste prova o fluxo atual do host ODH, não a fidelidade final do
número.

O pedido pode chegar enquanto uma ação interna da rota ainda está aberta. Nesse
caso, o controlador guarda o pedido, espera essa ação devolver o controle e só
então chama a BHAV398. Um teste de unidade cobre essa espera; a integração
House07 cobre cancelamento no começo da rota:

```sh
python3 tests/house07_route45_integration.py \
  --binary /tmp/odh-build-release/odh-person \
  --game-root '/home/rafael/_PROJETOS/TheSims1/referencias/The Sims' \
  --ticks 256 --go-here 27 26 1 \
  --go-here-interrupt-at-tick 2 --go-here-expect-cancelled
```

Também foi testado o cancelamento logo após o primeiro passo real da caminhada.
O avatar andou até `[393,440,1,2]`, o pedido interrompeu a rota ali, a BHAV398
foi chamada e a fila terminou vazia. Esse gatilho conta passos confirmados, em
vez de depender do tempo do relógio:

```sh
python3 tests/house07_route45_integration.py \
  --binary /tmp/odh-build-release/odh-person \
  --game-root '/home/rafael/_PROJETOS/TheSims1/referencias/The Sims' \
  --ticks 256 --go-here 27 26 1 \
  --go-here-interrupt-after-movements 1 --go-here-expect-cancelled
```

Na janela House07 completa, eventos de teclado dirigidos à janela selecionaram
o chão em `(23,26)`, iniciaram Go Here com Space e cancelaram com Backspace.
O título mostrou `cancelled by player`; o avatar não saiu do início, a rota
ficou inativa, a fila esvaziou e a VM registrou a BHAV398. Os eventos foram
enviados à própria janela sem mover o cursor. Isso cobre o cancelamento no
começo da rota; não conta como teste de cancelamento depois de passos de
caminhada.

Separadamente, o backend X11 confirmou que Backspace gera um pedido, não se
repete enquanto fica segurado e volta a funcionar após a liberação. Isso testa
a entrega da tecla, sem substituir a integração House07 acima.

O ponto `(25,26)` usado numa tentativa anterior era a porta salva 26
(`Doors.iff`), e o personagem conseguiu chegar ali; ele não foi contado como
prova de mobília bloqueando o destino. Naquela execução, escadas ainda não
tinham sido testadas; a validação posterior da House03 está registrada abaixo.

## Escadas da House03: análise e teste

A leitura salva da House03 contém um grupo de nove instâncias de escada nos níveis 1 e 2. As
definições estão em `GameData/Objects/Objects.far`, no recurso `stair.iff`.
Todas apontam para BHAV 4098 na entrada 1. Os patamares baixo e alto
(`0x79E2B825` e `0x821EB6EE`) também apontam para BHAV 4105 na entrada 15; as
outras seis peças não têm ação nessa entrada. No código FreeSO que acompanha o
Simitone, a entrada 15 é a porta especial que o planejador chama para mudar de
cômodo ou andar.

Leitura estrutural repetível:

```sh
build/linux-checkpoint/odh-wall-appearance \
  '../referencias/The Sims/UserData/Houses/House03.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --entry-point-probe
build/linux-checkpoint/odh-bhav \
  '../referencias/The Sims/GameData/Objects/Objects.far' stair.iff 4098
build/linux-checkpoint/odh-bhav \
  '../referencias/The Sims/GameData/Objects/Objects.far' stair.iff 4105 \
  --semiglobal '../referencias/The Sims/GameData/Global/Global.far' DoorGlobals.iff \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

O relatório mostra as entradas 1 e 15; os dumps BHAV mostram o conteúdo das
ações sem executar código. BHAV 4098 chama rotinas 280 e 4118. BHAV 4105
chama as globais 278 e 279, as privadas 4109 e 4110 e a semiglobal 8197. Os
rótulos dos recursos são “standard multi-tile entry”, “standard multi-tile
exit”, “portal core”, “portal wait” e “fail portal call”. O vínculo da escada
é um chunk GLOB de ID 1 chamado `DoorGlobals`; `Doors.iff`, a porta comum, usa
GLOB de ID 128 com o mesmo nome. O Simitone escolhe o primeiro chunk GLOB do
recurso. O ODH aceita esse vínculo único sem exigir o ID 128 e recusa recursos
com vários GLOBs, para não escolher um vínculo no escuro. A saída estática de
`odh-bhav` registra `glob_id=1`; nenhuma dessas rotinas é executada neste
probe.

Esses nomes e chamadas descrevem a preparação e o uso do portal, mas ainda não
provam qual passo move o Sim para outro piso.

No código de referência do Simitone, uma comparação do campo `MyObject.Room`
com o literal `1024` consulta o andar do Sim e o `LevelOffset` do objeto. O
comentário do código diz que essa exceção atende às escadas. A verificação
estática de `Sims.exe.bak` no Ghidra não revelou um endereço ou rotina confiável
para a troca de piso. A sondagem direta abaixo confirma a mudança de posição
no World ODH para a House03; não prova equivalência ao executável original.

`tree_table_id=65535` nas peças só indica que não há tabela de interações; não
deve ser usado para concluir que a definição do objeto está ausente. Esta
sondagem corrige essa interpretação anterior.

### Go Here subindo a escada — 2026-10-22

O Go Here salvo agora aceita um destino em outro andar quando a rota encontra
uma passagem válida num grupo salvo de escada validado. Na House03, o grupo tem
nove membros; o teste confirmou os dois patamares (IDs34 e38) e as outras sete
peças do mesmo grupo. Só patamares validados pelo grupo, posições dos andares e
ação EP15 podem chamar a passagem. A
distância entre pisos segue a regra TS1 presente na revisão local do Simitone:
`max(20 × diferença_de_andar, distância_no_plano + 5 × diferença_de_andar)`,
limitada à codificação exata do Go Here salvo.

`tests/house03_cross_floor_go_here.py` parte de `[320,424,1]`, usa o patamar
baixo, sobe e continua andando no piso 2 até o tile `(20,21)`, terminando em
`[328,344,2]`. O teste registrou 24 movimentos (nos dois pisos), uma chamada e
um retorno de Portal15, uma troca de cômodo/andar e quatro atualizações do
mapa. Ele usa um avatar sintético e a Main parada numa cópia temporária; os
dados comerciais permanecem somente leitura. Os testes focais `routing` e
`distance_to` passaram (2/2); a integração House03 repetiu subida, descida e
chegada ocupada sem mudança de andar. A build Debug integral passou e o CTest
completo teve 165 testes aprovados, um pulado (`linux_window_backend`) e zero
falhas; o teste pulado depende de uma janela gráfica. Isso valida o caminho
headless deste cenário, não o clique da interface no andar 2, outros lotes,
bloqueadores que se movem, comportamento do jogo original ou uma build jogável
completa.

Reprodução:

```sh
cmake --build /tmp/odh-linux-fresh --target odh-person odh-distance-to-tests -j2
ctest --test-dir /tmp/odh-linux-fresh -R '^(distance_to|routing)$' --output-on-failure
python3 tests/house03_stair_approach.py --binary /tmp/odh-linux-fresh/odh-person \
  --game-root '../referencias/The Sims'
python3 tests/house03_cross_floor_go_here.py --binary /tmp/odh-linux-fresh/odh-person \
  --game-root '../referencias/The Sims'
python3 tests/house02_cross_floor_go_here.py --binary /tmp/odh-linux-fresh/odh-person \
  --game-root '../referencias/The Sims'
```

### House02 — escada salva entre dois andares — 2026-10-22

O teste `tests/house02_cross_floor_go_here.py` inicia o avatar sintético em
`[376,456,1]`, ao lado da escada, e termina em `[408,392,2]`. O rastro contém
BHAV4113 com opcode46 (Snap da escada); houve 144 passos, duas chamadas e dois
retornos de passagens entre cômodos, com duas transições confirmadas. O começo
original `[392,456]` foi descartado porque fica dentro da peça 99 da escada:
seu retângulo salvo cobre `[384,448]–[400,464]`, então o mapa corretamente o
considera bloqueado. O registro de posições fica limitado e foi truncado; a
prova do andar final vem da posição final e do Snap, não de uma lista completa
de cada passo. O teste usa arquivos comerciais só para leitura, avatar
sintético e Main parada; não demonstra equivalência ao executável original,
animação visual nem interação por clique.

## Entrada de interação pelo mesmo caminho da janela sem display

O modo `--window-input` do teste House07 renderiza o lote com pixels de seleção,
acha um pixel que pertence à cadeira128 e alimenta dois `WindowInput` no mesmo
despacho usado pelo aplicativo: Button1 seleciona o objeto e Space solicita a
ação. A partir daí, o código de produção faz o pick, carrega TTAB, verifica a
ação disponível e enfileira a função privada. O teste exige que a seleção
continue sendo a cadeira128 e que Sit4097 conclua com resultado true.

Reprodução sem servidor gráfico:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/house07_interaction_integration.py \
  --binary /tmp/odh-current-normal/odh-person \
  --game-root '../referencias/The Sims' --window-input
```

Isso cobre o caminho interno entre entrada lógica, pick do pixel e ação, usando
os recursos comerciais apenas para leitura. Não testa como o sistema gráfico
entrega um clique/tecla real, nem uma sessão completa; para isso ainda é preciso
uma janela X11 acessível.

## Integração Sit/Stand em House07

`odh-person --headless-main --headless-interaction ID TICK` agenda a seleção
de um objeto salvo e passa pelo mesmo carregamento de TTAB, verificação de
elegibilidade e enfileiramento da janela X11. O teste de integração
`tests/house07_interaction_integration.py` usa a instalação externa do jogo;
nenhum recurso comercial é versionado. A opção `--headless-stand ID TICK`
agenda uma solicitação explícita de saída após Sit. Ela exige que a mesma
ação tenha retornado true, que o avatar ainda esteja em Posture1 anexado ao
objeto e que o alvo tenha a função privada carregada no entrypoint27. O host
enfileira essa função na mesma InteractionQueue e VM Main, sem reiniciar a VM.

Reprodução após compilar `odh-person`:

```sh
python3 tests/house07_interaction_integration.py \
  --binary ./odh-person --game-root '../referencias/The Sims'
```

O teste padrão avança até512 ticks. Para avatar105 e cadeira128, Sit4097
retorna true no tick240 e anexa o avatar ao container128/slot0. No tick241,
com a precondição sentada ainda confirmada, a solicitação consulta o
entrypoint27 da cadeira, encontra Stand4102 e o enfileira no Main ativo. Stand
retorna true no tick414; o Snap libera o avatar na posição [392,440,nível1,
direção0], remove o attachment e grava Posture0. O validador de saída usa a
grade de rota de sala preparada para o lote salvo, atualizada pela revisão
espacial; destino sem grade/sala correspondente ou célula não caminhável é
recusado. O Main continua em espera até tick512. O processo retorna3 para
sinalizar o orçamento de ticks, enquanto o JSON distingue o resultado de cada
interação, a postura e o attachment finais.

No checkpoint Sit-only, sem agendar Stand, o Main também foi observado até512
ticks e seguiu o ramo false de RunTreeByName na rotina8229/PC87 para
Animation44. Essa consulta não é uma expectativa do cenário encadeado: ao
enfileirar Stand no tick241, a sessão toma outro caminho antes desse ponto.

Isso prova uma cadeia controlada Sit→Stand em uma única VM Main, com dados
externos do jogo e callbacks ODH para a fila, animação, rota e Snap. A solicitação
Stand é um evento explícito de teste que enfileira a função do entrypoint27; não
prova que opcode20 ou o dispatcher completo do jogo original a disparariam.
O grid de sala valida clearance no perfil suportado, não todos os footprints,
raycasts ou regras do original. Dispatch de árvores nomeadas existentes,
scheduler geral de objetos e uma sessão completa jogável continuam em aberto.

## Comando do jogador para levantar (2026-10-22)

Depois de concluir uma ação que deixou o Sim sentado e anexado ao objeto
selecionado nesta sessão, a janela oferece `U to stand` no título e `U: Stand`
na legenda disponível. A tecla U é um controle explícito do host ODH: ela
solicita a função privada do EntryPoint27 do objeto. Não é uma linha TTAB
inventada nem comprova a interface ou o despacho automático do jogo original.

O pedido compartilha o carregamento da função e a mesma fila/VM do teste
`--headless-stand`. Antes de carregar e imediatamente antes de enfileirar,
confere identidade e geração do Sim e do objeto, GUID, postura sentada,
attachment ao objeto selecionado e fila ociosa. A identidade da ação sentada
bem-sucedida é preservada quando a seleção muda; selecionar novamente a mesma
cadeira continua permitindo levantar. Objetos recriados sob o mesmo ID não
herdam essa autorização. A função do recurso original ainda precisa executar
e retornar seu próprio resultado; o host não força sucesso.

O teste abaixo injeta clique e Space, depois um evento `WindowInput.stand` no
tick270, pelo mesmo dispatcher usado pela janela. Em House07, Sit4097 e
Stand4102 retornaram true, a postura terminou em0, sem attachment, e Main
continuou aguardando até512 ticks:

```sh
python3 tests/house07_interaction_integration.py \
  --binary /tmp/odh-acceleration-clean/odh-person \
  --game-root '../referencias/The Sims' --window-input --window-stand
```

`--headless-window-stand-tick` é somente a entrada determinística do harness;
`stand_runtime.trigger=headless_window_input` a distingue da solicitação
fixture `headless_stand_request`. A entrada nativa usa
`stand_runtime.trigger=window_input`. O teste de backend X11 também cobre U,
retenção e soltura da tecla, mas continua pulado em sessões sem DISPLAY.
Nenhum desses resultados estabelece equivalência de todos os objetos ou uma
sessão completa jogável.

### Sit e Stand com um morador adicional — 2026-10-23

A entrada integrada agora também consegue direcionar uma ação ao segundo Sim
da família, usando a VM, a fila de ações e o código do objeto daquele Sim. O
teste House07 executa Sit4097 e depois Stand4102 para os dois moradores,
alternando qual deles é o principal. Nos dois sentidos, o Sit e o Stand
retornaram true; antes de levantar, a postura era sentada (1) e o attachment
apontava para a cadeira128; ao final, a postura era0 e o Sim estava solto.
As hashes de House07, Neighborhood.iff e dos dois personagens não mudaram.

```sh
python3 tests/house07_household_runtime_cli.py \
  --binary /tmp/odh-selected-actions/odh-person \
  --game-root '../referencias/The Sims' --ticks 1 --resident-sit-stand
```

Essa prova usa a entrada integrada e uma ordem headless determinística; ainda
não demonstra a seleção visual do segundo Sim por clique numa janela real.
O mesmo gate tenta salvar enquanto o segundo Sim está sentado e confirma que
a operação é recusada sem substituir o salvamento anterior. Enquanto o runtime
não consegue salvar com segurança um morador sentado ou com código privado de
ação instalado, o salvamento continua recusado.

### Sit, Stand e Go Here na mesma sessão — 2026-10-23

O harness também pode enfileirar um pedido Go Here depois que Sit e Stand
retornarem sucesso na mesma VM Main. Com a instalação Legacy e House07, o
comando abaixo concluiu as três ações, chegou ao tile `(20,20)` e fechou a fila
sem falha do host:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/house07_interaction_integration.py \
  --binary /tmp/odh-linux-fresh/odh-person \
  --game-root '../referencias/The Sims' --ticks 2048 \
  --go-here-after-stand 20 20 1 600
```

Sit4097 e Stand4102 retornaram true; Go Here chegou com 126 movimentos, de
`[392,440,1,0]` até `[328,328,1,7]`, e a fila terminou vazia. A execução
encerrou a prova ao concluir a caminhada no tick772 (`main_status=yielded`);
isto demonstra as três ações no mesmo processo com recursos reais, mas não
uma sessão que continue por todo o limite de 2048 ticks, nem salvamento ou
retomada.

### Go Here pelo caminho de clique e tecla — 2026-10-23

O harness agora injeta, pelo mesmo caminho de entrada usado pela janela, um
clique numa célula de chão visível e a tecla de ação no passo seguinte, depois
de Sit e Stand. O alvo `(20,20,1)` é uma preferência; quando está coberto na
imagem, o teste escolhe a célula visível, livre e alcançável mais próxima. Na
instalação Legacy, o destino escolhido foi `(22,26,1)`. A sequência chegou ao
destino com 32 callbacks de movimento, fila vazia e `host_failed=false`.

```sh
python3 tests/house07_interaction_integration.py \
  --binary build/linux-checkpoint/odh-person \
  --game-root '../referencias/The Sims' --window-input --window-stand \
  --go-here-after-stand 20 20 1 600 --ticks 900
```

Os eventos foram simulados sem DISPLAY. Isso valida picking e despacho de
entrada, mas não uma janela X11 visível, clique físico, salvamento ou uma
sessão completa.

### Reexecução no checkout atual com a instalação base — 2026-10-24

No commit `20aa8d0`, o mesmo caminho completou Sit, Stand e Go Here usando a
instalação `../referencias/The Sims` (sem depender da Legacy Collection). O
harness encerrou com código 0. O processo do jogo reportou o limite solicitado
de 900 ticks (`process_exit=3`, Main em `waiting`); isso não foi falha da VM.
Sit 4097, Stand 4102 e Go Here 4096 retornaram `true`, a rota usou 32 passos,
a fila terminou vazia e `host_failed=false`. O destino pedido `(20,20,1)` não
estava visível; o picking escolheu a célula livre alcançável `(22,26,1)`.

Reprodução com logs e estado XDG isolados:

```sh
ODH_RUN_DIR="$(mktemp -d /tmp/odh-house07-flow.XXXXXX)"
XDG_STATE_HOME="$ODH_RUN_DIR/state" XDG_DATA_HOME="$ODH_RUN_DIR/data" \
  PYTHONDONTWRITEBYTECODE=1 python3 tests/house07_interaction_integration.py \
  --binary build-household-session/odh-person \
  --game-root '../referencias/The Sims' --window-input --window-stand \
  --go-here-after-stand 20 20 1 600 --ticks 900
```

É uma execução controlada com recursos comerciais lidos da instalação e
eventos de janela simulados pelo harness, sem DISPLAY ou clique físico. Não
salvou nem retomou uma sessão; o caminho visual interativo e a partida completa
continuam pendentes.


O fluxo integrado agora também pode voltar à vizinhança com **M** e retomar o lote carregado sem avançar o relógio nem recarregar o World. A prova headless Sit→mapa→mesmo lote→Stand está em [neighborhood-return-validation.md](neighborhood-return-validation.md); trocar para outro lote continua indisponível.

### Lista provisória de ações visíveis — 2026-10-24

Ao selecionar um objeto com ações elegíveis, a janela Linux agora desenha até
oito rótulos do TTAB em uma lista sobre a cena e destaca a ação selecionada.
Tab/Shift+Tab ainda percorrem as opções e Space/Enter ainda enfileiram a ação;
o título da janela deixou de expor GUID, TTAB e outros identificadores internos.
O painel reutiliza o texto localizado já lido dos recursos e o renderizador
Linux existente. A compilação de `odh-person` e os testes
`linux_window_backend`, `saved_interaction_discovery` e `person_cli` passaram
(3/3, sob Xvfb).

Este é apenas um passo de interação visível. Não foi feita captura de uma
partida com uma ação aberta; o painel não é o pie menu original nem o menu do
Simitone e não prova paridade visual. Naquele checkpoint, a escolha por mouse
ainda não estava ligada; o avanço seguinte está registrado abaixo.

### Escolha de ação pelo clique injetado — 2026-10-24

O clique na lista agora identifica a linha visível, destaca a ação e a envia à
fila do morador selecionado. O teste `house07_interaction_integration.py`
usou a instalação original e House07: selecionou a cadeira salva 128, clicou
na primeira linha, executou Sit 4097 com retorno `true` e depois Stand 4102
com retorno `true`; a fila terminou vazia e `host_failed=false`.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/house07_interaction_integration.py \
  --binary build-household-session/odh-person \
  --game-root '../referencias/The Sims' --window-input --window-stand --ticks 512
```

O harness injeta as coordenadas de clique pelo mesmo tratamento usado pela
janela, mas executou sem DISPLAY e não mostrou a lista numa tela. Isto confirma
que a linha escolhida chega à ação correta com recursos reais. A captura X11 e
o clique numa janela são evidência separada, registrada no checkpoint seguinte;
save/retomada após essa cadeia continuam pendentes.

### Captura X11 e clique na lista — 2026-10-24

Com os arquivos de piso e parede da instalação base carregados, a janela
House07 abriu em Xvfb (320x240). A captura mostrou o rótulo localizado `Sit`
ao lado da cadeira salva 128, em `(110,123)`; o HUD permaneceu visível. Um
clique X11 em `(114,133)` fechou a lista e enfileirou Sit 4097. No relatório de
encerramento, depois de 103 ticks, `host_failed=false`, a fila ainda estava
ativa, o Sim estava anexado à cadeira e tocava `a2o-sit-behind`. A janela foi
fechada antes do retorno final da ação; a conclusão `true` continua provada
somente pelo gate headless acima. Xvfb, Xephyr e `odh-person` foram
confirmados encerrados.

A captura temporária foi mantida fora do Git para não incluir arte comercial:
`/tmp/odh-ui-visual-5e7446e/menu-x11-full.png`. O painel está perto do clique e
não cobre o HUD nesse caso; ele continua provisório e não reproduz o pie menu
ou a arte original. Ainda faltam fechar a ação numa sessão X11, testar outros
objetos/idiomas e salvar/retomar após a sequência.

### Clique X11 no launcher integrado — 2026-10-24

Com `tools/play_linux.py`, a House07 abriu no Xvfb em 800×600, mostrando a
casa, os dois moradores e o HUD. O clique real em `(338,291)` selecionou o
objeto 128 (GUID `0x26BFBB29`, TTAB 128); a lista provisória mostrou `Sit`.
O clique na linha enfileirou BHAV 4097. No tick 120, o ator estava anexado ao
objeto 128 e tocava `a2o-sit-behind` no frame 19,2 de 161. O processo foi
encerrado antes da conclusão: a fila permanecia ativa, o retorno era nulo e
`host_failed=false`.

Capturas mantidas fora do checkout e sem inclusão no Git:
`/tmp/odh-x11-retry/retry-menu.png` e
`/tmp/odh-x11-retry/retry-after-30s.png`. A segunda captura não prova que Sit
terminou; o recibo no encerramento confirma que a ação ainda estava em curso.
Na mesma rodada, o gate headless com entrada de janela completou Sit 4097 e
Stand 4102, ambos com retorno `true`, fila vazia e sem falha do host. A janela
X11 real ainda precisava confirmar a conclusão; o teste posterior que fechou
essa lacuna está registrado na seção seguinte.

### Sit/Stand X11 e retomada visual — 2026-10-24

A build Release `build/linux-checkpoint/odh-person` abriu a House07 em Xvfb,
800×600. Depois de a captura mostrar o lote (não só o cartão “Loading lot...”)
o clique `(338,291)` selecionou a cadeira 128 e mostrou `Sit`. Clicar na linha
enfileirou BHAV 4097; a janela mudou o título para
`interaction returned_true BHAV4097`. O ator estava sentado e o overlay
`Stand up` apareceu. Enviando a tecla `U` diretamente à janela, Stand BHAV
4102 foi enfileirado e retornou `true`. O recibo final: tick 556, fila vazia,
`host_failed=false`, postura 0 e sem attachment.

Depois de Stand, `S` salvou o household no tick 552 em
`/tmp/odh-x11-release2/data/opendollhouse/saves/mode-1-house-7-family-5.odhs`
(118130 bytes). Um segundo processo carregou esse arquivo numa janela X11;
`session_loaded=true`, `resumed_tick=552`, encerrou no tick 590 com postura 0,
sem attachment, fila vazia e sem falha do host. A captura do lote restaurado é
`/tmp/odh-x11-release2/reload-visible.png`. O save e os recursos comerciais
continuam fora do Git.

Capturas complementares fora do checkout: `sit-menu.png`, `seated.png` e
`after-stand.png` no mesmo diretório temporário. Esta prova cobre somente a
cadeira 128, House07 e o idioma de código 1. A lista ainda não é o pie menu do
The Sims nem a interface final do Simitone; outros objetos, vários idiomas e
paridade visual continuam pendentes. No build Release, os CTests focados
`linux_window_backend`, `saved_interaction_discovery` e `person_cli` passaram
3/3 sob Xvfb.
