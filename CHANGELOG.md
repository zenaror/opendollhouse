# Changelog

Mudanças relevantes do OpenDollhouse, em português e com os limites de
validação indicados por versão.

## Não lançado

### Alterado

- Ao escolher uma casa vazia quando há famílias sem casa, mas nenhuma tem saldo
  suficiente, a tela usa a mensagem e o botão Cancelar do idioma selecionado nos
  dados do jogo.
- O mapa de vizinhança mostra o título e a tecla de cancelamento no idioma
  selecionado. Os textos STR# são convertidos da codificação original do jogo
  para UTF-8 antes de serem desenhados na janela Linux.

### Planejado e acompanhado

- Criada uma matriz entre o estado atual e os caminhos necessários para a
  primeira build Linux jogável. Ela separa mapas/leitores parciais de gameplay
  integrado e organiza o próximo trabalho por fluxos reutilizáveis; não reduz
  os critérios de conclusão. Veja
  [`playable-feature-matrix.md`](docs/playable-feature-matrix.md).

### Investigado

- Confirmado no código TS1 do Simitone que despejar uma família devolve
  `FAMI.ValueInArch`, zera esse campo e libera a casa. Não foi encontrado nessa
  revisão um ponto que atualize o valor após compra ou construção; o reembolso
  de famílias recém-mudadas permanece sem suporte até essa origem ser provada.
  É análise estática, não comparação com o jogo original. Veja
  [`family-overlay-linux-validation.md`](docs/family-overlay-linux-validation.md).
- A análise inicial no Ghidra do executável original não identificou a gravação
  de FAMI. As funções candidatas mais fortes eram rotinas de acesso a arquivos,
  sem vínculo comprovado com famílias; o resultado não prova ausência de gravação.
  Veja [`family-overlay-linux-validation.md`](docs/family-overlay-linux-validation.md).

- Rastreada sem execução a fila de Hecuba na House77: a árvore 8236 existe em
  PersonGlobals, mas a TTAB#129 dessa fonte associa a ação 8236 ao índice 52,
  enquanto a fila salva marca TTA 33 (que aponta para outra ação). Nenhuma
  fonte instalada explica essa combinação e a tabela correta permanece
  desconhecida porque falta o OBJD do personagem. Não se altera o índice nem
  se executa a fila. Veja
  [`saved-world-definition-audit.md`](docs/saved-world-definition-audit.md).

### Corrigido

- A configuração Linux padrão agora para com instrução clara se PangoCairo não
  estiver disponível. Antes, o build podia continuar e abrir uma janela sem os
  menus e rótulos necessários. Também exige os arquivos de desenvolvimento X11;
  `ODH_X11=OFF` fica explícito para builds sem janela, enquanto
  `ODH_LINUX_TEXT_RENDERER=OFF` só é indicado para depurar uma janela sem texto.
- A família House07 agora pode ser salva depois que o morador adicional conclui
  Sit e Stand. Bancos de ações já instalados e sem referências vivas não impedem
  o save; referências dinâmicas ainda usadas pela VM ou animação continuam
  recusadas antes da escrita. O gate headless passou nos dois sentidos de
  seleção do morador: save no tick512, outro processo até1024 e arquivo igual
  à continuação contínua. Salvar sentado continuou recusado, o save anterior
  permaneceu intacto e os quatro arquivos comerciais cobertos mantiveram seus
  hashes. Oito testes focados passaram. Janela X11 e jogo completo não foram
  validados. Veja [`family-restore-validation.md`](docs/family-restore-validation.md).

### Adicionado

- Adicionado o CTest `linux_move_out_integration`, que inicia o app Linux com
  dados sintéticos e percorre a janela real: House07 ocupado → confirmação e
  gravação do overlay → mapa atualizado → seleção cancelada em House08 →
  reabertura com o estado preservado. O teste liga a janela ao PID do processo
  criado por ele e usa um perfil temporário; não executa a simulação nem lê
  dados comerciais. Build completo e 214/214 CTests passaram sob Xephyr. Isso
  valida esse fluxo sintético, não uma sessão jogável nem entrada humana. Veja
  [`family-overlay-linux-validation.md`](docs/family-overlay-linux-validation.md).

- O estado local de famílias agora aceita uma sequência versionada de ações:
  sair de uma casa originalmente ocupada e mudar para outra preservam o saldo,
  liberam o lote anterior e zeram `ValueInArch`. O formato antigo continua
  legível. Um novo despejo após uma compra feita pelo jogador fica bloqueado
  porque ainda não provamos como o jogo atualiza o valor da arquitetura. Na
  vizinhança, `E` oferece a saída somente quando o reembolso tem origem
  verificada; a confirmação mostra o valor e o saldo final, e o mapa é atualizado
  depois de salvar. Testes sintéticos de teclado cobrem as telas; build Linux e
  213 testes CTest passaram. Um teste integrado isolado, com dados originais em
  leitura, confirmou House05 → saída → mapa atualizado → seletor da House01, e a
  saída persistiu ao reabrir o app. Isso não prova gameplay nem entrada humana.
  Veja
  [`family-overlay-linux-validation.md`](docs/family-overlay-linux-validation.md).

- Adicionado o codec versionado do estado local de mudança de família para lote.
  Ele identifica FAMI e Neighborhood por hashes, valida lotes livres e exige
  que o saldo final corresponda exatamente ao preço informado. Testes sintéticos
  cobrem dados antigos, conflitos e limites. Ainda não há gravação no perfil,
  ligação com o mapa/entrada do lote ou criação de moradores ausentes; esta
  etapa não habilita move-in jogável. Veja
  [`family-restore-validation.md`](docs/family-restore-validation.md).

- O host Linux agora avança um relógio do jogo nos passos não pausados, atualiza
  sete necessidades com as regras padrão documentadas no Simitone, mostra esses
  valores no título e guarda relógio/frações por morador no save. O gate real
  House07 confirmou necessidade alterada e igualdade entre execução contínua e
  save/resume; não valida paridade com o executável original nem uma build
  completa. Veja [`house07-clock-validation.md`](docs/house07-clock-validation.md).

- O gate House07 executa Sit, Stand e Go Here pelo caminho de clique e tecla
  usado pela janela. Sem DISPLAY, o Sim chegou a um quadrado de chão visível
  em 32 callbacks, com fila vazia e sem falha do host. Isso ainda não valida
  clique físico nem uma janela X11. Veja
  [`interaction-validation.md`](docs/interaction-validation.md).

- O probe headless `odh-person` mantém o limite padrão de 10.000 instruções e
  aceita um teto explícito de até 1.000.000 em sessões novas e limitadas por
  ticks. O gate opcional da família House05 completou 4.096 ticks com 50.000
  disponíveis (14.439 usados), sem falha do host; o morador adicional 16 se
  moveu e os 132 arquivos de recursos permaneceram sem alteração. Isso amplia
  a observação Linux da família, sem demonstrar interação visual ou paridade.
  Veja [`current-handoff.md`](docs/current-handoff.md) e
  `tests/house05_household_budget_cli.py`.

- `odh-person --game-root` prepara a entrada integrada da vizinhança ao lote,
  carrega recursos limitados a uma instalação e reutiliza a mesma janela no
  runtime. Depois de carregar o lote, dados e idioma podem ser lembrados na
  configuração privada do usuário; `--plan-only` continua sem gravar. Os 179
  CTests passaram, com o teste X11 pulado por falta de display; House07
  resolveu os dois moradores em 20 idiomas nas instalações original e Legacy
  em modo headless. O teste visual
  ainda depende de um servidor X11 acessível. Um teste sintético também cobre
  entrada integrada, recuperação de configuração inválida e recusa de atalhos
  que escapam da instalação. Esta etapa não implementa um ciclo jogável nem
  salvamento/retorno. Veja
  [`linux-session-entry-validation.md`](docs/linux-session-entry-validation.md).

- A tela da vizinhança agora pode ser mostrada numa janela Linux existente, e o
  backend pode redimensionar essa mesma janela preservando seu identificador.
  Isso prepara a transição bairro→lote; o fluxo contínuo ainda não está ligado
  e o teste X11 foi pulado porque não havia servidor gráfico acessível.
  Veja [`linux-launcher-preview-validation.md`](docs/linux-launcher-preview-validation.md).

- A resolução dos dados de lote e família selecionados agora também é uma
  função reutilizável da biblioteca, `odh::load_session_entry`. O CLI existente
  e o futuro fluxo integrado compartilham o mesmo caminho de leitura.
  A validação sintética e a House07 real continuam aprovadas.

- Prévia de launcher Linux encadeia seleção de lote, família e preparação do
  lote salvo. `odh-person` agora resolve o GUID do morador durante a leitura do
  World (House07: vizinhança 48/47 → objetos 105/137), sem uma leitura extra do
  lote. CLI e smoke real de 32 ticks passaram; o limite curto termina com Main
  aguardando e não comprova ação completa, sessão contínua, salvamento/retorno
  nem equivalência jogável. Veja
  [`linux-launcher-preview-validation.md`](docs/linux-launcher-preview-validation.md).

- A bancada `odh-person` aceita um IFF próprio para o segundo avatar de teste,
  com rotinas e ajustes privados separados. Verificado com dois arquivos
  sintéticos e rotinas de mesmo número; isso ainda não executa dois moradores
  restaurados de um lote real. Veja [`person-host-validation.md`](docs/person-host-validation.md).

- `odh-session-entry` lê o lote e a família selecionados e confere cada morador
  contra a vizinhança e seu arquivo em `Characters`. A leitura é limitada e
  falha quando um vínculo está ausente ou ambíguo. Testado com famílias das
  instalações externas original e Legacy Collection; a ferramenta ainda não
  carrega o lote nem inicia uma sessão jogável. Veja
  [`session-entry-validation.md`](docs/session-entry-validation.md).

- Janela Linux `odh-neighborhood-window` mostra seis mapas do UIGraphics.far e
  permite selecionar uma casa pelas coordenadas de LotLocations. Marcadores
  distinguem FAMI, casa sem FAMI e arquivo ausente; modo 6 é explicitamente
  indisponível. Retorna a seleção sem iniciar gameplay. Veja
  [`neighborhood-map-validation.md`](docs/neighborhood-map-validation.md).

- `read_bmp` decodifica BMP indexado com compressão RLE8, incluindo runs,
  linhas, saltos e preenchimento; a imagem é limitada a 1 milhão de pixels e o
  fluxo comprimido a 8 MiB. A imagem da vizinhança original agora abre sem
  extrair ou alterar arquivos do jogo. Veja
  [`bitmap-rle8-validation.md`](docs/bitmap-rle8-validation.md).

- `odh-neighborhood` lista/seleciona lotes via LotLocations STR#, FAMI e caminhos
  HouseNN, com coordenadas e IDs de família distintos. Parser CSV limitado e
  testes sintéticos; modo 6 explicitamente unsupported. Resolve casa → família
  → lote somente em leitura, sem abrir runtime nem desenhar overworld. Veja
  [`neighborhood-catalog-validation.md`](docs/neighborhood-catalog-validation.md).

- A janela `odh-person --render-window` pode avançar além de 4.096 ticks e
  10.000 instruções acumuladas, com orçamento renovado por entidade/tick.
  `--ticks` explícito e probes sem janela conservam seus limites. Diagnósticos
  saturados passam a descartar evidência com contadores no JSON; a VM continua
  limitada por slice e preserva faults reais. Logs incluem início,
  pausa/retomada e fechamento. É continuidade do host/preview, sem declarar
  gameplay completo. Veja [`person-host-validation.md`](docs/person-host-validation.md).

- A prévia Linux grava logs locais com versão/commit, etapas de inicialização,
  idioma, erros e sinal fatal. Mantém até três arquivos anteriores e limita cada
  sessão a 1 MiB; na próxima execução, registra a última etapa conhecida se a
  anterior terminou inesperadamente.

- A prévia Linux desenha o rótulo da interação selecionada sobre o cenário,
  usando PangoCairo, shaping e fallback para fontes do sistema. O teste sintético
  confirma amostras em português, japonês, chinês simplificado, hebraico e
  tailandês; ainda não há seletor ou preferência de idioma persistida.

- `odh-strings` agora consulta `STR#` diretamente em um IFF solto, como
  `UIText.iff`, para facilitar a inspeção de idiomas sem extrair recursos.
  A saída continua preservando bytes originais; não converte nem desenha texto.
  O teste usa apenas IFF sintético.

- Go Here agora pode sair do piso térreo, usar escadas salvas da House02 e
  House03 e continuar andando no andar de cima. A validação usou avatar
  sintético e Main parada; distância entre pisos segue o perfil TS1 do Simitone
  só na chamada exata do Go Here salvo e nos grupos de escada reconhecidos.
  Ainda não prova equivalência ao jogo original, clique no andar de cima nem
  comportamento de outras escadas. Veja
  [`interaction-validation.md`](docs/interaction-validation.md).

- `odh-saved-person-audit` lê os prefixos e dados finais das entidades Person de
  uma casa sem exigir OBJD ausente para os personagens. A escolha de multitile
  quando OBJD falta segue o fallback do Simitone baseado em nome vazio de OBJT;
  é uma leitura diagnóstica, não monta o lote e não executa ações. Em House77,
  confirmou em C++ os quatro residentes e a fila de Hecuba (UID4051, árvore8236,
  TTA33). Veja [`saved-world-definition-audit.md`](docs/saved-world-definition-audit.md).

- A leitura de prefixos de casas salvas agora permite um orçamento cumulativo
  de até 131.072 palavras para arquivos grandes. Isso permite representar
  os bancos fixos de um lote nativo inspecionado com 847 entidades; o teste cobre
  esse tamanho. Na busca estática dos saves Legacy, House77 contém uma
  interação pendente na fila de Hecuba (ID 11). A importação completa do World
  continua sem sucesso: com `Objects.far` sozinho, falta a definição do
  objeto 649; ao acrescentar algumas fontes de expansão, a leitura avança e
  para no objeto 211. Diagnósticos agora separam definição ausente de duplicada.
  A auditoria estrutural localizou 649 e 211 em recursos de expansão/download;
  com os downloads identificados, o próximo bloqueio é a definição do
  personagem Menelaus, ainda sem OBJD correspondente na cópia examinada. Os
  detalhes e limites da descoberta estão em
  [`saved-world-definition-audit.md`](docs/saved-world-definition-audit.md) e
  [`saved-thread-plan-validation.md`](docs/saved-thread-plan-validation.md).

- `odh-saved-world` agora inclui um plano somente de leitura para as pilhas
  salvas dos personagens, seguindo a conversão de frames do Simitone fixado:
  resolve o dono de cada rotina via OBJT, preserva instrução, argumentos e
  locais, completa os registradores até 20 e aplica `ObjectUses` aos chamados.
  O plano não executa as pilhas; o programa continua informando
  `saved_threads_restored:false`. A opção `--verify-saved-threads` confere
  que cada rotina existe na fonte privada, semiglobal ou global indicada e que
  o endereço salvo cabe nela; erro interrompe a saída antes do JSON. House07
  confirmou planos distintos para Bob (105) e membro47 (137), com doze rotinas
  existentes e endereços válidos. A conferência também lê a instrução chamadora
  para classificar a volta como normal, repetição ou interação e marcar os
  frames de ação, seguindo o adaptador Simitone. Isso continua sendo apenas
  diagnóstico; o save House07 não contém `ObjectUses`, e execução segue
  desligada. Veja
  [`saved-thread-plan-validation.md`](docs/saved-thread-plan-validation.md).

- O núcleo agora oferece um scheduler limitado para estados de VM por entidade:
  ele compartilha o tick, visita as entidades em ordem crescente de ID e limita
  o trabalho total e por Sim. Se o orçamento não der uma instrução para todos,
  alterna quem recebe a vez, sem mudar a ordem de visita. Um teste sintético com dois Sims confirma filas independentes
  sobre o mesmo World. `tools/person` agora oferece um segundo Sim opcional de
  teste: cada um tem VM, rotinas privadas, Sleep, fila e animação separados e
  compartilham o mesmo banco global de BHAVs, World e RNG. O caminho usa o mesmo
  recurso de avatar para os dois; ainda não monta a família de personagens de
  um save nem dá ao segundo Sim os controladores de rota e Snap.

- A VM agora pode importar uma pilha completa já resolvida e mantê-la parada
  como `dormant`; `run()` não a executa até a ativação explícita do host.
  Testes sintéticos carregam dois quadros, iniciam no quadro superior salvo e
  voltam ao pai; uma pilha inválida é rejeitada sem alterar o estado anterior.
  Isso ainda não restaura uma partida OBJM completa nem estados especiais,
  filas, relógio ou serviços. Veja [`vm-validation.md`](docs/vm-validation.md).

- Planos de pilha com retornos normais agora podem ser convertidos para frames
  da VM parada, que confere as rotinas nos bancos recebidos. O chamador ainda
  precisa fornecer os recursos corretos. Retornos de repetição/interação são
  recusados; as filas salvas também não são carregadas por essa conversão.

- `odh-saved-world --import-saved-threads-dormant` lê as BHAVs indicadas por
  cada pilha salva, usa os mesmos recursos explícitos da auditoria e guarda
  as VMs pausadas junto do World restaurado, por ID e geração de Sim. O comando
  não ativa as pilhas nem carrega as chamadas feitas por essas BHAVs; a partida
  continua não restaurada.

- `EntityThreadControllers` reúne a fila de interação e o controle de Sleep de
  uma entidade, mantendo um relógio e uma fila independentes por instância.
  `tools/person` usa esse pacote no avatar principal e, quando solicitado, no
  segundo Sim de teste.

- Opcode49 agora encaminha o StackObject para a thread registrada. O scheduler
  pode executar no mesmo tick uma thread posterior que acordou, se restar
  orçamento; uma thread cuja vez passou espera o próximo tick. Testes sintéticos
  cobrem Sleep e IdleForInput, nas duas ordens de ID. O modo de duas VMs usa
  ordem crescente de ID já no primeiro despacho. Notify antes de Sleep guarda
  o sinal de interrupção para o próximo Sleep/IdleForInput, como no perfil
  Simitone consultado. Esses testes usam BHAVs sintéticas e não confirmam
  Notify de um bloqueador comercial.

- A projeção isométrica agora tem a operação inversa `unproject_tile`: dado
  um pixel e o nível do piso escolhido pelo host, calcula a posição contínua
  correspondente no lote, cobrindo as quatro câmeras e três níveis de zoom.
  Isso é a base matemática para um clique no chão apontar um destino; ainda não
  detecta piso/ocupação nem inicia movimento do avatar.

- O scheduler Route45 agora executa a travessia real EntryPoint15 de Door26 no
  House07. O probe coloca o avatar na sala 1, chama Route45 pelo Main sintético,
  executa a ação da porta, confirma a mudança para a sala 3, atualiza a grade e
  continua até a aproximação do SLOT; terminou `returned_true` sem diagnóstico.
  O começo da rota foi preparado pelo teste, então isso ainda não confirma
  controles normais nem uma sessão Linux jogável.

- Route45 agora pode suspender o ticket raiz para planejar salas em uma grade
  completa de portais, executar EntryPoint15 validado, confirmar a sala real
  após o retorno, atualizar a grade e retomar a rota local. O provider mantém
  estado por ticket, exclui arestas que retornaram falso e respeita
  `IgnoreRooms`. No House07 real, o Route45 do Portal26 agora alcança o SLOT
  `[408,408]` em 32 movimentos pela grade de obstáculos da sala atual, sem
  agendar Portal15. O leitor de `RoomByTemp0` agora usa o cômodo principal
  derivado das ligações do mapa preparado, e o Portal26 do House07 retorna
  `true`. O Sim continua no mesmo cômodo: isso não prova travessia pela porta
  nem uma sessão de jogo completa.

- `RoomByTemp0` lê o valor usado pelo Simitone para os índices 0 a 4 em mapas
  preparados com nomes de piso resolvidos. O índice `Temp0 + 1` é limitado ao
  mapa disponível; o valor externo vem do cômodo principal compartilhado pelos
  cômodos conectados. Mapas incompletos e índices desconhecidos continuam sem
  resposta. Testes cobrem limites, mapa incompleto e diferença entre o cômodo
  selecionado e seu cômodo principal.

- O retorno falso de uma BHAV externa de portal pode consultar um provider do
  host para tentar outra chamada EntryPoint, com limite de 32 tentativas e
  validação do ticket/CodeOwner em cada retorno. Se a tentativa falha após
  mover o avatar, exige refresh válido da grade antes do retry e replana o
  trecho original depois do sucesso. Sem provider ou alternativa, Route45
  falha de forma explícita. A ODH ainda não calcula automaticamente o caminho
  entre salas nem descobre/agenda Portal15 no Main.

- O diagnóstico AttemptWalk separa a equivalência do algoritmo de ordenação
  de `RoomInfo.Entities` da equivalência do conjunto runtime de entidades.
  Simitone e `World` percorrem IDs em ordem crescente; um teste cobre criação
  fora de ordem. A composição por sala, avatares considerados e estado dinâmico
  de portas seguem sem validação.

- O teste do snapshot live AttemptWalk cobre um bloqueador com footprint
  dinâmico antes e depois de mover a entidade. A nova montagem conserva o
  bloqueador e atualiza sua caixa; isso não valida a transição de estado de
  portas comerciais nem clearance runtime.

- `Routing::tick` é testado contra uma grade que o host atualiza após planejar:
  se o próximo passo ficou bloqueado, Route45 encerra sem mover o avatar. O
  `odh-person` já recompõe o snapshot e atualiza a grade antes de cada tick
  ativo quando muda a revisão espacial; isso não valida clearance completo.

- A integração Route45/Main de House07 foi executada com o Main sintético em
  arquivo temporário: avatar percorreu 32 passos; ao mover o objeto 106 para a
  segunda célula da rota no tick 2, o host refez o snapshot e encerrou com
  `returned_false` após apenas um passo, antes de entrar na célula ocupada.
  Isso valida o refresh/fail-closed no host, não interação natural ou travessia
  de portais.

- A inspeção estática da ação Portal15 `Doors.iff:4105` encontrou chamada à
  rotina privada 4113, que contém dois opcodes Route45. A árvore também chama
  outros BHAVs privados. O `GLOB128=DoorGlobals` resolve 8197 em
  `Global.far/DoorGlobals.iff`; a rotina antiga atribui literal zero ao
  argumento 0 no mapeamento atual do decoder, mas não tem metadata 0x8002 e não
  foi executada. A documentação também relaciona esse fluxo ao contrato
  Portal15/child-route do FreeSO pinado, sem afirmar equivalência runtime.
  O BHAV 4113 ramifica por 4115 e escolhe SLOT type-3 literal 0/1 do StackObject
  com `NoFailureTrees`; um teste sintético cobre essa seleção no kernel VM.
  `Routing` ainda recusa esse escopo e flag, e nenhum BHAV comercial foi executado.

- `build_world_room_route_grid` rasteriza um snapshot AttemptWalk completo de
  RoomInfo/RoomObs/WallObs/EP5 numa grade local bounded, incluindo as faixas de
  limite da sala e preservando o motivo de snapshots incompletos. Há uma
  fixture de geometrias e um teste integrado ao snapshot vivo. A grade ainda
  não alimenta o `Routing` nem substitui o VMRectRouter do Simitone.

- Após uma frame externa de rota mover o avatar, `Routing` permite ao host
  reconstruir a grade da sala/andar atual antes de continuar o caminho. `odh-person`
  recompõe o snapshot AttemptWalk e o scheduler replana na grade substituída;
  uma fixture sintética cobre a mudança de posição e de grade. A execução real
  de Portal15 e a navegação entre salas continuam pendentes.

- `Routing` agora preserva até 16 jobs Route45 aninhados quando uma BHAV
  externa em execução dispara outro Route45. O controller valida o vínculo
  entre frame VM, ticket e identidade do avatar, mantém o job pai suspenso,
  propaga `PortalTurns` pelo contexto parent-or-self e recalcula o trecho pai
  após o filho mover o avatar. Fixture sintética cobre a sequência e o retorno
  ordenado. A malha segue única e fornecida pelo host; não é ainda roteamento
  runtime entre salas nem integração de gameplay.

- O inventário de portal agora preserva arestas dirigidas por ID de objeto e
  sala destino, escolhendo a primeira outra parte Portal na ordem do grupo como
  faz `AddRoomPortal` no Simitone. `find_room_portal_route` busca uma sequência
  bounded com a métrica de distância do `VMRoutingFrame` e permite ignorar uma
  conexão que falhou para encontrar alternativa. Ainda não executa a BHAV nem
  monta caminhos geométricos por sala; veja
  `docs/room-portal-routing-validation.md`.

- `DynamicRoomPortalInventory` mantém um conjunto bounded de membros informados
  pelo host e recompõe/publica o inventário após registro, movimento ou remoção,
  incluindo substituição em lote para snapshots do host.
  Posição desconhecida publica estado incompleto para não reter uma abertura
  antiga; identidade duplicada, remoção desconhecida e excesso do limite não
  alteram a geração. Ainda exige notificações do host e não está ligado aos
  callbacks de `World` nem ao replanejamento. No perfil Simitone observado,
  `Portal15` é estrutural; o inventário não modela animação de porta. O probe
  AttemptWalk já consome o snapshot via substituição em lote, ainda sem refresh
  persistente entre ticks.
  Consulte `docs/room-obstacles-validation.md`.

- O host agora pode preparar uma chamada VM para `Portal15`: valida o registro
  salvo e a tabela ligada à entidade, preserva a proveniência do recurso,
  carrega a closure BHAV privada e constrói Caller/Callee/StackObject com um
  `CodeOwner` alocado explicitamente. Condições e chamadas semiglobais
  continuam fora do perfil; as dependências globais são reportadas ao host.
  A rotina ainda não é despachada pelo loop de rota. Consulte
  `docs/entry-points-validation.md`.

- `Entity` agora mantém `radian_direction` contínuo para avatares, enquanto
  `EntityPosition.direction` e ObjectData expõem o notch arredondado de oito
  direções. Movimentos que permanecem no mesmo notch preservam o ângulo; um
  notch explícito novo o canonicaliza. Isso prepara o dado necessário para
  `OptimizeLines`, mas ainda não o aplica à busca angular ou a estados de
  portal. Testes World/save-bank passaram; veja também
  `docs/world-attempt-walk-validation.md`.

- Jobs de `Routing` agora capturam e revalidam o ângulo contínuo do avatar,
  cancelando se ele muda durante uma busca pendente mesmo que o notch continue
  igual; passos do job atualizam esse snapshot e a chegada canonicaliza o
  facing final. O controller também expõe estados explícitos para o frame raiz,
  e helpers testados reproduzem a seleção de direção de entrada e o limite de
  viradas em portal. `odh-routing-tests` cobre esses contratos; a pilha de
  frames-pai e a máquina completa ainda faltam. Consulte
  `docs/routing-validation.md`.

- A VM pode empilhar uma BHAV filha a partir de um serviço em espera, manter os
  tickets aninhados de forma LIFO e retornar ao serviço sem reexecutar a
  instrução pai. `odh-vm-tests` cobre esperas e retornos aninhados; o
  `Routing::call_external_frame` usa o mecanismo para uma BHAV de portal
  resolvida explicitamente pelo host; descoberta de portal/EntryPoint 15 e
  replanejamento ainda faltam. Consulte
  `docs/vm-validation.md`.

- `optimize_rect_route_lines` adiciona a otimização parcial de corredores do
  Simitone: deduplicação e corner-cutting validado por retângulos livres, com
  crossings fixos HiP. O ajuste opcional `dirIn` usa o orçamento inicial de
  oito unidades e as conversões float32/Point da referência; o diagnóstico
  AttemptWalk de House07 preserva o corredor bruto e emite a saída otimizada.
  `dirIn` permanece omitido porque o estado da rota-pai não é reconstruído.
  Os casos sintéticos foram comparados com uma transcrição C#/.NET 10 das
  rotinas e MonoGame 3.8.4, não com o Simitone compilado. Consulte
  `docs/rect-router-validation.md`.

- `snapshot_world_room_info` expõe o inventário bounded e completo de
  footprints atuais por sala, com coleção estática/dinâmica e ordem por ID;
  `query_world_room_info` deriva a query candidata do mesmo contrato. O Main
  diagnóstico publica o inventário vivo junto do probe atual. House07 no tick
  214 retornou 19 footprints na sala 3. Isso ainda não compõe WallObs/RoomObs,
  EntryPoint5 ou clearance. Consulte
  `docs/world-room-info-validation.md`.
- Router bounded de corredores baseado nas primitivas observadas em
  `VMRectRouter`/`VMObstacleSet` do Simitone. O diagnóstico House07 rodou sobre
  o snapshot salvo de 153 retângulos e encontrou corredor para os três destinos
  que passaram pelos gates ODH; mantém clearance runtime e integração com Main
  explicitamente falsos. Não implementa `OptimizeLines`, RoomInfo/portais vivos
  nem movimento. Consulte `docs/rect-router-validation.md`.
- `rough_balance_obstacle_prefix` replica a permutação `new Random(1)` de
  Simitone para o prefixo arquitetônico WallObs+RoomObs, deixando bandas de
  limite e obstáculos de entidades depois dele. Um vetor golden com 16
  entradas corresponde ao runtime .NET 10 instalado; a CLI passou a ordenar
  esse prefixo antes da busca House07. `OptimizeLines` e a ordem exata de
  `roomInfo.Entities` seguem pendentes. Consulte
  `docs/rect-router-validation.md`.
- Snapshot bounded de obstáculos `AttemptWalk`: compõe WallObs/RoomObs, as
  quatro faixas externas de um tile, footprints com margem 3, flags de
  interseção e EP5 bilateral; inventário incerto ou fora do orçamento falha
  fechado. O diagnóstico House07 resolve as tabelas EP5 dos assets exatos e
  inclui portas com caixa salva e HasZeroExtent desligado sem afirmar estado
  runtime. O snapshot House07 fica pronto com 153 retângulos, mas nenhuma rota
  é calculada e `route_clearance` segue false. Consulte
  `docs/saved-attempt-walk-obstacles-validation.md`.
- Adaptador runtime liga o inventário RoomInfo do `World` a RoomObs/WallObs
  preparados e aos portais Portal15 do lote, aplicando flags e EP5 bilateral
  por meio da política AttemptWalk existente. House07 Main no tick 214 ficou
  `ready`: arquitetura e inventário de portais completos, 24 aberturas, 8
  retângulos RoomObs, 128 WallObs e 153 obstáculos; início sem interseção.
  Clearance, estado mutável de portas e movimento no Main continuam falsos.
  Consulte `docs/world-attempt-walk-validation.md`.
- `odh-person --attempt-walk-destination X Y` expõe uma busca de corredor
  diagnóstica no snapshot vivo para destinos na mesma sala. House07 Main,
  avatar 105 até `(408,456)`, retornou `found` com duas entradas. O perfil de
  avatar é uma raiz isolada com threads salvas dormentes; ordem de entidades,
  `OptimizeLines`, clearance, estado de portas e movimento continuam sem
  validação. Consulte `docs/world-attempt-walk-validation.md`.
- Adaptador de auditoria para política de sobreposição entre entidades de um
  `World` restaurado: resolve flags e EntryPoints vinculados, preserva
  inventário incompleto como unsupported e respeita a parada sequencial por
  EP26 sem consultar bloqueadores posteriores. O teste sintético cobre também
  esse curto-circuito. O CLI informa apenas uma auditoria de sobreposição de
  entidades, não uma decisão final de colocação; BHAVs EP5 fora da pequena
  allowlist read-only e obstáculos de arquitetura/portais continuam fora do
  escopo. Consulte
  `docs/world-avatar-place-audit-validation.md`.
- Executor isolado de uma forma BHAV EP5 read-only validada estruturalmente:
  `TestObjectType(StackObject)` seguido de igualdade entre
  `StackObjectAttributes[16]` e literal 1. Um World temporário e dois ticks
  limitados garantem que esse perfil não muta o Main; todas as outras formas
  retornam unsupported. House07 executou o Flamingo BHAV4108 para o candidato
  `(280,376,1,3)` e obteve false após uma instrução. Isso não prova isenção
  bilateral nem compatibilidade com o executável original.
- Módulo puro `odh_saved_avatar_place_overlap` modela uma regra limitada de
  sobreposição de `GetAvatarPlace`: grupo multitile, exceção/tri-state de
  EntryPoint 5, flags de interseção e preferência de parada de EntryPoint 26.
  Os 16 casos sintéticos preservam dados ausentes como unsupported. O módulo
  não executa BHAV nem descobre obstáculos dinâmicos; não prova colocação
  runtime. Consulte
  `docs/saved-avatar-place-overlap-validation.md`.
- Auditoria de bloqueadores agrega uma lista completa em ordem crescente de
  ObjectID, preserva o último bloqueador e a parada por EntryPoint 26. Falha
  fechada para inventário incompleto, IDs fora do domínio VM ou resultados
  ambíguos. São 14 casos sintéticos; não descobre geometria nem executa BHAV.
  Consulte `docs/saved-avatar-place-audit-validation.md`.
- Módulo puro `odh_slot_scoring` calcula e ordena scores do perfil SLOT
  House07 standing/círculo com RNG explícito e ordem de entrada fornecida.
  Preserva consumo de RNG para candidatos rejeitados após score e empates
  estáveis, e aceita penalidade conhecida de avatar/porta/reserva. Entradas
  incertas ou malformadas falham antes de alterar o seed; teto de 4.096.
  Ramos RandomScoring/FSOEqualProximityScore têm testes sintéticos. Não executa
  gates geométricos/PositionValid/EP5 nem liga seed ou ranking ao Main;
  equivalência ao executável original permanece não demonstrada.
- Routing aceita provider opcional de destinos SLOT previamente ordenados e
  tenta um por vez, avançando apenas após `no_path` definitivo. Empates mantêm
  a ordem de entrada; expansões acumulam entre tentativas e limites/entrada
  inválida interrompem sem fallback. A lista é limitada a 4.096 candidatos.
  Chegada preserva entry flags/facing do
  candidato escolhido. A política sequencial segue o Simitone fixado; score,
  RNG e elegibilidade continuam sob responsabilidade do chamador. Sem provider,
  mantém a seleção anterior por menor custo na malha construída.
- Seleção de destino SLOT sobre footprints salvos: o perfil v10
  standing=1/sitting=0 diferencia box livre, objeto sobreposto rejeitado e
  avatar sólido conhecido mantido com penalidade de score. O diagnóstico
  informa elegibilidade nullable e exige uma classificação suportada antes
  de enviar o destino à busca na grade. Portas, multipartes e candidatos
  incertos continuam unsupported; flags/EntryPoint5 e ocupação runtime não
  são executados. Isso não estabelece PositionValid ou clearance.
- Diagnóstico Route45 House07 agora exige a entrada/chunk SLOT explícitos,
  gera os oito candidatos do círculo type-3 de raio exato e publica máscara
  direcional, sala, bounds, piso FloorChangeValid, RaycastWall contra a fotografia
  salva e caminhabilidade. O círculo é um perfil ODH restrito; esses gates
  estáticos não cobrem GetAvatarPlace runtime, e resultado encontrado não prova
  clearance ou equivalência com o jogo.
- Probe offline de portas em saves: liga faces de porta, grupos multitile,
  salas geométricas e ação authored da entrada Portal 15 via OBJD/OBJf; o
  reporte deixa explícito que a BHAV e o clearance de rota não foram executados.
- Geração geométrica de obstáculos aceita pares explícitos tile/sala de portais
  e imita a interrupção de runs de `GenerateRoomObs`; a completude do snapshot
  continua condicionada à identidade inequívoca dos pisos usados e não equivale
  a clearance total.
- Probe de colisão por entidade une flags/OBJM, OBJD e a posição restaurada no
  World. Mantém `unsupported` para posição divergente, avatar sem estado
  `KillTimeout` e porta sem estado runtime; geometria possível não habilita
  clearance ou roteamento.
- Inventário Portal15 invalida a completude para porta/membro sem posição ou
  sala, fora do lote, identidade duplicada e grupos de portas sobrepostos no
  mesmo tile, refletindo a ambiguidade da seleção no Simitone.
- Auditoria estática por sala atribui candidatos a colisão pela sala geométrica
  do centro e compara caixas expandidas contra RoomObs e as quatro faixas
  externas de um tile de `VMRoutingFrame.AttemptWalk`. Os grupos são contados
  separadamente; interseção ou ausência de interseção não vira caminho livre
  ou clearance.
- Grade candidata por sala rasteriza pertencimento, RoomObs, WallObs
  e footprints salvos em coordenadas 1/16 tile, com tetos para memória e trabalho.
  Entradas incompletas falham fechadas. O adaptador ainda não cobre regras
  runtime nem habilita Routing45.
- Candidatos geométricos globais de WallObs usam as fórmulas de segmentos do
  Simitone e tratam faces de porta ausentes como sólidas. O inventário não é
  mesclado por sala como no runtime; consulte a cobertura e os limites em
  `docs/room-obstacles-validation.md`.
- Controller Route45 do ODH agora filtra destinos do perfil exact-radius pela
  máscara direcional usando os setores angulares, coordenadas floor em tiles
  e rotação do alvo de `VMSlotParser.GetSearchDirection` do Simitone fixado.
  Ainda não é a validação completa de destino, nem está ligado ao Main.
- O room map preparado distingue misses estritos de `TryGetValue` dos aliases
  FLRm declarados-sem-resolução efetivamente usados. Mantém o contador legado de
  misses, considera os bytes retidos por fallback como identidade e continua
  bloqueando referências a aliases declarados ambíguos. Segue
  `VMTS1Activator.RemapFloors/BuildFloorDict` do Simitone.
- Consultas de piso preparado e sala geométrica em posições de 1/16 tile,
  incluindo a seleção distinta das metades diagonais. As respostas espaciais
  não validam placement, portais ou aceitação de destinos.
- Tradução pura da regra `VMEntity.FloorChangeValid` do Simitone, com decisões
  distintas para terreno, água, piscina, primeiro nível e entidades comuns.
  O leitor OBJD136/138 também expõe `LevelOffset`; ainda sem integração com
  Main ou aceitação de destinos de rota.
- Adaptador de placement associa flags OBJM, `LevelOffset` OBJD e piso preciso
  da arquitetura preparada para avaliar apenas `FloorChangeValid`; dados,
  coordenadas ou identidade de alias insuficientes resultam em `unsupported`,
  e o runtime continua sem usar o resultado como clearance.
- Resolução dos aliases FLRm/WALm contra catálogos de nomes explícitos e
  ordenados, com IDs dentro da política ODH, diagnósticos de misses e duplicatas.
  A descoberta de arquivos comerciais e a precedência dos providers seguem
  externas à API.
- Preparo limitado de pisos/paredes salvos com aliases explícitos, road flip,
  flags e crop; o mapa de salas pode consumir os grids preparados sem repetir
  conversões. Descoberta de recursos e portas/portais vivos seguem pendentes.
- Composição CPU de terreno, pisos, paredes e avatar Main em House07 usando o
  mesmo World restaurado, com posição viva, resolução SLOT limitada e câmera
  acompanhando o avatar.
- Objetos salvos na mesma captura Main, com Graphic/Hidden/posição vivos,
  filtro de nível/contêiner e sprites alpha/Z sobre a cena opaca. Flags
  dinâmicas e centros multitile seguem o snapshot; novos objetos e reuso de
  identidade exigem atualização do renderer.
- Seleção de objeto por clique no preview Main com lote: o título informa ID,
  GUID e `TreeTableID`. O buffer de seleção respeita alpha/profundidade.
- Preview Main descobre linhas TTAB, testa funções elegíveis em VM isolada e
  permite solicitar a primeira ação privada suportada por Space/Return; no
  House07 a ação `Sit` é elegível e pode ser enfileirada. Em execução X11
  prolongada a VM consumiu a fila e chegou ao Routing opcode45, que faultou
  como não suportado; movimento e efeito de gameplay não foram demonstrados.
- Flags TTAB com `AllowVisitors` (`0x1`) agora são avaliadas contra o perfil do
  ator; bits desconhecidos, pets e perfil ausente seguem fora do suporte.
- Prévia X11 House07 foi aberta e capturada em desktop real; clique, seleção e
  pedido via Space foram validados. Em sessão curta ficou queued; em sessão
  prolongada a VM chegou ao opcode45 e faultou antes de mover o avatar.
- Manager HIT persistente por `TrackID`, com registro explícito de controllers,
  resolução global de eventos e agregação de fontes.
- Recusas de lookup e de capacidade antes de efeitos deixam controllers
  registrados disponíveis; falhas após callbacks continuam faultando o manager.

### Validação

- House07 real: o Sim neighbor 48 sentou na cadeira 128, salvou, abriu a sessão
  em um processo novo e levantou. Os hashes dos dados do lote, família,
  animações, globais e objetos permaneceram iguais. Evidência limitada a esse
  percurso headless; a tela e o clique natural ainda não foram validados. Veja
  [`session-save-design.md`](docs/session-save-design.md).
- Probe Portal15 no House07 da instalação: arquitetura preparada com 23 fontes
  loose explícitas; seis portas/grupos apontam para OBJf action 4105, cujo BHAV
  está presente no asset Doors.iff, e seis salas geométricas foram geradas.
  Sem execução BHAV/runtime ou clearance; nenhum recurso comercial foi incluído.
- Classificador de colisões salvas: build Linux completa; CTest passou em 141
  testes e pulou `linux_window_backend` no sandbox, que passou ao ser repetido
  com display real. Os 142 testes passaram ao considerar essa repetição. No
  House07, 91/182 entidades geraram caixas candidatas, 69 foram ausentes e 22
  ficaram unsupported (8 sem posição, 2 avatares com `KillTimeout` incerto e
  12 portas com estado runtime desconhecido). As caixas ainda não são cruzadas
  com obstáculos de sala e não validam clearance.
- Candidatos room-obstacle: fixture sintética confirma abertura de tile Portal15
  e merge de retângulos igual ao perfil da referência. No House07, 24 pares
  tile/sala deram 8/7/8/6/0 retângulos para salas 1–5; com inventário completo,
  todos os candidatos têm `generation_complete=true`, sem declarar clearance
  nem uso pelo Routing45.
- Inventário de portais: 27 checks sintéticos cobrem abertura por sala-alvo,
  posição ausente/fora do lote, sala ausente, IDs duplicados e sobreposição de
  grupos. House07 mantém 24 pares, zero tiles sobrepostos e inventário completo;
  isso valida somente a montagem estática do snapshot.
- Auditoria estática ampliada: 25 checks focais cobrem as quatro faixas externas
  de `AttemptWalk`, limites inválidos e toque estrito, além das interseções com
  RoomObs. No House07, 50 caixas não cruzam RoomObs, 41 cruzam RoomObs, zero
  cruzam as faixas externas, 22 permanecem unsupported e 69 not_applicable.
  Ainda faltam `wallObs`, políticas dinâmicas, EntryPoint 5 e pathfinding.
- Consultas geométricas: build Linux e 139/139 CTests passaram; teste X11
  `linux_window_backend` foi pulado por ausência de DISPLAY. As consultas
  validam ambos os IDs packed da diagonal, bounds e ownership; isso é apenas
  geometria preparada, sem integração de roteamento ou prova de gameplay.
- Resolver FLRm/WALm: testes sintéticos de nomes, precedência, duplicatas,
  misses, ASCII suportado, imutabilidade e limites. Catálogo de instalação e
  equivalência com o jogo original não são afirmados.
- Completude de identidade do piso: testes cobrem fallback global com mapa vazio,
  alias declarado-sem-resolução usado/não usado e duplicatas com resultado final;
  probe House07 mantém 115 misses estritos, mas zero referências ambíguas, com
  candidatos geométricos completos quando o inventário de portais está completo.
  Clearance de rota permanece falso.
- Preparo de arquitetura: 88 checks sintéticos passaram; build Debug e suíte
  de 133 CTests sem falhas, com janela X11 pulada por ausência de DISPLAY.
  Catálogo externo, portas/portais e runtime do lote continuam pendentes.
- Picking: composição, renderer de objetos/lote e CLIs passaram em cinco CTests;
  o sexto gate de eventos X11 foi pulado sem DISPLAY. A janela ainda exige
  validação visual e de entrada em uma sessão gráfica ativa. Build e CTest
  completos também passaram (127 configurados, X11 pulado, zero falhas).
- Cinco CTests focais passaram para a integração de objetos: oclusão diante e
  atrás da parede, alpha, estado vivo, contenção/nível, remoção/reuso de ID e
  recusa de cutaway incompleto. Fixtures sintéticas, sem prova de gameplay.
- Build Linux e CTest: 127/127 aprovados; X11 foi pulado sem DISPLAY. O probe
  local House07 compôs Main, lote e 135 objetos em 166 camadas; não prova
  gameplay nem equivalência visual ao original.
- Debug e ASan/UBSan: 38/38 casos do manager e 98 checks por configuração.
  Cinco CTests companheiros por configuração foram reutilizados, sem rerun.
- Fixtures sintéticas não comprovam integração World, áudio físico, fidelidade
  completa ou uma build Linux jogável.
- Route45: `odh-routing-tests` passa 51 checks, incluindo a recusa de um
  destino caminável que não corresponde à máscara direcional do SLOT. Validação
  em mundo construído e na revisão Simitone fixada; sem movimento real,
  integração ao Main ou prova de compatibilidade com o jogo original.
