# SLOT de faixa e enumeração de offsets

`enumerate_reference_slot_range_lattice` reproduz somente a ordem e os limites
da grade de offsets usados pela referência FreeSO/Simitone em
`VMSlotParser.SLOTEnumerationFunction` para SLOTs tipo 3 de versão 10 com
proximidade em faixa. Não valida destino e não substitui a semântica do
executável original.

`filter_reference_slot_range_proximity` aplica o filtro seguinte do mesmo
parser: distância entre `min - 0.5` e `max + 0.5`, inclusive. Usa distância
Euclidiana, salvo quando `FSOSquare` está marcado, e preserva a ordem recebida.
As comparações são feitas com inteiros para evitar arredondamento de ponto
flutuante nos limites.

O registro SLOT8 de House07 tem proximidade raw `16/64/16` e resolução `8`.
Na referência, os campos são lidos na ordem min/max/optimal; max zero usa min
como fallback. Como min e max diferem aqui, a enumeração percorre `x` externo e
`y` interno, ambos de `-64` a `64`, em passos de `8`: 17×17 = 289 offsets. A
função aplica um teto de alocação configurável (4096 por padrão) antes de
reservar o vetor. A unidade retornada é o offset raw do SLOT; a conversão para
posição do lote é responsabilidade de outra etapa.

Aplicando somente a banda Euclidiana de proximidade ao perfil real, restam 204
candidatos em ordem original. Isso não comprova que qualquer candidato esteja
na sala correta, dentro do lote, alcançável, livre de ocupação ou escolhido pelo
jogo original. A rotina não calcula score/RNG, direção, facing, rotação do
objeto, bounds, paredes/portas ou estado dinâmico.

Releitura do recurso externo, sem copiar conteúdo comercial para o repositório:
`odh-slot '../referencias/The Sims/GameData/Global/Global.far' Global.iff 100 3 8`
retornou `decoded_fields_only`, SLOT v10, record index8, flags `0xa0ff`, raw
proximity `[16,64,16]`, facing `-3` e resolução `8`.

Após o filtro de proximidade, os offsets ainda incluem pontos que serão
rejeitados depois. Direção, sala, piso, paredes/portas, ocupação, score/RNG,
facing e política de failure trees não fazem parte destas rotinas. Portanto,
nem os 289 offsets nem os 204 candidatos são destinos utilizáveis ou autorizam
movimento. O controller Route45 continua recusando SLOT8 até haver
implementação e evidência para essas regras e clearance do lote.

Proveniência: FreeSO submódulo em `f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.simantics/Engine/VMSlotParser.cs`, filtro de distância na linha
147 e `SLOTEnumerationFunction` nas linhas 177–203. `FSOSquare` está definido
em `TSOClient/tso.files/Formats/IFF/Chunks/SLOT.cs`. Os valores do SLOT8 são registrados em
`entry-points-validation.md`. Essa referência não prova comportamento do
executável original; análise focada do Route45 original ainda está pendente.

`tests/routing.cpp` valida os 289 offsets, os 204 candidatos de distância
Euclidiana, o modo quadrado, ordem, teto de alocação, rejeição de resolução/
faixa/perfil inválidos e falha fechada do controller para o perfil real de
SLOT8. São fixtures sintéticas, não uma execução do jogo.
Build focal `odh-routing-tests`, build completo e os 138 testes CTest passaram
(137 executados com sucesso; `linux_window_backend` foi pulado por ausência de
display). Nenhuma validação de Main, movimento real, janela Linux ou jogo
original foi executada nesta etapa.
