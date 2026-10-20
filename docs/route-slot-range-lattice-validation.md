# SLOT de faixa e enumeração de offsets

`enumerate_reference_slot_range_lattice` reproduz somente a ordem e os limites
da grade de offsets usados pela referência FreeSO/Simitone em
`VMSlotParser.SLOTEnumerationFunction` para SLOTs tipo 3 de versão 10 com
proximidade em faixa. Não valida destino e não substitui a semântica do
executável original.

O registro SLOT8 de House07 tem proximidade raw `16/64/16` e resolução `8`.
Na referência, os campos são lidos na ordem min/max/optimal; max zero usa min
como fallback. Como min e max diferem aqui, a enumeração percorre `x` externo e `y`
interno, ambos de `-64` a `64`, em passos de `8`: 17×17 = 289 offsets. A
função aplica um teto de alocação configurável (4096 por padrão) antes de
reservar o vetor. A unidade retornada é o offset raw do SLOT; a conversão para
posição do lote é responsabilidade de outra etapa.

Os offsets ainda incluem pontos que serão rejeitados depois. Distância mínima e
máxima, direção, sala, piso, paredes/portas, ocupação, score/RNG, facing e
política de failure trees não fazem parte desta rotina. Portanto, os 289 pontos
não são 289 destinos utilizáveis nem autorizam movimento. O controller Route45
continua recusando SLOT8 até haver implementação e evidência para essas regras
e clearance do lote.

Proveniência: FreeSO submódulo em `f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.simantics/Engine/VMSlotParser.cs`, construtor de
`MinProximity`/`MaxProximity`/`DesiredProximity` e
`SLOTEnumerationFunction`. Os valores do SLOT8 são registrados em
`entry-points-validation.md`. Essa referência não prova comportamento do
executável original; análise focada do Route45 original ainda está pendente.

`tests/routing.cpp` valida 289 offsets, ordem inclusiva, teto de alocação,
rejeição de resolução/faixa/perfil inválidos e falha fechada do controller
para o perfil real de SLOT8. São fixtures sintéticas, não uma execução do jogo.
Build focal `odh-routing-tests` e CTest `routing` passaram;
nenhuma validação de Main, movimento real, janela Linux ou jogo original foi
executada nesta etapa.
