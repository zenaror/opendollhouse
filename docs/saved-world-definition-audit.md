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

OBJT identifica os quatro personagens da casa por nome: Menelaus (ID 12),
Hecuba (11), Gunther (578) e Cornelia (577). Os dados salvos apontam para
Neighbor IDs 122–125. Nenhuma das `Neighborhood.iff` encontradas na Legacy
Collection tem esses IDs. No código TS1 do Simitone
(`VMTS1ActivatorNew.cs:479–515`), cada GUID tenta obter um OBJD; se ele falta
para um personagem, o adaptador tenta localizar o vizinho pelo ID e pelo nome,
troca o GUID se ambos combinarem e, se não encontrar, pula essa entidade. Essa
é a recuperação do Simitone, não prova o comportamento do jogo original. A
cópia de bairro inspecionada não fornece essa recuperação para a House77.

## Leitura C++ somente dos personagens

`odh-saved-person-audit` lê prefixos OBJM, escolhe personagens pelo tipo OBJT e
decodifica apenas seus dados pessoais. Se houver um OBJD único, usa a definição
normal; se faltar, usa a regra do Simitone para o campo multitile: nome OBJT
vazio significa multitile, nome não vazio significa objeto simples. A nova
função `read_objm_person_tail_without_definition` recebe essa escolha de forma
explícita e não cria uma definição OBJD. O programa não monta um World nem
executa interações.

Na House77 da Legacy, com Objects, ExpansionShared, ExpansionPack5,
ExpansionPack7, Global e os quatro downloads nomeados acima, o leitor encontrou
4 personagens entre 847 objetos. A fila de Hecuba (ID 11) foi confirmada em
C++: ação UID 4051, caller/alvo 11, TTA 33, prioridade 50, árvore 8236; a ação
ativa está vazia e `ObjectUses` está vazio. Menelaus, Hecuba, Gunther e Cornelia
foram lidos sem OBJD. Isso confirma a interpretação estática dos campos pelo
leitor, mas a escolha do ramo de multitile vem do Simitone; não comprova a
semântica do executável original nem permite continuar essa ação.

O teste sintético `objm_tail` cobre a leitura Person sem passar uma definição
OBJD, ambos os tamanhos de PersonData, filas e recusa de tipo OBJT não Person.

Isto é análise estática. O World completo não foi importado, nenhuma fila de
ação foi ativada, e a busca não prova execução nem fidelidade ao jogo original.

## Rastreio da fila de interação da Hecuba

As cópias de `House77.iff` em `The Sims` e `The Sims Legacy Collection` têm
SHA-256 `1c95d02c85a5b8d32fbaebe979c69b7edf1a12d4b33f8aa1a2590c3456268c9c`.
A fila salva de Hecuba continua indicando UID 4051, alvo 11, TTA 33 e árvore
8236.

A rotina BHAV 8236 existe em `GameData/Global/Global.far!PersonGlobals.iff`
nas duas instalações. O leitor C++ a decodifica como versão `0x8002`, com 11
instruções. O ID 8236 também aparece em outros recursos de personagem e em
`People\Bones.iff`; portanto, o número isolado não identifica o dono da rotina.
Na instrução 2, o opcode é 42 e os operandos apontam ao GUID 570919237, com
posição 6. No mapeamento do Simitone, isso é `CreateObjectInstance` para
`OutOfWorld`; o GUID existe como OBJD do objeto `HDSI_Insult` em
`ExpansionShared.far!HDSI_Insult.iff#16807`. Essa tradução de opcode é uma
referência ao Simitone e não confirma o comportamento do executável original.

A `PersonGlobals.iff` de ambas as instalações tem TTAB#129, versão 9, 154
linhas. O índice 33 aponta para ação 8451/teste 8371; o índice 52 aponta para
ação 8236/teste 8237. Uma busca estrutural nas tabelas dos recursos instalados
não encontrou a ação 8236 no índice 33. Logo, `PersonGlobals.iff!TTAB#129` é
uma candidata útil para estudar, mas não pode ser declarada como a tabela que
a fila de Hecuba usou. O OBJD de Hecuba não foi localizado, então também não sabemos qual TTAB
essa entidade deveria escolher.

A busca por OBJD nos perfis aceitos pelo leitor foi repetida nos arquivos IFF
soltos e embutidos em FAR das duas instalações: 976 IFFs soltos e 1.216 IFFs
embutidos na instalação original; 1.578 soltos e 1.216 embutidos na Legacy
Collection. Os contêineres FAR/IFF foram lidos sem erros; OBJD em formatos
não aceitos não conta como correspondência. Nenhum OBJD com GUID de Hecuba
(1974311860), Menelaus (1515759482), Gunther (292943514) ou Cornelia
(104851579) foi localizado nessa cobertura. A leitura de 18 Neighborhood.iff
também não encontrou os IDs 122–125 nem os quatro nomes. Isso limita, mas não
prova por si só, a ausência de outras formas de definição ou de dados fora
dessas instalações.

No código TS1 do Simitone, `ConvertAction` conserva a árvore e o índice TTA; a
rotina de carregamento usa o GUID do dono para buscar a rotina, e depois usa o
índice para o texto da ação. Esse caminho ajuda a formular o teste, mas não
explica a diferença entre TTA 33 e árvore 8236 e não prova a lógica do jogo
original. Por enquanto, o leitor mantém a fila como dado inerte: não troca o
índice, não escolhe a linha 52 e não executa a ação.

Próximo passo: examinar no executável original como OBJM guarda e resolve o
dono do código, a árvore de ação e o índice TTA, e como personagens sem OBJD
são tratados. Só depois dessa ligação será seguro implementar a restauração da
fila.
