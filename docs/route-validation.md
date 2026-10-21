# Cálculo de percurso — 2026-10-13

`odh_route` fornece busca Dijkstra em malha inteira com múltiplos destinos,
células bloqueadas e paredes nas arestas. É uma camada de cálculo para o
controlador de roteamento; ainda não move entidades nem implementa a primitiva45.
`RouteGrid` aceita origem signed para permitir uma região limitada do World sem
perder as coordenadas absolutas; o construtor valida que sua extensão não
estoure int32 e mantém os orçamentos existentes.

O percurso tem passos em oito direções. Custos 10/14 definem a métrica desta
malha, sem representar duração ou provar a política de roteamento TS1. Empates
usam ordem estável de índices. Diagonais exigem ambas as passagens ortogonais
livres, evitando cortar cantos bloqueados ou extremidades de paredes.

O adaptador do mundo deverá definir unidades (previstas: 1/16 de tile), nível,
footprint/clearance e destinos SLOT. A malha recebe a máscara já preparada;
não importa paredes de um lote, detecta footprints por conta própria ou
classifica rooms/portais. Escolher o menor custo entre destinos também não
implementa os scores, aleatoriedade e facing do SLOT.

Limites padrão: 262.144 células, expansões e entradas na fronteira. Dimensões
até 32.767 por eixo e faixa de custo são verificadas antes da alocação. A grade
armazena bloqueios e paredes em dois bytes por célula. A busca comum adiciona
máscaras de alvo/fechamento, `distance` de 32 bits e um byte de predecessor:
9 bytes por célula incluindo a grade. A busca ranqueada usa um índice de alvo
`uint16_t` e máscara de fechamento: 10 bytes por célula incluindo a grade, com
o limite de 65.535 candidatos aplicado antes da alocação. A fronteira empacota
custo e índice num `uint64_t` por entrada. No teto atual, isso representa até
2,5 MiB de armazenamento por célula mais 2 MiB de entradas, além da capacidade
efetiva do vetor e do caminho retornado; não é medição de RSS ou limite total
de memória.
Estados distintos: found, no_path, invalid_input e budget_exhausted. Esgotar
um orçamento não significa que o destino seja inalcançável. A busca atual é
síncrona e não conserva sua fronteira para retomada; não equivale a um tick
de simulação. Memória/latência em consoles permanecem por medir.

CTest 11/11 passou em Debug e ASan/UBSan. A suíte route tem 71 checks de
malhas sintéticas: custo ótimo de diagonal aberta, desvio de obstáculo,
parede simétrica, isolamento, passagem por porta, destino múltiplo, estados
inválidos, orçamentos e regiões absolutas com origem assinada/overflow. As
oito headings cardinal/diagonal validam a reconstrução compacta do predecessor.
Cada segmento dos percursos obtidos é
conferido contra as células/arestas; custo é recalculado. LSan continua não
validado.

Não houve novo teste com o executável original ou percurso de um Sim real.
Próxima unidade: posição das entidades, geração/validação dos destinos SLOT,
ligação do serviço45 à espera da VM e avanço por ticks. Manter casos de falha,
cancelamento e alteração de obstáculos durante o percurso. O cenário Linux
completo, sprites/animações e persistência continuam pendentes.


## Retomada de Route45 após ociosidade e alvo autorreferente (2026-10-21)

O scheduler agora reinicia sua referência de tick quando uma cadeia raiz de
rota termina ou é cancelada; durante uma rota ativa continua exigindo ticks
consecutivos e rejeitando duplicatas, regressões e saltos. Isso evita que a
primeira atualização de uma nova rota seja rejeitada por causa do intervalo
ocioso desde a rota anterior. Um alvo com o mesmo ID do Caller conserva sua
posição inicial como centro do SLOT durante o movimento; identidades e geração
continuam validadas.

`tests/routing.cpp` cobre as duas condições, inclusive a retomada em tick 100,
e a guarda contra listas que excedem o índice `uint16_t`. O fixture sintético
com frame de portal também move o avatar que é seu próprio alvo; após atualizar
a grade da sala, a rota suspensa é recalculada e retomada. CTests `route` e
`routing` passaram (2/2); `odh-routing-tests` reportou 159 checks.

O probe Portal26 agora encerra no resultado da ação e conserva o diagnóstico
antes de a Main despachar outra rota. Com House07 restaurado, o trace filtrado
pelo CodeOwner contém 49 instruções: `Doors.iff:4105` chama 4112 e executa
GotoRelativePosition até o portal; depois chama 4113, cujo Route45 literal é
recusado porque os candidatos estão na sala1 e a grade ativa cobre a sala3.
`4105` termina `returned_false` sem fault, com o ator em `[408,424,1,0]`; a
travessia continua ausente. A peça25 falha antes de mover com
`routing start or pre-ranked goal input invalid`. Esses são resultados do host
ODH e da malha local, não falhas atribuídas ao executável original. Na primeira
execução desse checkpoint, o build Debug passou nos 160 CTests, incluindo
`linux_window_backend`, e `odh-routing-tests` reportou 159 checks. Uma
revalidação posterior, registrada em `docs/current-handoff.md`, reportou 160
CTest configurados, sem falhas, com o gate X11 pulado por falta de DISPLAY, e
280 checks de routing após os reparos subsequentes. O probe Portal26 parou no
resultado da ação (código 0, resultado da ação `false`).
