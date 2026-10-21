# Estado inicial das arestas de parede para raycast

`resolve_initial_wall_edges` transforma `SavedArchitecture` e o resultado de
`derive_wall_appearance_restore_order` em arestas TL/TR row-major que o
`saved_wall_raycast` pode consumir. Segmentos ausentes são não sólidos;
segmentos presentes são sólidos, salvo quando a face correspondente está
marcada como porta. Uma face que contradiz os bits de segmento ou a extensão do
lote retorna `unsupported`. A ausência de informação de porta nunca abre uma
parede.

O perfil é deliberadamente conservador. A saída só representa a aparência
derivada do snapshot inicial fornecido, não mudanças runtime, animação/estado
operacional de portas, portal do cômodo, colisão do objeto, colocação do avatar,
pisos ou movimento. A chamada deve usar a mesma arquitetura que produziu a
aparência. Não usar uma saída vazia ou incompleta de `WallAppearance` como prova
de que o snapshot de entrada continha todas as entidades arquiteturais.

A regra é inspirada no Simitone/FreeSO fixado em
`f86bbd34b2112296ed786741f640a0535886d180`: `VMRoomMap.cs` cria obstáculos para
segmentos TL/TR presentes e sem Door (linhas 159–169); as arestas inferior e
direita consultam as faces superiores vizinhas (linhas 171–180). Este helper só
resolve as arestas superiores armazenadas diretamente. O `saved_wall_raycast`
usa o armazenamento adjacente ao cruzar limites e mantém seu perfil separado.
Isso descreve a referência, não demonstra equivalência com o executável TS1.

`tests/wall_route_state.cpp` valida segmento desconhecido bloqueado, raycast
bloqueado/aberto, persistência da porta após sobreposição de janela, clear
posterior com estilo zero, inconsistência de face/arquitetura, nível inválido e
limite de faces, face duplicada e face fora da extensão. São fixtures sintéticas;
a comparação com os recursos reais de House07 verifica a ingestão e o alinhamento
do snapshot, mas não substitui validação do runtime original.

Para reproduzir o resumo com os arquivos comerciais mantidos fora do repositório:

```sh
build/odh-wall-appearance '../referencias/The Sims/UserData/Houses/House07.iff' --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' --neighborhood '../referencias/The Sims/UserData/Neighborhood.iff' --route-edges
```

O campo opcional `initial_wall_edges` resume segmentos presentes, arestas sólidas,
arestas não sólidas e faces marcadas como portas em cada nível. Os marcadores
`dynamic_portals=false`, `object_collision=false` e
`runtime_equivalence=false` delimitam a evidência: isso valida a combinação dos
decodificadores com o snapshot salvo, não uma rota navegável ou paridade com o
jogo original. A integração no Main/Route45 continua pendente.
