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
