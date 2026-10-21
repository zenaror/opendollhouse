# SLOT de faixa e enumeração de offsets

`enumerate_reference_slot_range_lattice` reproduz somente a ordem e os limites
da grade de offsets usada pela referência FreeSO/Simitone em
`VMSlotParser.SLOTEnumerationFunction` para SLOTs tipo 3 v10 com proximidade em
faixa. `filter_reference_slot_range_proximity` aplica a banda seguinte,
`min - 0.5` a `max + 0.5`, inclusive. Usa distância Euclidiana, salvo quando
`FSOSquare` está marcado, e preserva a ordem recebida. As comparações usam
inteiros para evitar arredondamento de ponto flutuante nos limites.

O registro Global.iff SLOT100 tipo 3 índice 8 da instalação de House07 tem
proximidade raw `16/64/16` e resolução `8`. Na referência, os campos são lidos
na ordem min/max/optimal; max zero usa min como fallback. Como min e max diferem
aqui, a enumeração percorre `x` externo e `y` interno, ambos de `-64` a `64`, em
passos de `8`: 17×17 = 289 offsets. O limite de alocação padrão é 4096. A
aplicação da banda Euclidiana retém 204 candidatos em ordem original. Nenhum
deles é, por isso, destino validado.

Releitura do recurso externo, sem copiar conteúdo comercial para o repositório:
`odh-slot '../referencias/The Sims/GameData/Global/Global.far' Global.iff 100 3 8`
retornou `decoded_fields_only`, SLOT v10, record index8, flags `0xa0ff`, raw
proximity `[16,64,16]`, facing `-3` e resolução `8`.

Esse índice 8 não é o SLOT selecionado pela ação da cadeira. A cadeia estática
4097→4100→4099 chega a Route45 com `data=1`, que seleciona o tipo 3 índice 1.
Uma releitura desse registro na mesma instalação mostrou flags `0x55`,
proximidade `[16,16,16]` e resolução `16`, um perfil de raio exato aceito pelo
protótipo `Routing` quando o chamador fornece uma `RouteGrid`. O Main host ainda
não conecta esse serviço, e a grade de teste não valida clearance do lote.

O perfil de faixa do índice 8 não é aceito pelo controller. Mesmo após o filtro
de proximidade, direção, sala, piso, bounds, rotação, paredes/portas, ocupação,
score/RNG, facing e política de failure trees ainda ficam por validar.
Portanto, os 289 offsets e os 204 candidatos não autorizam movimento nem
estabelecem semântica do executável original.

Proveniência: FreeSO submódulo em `f86bbd34b2112296ed786741f640a0535886d180`,
`TSOClient/tso.simantics/Engine/VMSlotParser.cs`, filtro de distância na linha
147 e `SLOTEnumerationFunction` nas linhas 177–203. `FSOSquare` está definido
em `TSOClient/tso.files/Formats/IFF/Chunks/SLOT.cs`. Essa referência não prova
comportamento do executável original.

`tests/routing.cpp` valida a grade de 289 offsets do índice 8, os 204 candidatos
Euclidianos, o modo quadrado, ordem, teto de alocação e entradas inválidas. A
fixture de Route45 usa `data=1`, os campos lidos do índice 1 e as coordenadas
salvas de Bob105/cadeira128, sobre uma grade construída. São fixtures
sintéticas, não execução do jogo nem clearance do lote.

Build focal `odh-routing-tests`, build completo e os 138 testes CTest passaram
(137 executados com sucesso; `linux_window_backend` foi pulado por ausência de
display). Nenhuma validação de Main, movimento real, janela Linux ou jogo
original foi executada nesta etapa.
