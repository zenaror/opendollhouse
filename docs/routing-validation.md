# Percurso, VM e ticks — 2026-10-13

`odh_routing` liga a primitiva45 à busca de percurso e ao World em um perfil
inicial. O estado muda ao longo de ticks, com conclusão da espera da VM quando
chega ao destino. Ainda faltam snap, animação, scheduler geral e fidelidade de
roteamento original; a meta Linux funcional permanece aberta.

## Estado e contrato

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
separado do controller. Aceita o perfil House07 v10 type-3 já descrito,
standing=1/sitting=0/ground=0, sem snap, proximidade min=max=optimal positiva
até 4.096. Também reconhece os bits RandomScoring (8192) e
FSOEqualProximityScore (1<<29), cobertos por fixtures sintéticas. Não modela
perfis sitting/ground, ranges, offsets, on-point ou SnapToDirection.

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
`maxScore = (floor(distance(objeto,ator)) + raio)/3 + 2`, com divisão inteira;
normalmente soma esse valor à fração e subtrai
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

Próximas etapas: decodificar/resolver SLOT privado da cadeira para snap46,
ligar animação44, sprites/paletas e composição Linux. Ampliar e comparar a
política de roteamento, clearance e tempo contra o original, sem promover
este percurso construído a prova de fidelidade.
