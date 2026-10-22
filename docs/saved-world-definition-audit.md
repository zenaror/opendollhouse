# Auditoria das definições da House77

Esta nota registra uma leitura estática da `House77.iff` da Legacy Collection.
O carregador percorre OBJT/OBJM e exige uma definição OBJD única para cada
objeto antes de montar o World. Ele agora informa se a definição está ausente
ou se há várias candidatas, e lista as candidatas quando há duplicidade.

## Objetos encontrados

Uma varredura estrutural das 90 FAR da Legacy Collection leu 1.216 recursos IFF;
outra varredura leu 1.578 arquivos IFF soltos. Ambas terminaram sem erros de
leitura. As buscas pelos GUIDs abaixo foram por chunks OBJD decodificados, não
por coincidência de bytes:

| Objeto salvo | GUID | Definição localizada |
| --- | ---: | --- |
| 649, `*Socials - Pet - Cat` | 3960898255 | `ExpansionShared.far!PetSocialsCat.iff#16807` |
| 211, `Nouveau Nites Party Lights` | 2686633740 | `Downloads/FenceParty/FenceParty.far!FenceParty.iff#16807` |
| 39 | 2144608932 | `Downloads/WallLite/WallLite.far!WallLite.iff#16811` |
| 15 | 355559527 | `Downloads/Plants2/Plants2.far!Plants2.iff#16810` |
| 595 | 1656088637 | `Downloads/Moose/Moose.far!MooseHead.iff#16813` |

Todas as definições encontradas são OBJD versão 138. Com os arquivos-base
Objects, ExpansionShared, ExpansionPack5, ExpansionPack7 e Global, a leitura da
casa para no objeto 211. Adicionando os quatro arquivos de Downloads acima, ela
avança até o objeto 12, GUID 1515759482 (Menelaus), para o qual a varredura não
encontrou OBJD correspondente. Também não foram encontrados OBJD para os GUIDs
1974311860, 104851579 e 292943514, ligados a outros personagens da mesma casa.

## O que isso demonstra

Os bloqueios em 649 e 211 eram definições ausentes do conjunto de recursos
passado ao carregador, não duplicidades. A casa depende de conteúdo adicional
instalado. Após incluí-lo, o próximo bloqueio é uma definição de personagem
que não apareceu como OBJD nos arquivos inspecionados; o caminho correto para
reconstruir personagens ainda precisa ser identificado.

Isto é análise estática. O World completo não foi importado, nenhuma fila de
ação foi ativada, e a busca não prova execução nem fidelidade ao jogo original.
