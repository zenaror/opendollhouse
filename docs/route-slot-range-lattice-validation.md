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

## Perfil Global de House07 no Route45 (2026-10-21)

A consulta ao frontend Simitone e ao submódulo FreeSO fixado confirmou as regras
de referência usadas para este registro: enumeração em faixa, máscara
direcional, scoring com RNG e a política `FaceAnywhere`. Com `RandomScoring`,
o score não soma proximidade desejada nem distância do chamador. `FaceAnywhere`
não força a direção final do avatar. Isso descreve o código Simitone/FreeSO,
não o executável original.

O ODH admite agora somente o perfil exato de House07 Global SLOT100 tipo 3
índice 8 (versão 10, flags `0xa0ff`, proximidade `[16,64,16]`, resolução 8,
facing -3 e demais campos verificados acima). O provider gera e ordena os 204
candidatos filtrados com RNG explícito; o controlador revalida a máscara
direcional e exige validador de destino fornecido pelo host. Outros perfis de
faixa continuam rejeitados. A política completa do original não está
estabelecida.

Cobertura: `tests/slot_scoring.cpp` verifica scoring aleatório e candidatos;
`tests/routing.cpp` testa a admissão desse perfil, validador e orientação de
chegada. Os CTests `slot_scoring` e `routing` passaram no build limpo
`build/linux-checkpoint`. A integração
`tests/house07_route45_integration.py` executou a Main sintética na instalação
externa de base: Route45 retornou `true` após 32 movimentos. Com mutação de
obstáculo, retornou `false` antes de entrar na célula ocupada, após atualizar o
snapshot. Isso valida o caminho delimitado no ODH, não gameplay natural nem
equivalência ao executável original.

O probe Portal26 permanece separado da integração sintética. O Route45 chega
até `[408,408,1,0]` em 32 movimentos e `Doors.iff:4105` termina
`returned_true`, sem fault. O probe encerra nesse resultado antes de a Main
despachar outra rota. Isso é uma ação diagnóstica host-scheduled, não interação
natural nem prova de compatibilidade com o executável original.

Proveniência: Simitone em `4906db3156616bfaffc10ce0963cf08062975fef` e seu
submódulo FreeSO em `f86bbd34b2112296ed786741f640a0535886d180`;
`TSOClient/tso.simantics/Engine/VMSlotParser.cs` (filtro de distância,
`SLOTEnumerationFunction`, scoring e `FaceAnywhere`),
`Engine/VMRoutingFrame.cs` (orientação ao chegar) e
`tso.files/Formats/IFF/Chunks/SLOT.cs` (flags). Essa referência não prova
comportamento do executável original.

`tests/routing.cpp` também conserva as fixtures de raio exato do índice 1 e
verifica a faixa Global acima em World/grade sintéticos. O fixture de frame de
portal usa um avatar como Caller e Target, move-o entre salas, atualiza a grade
e retoma a rota suspensa; é um teste do scheduler, não um portal descoberto nos
dados do jogo. A integração Main descrita acima usa os dados externos do lote,
com BHAV sintético.

O resultado anterior de 138 testes CTest e as fixtures históricas permanecem
como checkpoint anterior; a verificação deste perfil está registrada acima.
