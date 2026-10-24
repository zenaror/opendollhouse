# Criação de moradores FAMI ausentes — referência Simitone

Este registro compara a etapa de moradores ausentes do lote com a revisão local
do Simitone. É leitura estática do código-fonte, não validação contra o executável
original nem prova de partida jogável.

## Caminho efetivo do Simitone

As referências locais estão fixadas em Simitone `4906db3156616bfaffc10ce0963cf08062975fef`
e no submódulo FreeSO `f86bbd34b2112296ed786741f640a0535886d180`.

1. `Client/Simitone/Simitone.Client/UI/Screens/TS1GameScreen.cs:563-575`
   seleciona a família inteira, ativa a família e carrega/recria o lote.
2. `FreeSO/TSOClient/tso.simantics/NetPlay/Model/Commands/VMBlueprintRestoreCmd.cs:34-66`
   usa o save FSOV quando presente; sem ele, carrega o lote IFF com
   `VMTS1ActivatorNew`. Depois roda entry 2 nos objetos carregados e chama
   `VerifyFamily`. O activator antigo, que executa entry 11, está comentado
   nesse caminho e não deve ser tratado como parte do fluxo efetivo.
3. `FreeSO/TSOClient/tso.simantics/Model/TS1Platform/VMTS1LotState.cs:30-54`
   compara os GUIDs de `CurrentFamily.RuntimeSubset` com os avatares presentes.
   Para cada GUID ausente, cria o personagem fora do lote apontado ao norte,
   grava o chunk FAMI em `PersonData[61]`, procura uma das caixas postais
   `EF121974` ou `1D95C9B0`, tenta `FindLocationFor`, atribui permissões de
   proprietário e agenda o Sim.
4. `FreeSO/TSOClient/tso.simantics/VMContext.cs:1461-1489` mostra a criação do
   avatar: adiciona a entidade, chama `group.Init`, posiciona inicialmente no
   destino pedido e herda NBRS pelo GUID. `VMMultitileGroup.cs:397-402` chama
   `Init` em cada objeto do grupo.
5. `FreeSO/TSOClient/tso.simantics/Entities/VMEntity.cs:485-513` executa Init
   (entry 0) e entry 8 imediatamente, depois empilha Main (entry 1) sem executar
   seu corpo. `FreeSO/TSOClient/tso.simantics/Entities/VMAvatar.cs:393-417`
   continua a inicialização após esse método base; só depois o avatar
   volta a `VMContext`, recebe posição e herança NBRS. `VerifyFamily` grava então
   o chunk FAMI e tenta a chegada pela caixa postal. O scheduler executa a
   continuação posteriormente.

Assim, a ordem confirmada para o morador ausente é: construção e Init; entry 8;
Main empilhado; restante da inicialização do avatar; posição inicial no destino
solicitado; herança NBRS; chunk FAMI; tentativa de chegada pela caixa postal;
permissões e reagendamento. Empilhar Main não equivale a executar a rotina.

## Limites conhecidos

Não foi identificada nessa rota uma chamada explícita de chegada além de
`FindLocationFor`; a leitura não prova qual BHAV do jogo comercial participa,
nem se a posição final corresponde ao executável original. A gravação de FAMI e
a tentativa de chegada são etapas separadas. Salvar um FSOV do Simitone tampouco
prova que ele atualize os formatos OBJM/OBJT do jogo original.

No ODH, `stage_household_members` preserva um World candidato sem posição nem
herança. `verify_household_members` processa um morador por vez: prepara, herda
NBRS/FAMI, tenta as posições na ordem e ativa o Main empilhado mesmo se ninguém
couber e o morador continuar fora do lote. A integração Linux em
`tools/person.cpp` agora usa esse fluxo para moradores adultos cujo recurso
exato foi resolvido, inclusive o avatar escolhido pelo jogador quando ainda não
está no OBJM/OBJT da casa. Nesse caso, o launcher conserva o GUID selecionado e
só o resolve depois que o avatar único da família ativa estiver preparado e
posicionado. Um GUID já salvo continua exigindo correspondência exata com o
avatar serializado e com o recurso de personagem selecionado. Init e entrada 8
executam num World privado; Main é empilhado, mas não roda antes da chegada. O
baseline da sessão é reconstruído a partir do World publicado antes de salvar e
retomar a partida.

O perfil falha fechado para crianças, recurso ou relógio indisponível, EP9 de
colocação, colisão/flags não modelados e chamadas que escapem da memória privada.
`World::read(Room)` devolve -5 para entidades sem posição,
como no Simitone. Durante Init/entrada 8, a integração aceita `CanWalk` e somente
a troca isolada de `FSODynamicFootprint` no próprio avatar ainda fora do lote,
atualizando `static_footprint` antes da chegada. Também consulta as tabelas BCON
do código ativo e os valores globais disponíveis do relógio, família, lote e
bootstrap. A chamada genérica TS1 de zoneamento do lote e o gerador aleatório
determinístico usam os leitores já existentes sobre o candidato temporário; se
qualquer etapa posterior falhar, esses sorteios não mudam a partida. Outras
mudanças em ObjectData[8] continuam recusadas porque podem exigir registrar uma
pegada posicionada, recalcular a colisão/AvatarSolid ou alterar a thread; por
exemplo, mudar `Burning` reinicia a thread do avatar no Simitone.

A chegada ainda não é equivalente ao carregamento completo do Simitone: ele
executa EP2 em todos os objetos antes de `VerifyFamily`, enquanto esse caminho
ODH não reproduz essa etapa. O perfil aceito precisa ser ampliado somente quando
esses efeitos estiverem modelados.

Os testes sintéticos cobrem o fluxo de sucesso, a sincronização da pegada dinâmica
e as recusas conhecidas, incluindo a escolha de um adulto ausente do OBJM original
e sua resolução como avatar ativo depois da chegada. A fixture ainda contém outro
morador salvo e uma caixa postal; não prova entrada numa casa totalmente vazia.
O teste visual com os dados comerciais avançou pelo Init real, incluindo a
consulta de zoneamento e o sorteio, mas parou quando a janela tentou localizar
`b010fafit_01` no catálogo BCF. O arquivo original existe como
`GameData/Skins/B010FAFit_01.cmx`; o renderer atual ainda não carrega aparências
CMX/SKN da pasta Skins, então não há evidência de renderização comercial completa.
Uma auditoria somente leitura das casas selecionáveis House05 e House07 na
instalação local confirmou que todos os GUIDs FAMI já têm uma entidade salva;
portanto não há, nessas casas, um caso comercial real de morador ausente para
exercitar. Essa auditoria não valida a criação contra recursos comerciais.

O teste `household_startup` também prepara dois moradores ausentes no mesmo
World candidato e executa Init e entrada 8 sintéticos com RNG próprio para cada
um. Ele confirma que Main não é iniciado, que herança NBRS/FAMI continua sendo
uma etapa separada e que a falha do segundo preparador rejeita o candidato
inteiro. É uma prova isolada do helper; `linux_household_materialization_cli`
também verifica a integração com recursos e fixtures sintéticos, sem usar dados
comerciais.

## Busca da caixa postal e risco das tentativas

`VMFindLocationFor.cs:113-170` define uma ordem determinística: tenta primeiro a
posição exata da caixa postal com quatro direções; depois percorre os anéis
quadrados de raio 1 a 9, mantendo o nível da caixa. São 361 posições distintas
no máximo, com quatro tentativas por posição (1.444 chamadas). O helper ODH
`household_spawn_candidates` espelha a lista completa, inclusive tentativas fora
dos limites do lote. `tests/household_spawn.cpp` verifica a ordem inicial e a
ordem na borda, além da seleção do primeiro candidato aceito. Esse teste cobre
a lista de tentativas, não se uma posição é válida no jogo.

No Simitone, cada tentativa chama `SetPosition`; ela não é uma consulta sem
efeitos. `VMMultitileGroup.ChangePosition` remove registros antes de validar e,
quando falha, tenta recolocar o slot e chama `PositionChange`, que pode disparar
entry 9. `VMEntity.SetPosition` também pode separar
um membro multitile antes da validação. As regras de sobreposição podem executar
entry 5, inclusive BHAVs. Portanto, testar vários candidatos no mesmo World
ativo pode mudar o próprio estado que será testado em seguida.

No ODH, `World::set_position` valida a posição básica e as relações do World,
mas não reproduz piso, sala, bloqueios e EntryPoints do Simitone. As consultas
de RoomInfo e a auditoria de sobreposição disponíveis ainda deixam casos
`unsupported`, especialmente BHAVs de entry 5 e efeitos de entry 9. A chegada
deve continuar bloqueada quando qualquer regra necessária for desconhecida; a
lista geométrica não deve ser tratada como um resolvedor de chegada.

## ODH: planejamento tipado de chegada

`preflight_staged_household_arrival` (`household_arrival.hpp/.cpp`) consulta um
World candidato com avatar ainda sem posição. A consulta procura exatamente uma
caixa postal posicionada e não anexada, pelos GUIDs acima; múltiplas caixas são
recusadas conservadoramente. Ela percorre a ordem de `household_spawn_candidates`
e exige os quatro gates de `AvatarCandidatePreflight` mais um gate separado de
semântica `PositionChange`/entry 9. Todos começam `unsupported`. Um bloqueio
conhecido permite tentar o próximo candidato; um desconhecido sem bloqueio
conhecido interrompe a busca, porque ignorá-lo não provaria a primeira chegada
válida. Tentativas fora dos limites horizontais continuam contando na ordem,
mas são classificadas como bloqueadas antes do avaliador; o Simitone as envia a
`SetPosition`, cujo `PositionValid` rejeita coordenadas x/y fora do lote. Esta
API não recebe quantos andares existem: o gate de piso do avaliador deve rejeitar
um nível acima da casa. O avaliador fornecido pelo host deve ser somente leitura,
inclusive para RNG e controllers.

O helper de preflight isolado continua sem avaliador comercial e `clear` registra
somente as evidências fornecidas pelo avaliador. A integração Linux usa um
avaliador tipado com arquitetura preparada, piso, sala, footprint e bloqueios;
EP9 de colocação é exigido ausente e as regras de colisão desconhecidas continuam
recusadas. Isso não prova equivalência da chegada ao Simitone ou ao jogo original.

`household_arrival_preflight` cobre 243 combinações sintéticas dos cinco gates,
ordem de tentativas, caixas postais ausentes/ambíguas e os dois GUIDs, rejeição
de alvo já posicionado, propagação de erro e preservação do World original com
um morador primário salvo e um adicional staged. Isso complementa
`household_startup`; não executa BHAVs ou recursos comerciais e não prova
equivalência de chegada, permissões ou partida jogável.

## ODH: runtime de casa com moradores já salvos

Em 2026-10-24, `tests/house05_household_budget_cli.py` foi executado contra a
instalação externa `referencias/The Sims`, em modo somente leitura, com 32 ticks
e orçamento de 50.000 instruções. O teste passou: 948 passos, estado `waiting`,
sem falha do host nem esgotamento do orçamento. O morador adicional 16 avançou
de `[440,360,1,0]` para `[449,387,1,2]` em 31 callbacks de movimento; o morador
18 permaneceu em `[440,408,1,3]`. Os hashes de 132 arquivos do jogo foram iguais
antes e depois.

Isso valida o runtime de House05 com seus três moradores já representados no
World salvo. Não exercita criação de um membro FAMI ausente nem equivale ao
Simitone ou ao executável original.
