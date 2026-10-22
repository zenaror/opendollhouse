# Snap, binding e ocupação — 2026-10-13

### BHAV4107: primeiro caminho completo no teste de diagnóstico — 2026-10-22

O cenário `--snap-probe` agora prepara para o objeto Destination as palavras
ObjectData que o perfil sintético do `tools/person` prepara, incluindo Flags
(ObjectData[8]) com valor inicial 256. Isso deixou a ação passar também pela
rotina Global273. No conjunto comercial consultado, o caminho observado foi
BHAV4107 PCs 0, 12, 4, 2, 3 e Global273 PCs 0 e 1; a VM terminou
`returned_true` depois de sete instruções.

A leitura dos operandos, comparada ao código Simitone fixado em
`4906db3156616bfaffc10ce0963cf08062975fef`, mostra que Global273 limpa o bit
de valor 32 de StackObject Flags (ObjectData[8]) e copia o primeiro argumento
para ObjectData[25]. O código do Simitone nomeia essa flag `Occupied` e chama
ObjectData[25] `LockoutCount`; são rótulos da referência, não confirmação dos
nomes ou intenções no executável original. O perfil `--snap-probe` prepara o
banco de palavras do Destination sintético igual ao perfil temporário do
`tools/person`, usando Flags 256, para o diagnóstico percorrer esse caminho.

Esse resultado não prova que o Sim se moveu nem que a notificação acordou uma
fila real: o relatório da prova registra `no_thread`, não houve movimentos e
o cenário usa uma VM e entidades sintéticas. Também não confirma o significado
final da ação no executável original. O próximo passo é entender o restante da
BHAV4107 e reproduzir o contexto real em que ela é colocada na fila de um Sim.
O teste sintético `bhav_cli` e a bateria completa de testes devem ser usados
para validar mudanças futuras; os dados comerciais continuam fora dos testes
versionados.

### Bloqueio de móvel no ponto de chegada da escada — 2026-10-22

`tests/snap_arrival_blockers.cpp` compõe as três etapas usadas pelo teste de
chegada: um retângulo de colisão de móvel entra no retrato de obstáculos, o
mapa marca a coordenada do patamar como ocupada e o validador do Snap devolve
“bloqueado”. A VM retorna falso e mantém o avatar no ponto de partida. O caso
de controle sem móvel mantém a chegada livre e troca o avatar para o outro
piso. CTest: `snap_arrival_blockers` passou.

O mesmo teste agora inclui um avatar sintético parado no patamar e confirma que
o retrato de obstáculos conserva o ID `30` e marca o quadrado como ocupado.
Isso só valida a identificação geométrica quando o chamador fornece a lista
completa de avatares a considerar; não executa uma segunda fila de Sim nem
manda o bloqueador se mover.

É uma fixture sintética que testa a ligação entre os componentes locais; ainda
não injeta um móvel real salvo na House03. Não afirma como o executável original
trata esse caso. O teste de integração da House03 segue validando os dois
sentidos sem bloqueadores. O mesmo roteiro agora cria um segundo avatar
sintético no patamar de destino. A verificação agora inclui avatares do mesmo
cômodo no retrato de obstáculos e decide pela ocupação do quadrado de chegada,
em vez de parar só porque há outro Sim em algum lugar do cômodo. No caso
ocupado, o Snap retorna falso, o avatar continua no piso de origem e a trilha
mostra que BHAV4113 chegou ao PC23. A condição salva no fixture encerra a ação
ali; os Snaps alternativos dos PCs15/17 não são executados nesse caso. Isso
confirma a continuação até esse teste de condição, mas não implementa o pedido
para o outro Sim se afastar, nem confirma a política do executável original.
Um controle põe outro Sim no mesmo cômodo, um tile ao lado da chegada; nesse
caso o avatar atravessa normalmente. O teste continua verificando também as
quatro passagens normais.

Na revisão fixada do Simitone, `VMRoutingFrame.Init` inicia a lista de pessoas
consideradas com Sims que já têm uma rota parada esperando. `AttemptWalk` usa
essa lista ao montar os obstáculos. Se a caminhada encontra outro avatar e
recalcular a rota também falha, `CanShooAvatar` evita repetir um pedido que já
está ativo; quando pode, o Simitone executa o BHAV4107 no Sim que já está
roteando ou põe a interação3 do objeto GOTO na fila dele, e espera 60 passos
antes de tentar de novo. Isso dá uma referência concreta para a futura
coordenação de duas filas no ODH. É comportamento observado no código do
Simitone fixado, não confirmação do executável original. O host ODH ainda
mantém uma só fila de roteamento ativa.

Foi acrescentado também um teste pequeno com dois avatares, duas VMs e duas
filas ligadas ao mesmo mundo. O teste põe uma ação sintética na fila do avatar
bloqueador; ela altera um dado compartilhado, enquanto a fila do avatar que
tentava andar permanece independente. Isso verifica que a base do ODH consegue
representar “o Sim que bloqueia recebe o pedido”. O teste entrega a ação
diretamente à fila: ainda não conecta o Snap a esse pedido, não executa o
BHAV4107 comercial e não move o avatar. No Simitone, o Snap cria um objeto GOTO
temporário e pede a interação3 na fila do bloqueador; o frame usa o bloqueador
como Caller e o GOTO como Callee e StackObject. A TTAB comercial do GOTO já foi
conferida: a interação3 seleciona a ação4107 e o teste4108. Essa leitura confirma
a ligação entre os dados, mas não executa a ação em uma fila real do bloqueador.

### Modelo de execução por Sim — integração em andamento

Na revisão Simitone fixada `4906db3156616bfaffc10ce0963cf08062975fef` (FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`), `VM.InternalTick` avança o relógio
e chama um único `VMScheduler`; esse scheduler percorre as entidades previstas
para o passo e chama `ent.Tick()`. Cada `VMEntity` possui seu próprio
`VMThread`. Assim, o shoo da fila do bloqueador é uma segunda thread de entidade
dentro da mesma VM do lote, não uma segunda VM completa. Fontes: `VM.cs:380-429`,
`Engine/VMScheduler.cs:79-97`, `Entities/VMEntity.cs:61,490`.

No ODH, cada `Vm` possui uma pilha e registradores próprios
(`include/odh/vm.hpp`). `VmProgram`
permite compartilhar o banco BHAV imutável entre objetos `Vm`, enquanto os
owners instalados em tempo de execução continuam locais a cada VM. O teste
`vm_code_owners` verifica que um owner exclusivo de uma entidade não aparece na
outra. O novo `EntityVmScheduler` aplica um tick crescente comum e visita as
entidades em ordem crescente de ID. Primeiro chama os hooks de preparação de
todas, que atualizam filas e relógios; depois distribui instruções apenas entre
VMs prontas para executar, e visita essas VMs em ordem crescente de ID. O hook
de observação de cada entidade roda após sua fatia, então entidades posteriores
veem as mudanças anteriores no mesmo tick. O orçamento total e o limite por
entidade são explícitos. Se o orçamento não cobre todas as VMs ativas, a escolha
de quem recebe instruções alterna entre ticks, sem alterar a ordem das visitas.
Essa regra de orçamento é uma política do ODH; não afirma reproduzir a agenda
interna do Simitone. A lista tem capacidade máxima.
`EntityThreadControllers` agrupa Sleep e InteractionQueue de uma entidade; cada
instância liga seus controladores a uma VM própria e compartilha apenas o World.
`tools/person` usa esse pacote para o avatar atual, sem ainda construir vários.
`entity_vm_scheduler` testa dois desses pacotes, duas VMs e um World compartilhado:
a fila de cada um fica separada, e avançar o relógio de Sleep de um não altera o
do outro. A ação endereçada ao bloqueador altera o objeto compartilhado, sem consumir a fila
do outro Sim. Também verifica a ordem de observação, a distribuição de um
orçamento de uma instrução entre entidades ativas e que VMs paradas não gastam
esse orçamento. `tools/person` usa o scheduler para os turnos Main depois
da execução inicial de preparação; os hooks observam a fila já existente antes
e depois de cada fatia. A janela e a rota continuam dirigidas pelo loop externo,
e esse host ainda cria uma única VM/avatar. Portanto o teste com dois Sims segue
sintético: não demonstra threads comerciais simultâneas, movimento do bloqueador
ou animação. Opcode49 agora encaminha StackObject ao callback da thread
registrada. `EntityVmScheduler` permite executar uma thread acordada de ID maior
no mesmo tick quando ainda há orçamento; se sua vez já passou, ela roda no tick
seguinte. `entity_vm_scheduler` testa as duas ordens e os dois tipos de espera
suportados aqui (Sleep e IdleForInput) com VMs sintéticas. O host `tools/person`
ainda registra apenas um avatar, então isso não prova um bloqueador comercial.
Compartilhar rotinas evita cópias por Sim, importante para os ports portáteis.

O próximo passo é separar do `tools/person` o pacote de execução de cada avatar:
VM, fila, sono e dispatch de primitivas. Depois o host poderá registrar vários
pacotes no mesmo relógio e executar BHAV4107 no bloqueador, medindo sua posição
em vez de apenas escrever um campo de fixture.

Comecei também a executar a ação real BHAV4107 no probe de VM do ODH. Seu
primeiro passo é GetDistanceTo (opcode11) usando os campos `Temp0`,
`MyObject[11]` e scope3, numa codificação diferente da ação Go Here. O adaptador
agora aceita essa sequência exata do recurso e o teste `distance_to` confirma
que retorna a mesma distância no cenário sintético de mesmo andar. No probe com
rota e Snap, a VM passa por esse passo e pela notificação49, mas para ao tentar
escrever StackObject Attribute[0], que o objeto artificial desse probe não
declarou. Portanto BHAV4107 ainda não foi executada até o fim nem moveu um Sim.
Comando usado:

```sh
build/linux-checkpoint/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' Destination.iff 4107 --run --world-probe --route-probe --snap-probe --object-def 16807 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

O comportamento de GetDistanceTo foi conferido no leitor de operandos do
Simitone fixado; a nova forma é registrada também pelo teste, mas não comprova
o significado no executável original nem completa a interação de desobstrução.

### Pista de “Shoo” no Simitone e no BHAV da escada — 2026-10-22

Na revisão Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, submódulo
FreeSO `f86bbd34b2112296ed786741f640a0535886d180`, o frontend
`TS1GameScreen.InitializeLot` cria o VM com `VMContext`; esse contexto registra
`VMSnap` para o opcode46. Portanto, esse código de referência também é usado
pela tela TS1 daquele fork, não apenas pelo modo TSO.

Em `VMSnap.cs`, quando `SetPosition` falha por colisão com um avatar e a flag
Shoo está ligada, o handler verifica se já existe a interação3 do objeto GOTO
(GUID `0x000007C4`). Se não existir, cria esse objeto e põe a interação3 na fila
do avatar que bloqueou a chegada. Em seguida, o `Snap` retorna falso. O código
do slot parser também reduz muito a prioridade de uma chegada ocupada por outro
avatar. Isso explica como o Simitone tenta pedir que o outro Sim saia do
caminho; não prova que o executável original faça o mesmo.

Na House03, a leitura estática do BHAV4113 mostra dois Snaps com Shoo ligado
(PC3 e PC10). Se eles retornam falso, o fluxo segue pelo PC23 e pode chegar a
outros Snaps sem Shoo (PC15 e PC17). Isso indica que o teste atual de dois Sims
para no primeiro pedido de Shoo e ainda não cobre a continuação que o recurso
comercial descreve. O que esses ramos fazem depois do primeiro falso, e se o
Simitone reproduz a intenção do original, permanece sem confirmação.

### BHAV4114: filtro de Sims no mesmo cômodo — 2026-10-22

A leitura das instruções e dos operandos do BHAV4114 permite reconstruir este
caminho sem executar o recurso: a rotina começa procurando Sims; para cada um,
compara o valor `Room` do StackObject com o `Room` do Caller; se forem
diferentes, procura o próximo; se forem iguais, compara `StackObject
PersonData[66]` com `1`. Um resultado igual termina com verdadeiro; um diferente
retoma a procura. Se não houver outro Sim, termina com falso. O nome “do I need
to shoo?” combina com esse filtro, mas o rótulo não prova a intenção completa.

Essa leitura usa os significados de escopo/variável já implementados no ODH
(scopes3/4 para Caller/StackObject, índice29 para Room, scope19 para PersonData)
e compara o opcode31 com a definição `Person` do SetToNext no Simitone fixado.
O enum do Simitone chama PersonData[66] de `Private`; isso é uma pista de nome,
não uma confirmação do significado no executável original. Os comandos
`odh-bhav` continuam sendo dumps estáticos. O BHAV4112 ainda contém opcodes
272,273,49 e370 sem interpretação confirmada; não se deve inferir deles o
efeito de mover ou colocar o Sim na fila.

O teste `destination_shoo_filter` agora executa em uma VM sintética o fluxo de
quatro instruções lido do BHAV4114: procura avatares por ID, exige
`PersonData[66] == 1` e compara o cômodo do avatar com o do chamador. Os casos
confirmam que um avatar elegível no mesmo cômodo retorna verdadeiro, que um
avatar em outro cômodo é ignorado e que nenhum avatar elegível retorna falso.
Os operandos e destinos de PC vêm do dump de `Destination.iff` no conjunto
comercial instalado; o teste não carrega arquivos comerciais, não executa o
BHAV4107 e não move nenhum avatar. É evidência de que os opcodes usados por esse
filtro funcionam juntos na VM sintética, não de que a interpretação dos nomes
ou a intenção do executável original esteja confirmada.

Também foi feita uma nova importação isolada do executável original no Ghidra
12.1.3 PUBLIC, usando PE32 x86. A análise automática terminou, mas não
identificou ainda uma rotina que confirme o despacho de interação3 ou seu
efeito no mundo. Isso confirma a preparação da ferramenta, não o comportamento
do jogo. O hash da entrada lida é
`32857c25e00594a9bc5d700f7b373b6df59ab76ec51b36aa1d30f3dd973eff58`.

Fontes consultadas: `refs/upstream/Simitone/Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs`;
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/VMContext.cs`;
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Primitives/VMSnap.cs`;
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Engine/VMSlotParser.cs`.
Recurso comercial House03 somente leitura: `Objects.far`, `stair.iff`, BHAV4113,
lido com `odh-bhav` como estrutura estática.

Também foi localizado no jogo instalado `GameData/Objects/Objects.far`, entrada
`Destination.iff`: o OBJD de rótulo “Destination - For Go Here & Shoo” tem GUID
`0x000007C4` (subchunk OBJD ID 16807). O TTAB128 associa a interação de posição
3 à ação4107 e ao teste4108; o BHAV4107 tem o rótulo “Move out of way”. O mesmo
IFF contém BHAV4112 (“Shooed Out”) e BHAV4114 (“do I need to shoo?”). A leitura
confirma que o objeto/rotinas citados pelo Simitone existem nos dados TS1
instalados. Ainda não interpreta completamente esses BHAVs nem comprova a
execução no jogo original. Consulta estrutural reproduzível:

```sh
/tmp/odh-stair-build/odh-ttab '../referencias/The Sims/GameData/Objects/Objects.far' Destination.iff 128
/tmp/odh-stair-build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' Destination.iff 4107
/tmp/odh-stair-build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' stair.iff 4113
```

`odh_snap` implementa um perfil inicial do snap46 Literal para ocupar um
recipiente. Ele permite avançar o BHAV real da cadeira até a animação44,
ainda ausente. Não comprova sentar visualmente ou compatibilidade completa.

O objeto é ligado por OBJD selecionado explicitamente: prefixo de 42bytes
retorna versão/GUID/AnimationTableID/SlotID, nas versões136/138/139/140/141/142.
O corpo restante é opaco, sem validação completa. O loader confirma um OBJD
e um SLOT de ID correspondente; duplicatas ou ausência falham antes de executar.
Binding inclui geração da entidade e identidade da VM, evitando reutilizar
outro objeto com o mesmo ID.

Perfil: Literal Mode3, flags4 e tailzero, SLOTv10 tipo3 com offsetzero,
flags/scoreszero e SnapTargetSlot>=0. O slot de armazenamento tipo0 precisa
de offsetzero e height7. Outros modos/geometrias/alturas dão unsupported.
O flag FootprintExtension é reconhecido no operando, mas não cria footprints
ou a política espacial do original. Falhas de ocupação retornam false.

`World::place_in_slot` mantém posição física e vínculo juntos. Destino ocupado,
índice inválido, ciclo e vínculo a outro recipiente falham sem alterar posição
ou relações. Repetir no mesmo slot é idempotente. Direção do ocupante é
preservada. Contenção posicionada propaga x/y/nível quando o recipiente muda,
inclusive aninhada, por lista iterativa; alocação ocorre antes da mudança.
Detach deixa a última posição física. Transferência entre recipientes e
geometrias não zero ainda não estão implementadas neste perfil.

Height7 é classificação de altura do SLOT, disponível nos dados. Não somar
arbitrariamente sete unidades à posição física. Offset/altura visual,
skeleton/personagem, multi-tile, colisões e política completa de snap ainda
faltam. A referência FreeSO foi consultada; original executável não foi testado.

## Validação

CTest13/13 passou em Debug e ASan/UBSan. A suíte snap/prefix tem59checks
sintéticos: offsets do prefixo, truncamentos, ocupação/idempotência, recusa
atômica, geometria/modeunsupported, IDreutilizado, binding e propagação aninhada.
A CLI integra OBJD/SLOT/BHAV sintéticos. LSan continua não validado por ptrace.

Dados fornecidos: DiningChairs.iff, OBJD16808, versão138, GUID0x26BFBB29,
SlotID128 e AnimationTableID129. SLOT128/type3/index0 (registro absoluto1)
aponta SnapTargetSlot0; armazenamento type0/index0 tem offsetzero e height7.
Identidades dos recursos estão nos checkpoints anteriores.

Probe construído com BHAV1001 real: roteamento33ticks já validado; snap muda
a posição física de(48,64) para(64,64), liga caller1 ao recipiente2/slot0 e
preserva direção2. O BHAV escreve postura1 e avança até opcode44 em1003PC8.
São18instruções totais, três frames, saída3 e diagnóstico esperado
`unsupported primitive 44`. O teste permanece em cena ODH construída, sem
animação/janela, save original, execução do Sims.exe.bak ou hardware.

JSON Debug e sanitizadores idênticos, stderr vazio. Reports ignorados:
`reports/chair-snap-probe.json` e `reports/chair-snap-probe-sanitize.json`.

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

Próximo: resolver tabela de animações129 e os recursos do personagem usados
pela primitiva44; executar animação/retomada e depois composição Linux.
Fidelidade de tempos/rotas, sistemas e persistência continuam pendentes.

## Escada House03 com animação — 2026-10-22

O probe `tests/house03_stair_approach.py` agora fornece as coleções comerciais
`GameData/Animation/Animation.far` e `ExpansionShared/ExpansionShared.far` ao
Playback existente. Para o escopo Object da primitiva44, o host primeiro usa o
CodeOwner da VM; em chamadas externas de objeto, quando esse identificador é
apenas a identidade do banco de código, usa o objeto que está executando a
rotina. O perfil multi-tile de animação só aceita as duas definições conhecidas
de `stair.iff` dentro de um grupo com patamar nos níveis1 e2.

Quatro execuções com save real House03 e avatar/Main sintéticos terminaram a
ação do portal (`returned_true`): subida e descida, cada uma com idade fixture
0 e21. Em todos os casos a caminhada fez oito callbacks, o Snap trocou para o
patamar oposto, a animação foi resolvida e registrou14 eventos de passos. Isso
confirma a cadeia de simulação até o retorno da ação nos dois sentidos para
essas idades; não confirma pixels animados, som, colisão geral ou equivalência
com o executável original. Os dados comerciais continuam somente leitura e
fora do Git.

### Pose animada em imagem — 2026-10-22

O mesmo probe agora pode exportar quadros do Playback em `--capture-tick`.
Para o diagnóstico da animação `a2o-stair-climb`, a câmera da imagem de teste
é deslocada até a posição em que a pose aparece; isso não altera a posição do
Sim no World. O teste gera apenas a imagem do Sim, sobre fundo claro, sem
compor escada, móveis ou o restante do lote.

Foram capturados os ticks14,16 e20. Os três passaram pelas verificações de
ator, clipe e desenho, e resultaram em imagens diferentes. A inspeção lado a
lado mostra a pose mudando durante a subida. Os arquivos PPM de diagnóstico
foram mantidos em `/tmp/house03-stair-sequence-tick{14,16,20}.ppm`; o mosaico
para inspeção é `/tmp/house03-stair-sequence.png`. SHA-256, na mesma ordem:
`92406d40ff9724b40d36a97a82302fa149c1c045a649b21fc97f8cbd9e2c253f`,
`0bc23a3299d72b259027338946c165a3274c814bba44053f199ad78f5d5f6a4c` e
`db419312e6e75895e1f521a2040ffcfdc594070c07e02f00191006dca440b6ad`.

Isso demonstra que o Sim da simulação chega ao desenho com poses diferentes.
As imagens acima são do Sim sobre fundo claro; a próxima captura registra a
composição com o lote.

### Sim animado sobre paredes, piso e objetos — 2026-10-22

O compositor agora ignora uma camada de sprite somente quando ela fica
inteiramente fora da tela. Isso reduz o trabalho visual da captura sem remover
objetos do lote ou mudar o estado do World. Os testes sintéticos verificam que
80 camadas fora da tela não gastam a cota de pixels, enquanto as camadas
visíveis continuam sujeitas ao limite original.

O teste de House03 centraliza a câmera no ponto gráfico do patamar usado pela
travessia e agora lê `GameData/walls.iff`. A composição desenha paredes, piso,
objetos e a pose do Sim. A vista da descida seleciona o piso1 para mostrar a
escada; a entidade continua no andar2 no World. Em ambos os sentidos, o teste
exige pixels do Sim no mapa de propriedade que sobrevive à composição de
paredes e objetos. Na subida do tick14, 4103 pixels foram desenhados para o Sim
e 4044 continuaram identificáveis depois da composição.

As seis capturas foram inspecionadas em
`/tmp/house03-stairs-walls-overview.png`. As imagens PPM são
`/tmp/house03-{up,down}-walls-sequence-tick{14,16,20}.ppm`; cada direção passou
na checagem de hashes diferentes entre ticks. SHA-256:

- Subida 14: `c869e77bcb81ddbd75421f8eb40e3b7fd5120261ffac7b14cfa6107843de106e`
- Subida 16: `334b0c6d4eca0a81d0f9411f220830087ba10aadfc1cdf672f3081e8528be6f3`
- Subida 20: `b86d68b731fa6e6637b3133086cbdf86d8039c6db0ac0e295ed547bd48a023bd`
- Descida 14: `9c7230922799d83b67c292f4fe86de034ac3a1df4bd030abbc6db2dd350c5de1`
- Descida 16: `3767c0e33494aaafc951778a9ddbd6834e1f50958db3dce7294998bd9c49779f`
- Descida 20: `48b37e9f6cd702f99707d29100f6ccd81b1429dcae78551634f7d643c7e9fb92`

O teste funcional também percorre os dois sentidos para idades fixture 0 e21,
com oito passos de caminhada, troca de piso e retorno da ação. As novas imagens
mostram poses da subida e da descida num save real House03, com avatar e Main
sintéticos. Isso não demonstra fidelidade visual ao original nem generalidade
para todas as escadas, paredes ou estados de lote.

Repetição das sequências com os dados instalados:

```sh
python3 tests/house03_stair_approach.py --binary /tmp/odh-stair-build/odh-person \
  --game-root '../referencias/The Sims' --capture-tick 14 --capture-tick 16 \
  --capture-tick 20 --capture-portal 34 --capture-output /tmp/house03-up-repeat.ppm
python3 tests/house03_stair_approach.py --binary /tmp/odh-stair-build/odh-person \
  --game-root '../referencias/The Sims' --capture-tick 14 --capture-tick 16 \
  --capture-tick 20 --capture-portal 38 --capture-level 1 \
  --capture-output /tmp/house03-down-repeat.ppm
```

Build completa e CTest: 160/160 passaram; `linux_window_backend` foi pulado por
falta de display. A suíte completa não prova a execução de janela nesta sessão.
