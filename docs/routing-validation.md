# Percurso, VM e ticks — 2026-10-13

`odh_routing` liga a primitiva45 à busca de percurso e ao World em um perfil
inicial. O estado muda ao longo de ticks, com conclusão da espera da VM quando
chega ao destino. Ainda faltam snap, animação, scheduler geral e fidelidade de
roteamento original; a meta Linux funcional permanece aberta.

## Estado e contrato

`build_world_room_route_grid` agora converte um `AttemptWalkObstacleSnapshot`
runtime completo em uma malha local bounded. Usa os limites Room.Bounds já
ajustados, inclui as quatro faixas externas da política AttemptWalk e rasteriza
os retângulos no lattice 1/16 tile com interior estrito (`x1+1 <= x < x2`,
`y1+1 <= y < y2`). Os limites padrão são 16.384 retângulos, 262.144 células
alocadas e 262.144 células acumuladas para rasterização. Snapshot incompleto,
limites desalinhados, retângulos inválidos ou orçamento excedido não publicam
malha parcial; o motivo original do snapshot é preservado.

O teste sintético cobre limites, faixas, convenção de borda e orçamentos; o
teste de integração passa o snapshot vivo já montado com RoomInfo e EP5 por esse
conversor. Isso valida apenas a conversão para RouteGrid. A grade ainda não é
consumida por `Routing`/Main, não replica a geometria contínua do VMRectRouter
nem prova `GetAvatarPlace`/PositionValid por passo.

World tem posição opcional (x/y inteiros, nível positivo e direção 0..7), com
unidades de 1/16 tile no adaptador. Posições fora da faixa, entidades ausentes
ou vinculadas a um recipiente recusam mudança. Posição não integra ainda todos
os campos brutos, flags espaciais, multi-tile ou dados de lote do TS1.
Cada entidade recebe uma geração monotônica; reutilizar ID não faz um job
antigo mover a nova entidade.

O callback recebe identidade da VM e ticket antes de planejar. Um controlador
liga-se a uma VM de endereço estável e recusa outra origem. Planejar não move
o avatar. Um tick consecutivo aplica um segmento válido; duplicatas, passado
e lacunas de ticks são recusados. O chamador deve processar ticks em ordem.
Na chegada aplica facing/person data37 e conclui o ticket uma vez.

Cada entidade mantém `static_footprint` como estado runtime separado da posição
salva. No perfil Simitone fixado, a caminhada torna o footprint do avatar
dinâmico antes do primeiro passo; saída ou cancelamento do job o torna estático
novamente. Rotas turn-only não iniciam caminhada. `World` não retém um índice
RoomInfo; `query_world_room_info` reconstrói os obstáculos do estado vivo a cada
consulta e falha fechada se os dados relevantes mudarem durante os resolvers.
O caminho Main ainda não alimenta essa consulta na clearance da rota. A mesma
transição ainda precisa de confirmação contra o executável original.
Proveniência: Simitone fixado, submódulo FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.simantics/Engine/VMRoutingFrame.cs` (`StartWalkAnimation`,
`PreExit`) e `Entities/VMEntity.cs` (`StaticFootprint`, `SetObstacleStatic`).

Desde 2026-10-21, cada job também captura `Entity.radian_direction` junto da
posição. Callback de destinos e tick ativo invalidam a rota se o ângulo muda,
inclusive quando o notch de oito direções permanece igual. Passos que o próprio
job publica atualizam o snapshot angular; a chegada define o ângulo contínuo
canônico do facing final. Isso evita usar uma rota planejada com direção de
entrada obsoleta, mas ainda não implementa `VMRoutingFrame`, `PortalTurns`,
curva de virada nem animação de caminhada. Teste fixture cobre a mudança
fracionária no mesmo notch e cancelamento sem deslocamento adicional.

`RouteFrameContext` representa cada frame ativo em ODH, com estados enumerados
INITIAL/BEGIN_TURN/WALKING/TURN_ONLY/END_TURN/SHOOED/ROOM_PORTAL/STAND_FUNC/
FAILED. O controller mantém jobs pai suspensos em uma pilha limitada a 16;
aceita Route45 aninhado somente quando o topo da VM prova que a chamada veio
da BHAV externa ligada ao ticket pai e conserva a mesma identidade do avatar.
O estado pai fica ROOM_PORTAL enquanto o filho executa. `incoming_direction` e
`consume_portal_turn` consultam o contexto pai efetivo e atualizam seu contador
`PortalTurns`, seguindo a seleção parent-or-self observada na referência.
Quando um filho termina, o controller restaura o job e o estado de footprint do
pai. Se a BHAV externa mudou a posição do avatar, o pai tenta recalcular seu
trecho até o mesmo destino na malha fornecida; falha fechada se não houver
caminho. Essa malha continua única e não representa clearance por sala nem
prova a semântica de mudança de sala do jogo.
Referência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`,
`FreeSO/TSOClient/tso.simantics/Engine/VMRoutingFrame.cs` (`GetParentFrame`,
`CanPortalTurn`, `AttemptWalk`).

`Routing::call_external_frame` liga uma chamada de BHAV resolvida pelo host ao
ticket pendente do Route45. O host pode preparar Portal15 privado a partir do
registro salvo, da entidade viva e do recurso de definição exato; a condição
precisa ser zero e as rotinas semiglobais/globais permanecem fora do perfil.
O host ainda aloca `CodeOwner`, instala a closure e agenda a chamada; não há
descoberta automática do portal pelo controller. Esperas da BHAV externa
pausam o movimento. Cada retorno valida ticket, geração e CodeOwner; mudança de
posição força novo planejamento até o destino pai na mesma malha. Falha de
BHAV propaga false para o frame Route45 correspondente. O teste sintético cobre
Route45 pai → BHAV externa → Route45 filho → retorno e replanejamento do pai,
além do consumo de `PortalTurns` no contexto pai. Isso não integra a resolução
de salas, RoomInfo/EP5 vivo, seleção de portal ou o host persistente Main.

Antes de cada passo, verifica ticket, gerações, posições esperadas, vínculo de
ocupação e próximo segmento na malha. Restart, exclusão, recriação de ID,
movimento externo ou novo obstáculo cancelam o percurso. Falhas de percurso
retornam false neste protótipo; árvores de falha do original não estão ligadas.
Observers recebem eventos após a mudança, não podem reentrar no controlador;
exceções de observers não revertem movimento já aplicado. Todos os objetos
referenciados devem sobreviver às chamadas; não há sincronização entre threads.

## Perfil provisório e lacunas

Operando45: Global scope2, flags e tail zero. O SLOT tipo3 é selecionado por
data. O perfil requer versão10, offsetzero, círculo min=max=optimal positivo,
flags0x55, standing1/sitting0/ground0, snap-1, height0, facing-2, resolution16,
maxsize/i10 zero e gradientbits0x3e400000. Outros layouts ou regras falham
explicitamente como unsupported.

Sem provider explícito, gera oito destinos do círculo exact-radius do perfil
e usa menor custo de malha, com tie determinístico. Antes da busca, cada
destino passa pela máscara direcional do SLOT; os `RouteEntryFlags` de chegada reproduzem
`VMSlotParser.GetSearchDirection` da revisão Simitone fixada: convertem posição
1/16 tile em tiles por floor, subtraem a rotação discreta do objeto, aplicam os
setores angulares do código-fonte e intersectam com os bits direcionais do
SLOT. Testes cobrem rotação, limites de tile fracionários, coordenadas
negativas, máscara vazia e setores diagonais. Isso elimina a estimativa anterior
por octantes, mas ainda não replica seleção por score/RNG, elegibilidade de
sala, raycast, ocupação, facing completo ou regras do jogo original. Não alegar
equivalência. Uma unidade de movimento por tick, inclusive diagonal, é
velocidade ODH provisória; não é tempo/velocidade de caminhada TS1 comprovados.
Proveniência do filtro direcional: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`,
FreeSO submódulo `f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.simantics/Engine/VMSlotParser.cs:GetSearchDirection` e
`tso.common/Utils/DirectionUtils.cs:NormalizeDegrees`.

### Destinos previamente ordenados — 2026-10-20

`Routing` aceita opcionalmente um `PreRankedSlotGoalProvider`, chamado com a
tabela SLOT, índice type-3 e posições do ator/alvo. O chamador fornece candidatos
já elegíveis e ordenados, incluindo a ordem estável dos empates; cada item leva
posição, `entry_flags` e facing final (0..7). O controller recusa reentrada e
alterações do ator/alvo durante o callback, mantém o gate do perfil SLOT
restrito e recusa flags fora da máscara suportada. Não ordena os itens, não
calcula score e não consome RNG da VM. A enumeração N/NE/E existente
é apenas ordem geométrica ODH; não se tornou uma ordem de score Simitone.

`find_pre_ranked_slot_route` tenta cada destino isoladamente. Apenas `no_path`
definitivo permite tentar o próximo. Destino bloqueado é `no_path`; célula fora
da grade, flags vazias, facing inválido, origem inválida ou lista vazia são
`invalid_input`. A lista é limitada a 4.096 candidatos. Expansões acumulam entre tentativas; esgotamento interrompe a
seleção como `budget_exhausted`, inclusive quando a tentativa anterior consumiu
exatamente o teto. O limite de frontier é por tentativa e limita memória viva.
O resultado identifica o índice escolhido e as expansões totais. Ao chegar,
`Routing` aplica exatamente flags/facing desse item, sem recalcular a política.
Sem provider, o caminho de menor custo e o atalho turn-only anteriores continuam.

Proveniência da política sequencial: Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`, núcleo FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`,
`VMRoutingFrame` (seleção de um destino SLOT ordenado e nova tentativa após
falha). A busca Dijkstra e os limites cumulativos são implementação própria ODH.
O provider é uma fronteira explícita: o chamador ainda deve demonstrar
ranking, elegibilidade e validação do destino; fornecer uma lista construída
não valida score/RNG, reserva, colisão runtime ou fidelidade ao jogo original.
O cancelamento durante movimento ainda conclui false, sem tentar outro destino.

### Score de referência com seed explícito — 2026-10-20

`odh_slot_scoring` / `score_reference_slot_candidates` é um adaptador puro
separado do controller. Aceita o perfil House07 v10 type-3 já descrito e o
perfil Door 16–32 (`flags=0x54`, facing -2), ambos standing=1/sitting=0/ground=0,
sem snap e com resolução16. O perfil de faixa exige proximidade exatamente
min=16/max=32/optimal=16. Também reconhece os bits RandomScoring (8192) e
FSOEqualProximityScore (1<<29), cobertos por fixtures sintéticas. Não modela
perfis sitting/ground, offsets, on-point ou SnapToDirection.

O chamador entrega em ordem explícita somente registros que já passaram pelos
gates de proximidade, sala e direção. Cada registro tem posição 1/16 tile,
distância medida pela enumeração (no círculo, o raio SLOT mesmo nas diagonais),
entry flags, facing final e resultado conhecido da verificação posterior:
elegível, elegível com penalidade ou rejeitado após score. `unknown` é inválido,
inclusive em um registro posterior a outros válidos. Isso não implementa
enumeração, geometria, bounds, raycast, cadeira, PositionValid, EP5, reserva ou
ocupação; classificar um registro como elegível é responsabilidade do chamador.
Não converte automaticamente snapshots incertos em elegibilidade runtime.

Cada registro normal consome `RandomState::next(1024)` antes da verificação,
inclusive rejeitados. A fração é calculada como float32 e promovida a double.
Para o círculo, `maxScore = (floor(distance(objeto,ator)) + raio)/3 + 2`; para
a faixa, soma-se primeiro `max(abs(optimal-min), abs(max-optimal))` a esse valor,
conforme `VMSlotParser`. As divisões são inteiras. Normalmente soma esse valor
à fração e subtrai
`floor(distance(candidato,ator))/3.0`. As distâncias são euclidianas nas unidades
1/16, sem divisão por 16 antes do cálculo. RandomScoring omite ambas as
correções. FSOEqualProximityScore usa maxScore sem RNG e não soma o ajuste de
proximidade; ainda subtrai a distância ao ator se RandomScoring estiver
desativado. Multiplica pelo peso
standing (1 neste perfil) e divide uma única vez por 100.000 se houver
penalidade conhecida de avatar, porta ou destino reivindicado.

Retorna scores base/seed antes/depois por entrada e resultado global, além de
`ranked_goals` diretamente utilizável por um provider explícito e índices das
entradas aceitas. Ordena por score decrescente com estabilidade nos empates.
Rejeitados conservam score base, sem score final: a rejeição pode anteceder
as correções posteriores. Limita a 4.096 entradas e valida perfil, metadados,
finiteza, coordenadas short e distâncias sem overflow do int32 da referência
antes de consumir RNG. Entrada malformada preserva o seed. Lista vazia é
válida. Não presume seed inicial, estado da VM ou ordem do círculo ODH;
nenhum Main foi ligado a esse módulo.

Proveniência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, FreeSO
submódulo `f86bbd34b2112296ed786741f640a0535886d180`,
`tso.simantics/Engine/VMSlotParser.cs:141-173,207-321`,
`tso.simantics/VMContext.cs:524-531` e
`tso.world/Model/LotTilePos.cs:42-45`. Esta é evidência de comportamento da
referência fixada; equivalência ao executável original permanece não provada.

Validação focada em builds separados Debug (`/tmp/odh-slot-scoring-debug`) e
ASan/UBSan (`/tmp/odh-slot-scoring-sanitize`, `ASAN_OPTIONS=detect_leaks=0`):
CTest `slot_scoring|random_state|routing`, 3/3 em cada build, com
`slot scoring: 57 checks passed`, `131 checks passed` de RNG e
`74 routing checks passed (constructed world only)`. Cobre fórmulas dos ramos,
divisão inteira de maxScore, truncamento euclidiano 1/16, consumo em rejeições,
empates, penalidade, seed zero explícito e entradas inválidas/over-budget sem
alteração do seed. Não é validação de seleção em lote real ou hardware.

Em 2026-10-21 o scorer passou a aceitar o SLOT128 type3 índice3 real de
`Doors.iff`, cuja faixa raw é `[16,32,16]`. Testes extras cobrem maxScore da
faixa, pontuações nos limites mínimo e máximo, penalidade, consumo de RNG em
rejeição, ordenação e recusa de candidatos fora da faixa ou máscara. O teste
focal agora registra 66 checks. Isso valida aritmética sobre entradas já
filtradas; não integra enumeração, sala, colisão/ocupação ou seleção ao
controller Route45. O runtime continua falhando fechado nesse SLOT até que
esses gates sejam implementados.

A máscara da malha é fornecida explicitamente e deve considerar clearance.
O atalho sem movimento (`start == goal`) só é aceito quando a célula também
está dentro da malha e livre; coincidência geométrica não valida destino.
Na revisão local de Simitone, `VMSlotParser.VerifyAndAddLocation` valida
raycast/ocupação antes de `VMRoutingFrame.AttemptWalk` usar `TURN_ONLY`.
Isso justifica exigir validação do goal antes do atalho, mas não prova que a
malha ODH atual modele toda a política nativa. Não há carregamento ou cálculo
dos footprints originais. Busca é síncrona;
limite esgotado é diagnóstico, não retorno false de destino inalcançável.
Obstáculo novo cancela; replanejamento, concorrência entre Sims, portais e
reservas de destino ainda precisam ser implementados.

## Evidência

CTest12/12 passou em Debug e ASan/UBSan no checkpoint anterior. No checkout
atual, `odh-routing-tests` passa 74 checks e `odh-route-tests` 71 checks; são
malhas/mundos construídos, não validação do lote nativo. Cobrem espera/avanço,
relógio, chegada, restart, obstáculo novo,
entidade movida/removida/recriada, profiles unsupported, nível, outro VM e
reentrada. O incremento de destinos ordenados passou os dois testes CTest
`route|routing` em builds separados Debug (`/tmp/odh-ordered-goals-debug`) e
ASan/UBSan (`/tmp/odh-ordered-goals-sanitize`, `ASAN_OPTIONS=detect_leaks=0`),
com 71 checks de route e 74 de routing em cada build. Inclui preferência
distante, desempate estável, fallback após destino bloqueado/inalcançável,
limites cumulativos e de frontier, entrada inválida e chegada com flags/facing
do provider. A CLI usa
FAR/IFF/BHAV/SLOT sintéticos para testar a integração.
LSan permanece não validado (`detect_leaks=0`, ptrace).

Probe com `DiningChairs.iff`/BHAV1001 e `Global.iff`/SLOT100 reais, identidades
registradas em `slot-validation.md` e `world-validation.md`. O cenário ODH tem
avatar(16,64), cadeira(64,64), nível1, malha128x128. A região55..73 de ambos
os eixos é bloqueada; parede entre x39/40 com abertura y50..55. Essa geometria
é construída e não vem de um lote/save original.

Resultado observado: 33 ticks/movimentos até(48,64), direção2, entryflags64.
Cada passo tem deslocamento máximo de uma unidade, evita a região bloqueada
e cruza a parede pela abertura. A VM retoma, executa11instruções totais e
para em BHAV1003/PC19, `unsupported primitive 46` (snap), com três frames.
Posição/person data mudaram; sentar/levantar completo ainda não ocorreu.

JSON Debug e ASan/UBSan idênticos, stderr vazio, saída3 esperada. Reports
ignorados `reports/chair-route-probe.json` e `reports/chair-route-probe-sanitize.json`.
Nenhuma execução do Sims.exe.bak, janela, animação ou teste de hardware.

## Route45 com SLOT Literal do StackObject — 2026-10-21

`Routing` agora aceita os escopos Global (2) e Literal do StackObject (1).
Para escopo 1, exige um provider de host que devolva a tabela SLOT do objeto em
`VmFrame::context.stack_object`; ausência, ambiguidade e falha de carregamento
recusam o pedido, sem fallback para SLOT100. O host `odh-person` resolve o
GUID pela definição única do objeto, valida OBJD/GUID/MasterID e lê somente o
SLOT indicado pelo OBJD. O cache de tabelas carregadas é limitado a 256
entradas. No Route45, flags de operandos aceitam somente `NoFailureTrees`
(bit 0), com os bytes finais zerados. Essa aceitação ainda não demonstra toda
a semântica de falhas/árvores de falha do jogo.

Leitura estática do recurso comercial externo `Objects.far` / `Doors.iff`:
BHAV4113 compara a condição 4115 e escolhe Route45 Data 0 ou 1; ambos usam
Scope Literal (1) e flag `NoFailureTrees` (1). O OBJD3114 (GUID985) vincula
SLOT128; type-3 índices 0/1 têm flags `0x0801` (NORTH|IgnoreRooms) e `0x0810`
(SOUTH|IgnoreRooms), facing -3 (FaceAnywhere), raio exato 16 e offset zero.
A referência FreeSO fixada trata `IgnoreRooms` como bypass do teste de sala,
e `FaceAnywhere` como ausência de uma rotação final forçada. O perfil ODH
reconhece esses dois registros exatos além do House07, filtra cada candidato
pela sala da grade ativa através de um `RouteGoalValidator` do host e preserva
a orientação de chegada para FaceAnywhere. Sem o validador de sala, SLOT com
IgnoreRooms falha fechado; candidatos em outras salas são removidos, não
roteados pela grade atual. `NoFailureTrees` bit0 cria o frame de rota com
failure trees desabilitadas na referência.

`odh-routing-tests` agora tem 134 checks, incluindo os dois candidatos reais
N/S sintetizados dos campos lidos em SLOT128, seleção pelo StackObject, filtro
de sala requerido, rejeição sem filtro e orientação final FaceAnywhere. Build
`odh-person` e CTest completo passaram (159/159; backend X11 pulado sem display).
A integração House07 Sit/Stand também passou, mas continua sem chamar o provider
(`stackobject_slot_resolutions=0`); ainda não há sessão viva que carregue uma
instância Door por Route45. Portal15,
transição multi-sala, ranking/ocupação completos e equivalência ao executável
original permanecem pendentes. Referência: FreeSO fixado
`f86bbd34b2112296ed786741f640a0535886d180`, `SLOT.cs`, `VMSlotParser.cs`,
`VMGotoRoutingSlot.cs` e `VMRoutingFrame.cs`.

Próximas etapas: criar uma integração que invoque a BHAV real de Door e prove
a resolução de SLOT Literal, ligar Portal15 e grids por sala, e conectar a
Main natural por RunFunctionalTree. Snap46, animação44, sprites/paletas,
composição/janela Linux e comparação contra o original ainda fazem parte do
trabalho; os resultados construídos não provam fidelidade.

### Probe dos BHAVs e SLOTs reais da porta — 2026-10-21

`odh-bhav --route-slot-probe --object-def 3114` agora carrega o OBJD3114 e a
tabela SLOT128 vinculada e fornece essa tabela ao Route45 Literal da VM. Esse
modo não ativa Snap nem requer um OBJD monopeça: o recurso 3114 informa
`MasterID=1`, mas o probe usa apenas sua tabela SLOT como StackObject do mundo
construído. O atributo1 da StackObject pode ser configurado com
`--route-attribute1 0|1`; o valor default continua explícito e sintético.

`tests/door_route_probe.py --game-root '../referencias/The Sims'` executou a
BHAV real `Doors.iff:4113`, que chama `4115`, e ambas as chamadas Route45 com
`Objects.far` e `Global.far` originais. Atributo1=0 tomou o ramo pc2, resolveu
SLOT128 type3 índice1 e terminou no ponto `[64,80,1,3]`, flags16, após53
ticks. Atributo1=1 tomou pc1, resolveu type3 índice0 e terminou em
`[64,48,1,1]`, flags1, após48 ticks. Cada ramo recebeu exatamente uma
resolução do provider e retornou true. Os 134 checks de `odh-routing-tests` e
159 CTests passaram; `linux_window_backend` foi pulado sem display.

Isso prova execução ODH de BHAV real e consumo de SLOT comercial em um mundo e
grade construídos, não interação normal, instância Door restaurada, hierarquia
multitile, clearance dinâmico, portal/transição de sala ou equivalência ao
executável original. O script é manual/opcional e recebe os arquivos externos;
eles não são adicionados ao repositório.

### Instância Door multitile em House07 — 2026-10-21

O import real de House07 contém portas ID25, 27 e 57 com GUID985 e
`master_guid=458`; ID25 pertence ao grupo `[25,26]`. O provider de
`odh-person` agora aceita partes multitile somente se o OBJD da parte concorda
com o índice (GUID, MasterID e SubIndex), há um master único (`SubIndex=-1`) e
o `master_guid` restaurado da instância bate com esse master. Continua usando
o SLOT indicado pelo OBJD da parte (SLOT128 para GUID985); falha fechado se a
identidade for ausente ou ambígua.

`tests/house07_route45_integration.py --binary ./odh-person --game-root
'../referencias/The Sims' --stack-object-id 25 --literal-slot` injeta Route45
Literal no Main de uma cópia temporária do avatar, usando a instância Door25
restaurada e a grade dinâmica da sala ativa. Bob saiu de `[392,440,1,3]` e
chegou a `[408,424,1,1]` em Room3 com16 movimentos, uma resolução e uma carga
de SLOT. Na variante com mutação, um obstáculo móvel causou retorno false após
um movimento; o diagnóstico foi `next segment blocked during route`. O teste
valida a relação multitile, o provider runtime e refresh/cancelamento no mundo
salvo.

A chamada Route45 ainda é injetada num Main temporário: não valida interação
natural do jogador nem executa a BHAV 4113 em House07. O probe anterior executa
4113/4115 reais, mas constrói a entidade e a grade. Juntos eles não demonstram
travessia de Portal15, simulação completa de portas/clearance ou equivalência
ao executável original.

### GetDistanceTo e Route45 StackVariable da porta — 2026-10-21

O opcode11 agora implementa o perfil observado em `Doors.iff:4114`: saída
Temp0, destino Caller default, Caller e StackObject posicionados no mesmo nível
e operandos restantes zerados. A distância é `floor(sqrt(dx²+dy²)/16)` sobre
coordenadas em 1/16 de tile. Outros operandos, posições ausentes e níveis
diferentes falham fechados. O serviço está ligado ao probe BHAV e ao runtime
`odh-person`; quatro testes cobrem distância, truncamento, nível e formato.

Route45 agora reconhece scope 0 StackVariable: lê o índice de SLOT a partir do
argumento BHAV selecionado pelo campo Data e obtém a tabela pelo StackObject,
com teste dedicado de resolução e conclusão assíncrona. Em `Doors.iff:4114`, o
trace real percorre GetDistanceTo e chamadas 4115, então chega ao Route45 de
pc4 com Data0/scope0. A expressão anterior grava 3 em Args[0], logo o alvo é
SLOT128 type3 índice3. Esse registro real possui `flags=0x54`, facing -2 e
faixa de proximidade 16–32. A integração gera offsets limitados, filtra
proximidade/direção e classifica os candidatos antes de entregá-los ao Route45,
com estado RNG explícito. O probe com os recursos reais termina após 21
instruções em `Doors.iff:4162` pc1 com `unsupported primitive 28`
(`RunTreeByName`). Antes disso, a rota construída percorre 33 células até
`[48,64,1,2]`, com `RouteEntryFlags=64`, e opcode8 avança.

A grade usa walkability raw construída e bounds de piso, sem clearance dinâmico
de avatar. Isso não valida interação natural, porta restaurada em House07,
Portal15, gameplay jogável ou equivalência ao executável original. Verificação:
142 checks de `odh-routing-tests`, 76 de `odh-slot-scoring-tests`, 4 de
`odh-distance-to-tests` e 160/160 CTests passaram; `linux_window_backend`
passou nesta sessão. Nenhum dado comercial foi alterado. Os caminhos TS1 do
Simitone fixado `4906db3156616bfaffc10ce0963cf08062975fef` orientam a malha, sem
provar equivalência ao original.
