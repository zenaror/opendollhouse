# Composição de sprites de parede

Checkpoint 2026-10-14. `odh_wall_sprite` fornece uma primitiva CPU portátil para
combinar padrão SPR#, máscara SPR# e mapa de profundidade gerado. Ainda falta
integrá-la ao `odh-saved-scene`; essa unidade não desenha a arquitetura do lote
nem executa a simulação salva.

## Referência e contrato

Implementação própria a partir de FreeSO
`4c6b3e8f5835b228723caea3c9f683c62f244f73` (MPL-2.0):
`TSOClient/tso.content/ContentSrc/Effects/2DWorldBatch.fx`,
`TSOClient/tso.common/Utils/TextureGenerator.cs` e
`TSOClient/tso.world/Components/WallComponent.cs`.
O renderer RC avisa que não é usado no modo 2D por representar algumas direções
incorretamente; seus quads e regras de alpha não são misturados com este perfil.

`composite_wall_sprites` amostra cada textura independentemente com POINT/CLAMP
no centro de cada pixel de destino. A coordenada inteira equivale a
`floor((2*d+1)*source_extent/(2*destination_extent))` e evita overflow.
Os tamanhos do padrão, máscara, depth e destino podem diferir. A máscara
**substitui** o alpha do padrão: zero não escreve cor ou profundidade; 255
escreve o RGB do padrão com saída opaca. Máscara nula usa alpha do padrão.
Alpha parcial é rejeitado. RGB não recebe iluminação, gamma ou composição de
salas. Não há equivalência ao shader GPU completo ou ao jogo original.

Depth byte usa `back + ((1-z/255)/0.4)*front_delta`, cálculo intermediário double
convertido para float como no compositor existente. Menor vence; empates
preservam o fragmento anterior. Base deve ser opaca com depth finito ou +inf.
Todos os inputs, inclusive camadas fora da tela, são validados antes da cópia.
Metadados SPR# e índices não entram na composição.

`wall_sprite_depth` gera as doze configurações de parede da referência, com
incrementos float iterativos, clamp superior 255 e ties-to-even explícito.
Zoom 1=far, 2=medium, 3=near; orientação 0=TL, 1=TR, 2=horizontal, 3=vertical.
As alturas dos mapas medium/far cardinais são 135/67; os destinos da referência
usam 136/68. Isso exige normalização independente, não arrays de mesmo tamanho.
Junções, recortes/down, iluminação e posicionamento arquitetônico ficam pendentes.

Limites padrão: 8192 camadas, 2048 por eixo, 1Mi pixels de base,
16Mi visitas a pixels fonte, 16Mi pixels de destino e 16Mi candidatos recortados.
Visitas fonte contam padrão+mask efetiva+depth por camada, inclusive aliases e
repetições; áreas completas contam mesmo quando fora da tela. Extensões exatas,
alpha, profundidade representável e budgets são verificados antes de devolver
qualquer resultado. Inputs não são alterados. Esses limites não provam orçamento
real de memória/tempo no New 3DS.

## Validação

90.801 verificações sintéticas passaram no target CMake Debug e ASAN/UBSAN.
Incluem todos os 90.684 pixels dos doze mapas via cálculo analítico, modos de
arredondamento do host, tamanhos distintos, alpha substituído, buracos, depth,
empates, clipping, limites e overflow uint64. Os três testes afetados
`wall_sprite_composition`, `sprite_composition` e `legacy_sprites` passaram nos
dois builds sem avisos. LSan desativado por ptrace; não houve leak check.
Não foi repetida a suíte completa nesta unidade.

Um cálculo independente Python comparou todos os bytes RGBA/depth float32 e
mapa Z de uma composição 80x290. Inputs são exports previamente verificados
pelo oracle SPR# descrito em `wall-resource-validation.md`: padrão global
SPR2049/frame0 near 64x271 e máscara Windows SPR350/frame0 far 16x67. Essa
combinação de zooms é deliberadamente um teste de tamanhos distintos, não uma
parede selecionada do lote. Destino 64x271, origem (-5,7), back=2/front_delta=-4,
base com depth -20/100 em duas regiões. Nenhum original comercial foi escrito
ou relido por esse experimento.

Debug, ASAN e oracle coincidiram: 15989 candidatos, 2212 escritos,
12200 transparentes e 1577 ocluídos. Hash RGBA:
`31a6d687fa4e57eac635c06b7adfbe7bfb18e1e0edbdae5b3724e15e5a36bf7f`;
depth: `26eedb852b0f849dccac1fe7919d9206774613886d977cc6bb60fb83f3b44560`.

Relatórios ignorados: `reports/wall-composition-native.cpp`,
`verify-wall-composition.py`, `wall-composition-native-validation.json`, binários,
exports `.rgba/.depth/.z` e imagem PPM. Nenhum recurso comercial entra no Git.
Próxima unidade: resolver padrões/máscaras globais e objetos, orientar faces e
projetar paredes do World salvo no mesmo espaço de profundidade de pisos,
avatars e objetos; depois completar junções e recortes. A meta integral Linux
funcional continua ativa.
