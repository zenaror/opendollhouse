# Publicação transacional de WorldSnapshot

`World::restore_snapshot` continua aceitando apenas um `World` recém-criado.
`World::publish_snapshot` publica uma cópia validada sobre um `World` já
existente: valida identidades, relações, bancos e limites; prepara o mapa novo
inteiro; só então troca o estado e seus contadores. Snapshot inválido retorna
`false`. Se uma alocação falhar, a exceção é propagada e o estado anterior fica
intacto.

A publicação também impede que os contadores de identidade e posição diminuam.
Moradores que continuam no lote precisam manter o mesmo número de identidade;
entidades novas precisam receber números acima de todos os já usados naquele
`World`. Isso evita que um leitor antigo confunda um morador recém-criado com
outro que já saiu.

O endereço do objeto `World` não muda. Portanto callbacks de `VmMemory` já
ligados a ele continuam consultando o estado publicado. Ponteiros para
`Entity` obtidos antes da publicação deixam de valer e precisam ser obtidos de
novo. A operação publica apenas os valores pertencentes ao `World`; não copia
VMs, controladores, filas, relógio, bindings de recursos ou estado de sessão.

Essa operação oferece ao host uma forma futura de adotar um `World` candidato
sem invalidar callbacks presos ao endereço do lote. Ainda não está ligada à
construção real de moradores FAMI: `tools/person.cpp` continua recusando
residentes ausentes ou sem posição. Publicar um snapshot também não prova
`SetPosition`, entrada EP9, chegada, salvamento ou uma partida jogável.

O teste `world_snapshot` confere substituição de estado completo, rejeita
regressão dos contadores e reciclagem de identidade, leitura e
escrita por um `VmMemory` criado antes da publicação, rejeição atômica de dados
inválidos e preservação do World em falhas de alocação. É cobertura sintética
da estrutura em memória, não validação de recursos comerciais.
