# AllowPush, fila limitada e retorno ao Main

Checkpoint de 2026-10-13. A biblioteca agora executa o limite de interação entre
Main preservado e ação de outro proprietário, com retomada na mesma instrução
no tick seguinte. A fila suporta ações explicitamente fornecidas como
executáveis. A integração do Main completo do avatar e das ações de cadeira
com o host Linux continua pendente; o preview existente não usa esta fila.

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

Na janela Main, um clique agora relata no JSON a elegibilidade calculada por
essa VM isolada junto de owner, ação, teste, rótulo, instruções e diagnóstico.
No preview de lote salvo, Space/Return solicita a primeira linha com teste
`eligible`, perfil de flags suportado e ActionFunction privado. Flags zero são
aceitas; `AllowVisitors` (bit `0x1`) é aceito depois de conferir o perfil do
ator. Um visitor actor (`PersonType==1 && GreetStatus<2`) requer esse bit.
Pets, perfil de ator ausente e bits desconhecidos continuam fora do perfil.
O recurso exato
`source`/`asset` fornece o banco BHAV privado; sua identidade recebe um CodeOwner
de alta faixa, instalado na VM Main já viva. A fila valida novamente actor,
target e geração antes de aceitar caller/target/StackObject. Não reinicia Main.
O preview não apresenta pie menu ainda; só a primeira opção elegível do perfil
manual. O JSON diferencia `not_requested`, `queued`, rejeição, falha da VM e
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
executar Animation44 e avaliar Snap46. Ações adicionais, serviços e retorno
sentar/levantar continuam em aberto.
Prioridades, flags/modes TTAB, ações adicionais, serviços e retorno sentar/
levantar continuam em aberto.
