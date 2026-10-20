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
até 32.767 por eixo e faixa de custo são verificadas antes da alocação. O
scratch fixo usa duas máscaras da grade, `distance` de 32 bits, um byte de
direção de predecessor e duas máscaras da busca: 9 bytes por célula. A
fronteira empacota custo e índice num `uint64_t` por entrada. No teto atual,
isso representa 2,25 MiB de arrays por célula mais até 2 MiB de entradas da
fronteira, além da capacidade efetiva do vetor e do caminho retornado; não é
medição de RSS ou limite total de memória.
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
