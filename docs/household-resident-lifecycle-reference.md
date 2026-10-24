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
herança. O teste `household_startup` liga esse staging sintético a Init/entry8 e
depois à herança, mas ainda não cobre o Main empilhado, os recursos BHAV reais,
`FindLocationFor`, permissões, reagendamento ou persistência. A integração deve
manter o candidato isolado até todas as etapas necessárias passarem e não salvar
residentes novos até haver reconstrução determinística desse estado no próximo
carregamento.

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

## ODH: planejamento tipado de chegada, sem entrada de gameplay

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

Essa infraestrutura não fornece um avaliador comercial, não executa EP9, não
aplica posição, não agenda Main e não publica o candidato. `clear` registra
apenas as evidências fornecidas pelo avaliador. O runtime normal continua
recusando moradores ausentes; save/resume não foi habilitado. Integrar com
segurança ainda exige antecipar a preparação de arquitetura hoje construída
após os controllers, ligar o startup ao World privado e demonstrar os efeitos
de chegada/EP9 no perfil aceito. O baseline de sessão também precisa ser
reconstruído deterministicamente antes de salvar moradores novos.

`household_arrival_preflight` cobre 243 combinações sintéticas dos cinco gates,
ordem de tentativas, caixas postais ausentes/ambíguas e os dois GUIDs, rejeição
de alvo já posicionado, propagação de erro e preservação do World original com
um morador primário salvo e um adicional staged. Isso complementa
`household_startup`; não executa os BHAVs ou recursos comerciais e não prova
chegada, permissões, persistência ou partida jogável.
