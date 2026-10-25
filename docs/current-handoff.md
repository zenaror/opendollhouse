# Continuidade atual

## Atualização: captura do menu com raio Simitone (2026-10-25)

O preview direto de `odh-person --saved-world-main --render-window`, em Xvfb
800×600, mostrou House07 e a ação `Have A Snack` ao clicar na geladeira. A
captura local ignorada pelo Git é
`reports/pie-menu-window/house07-fridge-menu-assets-radius60.png`, SHA256
`adfcabfa525f40d2b5d1f08de81eb33cfb0ccf75b322aa25ab6431ca2ceb75de`. Ela
confirma a arte PieBack/PieButt do `UIGraphics.far` e a posição ajustada; o HUD,
texto/destaque e outras partes continuam provisórios. Não é comparação com uma
captura Simitone/original.

Correção do diagnóstico anterior: os primeiros perfis temporários não tinham
idioma salvo e abriram o seletor de idioma. O clique feito depois de esperar
cerca de 78s selecionou uma língua; esse intervalo não foi uma medida do tempo
de carga do lote. As capturas preta e "Carregamento..." eram transições, não
prova de travamento. Uma captura posterior com `--language 1` foi feita após
espera semelhante, mas também não marcou o instante exato em que o lote ficou
pronto. A medição controlada mais recente abaixo substitui essa estimativa.
Com `tools/play_linux.py --mode 1 --house 7 --resident-id 48 --language 1`, o
clique na geladeira abriu `Have A Snack`; selecionar a opção iniciou a ação e,
dez segundos depois, a captura mostrou o morador diante da geladeira aberta.
Capturas locais ignoradas pelo Git:
`launcher-house07-explicit-language.png` (SHA256
`06d9f22ce81c943a77593c69eae7afad9d0e4bb28eb50bac8755ee4f80646f4d`) e
`launcher-house07-snack-click.png` (SHA256
`cf9a93d2e4b90c50f47cfba03b812634c71b05e99838801425b8594d0f9147bf`).
Uma terceira execução manteve a janela aberta por 35s após escolher a opção. O
JSON final registrou interação `returned_true`, BHAV4103, ator105 em `[377,456,1,2]`,
animação `a2o-eatloop-stand` no frame 20.2/31, fila ainda ativa e relógio 7:41.
A captura
`launcher-house07-snack-after-35s.png` (SHA256
`6c0be5dedc30e10c6066b77187ca245e1a47392b8bbbcac704cc9230e00cb92d`) mostra
o Sim comendo. Na captura de 35s, o retorno do BHAV estava confirmado, mas o
fim da animação e da fila ainda não; a atualização seguinte resolve esse ponto
para a ação headless. As execuções da janela saíram após Escape, `xvfb-run`
encerrou a tela e nenhum processo ficou aberto.

## Atualização: ação de lanche até a fila esvaziar (2026-10-25)

Um probe headless separado avançou a House07 por 4096 ticks. O BHAV4103
`Have A Snack` retornou `true`, a fila do ator105 ficou vazia (`queue_size=0`,
`queue_active=false`) e ele já estava na animação de repouso
`a2o-idle-neutral-lcrossed-breathe-1d`. O trace confirmou o ramo de criação
OutOfWorld esperado para `Food`; não restaram objetos temporários no mundo.
O recibo local ignorado pelo Git é
`reports/pie-menu-window/launcher-house07-snack-after-4096-ticks.json`, SHA256
`e9f20a522d09fc3b25f3c32dfa4f78065c7c8bd5c3d3a11823d6d941419aa3a2`.

Limite: o processo atingiu o teto de 4096 ticks (código3) enquanto as VMs
Main estavam `waiting`. Isso prova que a ação do lanche terminou e a fila
esvaziou neste percurso; não prova que toda a simulação autônoma da casa
terminaria nem que o efeito visual/comportamental da refeição corresponde ao
jogo original. Não foi aberta tela virtual neste probe.

## Atualização: tempo de abertura do lote medido (2026-10-25)

Com `xvfb-run`, perfil XDG temporário, idioma 1 e House07/residente48, usei o
título com relógio e necessidades (por exemplo, `OpenDollhouse | 7:01 |
Hunger ...`) como sinal de que o lote já estava jogável. A primeira abertura
com perfil novo levou 8,23s; a repetição no mesmo perfil levou 8,22s. Ambas
fecharam com Escape e código0. O cache de arquivos do Linux já estava aquecido
por execuções anteriores, então esses números não medem um início após
reinicialização ou com cache frio. O perfil temporário foi removido e nenhum
processo Xvfb ou do jogo permaneceu.

Um probe anterior esperou pelo título incorreto `OpenDollhouse Main preview`.
O launcher integrado troca o título para o relógio/necessidades quando o lote
abre; por isso, o probe esperou até ser fechado e pareceu um carregamento de
mais de quatro minutos. A janela já estava no lote. Esse resultado anterior
não é evidência de lentidão nem de travamento.

## Atualização: raio do menu alinhado ao Simitone (2026-10-25)

O layout do menu agora posiciona as oito primeiras opções a 60 pixels do
centro, conforme `UIPieMenu.RenderMenu` da revisão Simitone fixada. O teste
sintético confere topo/direita/base/esquerda e dez opções junto à borda de um
quadro 320×240; todas permanecem dentro da tela. Build de `odh-pie-menu-tests`
e `odh-person` passou. Os três gates focados (`pie_menu_data_model`,
`ui_bitmap_color_key_blit`, `linux_game_entry_cli`) passaram. O CTest completo
passou 244/244. A janela ainda usa texto e destaque provisórios e isso não
prova paridade visual com o Simitone ou com o jogo original.

Próximo: continuar a reconstrução visual do menu/HUD conforme Simitone e dados
locais; fazer uma medição após reinicialização apenas quando isso puder ser
feito sem confundir o tempo de espera por um erro de título. Manter cada
resultado limitado ao que a captura ou o trace demonstra.

## Atualização: BCON privado e retorno da Pizza (2026-10-24)

O avaliador read-only de `TestFunction` agora recebe os BCON privados do objeto
selecionado. Quando o recurso exato está carregado, tabela/chave ausente segue
o padrão Simitone de valor zero; owner não ligado continua unsupported. Os
modos semiglobal/global desse scope seguem fora do perfil. Fixtures cobrem
constante zero/não-zero, chave ausente e owner não ligado.

A fila também carrega os BCON privados do telefone quando a ação usa uma rotina
semiglobal: o código vem de `PhoneGlobals`, mas o Simitone mantém o recurso do
objeto em `ScopeResource`. No probe com House07, a ação Pizza 8312 leu
BCON4098[5]=5 e retornou `true` após454 instruções de trace, sem falha do host.
O código3 indica que o harness chegou ao limite de ticks enquanto Main
aguardava. Isso confirma o retorno da interação telefônica, não a chegada do
NPC nem a entrega da Pizza. O save original não foi alterado; o PPM temporário
foi apagado.

Próximo: conferir o efeito pós-chamada esperado no Simitone/jogo e continuar
outros serviços. Manter leituras sem recurso explicitamente ligado como
unsupported; não inferir entrega a partir do retorno do telefonema.

## Atualização: menu de serviços e primeira rota relativa (2026-10-24)

O harness headless agora aceita passos explícitos para abrir uma categoria,
voltar ou escolher uma ação, usando o mesmo tratamento de clique da janela.
Com os dados originais da House07, o telefone136 abriu `Services...` e voltou
à lista principal. O JSON registrou `complete=true`, dois passos concluídos,
histórico `[[],["Services..."],[]]` e `enqueued=false`. O processo terminou no
limite do harness (código3), com `host_failed=false`; não abriu tela virtual e
removeu o PPM temporário.

O Simitone lê `OldTrapCount` de `GotoRelativePosition`, mas não o usa para
escolher o destino. O ODH agora aceita o valor5 nos perfis relativos usados
pela ação de serviço e continua usando o primeiro SLOT temporário. O teste de
rota passou com esse valor. No primeiro probe integrado de Pizza, o avatar
andou por32 callbacks antes de parar em `unsupported read scope 26 index 261`.
Simitone mapeia esse índice para BCON4098, constante5 do recurso privado do
telefone. A leitura e o retorno posterior da ação estão documentados na
atualização mais recente acima; isso não prova que o serviço foi entregue.

Build de `odh-person` e `odh-routing-tests` passou; o teste `routing` passou.
O CTest completo passou 244/244. A UI atual segue provisória.

## Atualização: testes do telefone e vizinhos tipados (2026-10-24)

O avaliador isolado de `TestFunction` agora lê o saldo da família sem poder
alterá-lo e interpreta `SetToNext` mode7 como busca do próximo vizinho com o
GUID pedido. Fixtures sintéticas verificam saldo disponível/negativo, recusa de
tentativa de escrita, ordem por ID, filtro por GUID e resultado sem vizinho.
Release compilou e o CTest completo passou 244/244.

No probe anterior com dados originais da House07, a câmera de diagnóstico alcançou o
telefone136; TTAB129 foi decodificada. O teste `Services.../Pizza` (BHAV8303)
ficou elegível após a leitura read-only do orçamento. `Call Neighbor` (BHAV8310)
e `Services.../Butler` (BHAV8331) também ficaram elegíveis; outras linhas ainda
param em escopos sem suporte. A seleção automática enfileirou a ação Pizza
(BHAV8312), que primeiro parou em `GotoRelativePosition` e depois passou pela
rota após as correções acima. Isso prova a seleção e o retorno da ação no perfil
ODH, não a entrega do serviço nem paridade geral. O probe usou um PPM temporário
removido ao fim; nenhum Xvfb ou janela virtual foi iniciado. Os arquivos temporários
autorizados para limpeza já estavam ausentes.

Na retomada seguinte, conferir o efeito após o retorno do telefonema. A UI final
ainda deve seguir os recursos e o comportamento do jogo original, com Simitone
como referência; este painel continua provisório.

## Atualização: BHAV global ligado no avaliador de ações (2026-10-24)

`evaluate_saved_test_functions` agora separa o código global explicitamente
fornecido do código privado do objeto/semiglobal, colocando-o no programa
compartilhado da VM. Uma fixture sintética chama o global 321: sem o arquivo
global explicitamente ligado, a chamada permanece sem suporte; com o banco
ligado, retorna elegível após duas instruções. O host só passa `Global.iff`
quando esse recurso foi carregado para a sessão.

Se o teste headless não encontra nenhuma ação elegível, agora emite status
`no_eligible_action` em JSON em vez de encerrar sem diagnóstico. A tentativa de
abrir as ações categorizadas do telefone 136 em House07 não chegou ao menu
porque o objeto estava fora da área clicável daquela captura. Ainda não há
validação do submenu real do telefone.

Build Release e CTest completo: 244/244 passaram; `git diff --check` passou.
Ainda falta um caso categorizado real elegível, abrir/voltar do submenu e
despachar a ação escolhida. O painel atual continua uma ferramenta de
depuração; não é a interface final do jogo.

## Atualização: permissões TS1 e busca de item nos testes das ações (2026-10-25)

O avaliador isolado das ações agora considera as permissões de idade do TTAB:
`TS1NoChild` bloqueia crianças e `TS1NoAdult` bloqueia adultos. O bit
`TS1NoDemoChild` é preservado sem filtro, como na checagem TS1 consultada no
Simitone para o jogo completo. O teste isolado também lê os valores de
ativação TS1 `Global[17]=4`, `Global[20]=255` e `Global[25]=4` definidos pelo
Simitone. `FindToken` (opcode51, modo3) consulta o inventário sem modificá-lo.

O teste sintético cobre ator adulto/criança e inventário ausente, vazio e com
token correspondente, além de exigir a máscara de expansão para a ação mágica.
Build de `odh-saved-interactions-tests` e `odh-person` passou; CTest focal
passou 4/4 e `git diff --check` passou. No probe real, House07 selecionou a
geladeira112 e leu a TTAB128 de `Fridges.iff`: `Have A Snack` ficou elegível;
`Cast.../Enchant` (ação4145/teste4144/flags0x31) passou pelo filtro de idade e
edição, mas retornou falso em `FindToken`, PC2. O NGBH carregado não tem itens,
então a categoria realmente não aparece para o morador atual. O status `waiting`
após16 ticks, com `host_failed=false`, explica o código de saída3 como limite
curto da sessão. O PPM gerado foi transitório dentro de `TemporaryDirectory`
e removido ao fim; não há processo Xvfb nem `odh-person` persistente. Os seis
arquivos temporários aprovados para limpeza já estavam ausentes; nenhum foi
removido nesta retomada.

Próximo: identificar outra ação categorizada real elegível com os dados atuais
ou preparar uma fixture externa temporária com os dois tokens exigidos pela
geladeira, sem editar o save original; validar abrir/voltar do submenu e
despachar a linha TTAB. Não alegar paridade da UI original a partir do menu
provisório.

## Atualização: primeira composição com arte original local (2026-10-24)

A janela interativa agora recebe `UIGraphics.far` do diretório do jogo e lê
`Shared\\Sys\\PieBack.bmp` e `PieButt.bmp` com limites de tamanho. Um blit
portátil aplica magenta como transparente sem tocar na profundidade nem nos
IDs usados para selecionar objetos. O fundo aparece atrás do menu e cada ação
mostra o botão original ao lado do texto. Nenhum pixel comercial foi adicionado
ao código ou ao Git.

Na captura Xvfb `reports/pie-menu-window/house07-fridge-menu-assets.png`, a
House07 abriu, o clique na geladeira mostrou `Have A Snack`, o fundo e o botão
foram desenhados e as áreas magenta deixaram o cenário aparecer. O HUD e as
caixas do texto continuam genéricos; essa imagem é evidência de integração, não
paridade com a interface original. CTest focal passou `pie_menu_data_model`,
`ui_bitmap_color_key_blit` e `linux_game_entry_cli` (o teste CLI usa recursos
sintéticos). O Xvfb foi encerrado pelo `xvfb-run`.

Próximo: comparar a composição com o jogo original em execução e substituir as
caixas de texto/HUD genéricas por componentes e regras da interface original.
Ainda falta uma opção categorizada real na House07 para validar submenus.

## Atualização: janela real e imagens de menu localizadas (2026-10-24)

Em Xvfb, `odh-person --render-window` desenhou a casa House07, o avatar e o HUD
de diagnóstico. Um clique na geladeira exibiu `Have A Snack`; escolher essa
opção moveu o avatar até a geladeira. O título da janela permaneceu normal e
não mostrou falha. Essa captura visual não confirmou o retorno final da ação;
`returned_true` segue sustentado pelo teste headless separado. O comando precisou
receber os `.flr` e `.wll` locais como `--architecture-resource`; sem eles,
BHAV4103 registrou `prepared_architecture_unavailable`. O Xvfb terminou após
cada execução e não há processo virtual persistente. Evidência local ignorada
pelo Git: `reports/pie-menu-window/evidence.json` e duas capturas com hashes.

Uma revisão independente mapeou os recursos que o menu original pode usar.
`SMCtrlMgrRes.h/.RT` liga IDs 28 e 29 a `Shared\Sys\PieButt.bmp` (17×17) e
`PieBack.bmp` (195×195); `Res_CPanel.h/.RT` liga ID800 a
`cpanel\pieFace1.bmp` (201×201). `odh-bitmap` leu os três BMPs em
`UIGraphics.far` sem exportar pixels. SHA256 do FAR:
`109d8234cf5d65a1af82593d85b4e62c62302fde574a67b8b8f66c3462b14239`.
Isso identifica candidatos locais; a função e a composição ainda precisam de
comparação com o jogo original. O Simitone desenha seu fundo/botões
proceduralmente, conforme auditoria da revisão fixa `4906db3` e do submódulo
`f86bbd3`.

Próximo: confirmar os três IDs na interface original, carregar os recursos
locais sem copiá-los para o Git e comparar escala/posição. Ainda falta uma ação
elegível categorizada da House07 para testar abrir e voltar de submenu.

## Atualização: menu radial de ações ligado ao clique (2026-10-24)

O modelo `PieMenuItem` agora também calcula posições radiais e alimenta a tela
do lote. Ao clicar em um objeto, o menu mostra suas ações elegíveis ao redor do
ponto selecionado; categorias abrem submenus, o botão central volta um nível,
Tab troca a opção, Space/Enter escolhe e Backspace volta. Cada ação mantém a
linha TTAB original e o idioma do rótulo. `pie_menu_data_model` testa a árvore,
opções repetidas, nomes ausentes e limites das posições.

O fluxo `--window-input` real da House07 selecionou a cadeira128, executou
`Sit`, terminou `returned_true` e deixou o avatar105 anexado ao slot0 da cadeira,
sem falha do host. O processo termina no limite de 512 ticks enquanto Main
espera outra ação. O teste usa eventos de janela sem abrir display; confirma
clique e ação, mas ainda não é comparação visual em X11 nem prova de paridade.
O teste separado Have A Snack headless também passou seus seis checks na
geladeira112. CTest focal passou 11/11 e `git diff --check` passou.

Este é um primeiro menu de gameplay, ainda simples e sem a arte final do jogo.
HUD, molduras e outras partes da tela do lote continuam de diagnóstico. Próximo:
comparar a tela real com Simitone e o jogo original e tratar objetos com ações
categorizadas, idioma e opções que excedem o espaço visível.

## Atualização: Have A Snack conclui em House07 sem janela (2026-10-24)

`PushInteraction` agora carrega a ação exata do TTAB da Food no `CodeOwner`
instalado e a põe na fila do avatar. `PushHead` mantém a ação ativa na frente,
insere a continuação em seguida e preserva a validade dos snapshots mesmo quando
os UIDs ficam fora de ordem. A fila reconhece o `IdleForInput` de BHAVs chamados
pela ação como parte da mesma interação.

Na execução com os dados House07 da instalação selecionada, a geladeira112
enfileirou `Have A Snack`, o avatar105 foi até (377,456), e a fila terminou com
resultado `returned_true`, sem falha do host. O código Food4175 cria o objeto
auxiliar GUID `0x7F907075` fora do lote e testa seus atributos. Breakpoints no
handler RemoveObjectInstance confirmaram: PC9 remove o clone181 com Caller105 e
StackObject181; PC5 remove o objeto temporário Food180 com Caller105 e
StackObject180. O relatório CLI conserva só o último diagnóstico, por isso
esconde a primeira remoção. O alvo de
remoção espera todas as filas de moradores, avatar principal e segundo avatar
soltarem sua identidade; auto remoção não deixa um ticket apontando para uma VM
destruída. O saldo da família agora muda apenas na cópia em memória do ODH e é
salvo/restaurado em HOST v3/v4; HOST v1/v2 continua legível.

CTest focal passou 10/10 após essas alterações (fila, snapshot, CLI, família,
sessão, criação de objeto). A execução de House07 percorreu 1.024 ticks, terminou
a interação e deixou a Main esperando outra ação; o código de saída 3 indica que
o limite do teste foi alcançado durante essa espera. Esta prova cobre uma interação headless,
não a UI final, renderização durante a ação, nem paridade geral com Simitone.

Nenhuma tela virtual foi iniciada. Os diretórios temporários de HOME/XDG e o
perfil do teste foram removidos. Próximo passo: trocar a lista de depuração
por um menu de ações do lote inspirado na interface original. A tela atual
continua sendo ferramenta de teste; ainda não é a interface da versão jogável.

## Atualização: início síncrono de objetos e EP8 vazio (2026-10-24)

Adicionei `run_object_startup`, que executa as ações EP0 e EP8 pela tabela do
objeto e pelo `CodeOwner` independente do recurso. As duas etapas compartilham
o limite de instruções; IDs até 255 são pulados; a rotina retorna sem iniciar
Main, que permanece para o scheduler. A Food tem EP8=0, então essa etapa é
ignorada, como no Simitone.

O teste sintético confirma Caller=Callee=ID do objeto, StackObject=0, quatro
argumentos zerados, CodeOwner próprio, ordem de execução, EP8 vazio e rejeição
de tabela incompleta. Build dos testes `odh-object-startup-tests` e
`odh-avatar-startup-tests` passou; CTest focal passou 2/2; `git diff --check`
passou. O alvo principal `odh-person` também compilou por completo com essa
biblioteca ligada. Ainda não foi ligado ao carregamento da Food real, à fila
pós-tick, ao retorno do avatar nem ao scheduler Main.

## Atualização: criação controlada no compartimento 0 (2026-10-24)

`create_object_instance_in_slot0` agora combina um pedido opcode42 já decodificado
com recursos de objeto previamente lidos e um ID novo fornecido pelo chamador.
O perfil aceito é InSlot0OfStackObject: exige GUID igual, alvo existente com
slot 0 livre, cria os bancos do objeto, herda a posição do alvo quando ela
existe e anexa Food ao compartimento. Se a colocação falhar, remove a entidade
recém-criada. Os outros tipos de posição continuam explicitamente sem suporte.

O teste usa fixture IFF sintética baseada na definição Food: verifica a
colocação no slot0 e posição herdada, o slot já ocupado e uma posição não
suportada. Build dos alvos `odh-object-runtime-resources-tests` e
`odh-create-object-instance-tests` passou; CTest focal passou 2/2; `git diff
--check` passou. Isso prova o serviço de World com recursos preparados, não a
leitura/execução da Food real nem a chamada pela VM. Ainda faltam a fila segura
após o tick, Init0, retorno de StackObject e integração da ação Have A Snack.

## Atualização: leitura dos pedidos CreateObjectInstance (2026-10-24)

Adicionei um leitor tipado dos oito operandos do opcode42 e nomes para as dez
posições e oito opções descritas pelo Simitone. O teste usa os operandos reais
da ação Have A Snack (`[117,227,1,243,4,1,0,0]`) e confirma GUID
`0xF301E375`, compartimento 0 e `NoDuplicate`. Também verifica os bits das
opções, preserva posições desconhecidas e rejeita outro opcode.

Reconfigurei `build/linux-checkpoint` para reconhecer o novo alvo. Build do
teste passou; CTest `create_object_instance` e `object_runtime_resources`
passou 2/2; `git diff --check` passou. Isso só lê os operandos: ainda não cria
Food, não espera pelo scheduler, nem integra Have A Snack. Próximo: ligar o
pedido ao retorno adiado do scheduler e ao ciclo real de criação e Init0.

## Atualização: Init0 da comida termina com os recursos reais (2026-10-24)

Montei um probe separado para executar Food Init0 com `Objects.far!Food.iff`
OBJD16808, BHAVs privados do mesmo IFF, BHAVs e BCONs globais de
`Global.far!Global.iff`, e um objeto criado por
`materialize_runtime_object`. O `CodeOwner` foi um identificador de recurso
próprio (`0x80000002`), e o contexto Init usou Caller=Callee=Food e
StackObject=0, conforme `VMEntity.ExecuteGenericEntryPoint` no Simitone. A
leitura scope26 consultou o snapshot `BconTuning`; o World recebeu as demais
leituras e escritas.

Init0 terminou `returned_true` em 41 instruções. O trace percorreu Food BHAV
4100 → Food BHAV 4112 → Global BHAV 271; `ObjectData[23]` da Food ficou 2.
O probe tratou apenas Refresh opcode7 do tipo gráfico com o mesmo retorno
verdadeiro já usado pelo `person.cpp`; as demais primitivas continuam
unsupported. Build atual dos alvos `odh-object-runtime-resources-tests` e
`odh-vm-tests` passou; CTest `object_runtime_resources` e `vm` passou 2/2.

Isso prova a execução isolada de Init0 para esta definição e estes recursos em
um World construído pelo ODH. Não cria Food a partir de Have A Snack, não roda
Main1, não agenda uma VM nova no scheduler nem prova inicialização de outros
objetos. Próximo: transformar este fluxo em criação adiada do opcode42, manter
a VM do objeto por ID, e só concluir a espera do avatar após Init0 retornar.

## Atualização: conclusão de espera pode devolver novo StackObject (2026-10-24)

`Vm::complete` agora aceita, além do resultado e da saída opcional de argumento
ou local, uma atualização opcional do `StackObject`. A atualização só é aplicada
quando o ticket da espera corresponde; a VM então segue o desvio original da
instrução sem executá-la outra vez. Saídas inválidas do frame continuam sendo
rejeitadas sem consumir a espera. O teste sintético `services` cobre ticket
antigo e retorno válido de um novo ID.

Build `odh-service-tests` e CTest focal `services` passaram. Isso prepara o
retorno de `CreateObjectInstance` após o scheduler chegar a um ponto seguro,
mas opcode42 ainda não cria objetos e a ação Have A Snack ainda não foi
integrada. Próximo: testar a VM separada de Food com CodeOwner privado e rodar
Init0 isoladamente.

## Atualização: perfil estrutural para construção de objetos (2026-10-24)

O novo `object_construction_profile` deriva o tamanho dos bancos de atributos e
de compartimentos de um objeto comum. Ele conta somente registros SLOT do tipo
0, recusa definições multitile e não tenta completar campos OBJD ausentes.
Limites configuráveis impedem bancos excessivos. O `World` também ganhou
`create_runtime_object`, que publica de uma vez um objeto com seus bancos
zerados, GUID e entry points; falta de orçamento, ID repetido ou outro dado
inválido não deixa entidade parcial. A interface não resolve o dono do código,
não roda BHAVs e não agenda a VM do objeto.

O leitor `read_object_runtime_resources` agora reúne OBJD, OBJf, SLOT, BHAVs
privados e o vínculo GLOB somente do mesmo IFF escolhido. Exige identidade
OBJD consistente, tabelas únicas e limites para número/tamanho de BHAVs; não
busca substitutos em outros arquivos. `materialize_runtime_object` liga esse
pacote à criação atômica do World. O leitor não carrega código global ou
semiglobal, não cria VM e não executa rotinas.

Um probe de leitura nos dados reais de `Objects.far!Food.iff`, OBJD16808,
encontrou GUID `0xF301E375`, SlotID 128, 16 atributos, um compartimento,
31 entry points e 131 BHAVs privados; GLOB está ausente. Os entry points 0 e 1
apontam para 4100 (Init) e 4177 (Main), enquanto o 8 é zero. O probe usou esse
pacote para materializar Food num World de teste e confirmou os bancos; não
executou BHAV nem a ação de comer.

Build `odh-object-runtime-resources-tests`, `odh-object-construction-tests`,
`odh-world-tests` e `odh-person` passou. CTest focal `object_runtime_resources`,
`object_construction_profile`, `world` e `person_cli` passou 4/4. Próximo:
integrar o opcode42 ao scheduler em ponto seguro, criar a VM de Food com o dono
privado vinculado ao objeto, rodar Init0 e os entry points não nulos, atualizar
StackObject e deixar Main1 agendado. Para Food, EP8 é zero e deve ser pulado.

## Atualização: valores TempXL de 32 bits na VM (2026-10-24)

A VM agora mantém os dois valores TempXL assinados de 32 bits que o Simitone
armazena por personagem. O leitor de `TS1 Budget` usa o valor completo quando
essa rotina pede um TempXL como quantia; índice fora de 0–1 ou VM ausente segue
como não suportado. Iniciar outra rotina limpa esses valores, e uma restauração
de thread nativa que não os fornece começa com zero.

O arquivo interno de sessão da VM agora usa a versão 2 para guardar TempXL.
Versão 1 ainda é aceita e restaura os dois valores como zero, para não quebrar
sessões antigas. Testes cobrem valores positivos e negativos no codec, leitura
com valor maior que 16 bits e compatibilidade com arquivo versão 1. Build Linux
de `odh-person` e testes focados de orçamento/snapshot passaram.

Esse avanço não implementa ainda as rotinas que escrevem TempXL nem cria
instâncias de objetos. O próximo bloqueio da ação de geladeira continua sendo
`CreateObjectInstance` (opcode 42), incluindo inicialização e execução das
rotinas do objeto; a persistência de objetos criados também falta.

## Atualização: campos de construção lidos do OBJD (2026-10-24)

O perfil OBJD 136/138 agora expõe `SlotID` quando o prefixo alcança 42 bytes e
`NumAttributes` quando alcança 118 bytes. O parser mantém ambos como opcionais
para definições truncadas e atualiza o limite de bytes realmente interpretado.
Isso fornece entradas para o construtor de entidades, mas ainda não resolve a
tabela SLOT, não aloca objeto nem inicia sua VM. `odh-person` recompilou e os
testes `object_definition_metadata`, `object_definition_index` e
`collision_geometry` passaram (3/3).

## Atualização: ação da geladeira já alcança o objeto (2026-10-24)

O perfil de `GotoRelativePosition` foi ampliado para aceitar a forma exata
`InFrontOf/Facing` com `NoFailureTrees` (flag `0x02`). Flags desconhecidas
continuam recusadas. Dois testes de rota cobrem a aceitação dessa flag e a
recusa da flag `0x04`.

No probe headless com dados reais de House07, objeto112, o Sim caminhou 32
passos de `[392,440]` até `[360,456]`. Depois a ação parou em opcode42,
`CreateObjectInstance`, rotina4121. Os operandos `[117,227,1,243,4,1,0,0]`
pedem criar o GUID `0xF301E375` no slot0 da geladeira, sem duplicatas. O
Simitone confirma essa leitura do opcode e da posição; o ODH ainda não cria o
objeto nem grava a refeição. Nenhum display virtual foi iniciado para o probe.

Build Release `odh-person` e `odh-routing-tests` passaram; CTest focado em
rotas, orçamento e fluxo de interação passou 4/4. A definição já foi localizada;
agora é preciso integrar inicialização e agendamento de objetos, e então definir
como preservar essas novas entidades ao salvar.

### O que a criação exige

Uma busca estática nas definições de `Objects.far`, `ExpansionShared.far` e
`ExpansionPack1..7.far` encontrou uma definição para esse GUID em
`Objects.far!Food.iff`, OBJD16808, tipo4, versão138, sem partes multitile,
`TreeTableID=137` e `UsesFnTable=1`. A tabela OBJf associa Init (entry0) à
rotina4100, Main (entry1) à4177 e entry8 a zero. As rotinas locais 4100 e4177
chamam, respectivamente, 4112 e4178; essa cadeia foi lida estaticamente, não
executada.

No Simitone fixado em `docs/references.md`, `VMCreateObjectInstance` resolve a
definição, constrói o objeto e `VMMultitileGroup.Init` executa os entry points
Init0 e8 e agenda Main1. Depois o opcode coloca o objeto no slot0 do StackObject.
Portanto, apenas alocar uma entidade e anexá-la à geladeira não é suficiente.
O próximo passo é criar um serviço limitado de objetos do World com definição,
slots, execução de Init e agendamento de VM: hoje o scheduler de
`tools/person.cpp` registra os Sims da família, não objetos comuns. Depois,
integrar a persistência de novos objetos ao formato de sessão/salvamento antes
de alegar save completo.

## Atualização: geladeira real revela o próximo limite da VM (2026-10-24)

Um probe controlado do objeto 112 da House07 (GUID `1734088879`) confirmou a
descoberta em `Objects.far:Fridges.iff`, TTAB128, com nove linhas de texto
localizadas. Entre os nomes estão `Have A Snack`, `Have A Meal`, `Serve Meal` e
`Cast.../Enchant`; o último confirma que um rótulo pode carregar a separação de
categoria usada pelo menu circular. No estado residencial deste probe, só
`Have A Snack` ficou elegível. Três testes de ação ainda pedem a rotina
semiglobal 363, quatro linhas de depuração retornaram falso e a ação de magia
parou por leitura global não suportada. As nove linhas não são nove escolhas
válidas para este morador.

O probe de 800×600 selecionou `Have A Snack`. ODH agora implementa o trecho
`JustTest` de opcode25 `VMTS1Budget` e lê o saldo FAMI da família ativa. Os
operandos observados foram `[3,26,0,0,1,0,3,0]`; o teste unitário também cobre
leitura por escopo local, saldo negativo, ausência de família e a operação que
grava. Como ainda não há gravador persistente no host, modos que alteram o saldo
falham de propósito. O novo probe passou pelo opcode25, seguiu BHAV4103→4120→4142
e parou inicialmente em opcode27 `GotoRelativePosition`. Esse limite foi
resolvido no checkpoint acima: o avatar agora chega à geladeira, mas a ação
ainda não conclui porque opcode42 ainda não é suportado.
O detalhe está em
[pie-menu-reference.md](pie-menu-reference.md).

Build Release de `odh-person` e `odh-ts1-budget-tests` passaram. CTest focado:
`ts1_budget`, `vm`, `vm_interaction_return` e `saved_interaction_discovery`
passaram 4/4. A persistência do orçamento segue pendente antes de habilitar
qualquer modo que altere o saldo.

## Atualização: fluxo do menu circular de ações investigado (2026-10-24)

A lista retangular usada nos testes é provisória. A inspeção do Simitone e do
submódulo FreeSO correspondente encontrou o caminho do menu circular: clique no
lote → escolha de objeto/chão → leitura das ações TTAB locais e globais →
checagem das regras e BHAV de teste → rótulo padrão de TTAs ou nome dinâmico de
`ChangeActionString` → categorias separadas por `/` → escolha envia ação,
argumento e origem à VM. A aparência Simitone inclui fundo circular, botões e
cabeça do Sim no centro. Isso é referência, não confirmação de fidelidade visual
ao jogo original.

O leitor ODH já extrai partes estruturais da TTAB, mas a tela House07 provada
mostrou apenas uma ação e não cobre descoberta completa, nomes dinâmicos,
categorias, idioma ou grupos de opções. O novo
[guia do menu circular](pie-menu-reference.md) registra as fontes, limitações e
um primeiro gate de reconstrução. Próximo: seguir a descoberta TTAB/TTAs no
caminho real do ODH, identificar um objeto instalado com múltiplas ações e
categorias e completar a checagem de texto no idioma selecionado antes de trocar
a lista provisória por uma implementação do menu final.

## Atualização: Sit/Stand visíveis e retomada em processo novo (2026-10-24)

`build/linux-checkpoint/odh-person` foi recompilada em Release; a build Debug
`build-household-session` foi preservada. No launcher integrado, a House07
abriu numa janela X11 de 800×600 com a casa, os dois moradores e o HUD. O
relógio de inicialização não foi usado como sinal de prontidão: a interação só
começou quando a imagem da casa estava visível.

O clique `(338,291)` selecionou a cadeira salva 128 (GUID `0x26BFBB29`, TTAB
128), e a lista mostrou `Sit`. O clique da linha enfileirou BHAV 4097 e a
janela confirmou `interaction returned_true`. Um evento `U` enviado diretamente
à janela enfileirou Stand 4102; ambos retornaram `true`. No tick 556, Main
estava `waiting`, `host_failed=false`, a fila vazia, postura 0 e sem attachment.
Capturas mostram o estado sentado e depois em pé.

A tecla S gravou o save experimental no tick 552, com 118130 bytes, fora da
instalação do jogo. Um segundo processo carregou esse arquivo, informou
`session_loaded=true`, retomou do tick 552 e avançou até 590; o Sim permaneceu
em pé, sem attachment, com fila vazia e `host_failed=false`. SHA-256 do save:
`8839a693e8890ef1ee6ea6849a4c9dd6676b4258887363c0e627edcb1059e947`.
Relatórios, save e capturas permanecem fora do Git em `/tmp/odh-x11-release2/`.

Isto confirma um fluxo interativo completo para uma cadeira da House07 e
retomada em outra execução. O painel ainda é provisório, e a evidência não
estabelece paridade visual, outros objetos/idiomas ou compatibilidade geral.
Xvfb e os processos do jogo foram encerrados.
O alvo Release `odh-person` compilou, e os CTests focados
`linux_window_backend`, `saved_interaction_discovery` e `person_cli` passaram
3/3 sob Xvfb.

## Atualização: carregamento X11 e fingerprint dos recursos (2026-10-23)

A primeira retomada gráfica de House07 exibiu o cartão e levou aproximadamente
75 segundos para mostrar a casa. O caminho `--plan-only` isolou 46,134 s na
preparação e fingerprint de 132 recursos (cerca de 1,0 GiB). A build nativa
agora usa SHA-256 do OpenSSL quando disponível, com blocos de leitura de 1 MiB;
no modo sem OpenSSL, permanece o SHA-256 interno. A mesma verificação caiu para
0,719 s e produziu o fingerprint idêntico
`b4842d0534f9962992e4f1d977913c5b689eb7a74c0e5bda3761c66485bdc511`.

A retomada X11 seguinte mostrou o lote cerca de 10 s após o cartão, restaurou o
save no tick 81 e fechou sem erro no tick 90. “Loading lot...” foi centralizado
no cartão. Build com OpenSSL passou os seis testes focados de SHA, manifesto,
texto, entrada integrada, launcher e janela; o build sem OpenSSL passou os dois
testes de hash/manifesto. A build completa e o CTest integral passaram 222/222.
A build continua experimental, sem paridade geral com Simitone.

Próximo: investigar se outras fases da casa ainda podem ser aceleradas, validar
visualmente os demais modos e expansões e ampliar as ações jogáveis com save
seguro. Dados comerciais permanecem externos e inalterados.

## Atualização: orçamento limitado para a família House05 (2026-10-23)

O probe finito continua com o padrão histórico de 10.000 instruções. Para
rodadas headless novas da família, `odh-person --instruction-budget N` agora
permite escolher um teto explícito entre 1 e 1.000.000, sempre junto de uma
quantidade limitada de ticks. A entrada integrada só aceita essa opção para
uma sessão headless nova da família inteira; não altera sessões gráficas nem
loads de saves.

O gate externo `tests/house05_household_budget_cli.py` passou com 4.096 ticks e
50.000 instruções disponíveis. A partida usou 14.439 instruções, terminou no
limite solicitado de ticks, com Main em `waiting`, sem falha do host e sem
esgotar o orçamento de instruções. O morador 16 avançou de `[440,360,1,0]` a
`[472,392,1,5]` em 54 callbacks de movimento; o morador 18 permaneceu na
posição inicial e também ficou `waiting`. Foram conferidos os hashes dos 132
arquivos de recurso do plano; nenhum mudou. Save e configurações ficaram fora
da instalação, em área temporária.

Isso resolve o teto artificial que impedia essa observação de 4.096 ticks e
mostra movimento de um morador real nessa rodada. Ainda é uma execução
headless: não prova interação do jogador, animação visível, atividades de
expansão ou paridade com Simitone.

## Atualização: save/reload de assento na House07 (2026-10-23)

O gate opcional `tests/house07_session_resume_cli.py` passou após ampliar os
hashes para incluir `Global.far` e `Objects.far`. Com os recursos reais, o Sim
do neighbor 48 sentou na cadeira 128, salvou no tick 512 e encerrou o primeiro
processo. O segundo processo carregou o save no tick 512, executou Stand 4102,
terminou no tick 1024 em pé e desanexado. Os hashes de House07, Neighborhood,
User00000, Animation.far, Global.far e Objects.far permaneceram iguais. Saves
e XDG ficaram temporários.

O recibo identificou Sit 4097 e Stand 4102; ambas as ações apontaram para o
mesmo ID de dono do código, `2147483648`. O teste agora verifica esses IDs e a
igualdade do dono nesse caso padrão. A identidade do código é revalidada ao
restaurar, mas o recibo ainda não classifica a função como privada ou
semiglobal. As fases de processo aparecem no stderr do script para que uma
retomada lenta seja identificável.

Isto confirma mais um caso real de cadeira e retomada em outro lote. Continua
headless: não demonstra clique natural, tela visível, portais/expansões nem
paridade geral. A House07 usou 48/128; a House05 segue validada em 34/173. A
limitação anterior de 10.000 instruções e sua repetição com teto de 50.000
estão registradas acima.

## Atualização: objetos fora do lote na busca de assentos (2026-10-23)

O objeto salvo 218 da House05 tem posição `(-16,-16)`, que o Simitone trata
como `OUT_OF_WORLD`. A busca `FindBestObjectForFunction` do Simitone pula esses
objetos antes de conferir EntryPoint26. O preflight do ODH agora também pula
entidades sem posição, em vez de declarar incompleta a lista de assentos. O
teste sintético de SLOT cobre esse caso.

Depois de recompilar, a execução headless da família House05 por 512 ticks com
neighbor 34 e idioma 1 terminou com código 0, `host_failed=false` e sem
diagnóstico. O comando não pediu para gravar sessão; as pastas XDG ficaram
temporárias. Isso confirma que o objeto 218 deixou de bloquear esse percurso
headless de 512 ticks, mas não valida interações naturais ou a família em uso
visual. CTest passou 208/208, com `linux_window_backend` pulado porque não havia
display.

## Atualização: save/reload sentado no House05 (2026-10-23)

O formato SEAT agora grava a ligação exata do objeto ao banco semiglobal:
nome GLOB, arquivo Global FAR relativo à raiz selecionada e entrada FAR. Ao
reabrir, o ODH resolve novamente o GLOB do objeto, exige o mesmo arquivo e
entrada e compara os bancos de código Sit e Stand. Ao salvar esse perfil,
mais de um bloco GLOB no mesmo objeto é recusado; os outros leitores preservam
seu comportamento anterior. Arquivos SEAT v1 privados continuam legíveis; o
formato novo é v2. Limites de caminhos e código alterado são cobertos por
rejeições sintéticas.

O teste reproduzível `tests/house05_seated_session_resume_cli.py` passou com a
instalação original, modo 1, House05, morador neighbor 34 e banco 173. Um
processo concluiu Sit 8213, salvou no tick 512 e terminou sem falha. Um segundo
processo leu esse arquivo, confirmou o mesmo tick, executou Stand 8216, levantou
o morador e o desanexou do banco. House05.iff, Neighborhood.iff,
User00009.iff, Animation.far, Global.far e Objects.far mantiveram seus hashes.
O save ficou em XDG temporário e os dados comerciais permaneceram somente
leitura.

Esse marco comprova apenas esse percurso de assento e retomada em modo
headless. Ainda faltam save/load visual, outros assentos, interação por clique,
família completa (incluindo o problema já registrado do objeto 218), expansões
e paridade com Simitone. A build jogável completa continua sendo o critério.

## Critério de build jogável

Para este projeto, “build jogável” significa alcançar pelo menos a paridade
funcional com o Simitone TS1. O Simitone é o mapa principal de recursos e
comportamentos já conhecidos; o código original e os dados do jogo servem para
completar lacunas quando a implementação do Simitone não responder à dúvida.
Uma janela aberta ou um percurso isolado não satisfaz esse critério. A
validação Linux deve avançar por uma matriz de recursos comparada ao Simitone,
incluindo vizinhança e lotes das expansões, famílias e seus fluxos, seleção de
idioma e registro de falhas. Paridade ainda precisa ser verificada em uso; uma
build compilada ou testes unitários aprovados não bastam.

## Atualização: tabela de ações herdada do banco no House05 (2026-10-23)

O mapeamento por GUID corrigiu a escolha do lote: no House05 original, o objeto
salvo 173 é a peça esquerda do banco de jardim (GUID 3586796916,
`Sofas.iff#16848`) e o objeto 174 é a peça direita. As duas apontam para o
mesmo mestre GUID 2091978777; o grupo salvo é `[173,174]`, liderado por 173.
Esse mestre existe como metadado OBJD `Sofas.iff#16847`, mas não como uma
terceira peça salva. O TTAB compartilhado é o ID 131 do mestre; a primeira
linha é ação 8213/teste 8214. O ID 173 do House07 continua sendo outro objeto.

O OpenDollhouse agora procura o TTAB do mestre apenas quando a peça usa o
marcador `TreeTableID=65535`, e só através do mestre único no mesmo arquivo e
asset. O teste sintético confirmou que essa herança encontra a tabela 131 e
recusa usar um mestre de outro recurso; `odh-saved-interactions-tests` passou
46 verificações.

O teste 8214 do banco chama BHAV semiglobal 8260, ligado pelo GLOB do recurso
do objeto, e usa SetToNext para percorrer as peças do banco. O avaliador agora
carrega somente o semiglobal explicitamente ligado, usa a busca limitada às
peças salvas do mesmo grupo e lê atributos ausentes como zero, conforme o
comportamento observado no Simitone. A tabela GLOB é lida de seu conteúdo, e
não do rótulo do bloco IFF. A ação 8213 também é semiglobal; só é aceita quando
aparece no banco ligado. RunTreeByName agora chama rotinas privadas ou
semiglobais identificadas pelo rótulo, no perfil de subrotina da mesma thread.

No teste integrado de 512 ticks, instalação original, House05, modo 1,
residente neighbor ID 34, sessão de um morador e alvo 173, a ação 8213 foi
enfileirada e retornou `true`; o morador terminou anexado ao banco 173, slot 0,
com a animação `a2o-idle-sit-3`. A VM terminou sem falha. Isso demonstra o
primeiro sentar nesse lote real pelo ODH, mas não confirma levantar, salvar e
retomar o assento, interação natural pelo clique, execução de toda expansão ou
paridade geral com Simitone. O teste sintético de descoberta agora passa 49
verificações; também passaram os testes focados de fila, SLOT e VM. Próximo
passo: exercitar levantar e persistência do assento na mesma sessão e depois
repetir em mais lotes/casas. A paridade funcional com Simitone continua sendo
o critério da primeira build jogável.

## Atualização: limite do Stand e do save sentado no House05 (2026-10-23)

O alvo `build/linux-checkpoint/odh-person` estava mais antigo que
`tools/person.cpp`; depois de recompilar o alvo, a execução headless integrada
com a instalação `../referencias/The Sims`, modo 1, House05, morador neighbor
ID 34 e objeto 173 voltou a confirmar a ação 8213: retorno `true`, avatar
runtime 21 anexado ao slot 0 e animação `a2o-idle-sit-3` após 512 ticks.

`--save-session-on-exit` ainda recusa esse estado com “missing its exact
completed Sit/Stand object binding”, porque a continuação serializada atual
exige a ação privada 4097. Uma execução direta Sit→Stand na mesma sessão
primeiro foi recusada; o dispatcher foi então atualizado para carregar a
função semiglobal somente pelo GLOB exato do objeto. O teste real no House05
agora enfileirou 8213 e 8216, ambas retornaram `true`, e o morador terminou
em pé, desanexado do banco. O Snap de saída usa o SLOT tipo 3, índice 1,
versão 10, opção Shoo e o perfil bruto exato do banco; o suporte ficou limitado
a esse formato, com teste sintético de destino livre e bloqueado.

O ensaio integrado pelo launcher foi repetido com o alvo recompilado, sem
display: `--single-resident-session --headless-sit-object 173
--headless-stand-tick 300 --headless-ticks 512`. Terminou com código 0 e
`status=waiting`. Sit 8213 e Stand 8216 retornaram `true`; o Stand encontrou
postura sentada e vínculo com 173, e terminou em postura 0, desanexado, com a
fila vazia e sem falha do host. O limite geral de 512 ticks foi alcançado; as
ações solicitadas, porém, foram concluídas (`action_incomplete=false`). Os
hashes de House05, Neighborhood, User00009 e Animation.far permaneceram iguais.
Uma leitura estática separada do `Sofas.iff#16848` confirma EntryPoint26 8215
e EntryPoint27 8216; ambos são IDs semiglobais e o GLOB do objeto os liga ao
banco SofaGlobals.iff. Naquele checkpoint ainda faltava serializar e restaurar
a origem exata. A lacuna foi fechada no checkpoint descrito no início deste
documento.

Uma tentativa separada de executar Sit→Stand com o runtime de toda a família
parou antes da interação: o Main do morador 16 falhou ao encontrar o objeto
salvo 218 sem posição conhecida. Isso permanece uma lacuna independente da
continuação de um único morador.

Naquele checkpoint ainda não havia evidência de save/resume do assento House05,
clique natural ou Stand visual em janela. `odh-snap-tests` passou 350 verificações sintéticas e
`linux_game_entry_cli.py` passou 49 verificações sintéticas após essas mudanças.

## Atualização: condições simples da EntryPoint26 (2026-10-23)

O avaliador isolado de condições da EntryPoint26 agora aceita apenas BHAVs
compostos inteiramente por expressões (opcode 2), com orçamento de 256 passos e
sem permissão para escrever na memória do World. Retorna verdadeiro/falso para
comparações e recusa rotinas que chamem serviços, tenham metadados ausentes,
excedam o orçamento ou saiam da própria rotina. O caminho de roteamento usa o
resultado antes de despachar a ação da cadeira; quando a rotina de condição não
existe no banco vinculado, mantém a regra observada no Simitone de permitir a
entrada. Duas condições reais não nulas foram encontradas nos assentos de
diretor do Superstar (4116 e 4127), mas a integração delas com uma partida ainda
não foi demonstrada.

`odh-person` compilou e o teste `saved_entry_point_resource` passou, cobrindo
comparação verdadeira/falsa, recusa de chamada a serviço e bloqueio/liberação
do plano de ação conforme o resultado. O CTest completo passou 208/208; o teste
de janela Linux foi pulado por falta de display. O teste integrado House07 foi
reexecutado com dados da Legacy Collection em 32 ticks para cada seleção de
morador: ambos os residentes executaram Main no mesmo World, e save/resume em
processo separado gerou o mesmo arquivo que a execução contínua; arquivos
originais incluídos no fingerprint permaneceram iguais. Isso é um gate headless
curto, não prova de gameplay completo nem validação visual. A tentativa anterior
de 214 ticks ficou sem relatório verificável e não conta como aprovada.
Próximos passos: completar serviços necessários à ação real BHAV8215, observar
os assentos Superstar com os dados da expansão e repetir House05→House07→House05.
A prova atual do avaliador de condição é sintética e não altera o estado do
jogo.

## Atualização: rastreio direto da ação sentar (2026-10-23)

Com `Objects.far:Sofas.iff:8215`, `SofaGlobals.iff` e `Global.iff` da Legacy
Collection carregados explicitamente, `odh-bhav --world-probe --route-probe
--route-slot-probe --object-def 16848 --stack-object-word 8 0` percorreu o
`GotoRelativePosition` da ação e moveu o avatar sintético até `[64,48,1]`, na
frente do assento sintético. A próxima instrução é `Snap` (opcode46), que esse
perfil do probe ainda não instalou. A inspeção de `VMRoutingFrame` confirma que
Simitone chama EntryPoint26 no assento escolhido e interpreta o retorno antes
de continuar Route45. O runtime ODH agora permite que Snap e animação carreguem
recursos de uma peça multipartes apenas quando o ID pertence a um grupo salvo
validado e o Master GUID coincide. Um teste sintético cobre essa comprovação;
falta, porém, conservar a cadeira escolhida na viagem e despachar sua EP26 para
chegar até esse Snap com os dados reais.

Isso é um trace do BHAV comercial em um World construído pelo ODH; o percurso,
posição, atributos e entidade do assento são sintéticos. Não demonstra sentar
nem uma interação na House07 real. Próxima frente: conservar a identidade do
assento selecionado no Route45 e despachar a EntryPoint26 no dono correto. A
seleção por Groups/footprint, avaliação das condições e os serviços restantes
do BHAV continuam necessários antes da viagem real.

## Atualização: aproximação frontal do assento (2026-10-23)

O `GotoRelativePosition` exato usado por BHAV8215 (operandos
`00 00 00 FE 00 00 00 00`) agora cria o SLOT temporário que o Simitone cria
para `InFrontOf/Facing`: standing 1, faixa 16–24, máscara norte relativa ao
objeto, resolução 16 e os valores padrão `MaxSize=100`/`Gradient=0`. O roteador
filtra por sala antes da pontuação e verifica o bloqueio da grade depois dela,
na mesma ordem observada no Simitone. Em seguida busca o caminho até o ponto
frontal e vira o Sim para o banco. O teste cobre um VM e um banco sintéticos.
O Simitone confirma os defaults em
`Primitives/VMGotoRelativePosition.cs` e `Formats/IFF/Chunks/SLOT.cs`; a
enumeração e as verificações por destino estão em `Engine/VMSlotParser.cs`.

Build Debug e CTest completos passaram em 208/208 após a alteração; o teste
`linux_window_backend` foi pulado por falta de display. Essa prova valida só o
SLOT transitório de aproximação. O host exige que o objeto e cada destino estejam
na sala coberta pela grade única; `IgnoreRooms` não abre uma sala que a grade não
representa. A verificação da grade ocorre depois do consumo de RNG, como o estágio
`PositionValid` observado no Simitone. Ainda falta o raycast candidato→objeto, a
seleção do assento no SLOT100 e a execução da ação EP26 pelo dono correto.
House05→House07 real segue sem chegada. Não tratar o teste sintético como sentar
funcional.

## Atualização: assento da interação “do shy child” (2026-10-23)

### Conferência da identidade no lote salvo (2026-10-23)

Uma rechecagem do `House07.iff` atual nas instalações original e Legacy
Collection encontrou uma divergência importante: o ID salvo 173 resolve para
GUID `4088047151`, `comeandsee.iff`, TTAB 129, cuja única linha é ação 4102,
teste 4111, flags 1024. Portanto, esse ID não é o banco `Sofas.iff`/OBJD16848
no `House07.iff` que os testes atuais abrem. Tentar `--headless-sit-object 173`
nesses dados não enfileira uma ação: a checagem retorna `unsupported` porque
nenhuma linha TTAB passa pelo TestFunction ODH atualmente suportado. Não houve
execução de BHAV8215 nesse teste. A associação anterior entre “objeto 173” e
OBJD16848 pode pertencer a outro contexto de execução, mas não está confirmada
para este lote salvo; usar GUID, recurso e lote junto com o ID em traces futuros.

O teste real de viagem House05→House07 ainda para no BHAV8534, quando o
morador 16 procura um adulto usando o Global SLOT100 v10, tipo 3, índice 10.
Esse registro permite ficar em pé (peso 1) ou sentar (peso 10), no raio
32–80. Uma análise estática separada encontrou o OBJD16848 de `Sofas.iff`, GUID
3586796916, rotulado “Bench - Garden 1 - Left”. Seu OBJf16848 tem EntryPoint26
com condição zero e ação 8215. Ainda não foi demonstrado que essa peça seja o
objeto que bloqueia a rota no lote/estado atual. Na `SofaGlobals.iff` da Legacy Collection, a
ação 8215 está rotulada “sit function”; sua árvore chama “Scoot TEST” (8260),
“Scoot” (8259) e o BHAV global 363 (“Am I a Child?”). Isso confirma por dados
que o obstáculo é um assento e que não é correto ignorá-lo para forçar a
alternativa em pé. Um probe direto de 8215 sem serviços de World parou na
primeira leitura `scope 1, index 0`; isso localiza uma lacuna de serviços, mas
não comprova a ação do banco. O leitor GLOB agora reconhece o formato legado
Pascal ou C-string, seguindo a regra do FreeSO, e `odh-bhav` carrega a ligação
real `Sofas.iff`→`SofaGlobals.iff` para análise estática.

O Simitone confirma a regra de seleção: `VMSlotParser` trata um objeto sólido
como assento quando Sitting é positivo e EntryPoint26 tem ação; Route45 chama
EntryPoint26 e só conclui o caminho conforme o retorno dessa ação. Nosso
preflight agora bloqueia qualquer objeto do mesmo piso com uma ação EP26 ativa
e recusa quando posição ou tabela de ações está incompleta. Essa regra é
deliberadamente conservadora: ainda não compara a área ocupada de cada móvel
com os destinos individuais como faz o Simitone, e portanto não executa o
assento nem prova que um destino em pé está livre. Os testes focados e o CTest
completo passaram (208/208; `linux_window_backend` pulado por falta de
display), mas o teste real continua sem movimento, chegada ou retorno. O
próximo marco é implementar candidatos com geometria do móvel e executar a
ação do assento com seu dono de código exato, preservando Sitting=10, e repetir
a viagem com dados reais.

Referências locais: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`,
`FreeSO/TSOClient/tso.simantics/Engine/VMSlotParser.cs` e
`Engine/VMRoutingFrame.cs`; FreeSO `4c6b3e8f5835b228723caea3c9f683c62f244f73`.
Os arquivos comerciais foram somente lidos. As alterações de implementação
e este registro ainda estão sem commit.

## Atualização: Go Here atravessa escadas salvas House02 e House03 (2026-10-22)

O teste `tests/house03_cross_floor_go_here.py` agora acompanha um Sim sintético
que começa no piso de baixo, sobe pela escada salva da House03 e continua
andando até o piso de cima. Ele cruzou uma passagem da escada e terminou em
`[328,344,2]`, depois de 24 movimentos. Essa liberação vale apenas para esse
pedido Go Here e para escadas cujos dois patamares foram verificados; os outros
tipos de caminho continuam sem poder trocar de andar. Os arquivos comerciais foram
lidos, não alterados. O teste usa uma Main sintética e não comprova o
comportamento do jogo original.

A build Debug em `/tmp/odh-linux-fresh` terminou; o CTest completo teve 165
testes aprovados, nenhum erro e um teste da janela pulado porque o programa não
conseguiu acessar a tela X11. Portanto, a janela e o clique entre andares ainda
precisam ser testados numa sessão gráfica acessível. Uma busca pelas casas
salvas encontrou House02 e House03 com objetos no segundo piso; House07 só tem
objetos no primeiro. A House02 tem um grupo salvo de escada com nove peças,
master `0xbdcf6469`. As peças 102 (`0x92f1647e`, subíndice 5, piso 1) e 106
(`0x8c2c13c8`, subíndice 0, piso 2) têm EP15 apontando para BHAV 4105; a peça
107 (`0xb1074912`) tem EP15 zero. A leitura confirma os dados da tabela, sem
executar o BHAV. O programa agora encontra os dois patamares da House02 pelos
dados do grupo salvo. O teste da House02 também passou: o avatar sintético
começou em `[376,456,1]`, executou o Snap da escada e chegou a `[408,392,2]`.
Foram registradas duas passagens entre cômodos e 144 movimentos; a lista de
posições foi truncada pelo limite do registro. O ponto inicial anterior,
`[392,456]`, estava dentro do retângulo da peça 99 da escada, então foi
corretamente recusado como bloqueado.

O host agora reserva até 131.072 palavras para carregar a House02: o World usa
56.315 palavras e as tabelas EP somam 25.100, total de 81.415. O limite é
explícito e mantém a memória controlada. A validação não prova fidelidade ao
jogo original nem uso da interface gráfica.

O marco House03 foi publicado no commit `80c0fb3` em `main`. Este avanço House02
está em desenvolvimento e precisa de novo commit após revisão. Preservar
`Testing/` e os auxiliares locais não rastreados; não reutilizar diretórios de
build antigos copiados.

## Atualização: teste de móvel no ponto de chegada da escada (2026-10-22)

Foi acrescentado `tests/snap_arrival_blockers.cpp`. Ele liga o caminho real de
montagem de obstáculos por móveis, o mapa de caminhada e o `Snap` da escada:
com um móvel sintético cobrindo a chegada, o Snap retorna falso e o Sim
permanece no piso e na posição de origem; sem o móvel, a troca de piso termina.
O teste usa coordenadas e patamar sintéticos, não altera House03 ou outros
dados do jogo e não prova comportamento do executável original. A House03
continua passando nas quatro combinações já cobertas (subida/descida, idades
fixture 0/21). `cmake --build /tmp/odh-stair-build -j2` terminou e 160 testes
do CTest passaram; `linux_window_backend` foi pulado por não haver display.

O teste confirma a regra local para um móvel; ainda falta uma prova integrada
com móvel salvo no cômodo de chegada. O roteiro House03 agora também põe um
segundo avatar sintético no patamar de cima: a Main chega à escada, a VM retorna
`vm_fault` com o diagnóstico explícito de que “Shoo” ainda não foi implementado,
e o primeiro Sim permanece no patamar baixo, sem trocar de andar. As quatro
travessias sem bloqueador continuam passando. O teste usa a House03 como base,
mas os dois Sims e a Main são fixtures sintéticas. Isso verifica a recusa local,
não define o comportamento correto do jogo original. O Simitone TS1 fixado
mostra que, após uma chegada ocupada, o `Snap` cria `GOTO` (GUID `0x7C4`), agenda
a interação3 no avatar bloqueador e retorna falso. O BHAV4113 da House03
continua em PCs23/13 e Snaps sem Shoo nos PCs15/17 após alguns falsos; o teste
atual para no primeiro pedido e não cobre essa continuação. Os dados TS1
instalados confirmam `Destination.iff`, GUID `0x7C4`, interação3
ligada a BHAV4107 (“Move out of way”) e as rotinas4112/4114 para “Shooed Out” e
“do I need to shoo?”. Próximo: mapear as condições dessas rotinas e os ramos do
BHAV4113; depois conferir no executável original como a fila de interação é
processada. A referência Simitone e a leitura estrutural dos recursos não
provam execução fiel no original.

## Atualização: subida e descida compostas com o lote (2026-10-22)

O `--capture-tick` ainda acompanha o grande deslocamento já gravado em
`a2o-stair-climb`, sem alterar a posição do Sim no World. Agora a câmera usa o
ponto gráfico da escada real. A vista escolhe o piso de apoio para que o piso de
cima não cubra o Sim. A captura `/tmp/house03-up-walls-sequence-tick14.ppm`
mostra o Sim na escada junto do piso, das paredes e dos móveis visíveis; inclui
22 camadas de sprites e preserva as demais entidades do lote, pulando apenas
sprites que ficam inteiramente fora da tela. O hash é
`c869e77bcb81ddbd75421f8eb40e3b7fd5120261ffac7b14cfa6107843de106e`.
O teste conta pixels do Sim antes e depois da composição; exige pixels também
no mapa final devolvido pelo lote. Na subida do tick14, 4044 pixels do Sim
continuaram identificáveis após a composição. A instalação fornece
`GameData/walls.iff`, então as capturas agora incluem as paredes: a imagem
mostra paredes, janelas, escada, piso e objetos. Isso ainda não estabelece
equivalência visual com o original.

O probe capturou a subida e a descida nos ticks14,16 e20. Cada sequência gerou
imagens diferentes e o Sim permaneceu visível após compor paredes e objetos.
Para a descida, a vista mostra o piso1 para deixar visível a escada; o Sim
continua no andar2 do World. Isso é só uma escolha da câmera, não uma mudança
da simulação. As seis imagens e seus hashes estão em `docs/snap-validation.md`.
Próximo: validar outros objetos/chegadas na escada e revisar o comportamento de
paredes e colisões antes de generalizar o perfil de travessia.

## Escadas da House03: localizar a porta entre andares (2026-10-22)

### Atualização: caminhada até os dois patamares (2026-10-22)

O teste agora deixa a Main sintética esperando e pede a passagem pelo EP15 nos
dois patamares conhecidos. A forma exata de `GotoRelativePosition` da BHAV 4112
só é aceita quando o host confirma a chamada, o Sim, o dono da rotina e o
patamar House03 esperado. A regra geral continua proibindo rotas entre pisos.
Quando a caminhada não encontra caminho, o ODH informa que esse caso ainda não
é suportado; não inventa uma resposta de sucesso ou falha do jogo.

O roteiro `tests/house03_stair_approach.py` usa a House03 e os dados instalados
como leitura, mas cria uma cópia temporária do avatar com uma Main mínima que
fica esperando. No patamar baixo, o Sim andou de `[320,424,1,0]` para
`[312,424,1,0]`; no alto, de `[320,344,2,0]` para `[312,344,2,4]`. Foram oito
passos registrados em cada caso, sem mudança de piso. Em seguida, a VM parou na
próxima ação comercial, `Snap` da BHAV 4113 (PC10 na chegada pelo objeto 34;
PC3 na chegada pelo 38), porque o recurso é uma peça de grupo e a validação
atual ainda não conhece esse formato.

O contexto vivo da BHAV 4113 agora está conferido no JSON final da VM e é
verificado pelo roteiro: ao chegar pelo objeto 34, a instrução `Snap` usa o
objeto 38 como `StackObject`; chegando pelo 38, usa o 34. Os dois são os
patamares do outro piso no save. Isso confirma que a rotina escolhe a ponta
oposta em cada sentido; não confirma que o `Snap` conseguiu posicionar o Sim.

No Simitone fixado, SLOT128 tem `IgnoreRooms` (`0x0800`), offset zero e
`SnapTargetSlot=-1`. Como os bits baixos são zero, o parser verifica somente o
ponto exato do objeto; a proximidade 16 não vira uma busca por posições ao
redor. Depois, ele valida colisões e tenta `SetPosition`. Mesmo quando o destino
já coincide com o Sim, `SetPosition` e seus efeitos ainda são executados. A
implementação ODH precisa validar o grupo da escada, o piso de destino e a
ocupação antes de aplicar esse movimento. Próximo: criar esse perfil estreito
para os dois patamares de House03, mantendo `Snap` genérico e a rota comum
entre pisos fechados; então observar a execução além do `Snap` e cobrir
rejeições por destino ocupado.

Comando: `python3 tests/house03_stair_approach.py --binary /tmp/odh-stair-build/odh-person --game-root '../referencias/The Sims'`.

Corrigindo a leitura anterior: `tree_table_id=65535` quer dizer que a peça
não tem uma tabela de interações; não quer dizer que a descrição do objeto
esteja faltando. As oito peças da escada foram ligadas a `stair.iff` dentro de
`GameData/Objects/Objects.far`.

Na House03, todas as oito peças apontam para a ação 4098 da entrada de objeto.
Só os dois patamares — GUIDs `0x821EB6EE` (objeto 38) e `0x79E2B825`
(objeto 34) — apontam para a ação 4105 da entrada 15. No código do motor
FreeSO incluído na árvore do Simitone, a entrada 15 é a passagem usada pelo
planejador para trocar de cômodo e de andar. Os patamares, portanto, são os
primeiros candidatos reais para essa passagem.

As rotinas foram lidas sem executá-las. A ação 4098 chama a rotina global 280
(“idle”) e a privada 4118 (“walls changed”). A ação 4105 (“stair portal
function”) chama as globais 278 e 279, as privadas 4109 e 4110 e a semiglobal
8197. Os rótulos nos recursos chamam essas rotinas, respectivamente,
“standard multi-tile entry”, “standard multi-tile exit”, “portal core”,
“portal wait” e “fail portal call”. A 4109 também chama 4112, 4113 e 4114,
cujos rótulos descrevem a aproximação e a passagem pela escada. A rotina 8197
só prepara o retorno falso da chamada. Isso esclarece quem participa da
passagem, mas ainda não revela qual passo muda a posição do Sim.

Uma diferença importante: o recurso da escada usa um chunk GLOB de ID 1,
chamado `DoorGlobals`; a porta comum `Doors.iff` usa o chunk GLOB de ID 128
com o mesmo nome. O Simitone escolhe o primeiro GLOB do recurso. O ODH aceita o
vínculo quando há um único GLOB e recusa recursos com vários, para não escolher
no escuro.

O código de referência do Simitone contém uma regra especial para comparar o
campo `MyObject.Room` com o literal `1024`: ela usa o andar do Sim, o andar da
escada e o `LevelOffset` da escada para decidir de que lado o Sim está. Essa
pista combina com a cadeia acima, mas não prova que o ODH já mova alguém entre
andares. A leitura estática no executável original com Ghidra também não
revelou um despacho de EP15 ou uma alteração de andar confiavelmente
identificáveis; não foi feita autoanálise nem execução do executável.

O teste Go Here atual continua recusando destinos em outro andar antes de
iniciar a caminhada. Ainda não houve subida ou descida de um Sim no ODH. O
probe `--headless-portal` agora registra posições antes/depois da chamada. Em
House07, Door26 moveu o Sim de `[392,440,1,3]` para `[408,408,1,0]`, sem mudar
de piso; isso confirma o registro para uma porta comum, não para escadas.

House03 não contém avatares salvos. `odh-person` agora aceita
`--headless-fixture-avatar ID X Y LEVEL DIRECTION` junto de
`--headless-main --headless-portal`, criando um Sim descartável no World já
restaurado. O ID precisa estar livre; por isso 105 foi recusado e 30000 foi
usado no teste. O save e os dados comerciais permanecem somente leitura.

Ainda não há resultado de travessia da escada. Com uma cópia temporária de
User00000 cujo Main retorna logo, a simulação termina antes do tick agendado e
EP15 fica `not_requested`. Com o Main original de User00000, EP15 é agendada
para o patamar 34, mas a VM para em `unsupported primitive 31`; o Sim continua
em `[312,424,1,0]`. Isso identifica a rotina Main de teste como o próximo
obstáculo, não prova falha nem sucesso da escada. O próximo passo é fornecer um
Main de teste que permaneça ativo sem executar essa primitiva e então repetir a
chamada em ambos os patamares. Só depois desses dados deve-se mudar a regra de
roteamento entre andares.

O probe `odh-wall-appearance ... --entry-point-probe` confirma os vínculos
estáticos e marca `bhav_executed=false`; o dump `odh-bhav` mostra apenas os
opcodes e operandos das rotinas. O vínculo GLOB1 e os rótulos foram conferidos
nas entradas exatas de Objects.far e Global.far. O próximo passo é observar,
dentro da VM limitada do ODH, qual mudança de posição a cadeia EP15 realmente
produz antes de adaptar o planejador.

## Go Here do cômodo vizinho (2026-10-22)

`tools/person.cpp` agora aceita um destino Go Here no mesmo piso em outro
cômodo e permite que a forma exata da BHAV4096 salva passe pelo planejador
Portal15 do `Routing`. A grade é atualizada após a porta, e a rota continua até
o destino. Na House07, o Sim saiu de `[392,440,1,3]`, atravessou a Door26 e
chegou ao tile `(20,20)` em `[328,328,1,7]`: 126 movimentos, 1 porta executada,
1 mudança de cômodo e 4 atualizações de mapa. O alvo na própria sala continua
passando (16 movimentos); a mesa 106 continua bloqueando o destino sem mover o
Sim. Teste unitário: `odh-routing-tests` passou 349 verificações. CTest depois
da compilação completa: 159 aprovados, nenhum falhou, `linux_window_backend`
ignorado por não haver display nesta sessão.

Comando de reprodução e limites em `docs/interaction-validation.md`. Isso
valida também o clique X11, o passeio entre cômodos e o cancelamento depois de
começar a andar. Os eventos foram enviados diretamente à janela, sem mover o
cursor. Escadas, outros andares e fidelidade total ao executável original seguem
pendentes. Próximo: investigar travessias por escadas e atualizações de andar.

No X11, a seleção do chão só ficou disponível quando o preview recebeu os 23
arquivos externos `.flr`/`.wll`; sem eles, faltava o mapa que associa os tiles
aos cômodos. Também é preciso omitir `--ticks 214`: esse limite é adequado para
uma captura estática, mas impede a janela interativa de avançar depois do tick
214. Com os recursos e o orçamento interativo padrão, o título mostrou
`Go Here arrived` no destino `(20,20)`, cômodo 1, e o JSON confirmou o resultado
da execução: 126 movimentos, uma Portal15, uma troca de cômodo e quatro
atualizações de mapa. Em uma segunda execução, Backspace enviado à mesma
janela interrompeu o passeio depois de 42 movimentos, já após a travessia da
porta: BHAV4096 retornou `true`, a rota foi marcada `cancelled`, e a posição
final foi `[408,398,1,0]`. Isso confirma os caminhos da interface ODH no
preview Linux com House07; não demonstra equivalência de tempo ou comportamento
com o jogo original.

## Conversão de pixel para posição isométrica do lote (2026-10-21)

`unproject_tile` inverte a projeção compartilhada da câmera para um pixel e um
nível de piso fornecido pelo chamador. O teste compara a volta com a posição
original nas quatro rotações, três zooms e uma câmera deslocada. Esse passo
prepara a futura ação de clicar no chão para andar, seguindo o caminho de
destino temporário usado pelo Simitone; não escolhe o piso, não rejeita
obstáculos e não move o avatar. Próximo: resolver o nível/piso e a ocupação no
mapa preparado, depois ligar um destino validado ao Route45 existente e cobrir
cliques sem teletransportar o Sim. O comportamento exato do executável original
para esse menu ainda não foi confirmado.

## Travessia real EntryPoint15 pelo scheduler Route45 (2026-10-21)

`odh-person` agora prepara a grade para o cômodo onde o avatar está antes de
começar uma travessia, e mantém os pedidos Route45 feitos por rotinas-filhas
da porta dentro da chamada externa ativa. O probe executou `Main` sintética
em uma cópia temporária do avatar House07, colocou o avatar na sala 1 pelo
harness e pediu um Route45 normal até o SLOT do Door26. A rotina comercial
EntryPoint15 foi planejada e retornou; o avatar mudou para a sala 3, a grade
foi atualizada 5 vezes, ele chegou à aproximação da porta e o VM terminou com
`returned_true`, sem diagnóstico. Isso prova a travessia pelo caminho de
execução da porta. A posição inicial foi preparada pelo teste: ainda não prova
que o jogador consiga iniciar essa rota por controles normais, nem que o jogo
esteja jogável.

Build Linux concluída e CTest passou 160/160. O modo anterior
`--second-route-room-refresh` também passou depois da mudança. Próximos passos:
validar o caminho inverso sala 3→1, depois iniciar uma sessão Linux com input
normal e confirmar movimento/uso de portas com evidência visual. Preservar a
separação entre probes com posição preparada e gameplay natural.

## Integração Sit/Stand após deslocamento Route45 (2026-10-21)

O harness de House07 agora pode agendar uma posição inicial do avatar e exige
callbacks de movimento nesse perfil. Cadeira ObjectID128: avatar105 partiu de
`(376,440,1,direction3)`, Route45 produziu16 movimentos, Sit/4097 e
Stand/4102 retornaram `true`, e o avatar terminou solto em postura0. Main
permaneceu `waiting` no tick1024; a conclusão do Stand foi no tick551. Esse
setup de posição é sintético e headless; não prova movimento por input do
jogador ou navegação por portas/portais. Reproduzir com o comando em
`docs/person-main-render-validation.md`.

No teste headless, deslocar o avatar para `(320,320,1,direction3)` fez a mesma
cadeira retornar `false`, sem callbacks de movimento. O diagnóstico do
validador mostrou `actor room 1 differs from prepared route room 3`; `(376,408)`
falha pela mesma fronteira. Isso não é uma rota longa no mesmo cômodo: o adapter
atual só prepara uma sala e ainda não executa portal/transição. Origens
`(376,424)` e `(360,440)` completaram Sit/Stand com 16 callbacks, além do caso
anterior `(376,440)`. Essa é evidência de deslocamento dentro do cômodo, não de
input do jogador ou navegação entre salas.

## Plano de cômodos para o destino cruzado (2026-10-21)

No probe headless `--saved-world-main`, House07 restaurado partiu da sala 3 e
`--attempt-walk-destination 320 320` resolveu o destino na sala 1. O inventário
Portal15 estava completo (24 aberturas, 12 arestas) e o planejador de grafo
retornou `cross_room_portal_plan_ready`: sala 3 → sala 1 via portal26, em
`[408,424,1]` (2 expansões; custo 9,92891). Isso comprova somente a cadeia
estática de salas/portais do save. O BHAV EntryPoint15 não foi executado, o
avatar não percorreu até a porta e nenhuma mudança de sala ou clearance runtime
foi validada. A próxima etapa é ligar essa cadeia ao scheduler Route45, executar
a ação Portal15 com owner/EntryPoint corretos e reconstruir a malha da sala após
o retorno antes de continuar até o SLOT.

## Refresh da malha após uma frame externa de rota (2026-10-21)

`Routing` agora aceita um callback do host que reconstrói a grade quando uma
frame externa move o avatar, e replana o trecho restante na nova grade. Em
`odh-person`, o callback resolve sala/andar pela arquitetura preparada, recompõe
o snapshot AttemptWalk vivo e atualiza os dados de clearance antes da retomada.
O teste do scheduler move um ator de x=4 para x=70, troca a grade de 64×32 para
128×32 e valida que o movimento retoma na malha nova. Essa fixture é sintética:
nenhuma EntryPoint15 comercial foi chamada e o host ainda não agenda a cadeia
portal Room3→Room1.

## Diagnóstico de destinos Route45 (2026-10-21)

`Routing` diferencia ausência de candidato compatível com a máscara direcional
da rejeição pelo validador do host. O relatório headless também expõe por que o
validador rejeitou: para `(320,320)` e `(376,408)`, o avatar está na sala 1 e a
malha ativa é da sala 3. `routing` passou 1/1 com fixture que rejeita todos os
candidatos válidos pela direção.

## Cache de sprites ao mudar a câmera (2026-10-21)

`SavedObjectRenderer` agora marca os SPR2 usados no quadro e evicta frames de
vistas anteriores quando o orçamento de 1.048.576 pixels fica sem espaço; após
composição bem-sucedida, conserva somente os frames da vista atual. As caches de
assets e DGRPs continuam reutilizadas. Teste sintético alterna orientações que
requerem cada uma 1 Mi pixels e verifica que o cache permanece limitado a uma
vista.

No preview House07 em X11, comandos de rotação e zoom foram aceitos, a janela
fechou normalmente e o relatório final ficou `waiting` no tick131, sem
diagnóstico. `object_cached_frame_bytes` no último quadro: 1.538.700. Antes, o
cache de ângulos acumulados chegava a 6.174.198 bytes e podia fazer o preview
falhar com `SPR2 dimensions exceed configured allocation budget`. Essa prova é
do host Linux e das mudanças ensaiadas; não mede desempenho de portáteis.

## Seletor de ações no preview Linux (2026-10-21)

O preview Main agora enumera todas as linhas privadas TTAB elegíveis para o
objeto clicado. Tab/Shift+Tab percorre essas linhas pela ordem da tabela, e o
título apresenta a ação selecionada; Space/Enter volta a executar os testes de
elegibilidade no limite da entrada e enfileira a opção selecionada. Se a opção
ficar inelegível antes do envio, a ação não é enfileirada. O seletor é mínimo e
não implementa o pie menu do jogo. O ciclo entre índices foi extraído para um
helper puro que cobre avanço, retorno, wrap-around, seleção stale e lista vazia.

Build completa e 160/160 CTests passaram. `linux_window_backend` foi pulado no
CTest headless, mas executado com X11 e passou 1/1. No preview House07, clique na
cadeira 128 mostrou `action 1/1: Sit`; Tab manteve a opção única e Space mostrou
`interaction queued BHAV4097`. Isso prova apresentação, entrada e enfileiramento
para essa linha, não seleção entre múltiplas ações elegíveis nem conclusão de
gameplay. `docs/person-main-render-validation.md` registra o teste visual.
House07 fornece um candidato adicional: objeto112/GUID1734088879 resolve a
`Fridges.iff`/TTAB128, com nove linhas decodificadas; o agendamento headless
enfileirou Action4103 no tick1. Isso não prova que duas linhas estão elegíveis
ao mesmo tempo. Próximo: testar essa tabela ou outro objeto na janela X11,
depois integrar o estado real de portas/RoomInfo ao fluxo natural e avançar rumo
a uma sessão contínua jogável em Linux.

## Door SLOT de alcance e estado atual da rota (2026-10-21)

Route45 integra seleção de candidatos para SLOT128 type3 índice3 de
`Doors.iff` (`flags=0x54`, facing -2, proximity16–32), usando offsets
limitados, scorer e RNG explícito. O runtime `odh-person` e o probe BHAV
executam esse perfil. Uma rota construída termina em `[48,64,1,2]` após 33
movimentos, com flags64, e opcode8 avança a VM.

No probe com `Objects.far`, `Global.far`, `Doors.iff:4105` e OBJD15089,
`RunTreeByName` (opcode 28) resolve `STR#303` local para “do idle”; não há uma
BHAV com esse nome no catálogo privado/semiglobal carregado e o VM toma o ramo
false, como no Simitone. O percurso então executa `DoorGlobals.iff:8197`
(`0x8001`) e retorna false em 135 passos. A execução aceita apenas o perfil
legado limitado a quatro argumentos e zero locais, respaldado pelo leitor
SimsLib do checkout Simitone. A rota construída ainda usa walkability raw e
bounds de piso, sem clearance dinâmico de avatar. Não representa interação
natural, porta restaurada de House07, travessia de Portal15, gameplay jogável
ou equivalência ao original. Simitone TS1 revision
`4906db3156616bfaffc10ce0963cf08062975fef` orienta a malha, sem provar
equivalência.

Verificação: 142 checks de routing, 76 de slot scoring, 4 de distância e
160/160 CTests passaram; `linux_window_backend` foi pulado por falta de display
X11. Switch, Windows e macOS continuam alvos futuros sem validação nesta
plataforma.

## Inventário RoomInfo completo do World vivo (2026-10-21)

`snapshot_world_room_info` reconstrói e valida o inventário completo de
footprints RoomInfo por sala, preservando coleção e ordenação por owner ID;
`query_world_room_info` agora filtra a caixa candidata dessa mesma fotografia,
mantendo a semântica de inclusão estática/exclusão dinâmica do alvo. O
`odh-person --saved-world-main` exporta o inventário junto do probe atual.
House07 no tick 214 retornou 19 footprints completos na sala 3, todos
estáticos nessa amostra. Build completa e 157 CTests passaram; o teste de
janela ficou pulado sem display X11. Próximo: combinar essa lista do World com
RoomObs/WallObs e regras de AttemptWalk no mesmo processo, incluindo portais e
EntryPoint5 antes de qualquer alegação de clearance ou uso no Main.

## Snapshot de obstáculos AttemptWalk no House07 (2026-10-21)

O módulo puro `odh_saved_attempt_walk_obstacles` compõe WallObs + RoomObs,
faixas externas de um tile (coordenadas inclusivas zero-based), footprints
elegíveis expandidos por 3 unidades, flags e EP5 bilateral. Action IDs <=255
resultam em false; acima disso o chamador precisa de um resolver restrito. O
curto-circuito do EP5 do ator evita exigir a tabela do bloqueador quando já
retorna true. Limites de entidades/retângulos/chamadas e dados desconhecidos
falham fechados, sem expor lista parcial. Nesta revisão a build completa passou
e CTest registrou 157 testes sem falhas, com `linux_window_backend` pulado
porque `DISPLAY=:0` está inacessível.

O diagnóstico House07 lê tabelas EP5 da definição exata e avalia apenas o
subconjunto read-only existente. Quatro portas com caixa salva e HasZeroExtent
desligado fornecem footprint candidato apesar do estado runtime desconhecido;
o snapshot ficou `ready` com 153 retângulos. O módulo bounded
`odh_rect_router` agora reproduz o corredor de `VMRectRouter`/`VMObstacleSet`
sobre ordem explícita de obstáculos, interseção de área estrita, Contains
inclusivo, expansão inicial por heading, ordenação A* estável e limites
fail-closed. `rough_balance_obstacle_prefix` agora reproduz o shuffle parcial
seed-1 aplicado por Simitone ao prefixo `wallObs+roomObs`; vetor de 16 itens
confere com .NET 10. A CLI reordena esse prefixo, mantendo bandas e entidades
depois dele. `OptimizeLines`, portais e movimento continuam fora dele; ordem
`roomInfo.Entities` ainda não foi provada contra owner-ID ODH. No probe House07,
os três destinos já aprovados pelos
gates estáticos deram `found`; dois tiveram corredores de 2 entradas e o
destino igual à posição salva do ator retornou corredor vazio. O heading
derivou da direção salva em oito notches, com arredondamento ties-to-even. A
busca com shuffle somou 4.402 unidades de trabalho e manteve `route_clearance=false`,
`runtime_door_state_validated=false` e `main_movement_integrated=false`. Isso
aplica o modelo Simitone à fotografia salva, não prova a rota runtime do jogo.
Próximo: resolver RoomInfo/portas e portais no mesmo World e testar destinos
fora da posição inicial antes de integrar Route45.

Validação real usou `House07.iff`, `Objects.far`, duas definições de avatar,
Neighborhood, 23 fontes arquitetônicas e `Global.far/Global.iff/SLOT100`.
`git diff --check` passou. Sem validação visual: a janela do CTest foi pulada
pelo display X11 inacessível.

## BHAV read-only EP5 e House07 bloqueadores (2026-10-21)

`odh-person --placement-candidate` agora passa um resolver de EP5 que só executa
BHAVs com formato validado: TestObjectType(StackObject), seguido de igualdade
de StackObjectAttributes[16] com literal 1. Cada execução usa VM e World
temporários, no máximo duas instruções, e não tem opcode de escrita permitido.
Todo formato, dado de atributo ou rotina fora desse perfil permanece
unsupported. Flamingos 155/156 do House07 fornecem EP5 4108 no asset exato
`Objects.far/flamingo.iff`; `(280,376,1,3)` intersectou o 155 e classificou
EP5(Bob 105)=false em uma instrução. `(456,504,1,3)` encontrou IDs 35/101 e
classificou a posição como bloqueada por 35, com dois pares e EP26=false.
RoomInfo completo descreve o inventário de entidades do World restaurado; essas
posições são probes, não movimento real. Todos deixam
`placement_decision=not_evaluated` e `route_clearance=false`.

Testes focais passam para o executor read-only, auditor de overlap e CLI. A
próxima etapa é clearance arquitetônico/portal e busca de rota limitada no World
atual; não conectar Routing45 ao Main até validar esse clearance e estados de
porta/ocupação runtime.

## Consulta RoomInfo a partir do World vivo (2026-10-21)

`query_world_room_info` enumera o World atual, deriva OBJD boxes, salas e
coleções static/dynamic e reconstrói o índice RoomInfo antes de cada query.
Isso mantém consultas frescas sem observers para toda mutação. O limite é 4096
entidades por consulta. O caller precisa fornecer a sala runtime e o
`KillTimeout` atual do avatar; dados desconhecidos, flags ausentes, definição
ambígua e geometria/sala sem suporte falham fechados. A API preserva a regra de
RoomInfo de incluir o alvo na coleção estática e excluir da dinâmica.

`SavedLotRenderer` agora guarda mapa de sala derivado de arquitetura preparada
com aliases de piso/parede não ambíguos. O saved-world preview adiciona ao JSON
`room_info_probe`, consulta diagnóstica do avatar selecionado contra obstáculos
live no mapa congelado. `World` inicia avatares com KillTimeout=-1 e expõe
setter validado; lifecycle também faz parte da checagem de snapshot.
`odh-world-room-info-tests` passou com fixtures sintéticas para movimento entre
salas, contenção, zero extent, classificação, ordem, entradas desconhecidas e
orçamento, incluindo mutação disparada por callback. Callback recebe cópias e a
API verifica a projeção do World antes de aceitar o índice. `World` sincroniza
ObjectData[1] com a direção espacial em escritas VM e mudanças de posição. O
probe não executa EntryPoint5/26, policy allow/disallow, nem clearance, rota ou
interação; a arquitetura ainda é imutável no host. Build/CTest: revalidar após
este incremento. Esse contrato ainda não alimenta
por Main e não valida clearances, EntryPoint5/26, portas/portais, movimento ou
Sit/Stand no lote. Próximo: fornecer um resolver de sala runtime derivado do
lote Main sem promover o mapa geométrico salvo a estado de porta/portal; depois
compor as regras restantes de GetAvatarPlace e conectar clearance a Route45 e
Snap46 no mesmo World da UI.

## Estado runtime do footprint durante Route45 (2026-10-21)

`World::Entity` agora carrega `static_footprint` como estado runtime, separado
do estado salvo. `Routing` mantém o ator estático enquanto planeja, torna-o
dinâmico no primeiro passo real e o restaura ao concluir, falhar ou cancelar
por restart do VM. A transição segue o perfil do Simitone fixado
(`VMRoutingFrame::StartWalkAnimation`/`PreExit`); ainda não foi confirmada no
executável original. O setter protege contra entidade removida ou geração
reutilizada.

Os testes `world` e `routing` passaram; rebuild completo e CTest passaram com
150/150 testes sem falhas, sendo `linux_window_backend` pulado por display
indisponível. `git diff --check` passou. Sem validação visual. Isso ainda não
atualiza o `RoomInfoObstacleIndex`: `World` não publica eventos de movimento,
reclassificação ou remoção para o índice, e Main não tem clearance runtime.
Próximo passo: construir adaptador owner-keyed entre eventos/lifecycle de
`World`, geometria de `ObjectDefinition` e o índice; invalidar consultas caso
algum evento ou footprint esteja incompleto.

## Seleção de destino SLOT sobre footprints salvos

`assess_saved_slot_goal_selection` separa seleção de destino da caminhabilidade
da grade. No perfil v10 type-3 standing=1/sitting=0, unsnapped, ele testa o box
do avatar na posição candidata contra os footprints salvos da mesma sala.
Box livre é `snapshot_clear`; objeto sólido sobreposto é
`object_overlap_rejected`; avatar sólido conhecido é
`avatar_overlap_score_penalty`, conservando a distinção da referência entre
rejeitar o destino e reduzir seu score. A API não calcula o score nem executa
GetAvatarPlace: trata os footprints conhecidos como sólidos conservadores.

Geometria ausente, inventário incompleto, avatar incerto sobreposto, porta
relevante e multipartes sobrepostos são `unsupported`, com elegibilidade
`null`. Auxiliares de identidade ocupam três bitsets fixos de 4 KiB e o
inventário padrão limita entidades/membros a 4.096. A CLI reporta estado,
motivo, elegibilidade e contagens por candidato; só envia à busca os destinos
que passam os gates estáticos anteriores **e** têm elegibilidade de snapshot
`true`. A grade continua bloqueando os mesmos footprints para o caminho.
Flags Allow/Disallow, EntryPoint 5 bilateral, registro dinâmico, consulta de
portas, política multitile e sitting>0/EntryPoint26 seguem não modelados.
`runtime_occupancy_validated` e `route_clearance` continuam false; Main não usa
o resultado.

Proveniência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, submódulo
FreeSO `f86bbd34b2112296ed786741f640a0535886d180`:
`Engine/VMSlotParser.cs:264-321` faz a seleção objeto/avatar/assento e penaliza
avatar/porta; `VMContext.cs:1190-1235` monta os obstáculos de GetAvatarPlace,
aplica EntryPoint5 e flags. O modelo atual usa apenas o ramo sitting=0 sobre
footprints conservadores; não afirma equivalência ao executável original.

No House07 local com as 23 fontes arquitetônicas externas, a CLI reportou
oito candidatos: seis `snapshot_clear`, dois `unsupported` por sobreposição
com multipartes (IDs 106 e 125), zero objetos simples rejeitados e zero
avatares conhecidos com penalidade. Permaneceram três destinos da busca,
com caminho de um ponto/custo zero pois o avatar já estava em um deles. Os
ramos objeto/avatar foram testados com fixtures sintéticas. O teste focal
passou 32 checks e o build completo concluiu. CTest passou 145/145 com
`DISPLAY=:0` acessível, incluindo `linux_window_backend`, sem skips.
`git diff --check` passou. Incremento revisado; ainda sem commit nesta etapa.

## Gates estáticos para candidatos SLOT Route45 (2026-10-21)

`odh-wall-appearance --route-grid-diagnostic` exige
`--route-slot-resource ARCHIVE ENTRY CHUNK_ID TYPE INDEX` e lê o SLOT
selecionado de forma exata e fail-closed. O perfil v10 type-3 isotrópico gera
oito posições circulares; cada uma reporta `RouteEntryFlags`, máscara, bounds,
sala, `FloorChangeValid`, `RaycastWall` e caminhabilidade. O raio/diagonal é
um perfil ODH restrito; o raycast usa a fotografia inicial salva das arestas.
House07 `Global.far/Global.iff/SLOT100`, type 3 index 1 (offsets zero) teve
três candidatos que passaram os gates estáticos; todos estão na sala 3, piso 6
e raycast limpo. Um coincide com a posição salva do avatar, resultando em
caminho de custo zero e um ponto. Continua sendo apenas
`candidate_grid_search_only`, com `route_clearance=false`. Não valida
`GetAvatarPlace` dinâmico, flags Allow/Disallow, EntryPoint 5, portas vivas ou
semântica do executável original.

`odh-routing-tests` passou 56 checks. Build completa e 143 testes passaram;
`linux_window_backend` foi pulado porque o X11 retornou `DISPLAY inaccessible`
mesmo com `DISPLAY=:0` e socket X11 presente. Não houve validação visual neste
checkpoint. A verificação CLI assertiva confirmou oito candidatos reportados,
três com todos os gates estáticos; todos os oito tiveram `FloorChangeValid`
allowed e raycast clear. `git diff --check` passou. O commit de candidatos
SLOT foi publicado em `origin/main` e confirmado por `git ls-remote`. O
incremento dos gates estáticos ainda precisa de revisão final e publicação.

## Máscara direcional do SLOT no Route45 (2026-10-20)

O `Routing` filtra os oito candidatos do perfil de raio exato pela máscara
direcional e grava no `RouteEntryFlags` o resultado de
`VMSlotParser.GetSearchDirection` do Simitone fixado. A regra usa floor de
coordenadas em 1/16 tile, rotação do objeto, setores angulares e interseção com
os bits baixos do SLOT. O dado comercial House07 confirma que Route45 usa
Global SLOT100, type3/data1, flags `0x55`, raio `16`, standing=1 e facing=-2;
isso foi lido, sem alterar ou copiar recursos comerciais.

`odh-routing-tests` passa 51 checks, inclusive uma célula walkable em um setor
que a máscara `0x55` rejeita. Build completa e CTest 144/144 passaram com o
teste `linux_window_backend` usando o display `:0`. Esse marco verifica apenas
o controller em mundos construídos e a semântica da referência Simitone; não
liga Routing45 ao Main e não prova movimento no lote. Próximo: substituir o
diagnóstico genérico ao redor da caixa do alvo por candidatos explícitos do
SLOT verificado, depois modelar validação de parede/ocupação/portal e integrar
somente quando a geometria do lote puder ser comprovada.

## Grade candidata por sala (2026-10-20)

Commits `28403d3` e `8bdb5d4` estão publicados em `origin/main`. O adapter
`odh_saved_room_route_grid` transforma uma sala pronta em grade 1/16 tile,
bloqueando áreas fora da sala, RoomObs, WallObs e caixas de entidade com margem
do AttemptWalk. O teste focal do adapter cobre 15 checks; build completa e
CTest 144/144 concluíram sem falhas (143 executados, `linux_window_backend`
pulou no sandbox); o gate X11 separado passou 1/1 em `:0`. A geração de
candidatos WallObs usa as fórmulas de `VMRoomMap` na
arquitetura salva; arestas sem face reconhecida são conservadas como sólidas.
O inventário atual é global e não mesclado por andar, enquanto Simitone coleta
e mescla `wallObs` por sala; ele não é byte a byte idêntico às listas runtime.
Limites: 262.144 células, 4.096 entidades, 16.384 retângulos e 4.194.304
células rasterizadas; o adapter falha fechado em entradas incompletas. WallObs
usa somente o snapshot inicial de faces e não modela estado runtime da porta.
A unidade ainda não executa política runtime, EntryPoint 5, validação de
SLOT/goal, portal ou VMRectRouter. `ready` ainda não significa clearance, e
Main continua sem usar o novo resultado.

O `odh-wall-appearance --route-grid-diagnostic 105 128` gera uma busca estática
opcional para Bob e o alvo 128 na sala 3 de House07. Na cópia local, a grade
ficou `ready` (10.240 células, 4.529 caminháveis), start caminhável e `find_route`
retornou 9 pontos/custo 80 até `[392,448]`. O probe procura a vizinhança da
caixa expandida do alvo, não lê SLOT, e não valida aceitação do jogo. Continua
com `route_clearance=false`; o resultado não conecta Main/Routing45. Os testes
de obstáculo agora cobrem as duas diagonais, wrap de portas vizinhas e flags
malformadas. Próximo passo: identificar com precisão as entradas SLOT e os
gates de AttemptWalk para a interação Sit, depois adaptar esse diagnóstico
para testar posições válidas sem rotular `find_route` como VMRectRouter.

## Estado verificado da interação e auditoria de limites (2026-10-20)

A nota abaixo sobre a fila parada foi superada: no teste posterior registrado em
`docs/interaction-validation.md`, a fila privada foi consumida e a VM chegou à
cadeia `4097→4100→4099` no opcode 45 aos 103 ticks. O resultado foi
`unsupported primitive 45`; isso confirma a execução BHAV até o serviço, não
movimento nem conclusão de gameplay. O Main segue sem integrar o controller
Routing ou fornecer uma malha de clearance.

A auditoria estática por entidade agora também compara a shape expandida com as
quatro faixas externas de um tile que `VMRoutingFrame.AttemptWalk` constrói ao
redor de `VMRoom.Bounds`, além de RoomObs. No House07: 50 não cruzam RoomObs,
41 cruzam RoomObs, zero cruzam as faixas externas, 22 permanecem unsupported e
69 not_applicable. São resultados de sobreposição estática por entidade;
`routing_clearance` continua false. A auditoria tem 25 checks focais; o build
completo e 143 CTests passaram. O gate X11 também passou em execução separada
com acesso ao display `:0`.

Próximo incremento: criar um adaptador de clearance local ao lote, com orçamento
explícito e estados fail-closed para chão, RoomObs, faixas externas, paredes e
entidades; antes de habilitar Routing ainda será necessário resolver elegibilidade
runtime, EntryPoint 5 bilateral, RouteEntryFlags/slot e caminho VMRectRouter.

## Ordem de restauração de portas nas paredes (2026-10-20)

O adapter `odh-wall-appearance` agora ordena uma cópia das entidades por
ObjectID ascendente para aplicar escritas de estilo, seguindo o caminho de
restauração documentado no Simitone: entidades ordenadas e replay sequencial de
`PositionChange(Context,true)`. A API genérica permanece caller-ordered. O
conflito sintético cobre uma escrita de porta seguida por clear em ObjectID
maior. Isso representa apenas o estado inicial no perfil dessa revisão do
Simitone; mudanças runtime posteriores e equivalência com o jogo original não
estão provadas. Documentação: [associações arquitetônicas salvas](wall-appearance-validation.md).

## Campos de colisão OBJD (2026-10-20)

O leitor OBJD136/138 preserva `TileWidth` e `FootprintMask` quando ambos estão
presentes nos offsets verificados pela ordem de campos Simitone/FreeSO; views
curtas continuam válidas e deixam ambos ausentes. Isso ainda não os converte
em obstáculos vivos nem habilita Routing45. Testes de offsets/truncamento e
build dos consumidores passaram; `odh-saved-world` também leu House07 original
com `Objects.far` e os dois IFFs de personagens sem erro. Veja
[campos de colisão OBJD](object-collision-validation.md).

O incremento local após `94c7dc7` adiciona `odh_collision`: cálculo dos bounds
do OBJD com rotação/fallback de referência, ausência para HasZeroExtent/objetos
contidos, box candidato do avatar ±3 e interseção estrita. `odh-collision-tests`
tem 16 checks de fórmula, não de colisão no lote. Build completa e CTest
129/129 passaram; a janela segue pulada sem DISPLAY. Ainda faltam validar floors,
doors, rooms, objetos dinâmicos e política do avatar; não conectar Routing45.
O avanço local atual adiciona `odh_route_geometry`: piso preciso por posição
1/16 tile e Bresenham TL/TR com solidez externa explícita. O novo CTest cobre
coordenadas fora de faixa, diagonais de piso, oito octantes, reversão e portas/
paredes sólidas, não sólidas ou sem resolução. A geometria continua sem vínculo
com o Main e não basta para aceitar destino: `FloorChangeValid`, rooms,
inclinações, obstáculos dinâmicos e construção autoritativa do estado de portas
seguem pendentes. Build limpa out-of-tree concluiu; CTest passou 130/130, sem
falhas, com `linux_window_backend` pulado por falta de DISPLAY. `git diff --check`
passou. Ver [geometria de rota](route-geometry-validation.md).

## Main em lote restaurado (2026-10-20)

`odh-person --saved-world-main` restaura House07 com `make_saved_world`,
seleciona o avatar 105 por GUID/User00000.iff e inicia um Main novo no mesmo
World da VM/Playback. Threads e filas salvas continuam dormentes; não é uma
retomada da simulação salva. `SavedLotRenderer` desenha terreno, pisos,
avatar Main, paredes/junções e objetos salvos nessa captura, sem construir
outro World. A câmera acompanha a posição gráfica viva. Outros avatares
continuam no World, mas ainda não são desenhados.

O incremento posterior a `c1df46d` liga `SavedObjectRenderer` à mesma cena,
lendo Graphic/Hidden/posição/direção vivos e SLOT do contêiner. Paredes
binárias precedem objetos alpha, com depth comum. Objetos removidos deixam
de desenhar e de fornecer aparência de parede; reuso de ID exige refresh do
renderer. Flags dinâmicas e centros multitile permanecem snapshots; criação
de objetos, movimento runtime de grupos e transparência global ordenada
continuam pendentes. Cinco CTests focais e o CTest completo (127/127) passaram;
`linux_window_backend` foi pulado sem DISPLAY.

Com House07, Neighborhood e recursos locais externos, aos 214 ticks o VM está
`waiting`, com 308 instruções e cinco frames ativos. A cena 320x240 usou 6.323
triângulos de terreno, 1.365 de piso, 69 camadas de parede e 49 junções; o
avatar usou 684 vértices/840 triângulos. O probe atualizado adiciona 135
objetos, 166 camadas, 6.174.198 bytes de frames de sprite, 130.766 amostras
testadas e 27.588 sombreadas. Recibo/PPM ignorados:
`reports/odh-person-main-lot-house07-objects-tick214.json/.ppm`; SHA256 do PPM
`c14edf44b16cdfe5d80fb9fc7b1251606f027dc6d5af8a4656fd2ba62bb88309`.
O frame CPU foi inspecionado; isso não valida gameplay ou aparência equivalente
ao original.

Os incrementos `bb2db4e` e `281a972`, posteriores a `e325706`, adicionam seleção
de objetos por Button1 no preview Main com lote e preservam `TreeTableID` do
OBJD136/138 no pick e JSON. House07 original confirma cadeira128/GUID
`0x26BFBB29`/TreeTableID128; sua TTAB128 em `DiningChairs.iff` decodifica uma
linha Action4097/Test4098. O clique usa o último frame apresentado, antes de
aplicar rotação/zoom agregados, e revalida geração, GUID e Hidden vivos. O
buffer opt-in usa o mesmo passe alpha/depth dos sprites e no máximo um uint16
por pixel (2 MiB no teto; 153.600 bytes em 320x240). Picking é coberto por
fixtures; a janela real ainda não foi exercitada.

No HEAD local `edd21fa`, `Vm::install_owner` permite anexar um banco
private/semiglobal em safe point, sem reiniciar Main nem alterar estado
suspenso. O commit `b888f63` liga Space/Return à primeira linha
comum elegível da TTAB: recalcula TestFunction contra o World atual, carrega a
closure privada da ActionFunction do `source`/`asset` exato, instala um
CodeOwner separado por recurso e ação e enfileira pela `InteractionQueue`.
JSON diferencia pedido, fila, rejeição, fault e retorno do BHAV. O limite padrão
da VM é 64 owners incluindo owner zero (até 63 anexados). Isso ainda não
apresenta o pie menu completo nem comprova sentar.

A ação House07 4097→4100→4099 alcança Routing45; o Main host ainda não liga esse
serviço nesse caminho. O controller roteia com Global.iff SLOT100/tipo3/data1,
mas não existe conversor de arquivos de arquitetura/objetos do lote para a
malha de clearance. House07 tem 64x64 tiles; a resolução de 1/16 tile do World
resultaria em 1024x1024 células para o lote todo, acima do limite padrão. O
checkpoint antigo usava 16 bytes/célula em Linux64: 4 MiB no limite 262144 e
16 MiB no lote inteiro. As alterações locais atuais reduzem os arrays fixos a
9 bytes/célula (2,25 MiB no limite e 9 MiB no lote inteiro), mais até 2 MiB de
frontier e memória do caminho no limite padrão. São contas de buffers máximos,
não RSS medido. Uma diagonal segment16 no piso1, x31/y30, deve resultar em
`unsupported_geometry` até o rasterizador diagonal existir. O atalho `start ==
goal` agora exige célula livre dentro da malha; o Simitone valida disponibilidade
do slot antes de entrar em `TURN_ONLY`, mas essa condição ainda não é fornecida
por um adaptador do lote no Main host. O opcode15 da
BHAV4099 só aparece após RouteEntryFlags fora de 1/4/16/64 ou Snap46 false;
payload do BHAV é idêntico entre The Sims original e Legacy, mas a semântica
desse opcode no executável original permanece desconhecida. Na referência
Simitone os ramos usam scope18/MyPersonData e index37/RouteEntryFlags; opcode15
está mapeado como breakpoint. O opcode49 é NotifyOutOfIdle.

Build completa sem warnings e CTest atual: 128/128 aprovados, sem falhas; o
gate `linux_window_backend` foi pulado pois `DISPLAY=:0` não tem servidor X
acessível. O teste sintético de descoberta agora tem 26 checks e o conjunto
focal 4/4 passa. A tentativa de ação real pela janela e a conversão de rota do
lote continuam pendentes. Outros avatares visíveis, stacks salvas, áudio audível,
serviços Main, save/restore ODH e validação X11 também estão pendentes; logo
ainda não é um build jogável. Detalhes e hashes em
[validação do Main restaurado](person-main-render-validation.md).

## Checkpoint anterior — Main construído e posição projetada (2026-10-20)

`odh-person` agora pode exportar um frame PPM headless usando o `PlaybackSample`
vivo do Main. O caminho usa `AvatarBodyStrings` diretamente para resolver corpo,
cabeça e seis variantes de mão, decodifica CFP pelo `AnimationHeader` exato
selecionado no catálogo multi-FAR e passa clip, frame e gestos ao renderer.
Não cria `ObjmPersonState` nem escolhe animação por nome. A opção
`--render-saved-avatar OBJECT_ID` valida GUID e tipo contra o recurso de pessoa
carregado e projeta a posição/direção de ObjM salvo; esse dado não é inserido no
World do VM. `--render-window` acrescenta preview X11 paced a 25 Hz (cadência de
apresentação, sem calibração de tempo original), usando o mesmo Playback/VM;
P pausa, setas giram, +/- ajustam zoom e Escape fecha. Cliques não disparam
interações.

Validação com os arquivos externos da instalação original: os relatórios
`reports/odh-person-main-first-sample-20261020.json`,
`reports/odh-person-main-tick214-20261020.json` e
`reports/odh-person-main-savedpos-verified-20261020.json` registram frames do
Main em ticks 2 e 214. No tick 214 o clip foi
`a2o-idle-neutral-trans-lstand-lhips`, frame 1.2; o frame usou 684 vértices,
840 triângulos e 425.984 bytes de textura. Os PPMs são preservados em
`reports/odh-person-main-*.ppm`. No frame posicionado, ObjectID 105/GUID
850010198 projeta para `[24.5,27.5,0]`, direção 3, com a câmera centrada nessa
posição. Sete CTests focais passam e
`git diff --check` passa. A execução encerra em `waiting` ao esgotar o limite de
214 ticks (código 3 esperado pelo host); não é falha de renderização nem prova
de que Main concluiu.

Limites atuais: a posição ainda é projeção de apresentação, não posição viva no
World; lote/arquitetura não são desenhados e não há entrada de usuário para
gameplay. O preview X11 compila, mas o ambiente de validação atual não tem
servidor DISPLAY acessível; a cena não é jogável e o Main ainda pode parar em opcode/comportamento não
implementado. Próximo passo de maior valor: unir essa posição com o desenho do
lote salvo, preservando o mesmo Playback/VM e sem clonar o World.
Só depois avançar para ações contínuas, tempo/necessidades e persistência ODH. Detalhes em
[validação do frame do Main](person-main-render-validation.md).

Meta integral: primeira versão funcional e verificável no Linux; New 3DS, Vita
e Android são etapas futuras com validação própria. Nintendo Switch também é
um possível build futuro, ainda sem estudo de toolchain ou validação. ODH exige
dados do jogo fornecidos pelo usuário, sem dependência do executável original ou
checagem de CD. Preservar os milestones de leitores, VM, cena/interação, tempo
e save; os resultados Linux não comprovam compatibilidade dos ports.

## Incremento atual — manager HIT por `TrackID`

O trabalho posterior ao checkpoint de áudio adicionou `HitTrackManager` como
orquestrador sintético de controllers registrados explicitamente. Um controller
é criado na fronteira de registro, nunca por Play; Play resolve evento→Track→
entrada global, aplica overrides, gates e contribuições; refresh/remove aplica
agregados ou fade sem apagar o controller. Recusas de lookup/registro antes de
efeitos são recuperáveis. O ajuste de `service+0x114` permanece apenas em
refresh/remove; o Play inicial usa o agregado bruto.

Cobertura final: receipt
`reports/hit-track-manager-20261020/validation.json` SHA256
`4a5bff2329e0fb5ac706c648e437719c580b023f161292c5502b54eeff04ec92`.
Debug e ASan/UBSan executaram o CTest do manager, cada um com 38/38 casos e
98 checks, sem falhas ou skips. Cinco CTests companheiros por gate foram
reutilizados explicitamente, sem rerun; os logs/receipts pré-fix continuam
preservados. Revisão independente aprovou o recorte.

Limites: os providers são sintéticos; a unidade não liga o manager a World,
gerações, scheduler/Clock, áudio físico, DSP/streaming ou runtime jogável. O
contrato e a matriz estão em
[validação do manager HIT](hit-track-manager-validation.md). Próximo caminho
para Linux jogável: conectar o host permanente `odh-person` ao renderer e criar
uma cena Linux com diretório de dados configurável, seleção/restauração de lote,
janela e entrada. A triagem Simitone localizou o fluxo TS1 de seleção de casa e
inicialização em `TS1GameScreen.cs`; ele serve como referência de lifecycle,
não como prova de comportamento original nem licença para importar caminhos
herdados de FreeSO/TSO. Depois da primeira cena com Main animado, fechar
interação sentar/levantar, tempo/necessidades e persistência ODH.

A investigação da lacuna Stand→idle está em
[stand-continuation-investigation.md](stand-continuation-investigation.md).
`odh-person` já mantém Main, `Sleep`, `InteractionQueue` e Playback no mesmo
relógio e preserva conclusão/retomada de ação; não se deve duplicar esse
scheduler. A saída sentada é específica ao probe `odh-bhav`, que termina a VM
do avatar depois de Stand. O contrato CFP multi-FAR está integrado ao renderer
e ao export headless do `odh-person`, mas não há ainda uma cena Linux interativa
com lote vivo, rota, posição ou entrada de usuário.

## Checkpoint atual — `afc836a` (2026-10-20)

O commit publicado implementa a ordem externa do NoteOn global: getter efetivo
controller38, writer do wrapper `(38,1,0)` quando não zero, NoteOn, getter
Patch38 vivo e A/B condicionados pelo valor pós-play, mesmo com resultado de
play falso. Os seis providers ficam explícitos e o perfil local continua
separado. A matriz tem 34 casos em lote; Debug, ASan/UBSan e sem SDL/X11
passaram cada um com 6 CTests e 34/34 casos. LSan ficou desabilitado. A revisão
independente não encontrou problemas no escopo.

A investigação subsequente fechou o writer concreto: ele grava o banco local
do wrapper em `wrapper+0x8c`, sem chamar o backend ou alterar flags do
controller. Veja [a evidência do writer global NoteOn](hit-native-noteon-writer-evidence.md)
e o relatório estático em `reports/hit-native-noteon-providers-20261020/`.
Providers A/B, semântica global posterior de ID38, adapter físico e primeira
versão Linux jogável continuam pendentes. Os testes feitos até aqui não
demonstram áudio físico ou compatibilidade completa.

A OMM é consultada no escopo `OpenDollhouse`. Nos checkpoints, registrar `repository`
(`opendollhouse`), `branch` e `git_revision` no handoff, com o `expected_sha256`
vigente. Confirmar por `get_handoff` com `observed_repository` e
`observed_revision`. SHA do handoff e HEAD Git são identidades distintas;
`freshness.matches` não comprova testes, árvore limpa ou backup remoto.

## Referência adicional recebida em 2026-10-15

`../referencias/The Sims Legacy Collection` foi fornecida pelo usuário como
edição Steam de 2025. Após confirmação humana do fim da cópia, o inventário
registrou 10.406 arquivos e metadados estáveis antes/depois. Os 81 HIT/HOT/HSM
comuns têm SHA256 iguais; quatro dos cinco FAR amostrados diferem. Identidade
PE32 x86 do executável e limites da comparação estão em
[legacy-reference-validation.md](legacy-reference-validation.md).
Consultar [referências](references.md#referência-comercial-adicional-legacy-collection).
Ela complementa a instalação anterior; a prioridade de desenvolvimento e a
proveniência das evidências já registradas permanecem.

## Controller HIT nativo — base `f5ab084`

`NativeHitController` executa um subconjunto HIT portátil com bancos próprios
1–16, 17–47 e 50–62, Clock explícito e callbacks sobre o binding de áudio
corrente. O caller fornece `entry_pc` já resolvido; `hitlist_id` não zero da
HOT é aceito sem chooser e sem sobrescrever ID50. Contrato, limites e
verificação estão em
[hit-native-controller-validation.md](hit-native-controller-validation.md).
A evidência estática que orientou o recorte está em
[hit-native-controller-execution-evidence.md](hit-native-controller-execution-evidence.md);
a auditoria conferiu 535 linhas, 1.594 bytes, oito entradas de opcode e
mapeamento PE (nota SHA256 `635e6f322f12efe922b3143872c5c4f0da58ca8573129d0005f6c8749707154f`;
relatório SHA256 `f0171402d4637498e9ada7449c7aec0303faa59e1d66eed97cd7c7f05048d9a8`). Nenhum executável original foi rodado.

Na base histórica, o core direto passou 82 checks com `-Werror -Wconversion -Wsign-conversion` e
ASan/UBSan. CTest Debug: 115 configurados, 114 passaram, um skip X11, zero
falhas (6,97 s); ASan/UBSan: mesmos resultados (13,10 s), LSan desligado;
sem SDL/X11: 9/9 (0,66 s). Oráculo Python independente: 6.331 casos Debug e
6.331 ASan, incluindo aritmética/fade signed 32-bit, INT32_MIN, igualdade e
flags. Logs e recibos estão descritos na validação.

Não houve probe da cadeira nem teste de backend físico porque a classe ainda
não está integrada ao host de áudio. O controller não implementa vozes/DSP,
cadência externa, manager persistente HOTTrackID, integração World, provider
espacial nem admissão. Os casos novos são sintéticos/host; não reivindicam
áudio físico, compatibilidade com o original ou ports.

## Próximos passos

Unidade de PCM reutilizável: load move/charge uma vez, replay usa mesmohandle,
stop/completion retêm PCM até release; loop e taxa efetiva explícitos. Troca
de taxa ancora na fase consumida usando taxa antiga. Lookup XA agora aceita
NativeHitPatchRef global e não depende do HOT local. Contrato e limites em
[hit-reusable-pcm-validation.md](hit-reusable-pcm-validation.md).
A [pesquisa do backend comum](hit-native-common-backend-evidence.md) fechou
classificador físico vs recurso virtual, preload/dirty, perda de pan/frequência,
NoteOn pre-controller38/post-Patch38 e loop latch. O adapter/callback matrix
externa foi implementada em `afc836a`; writer interno está documentado em
[hit-native-noteon-writer-evidence.md](hit-native-noteon-writer-evidence.md).
Providers físicos, DSP e streaming continuam pendentes; não reaplicar controles
nem inferir streaming por tamanho FAR.
Fonte final: mixer101/transporte152/catalog46/integração13 checks. Debug e
ASan/UBSan122cfg/121pass+1skipX11/0fail (15,84/26,78s), LSanoff;
semSDL/X11 só4selecionados pass. Oracle10.392/0, auditoria20ranges/4.320bytes.
Quatro probes Classic/Legacy×Debug/ASanUBSan Patch3212:54.296frames,
108.592bytes PCM retidos em1handle, replay/fase/loop/release pass, inputs82
hashes preservados por edição. Sink sintético, sem áudio físico. Recibos
`reports/hit-reusable-pcm/validation.json` e `native-*.json`.



Nova unidade de controles pré-wrapper: helper A portátil calcula ganho com
mul32/divisões sequenciais, grupo/duck explícitos e seleção pan/posição; helper B
combina local21/Patch21 antes do DSP. Perfil global exige callback de snapshot
para escritas19/20, sem fallback numérico; elas recalculam A somente. Registry
encaminha placement antes gain e pitch separado. Parser nativo conserva flags
Patch43/26/27 opcionais, sem transformar ausência em falso conhecido.
Contrato e cobertura em [hit-native-audio-controls-validation.md](hit-native-audio-controls-validation.md).
O adapter de áudio físico e providers efetivos ainda faltam.
Fonte final:119 checks recursos/163 controller/64 registry/51 integração e6
grupos helper. Debug eASan/UBSan121cfg/120pass+1skipX11/0fail (16,00/30,80s),
LSanoff; noSDL/X11 só5selecionados pass. Oracle20.324 casos sem divergência;
auditoria PE25recortes/2.131bytes. Corpus4 Classic/Legacy×Debug/ASanUBSan:
423 aquisições por caso sem falhas,26hashesfonte porreceipt conferidos.
Os vencedoresPatch têm ID43/26 falsos17.503/ausentes1;ID27 falsos17.480/
ausentes24. Dados não comprovam valores true. O parsing desses valores tem testes
sintéticos; seus efeitos físicos de preload/backend continuam pendentes.
Recibos em `reports/hit-native-audio-controls/validation.json` e `corpus/`.



O controller já oferece perfil `NativeHitPatchSource::global_catalog`, com
Patch de outra origem e outcomes zero/adquirida/ausente/tipo incorreto. A
inicialização global libera o binding anterior antes do lookup e pode seguir
ativa sem wrapper; tipo incorreto faz stop diagnóstico seguro e depois o
initializer restaura entrada/deadline/ID46/ativo. Há dois callbacks de aplicação
de controles, antes e depois da cópia do ID26 efetivo da Patch. A escrita
pública ID50 e timed opcode1 continuam fora do subconjunto. Contrato em
[hit-global-patch-binding-validation.md](hit-global-patch-binding-validation.md)
e [evidência dos resultados](hit-native-binding-outcomes-evidence.md).

`NativeHitPatchRegistry` agora possui backend por Patch ativa/geração, com
BindingLease/ResourceLease separados, factory externa RAII e views protegidas
por geração. Pausa chama Stop, resume não faz áudio; recursos são pedidos
explicitamente. Último release aposenta geração; Stop tardio não toca geração
nova. Os limites padrão64/256/256 e hard cap65536 são política ODH. No host,
armazenamento fixo padrão21.176 bytes exclui backends/capturas/allocator.
Revisão corrigiu overflow de releases adiados por tokens aposentados; regressão
`{1,1,1}` confirma cleanup. Registry deve sobreviver a todos os leases.

Fonte final: controller154 checks, registry55, integração29; CTest Debug e
ASan/UBSan120 configurados,119 aprovados,1 skip X11,0 falhas (15,42/25,87 s),
LSan desligado. Sem SDL/X11 três testes selecionados passaram. Auditoria PE
independente conferiu25 recortes/4.070 bytes. Recibos em
`reports/hit-global-patch-binding/`. O driver integrado ainda é sintético;
não há adapter DSP/PCM físico nem manager persistente/World/admissão no host.

Quatro probes Classic/Legacy × Debug/ASan+UBSan passaram:4.168 candidatos
por caso,423 adquiridos,3.744 zero,1 ausente; sem falha de binding/cleanup
ou recursos vivos. Birthday continua na falha esperada69. Driver sintético
ID26=0 apenas quando adquire; sem áudio físico. Hashes e receipts em
`reports/hit-global-patch-binding/{validation.json,corpus/summary.json}`.

O `NativeHitObjectCatalog`, agora em `HitResourceBank::objects()`, resolve
TrackID pelo namespace global compartilhado de Track/Patch. O parser nativo
conserva registros na ordem literal das linhas, incluindo duplicatas, e
ignora chaves zero de Track/Patch antes do payload. GlobalHitList usa outro
mapa. Chaves desses três callbacks usam o helper numérico nativo; o perfil
legado fica inalterado. `resolve_global_track_event` mantém três origens:
EventMapping escolhido, Track vencedor e HIT da entrada. Patch também é
global e pode ter outra origem. O lookup anterior de `entries().resolve_event`
permanece local ao HOT; não o transportar para o manager como autoridade.
Contrato, testes e recibos em
[hit-object-catalog-validation.md](hit-object-catalog-validation.md).

Checkpoint anterior do catálogo: recursos111, objetos27, banco19 checks; suites Debug e
ASan/UBSan 118 configurados,117 aprovados,1skip X11,0falhas (14,80/26,22 s).
Após última mudança da cobrança no banco, cinco testes focais recompilados
passaram nas duas configs; sem SDL/X11 cinco selecionados também passaram.
Auditoria independente: 50 recortes,7.146 bytes. Corpus em quatro probes:
21.831 registros/21.809 chaves,19 substituições de mesmo tipo e3 entre tipos;
9 eventos usam Track de outro HOT,1 mudaHIT/PC na ordem diagnóstica. Recibos
`reports/hit-object-catalog/`; ordem não é prova de enumeração original.

Para integrar ao host, usar o perfil global e o registry compartilhado;
o perfil local padrão permanece para diagnósticos anteriores. A
[pesquisa de ownership](hit-native-backend-ownership-evidence.md)
demonstrou um contador para os dois aliases espaciais e desequilíbrio de
releases no caminho estático; não copiar esse uso potencial de memória
liberada para ODH. A [pesquisa do registry](hit-native-group-registry-evidence.md)
confirmou escritores69/71 e leitor73; opcode70 não espera. Restam domínio
semântico dos índices, provider compartilhado e invariantes de reload/remoção.
Nenhum desses opcodes foi habilitado nesta unidade.

O `HitEntryCatalog` agora conserva arquivo HIT e PC por chave global. Grupos
posteriores na ordem fornecida substituem os anteriores, com recibos limitados.
`NativeHitResourceResolver` e frames de retorno permitem chamadas entre HITs,
sem trocar o HOT/track/binding do evento. O leitor precisa de
`HotReadProfile::native_entry_map` explícito para esse fluxo; o padrão legado
permanece. A investigação provou buffers independentes e o caminho TrackData,
não concatenação nem merge ENTP. Contratos e resultados atuais em
[hit-native-entry-map-validation.md](hit-native-entry-map-validation.md),
com [bytes do loader](hit-native-entry-map-evidence.md).

A [ordem do loader](hit-native-load-order-evidence.md) foi rastreada: raízes
na ordem de chamada, arquivos na ordem devolvida por Win32, HSM/HIT/HOT por
grupo. Não há sort interno no caminho estudado; ordenar no Linux é política
ODH e pode alterar qual chave repetida vence. O namespace HSM global usa
nomes exatos e substituição pela última definição; HOT guarda números já
resolvidos. O `HitResourceBank` agora possui recursos e catálogo com endereços
estáveis e entrada em ordem explícita; os streams podem ser fechados após a
construção. Contrato, orçamento e recibos em
[hit-resource-bank-validation.md](hit-resource-bank-validation.md).

Unidade atual: leitor106 checks, banco16 checks estritos; revisão independente
sem defeito concreto. CTest Debug e ASan/UBSan: 117 configurados por rodada,
116 aprovados, um skip X11, zero falhas (13,99/27,31 s), LSan desligado.
Sem SDL/X11: quatro testes selecionados aprovados. Quatro probes com dados
Classic/Legacy em Debug/ASan passaram após fechar os streams: 27 grupos,
13.207 registros HSM/8.431 nomes/4.776 substituições. O evento de aniversário
continua chegando à falha esperada do opcode69, sem efeitos de áudio. Recibos
em `reports/hit-resource-bank/`; auditoria estática conferiu 75 intervalos e
31 hashes de recortes. Isso não valida áudio físico ou jogo completo.

A [pesquisa do backend concreto](hit-native-engine-backend-evidence.md)
supersede a hipótese de pausa por cursor: o slot de pausa para buffers, e o
de retomada retorna sucesso sem reiniciá-los. Wrappers da mesma patch podem
compartilhar backend. Nenhum adapter novo foi implementado nesta unidade;
callbacks, ownership completo, streaming, controles e PCM já enfileirado
ainda precisam de contrato. Não encaminhar pausa de um binding ao sink
compartilhado. Integrar catálogo/banco ao manager persistente por TrackID,
contribuições e World; opcode69 continua pendente. Preservar os gates Linux
savedframes/Clock/Main, filas/interação, lote e save.

Orientação humana de 2026-10-17: lacunas do Simitone exigem investigação dos
dados e executável originais; o roadmap de terceiros não limita o objetivo
ODH. A issue8 e o fork alexjyong, com clone e revisão próprios, estão em
[auditoria de referências](reference-audit-2026-10-17.md). O fork tem caminhos
Linux/Legacy e melhorias de frontend úteis para comparação; nenhuma alegação
de fidelidade decorre deles. Seu FreeSO ainda não foi materializado/auditado.

A extensão inicial Call7 adicionou resolvedor explícito de chave DWORD para PC e pilha
limitada, compartilhada por Return8/End12. Stop limpa a pilha; restart ativo
ID55 preserva seus retornos. Isso não implementa população/precedência do mapa
do manager. Foi ampliada pelo catálogo acima. Resultados históricos e limites em
[hit-native-call-validation.md](hit-native-call-validation.md).

Concluir integração com adapter do host de áudio e manager persistente por
HOTTrackID, mantendo geração de World e binding PCM separados. Preservar gates
de admissão, ownership por fonte e teardown; não deduplicar por nome nem criar
ticket por admissão. Fechar backend/provider antes de alegar lifecycle completo.
A meta Linux também inclui saved frames/queues/Clock (105/137), Main sem reinit,
câmera/picking, input, lote com SLOT/failure trees/needs/inventory/save e
SDK/CC. Consulte os documentos de validação e evidência anteriores para
histórico de contratos, testes e proveniência.

Preservar originais, saves, refs pinadas, relatórios, builds, runners antigos e
artefatos da raiz. Não usar `git add -A` nem limpeza genérica.

## AttemptWalk composto com o World vivo (2026-10-21)

O `odh-person` agora monta RoomObs/WallObs da arquitetura preparada, inventaria
portais Portal15 e aplica a política AttemptWalk sobre o inventário RoomInfo do
World após Main. House07 no tick 214 ficou `ready` na sala 3: 24 aberturas de
portal, 8 RoomObs, 128 WallObs e 153 retângulos totais; 13 entidades entraram
como obstáculos e o início não intersecta a lista. O teste sintético cobre
flags/caixas/EP5 e mutação durante callback. Debug completo e CTest 158/158
passaram; `linux_window_backend` foi pulado sem X11. A execução real do Main
terminou no limite documentado de ticks (código 3), com JSON válido e frame
CPU. `route_clearance`, estado mutável das portas e integração de movimento
permanecem falsos. Não declara jogabilidade. Evidência detalhada em
[`world-attempt-walk-validation.md`](world-attempt-walk-validation.md).

Próxima unidade: auditar lacunas entre a composição atual e o pedido Route45
real (lista de avatares, estado/footprints dinâmicos de portais e seleção de
obstáculos por sala), validar destinos de SLOT fora da posição inicial e então
conectar a busca sem chamar isso de movimentação até o Main consumir a rota.

## Corredor de destino sobre AttemptWalk vivo (2026-10-21)

`--attempt-walk-destination X Y` agora executa `route_rectangles` no snapshot
vivo de AttemptWalk quando a meta está na mesma sala. RoughBalanced seed-1
ordena o prefixo RoomObs/WallObs; bandas e entidades seguem depois. Em House07
Main tick 214, avatar 105 até o destino SLOT `(408,456)`, a busca encontrou
corredor com 7 retângulos gerados, 3 expandidos, 2.258 unidades de trabalho e
entradas `[392,440]`, `[399,449]`. O host declara raiz isolada e threads salvas
dormentes, embora exista outro avatar na sala; isso não modela as threads de
um runtime multissím completo. A referência Simitone mantém `RoomInfo.Entities`
ordenada por `ObjectID` em `VM.AddToObjList`; o snapshot vivo ODH enumera o
`World` e os obstáculos por ID crescente. O JSON agora confirma somente
`entity_obstacle_order_matches_reference` quando a sequência efetivamente
serializada é estritamente crescente. `entity_order_matches_reference` continua
falso: essa prova cobre a subsequência de obstáculos, não todas as entidades
da sala nem equivalência comportamental com TS1. Clearance, estado dinâmico de
portas e integração de movimento permanecem falsos. `optimize_rect_route_lines` agora
reproduz o corner-cutting e o ajuste opcional de entrada `dirIn` do
`OptimizeLines` Simitone, com HiP preservado e limites fail-closed. O
diagnóstico de rota House07 já chama a API e mantém os corredores bruto e
otimizado separados. O JSON informa
`incoming_direction_context=not_reconstructed`: o estado da rota-pai que
decide `dirIn` não está disponível neste probe. A origem foi confirmada:
`VMRoutingFrame.GetParentFrame()` procura outro `VMRoutingFrame` na pilha do
thread, enquanto o `odh-person` expõe frames BHAV do `Vm`; o destino solicitado
pelo probe também não veio de um Route45 ativo. Portanto, não há estado-pai ou
`PortalTurns` vivo que esse diagnóstico possa recuperar. Integrar um frame de
rota ao runtime real é pré-requisito para alimentar `dirIn` e validar
waypoints/movimento. Uma transcrição temporária
C#/.NET 10 das rotinas da referência confirmou
os casos reto, curva, gap e duplicata; não invocou Simitone compilado nem o jogo
original. Os casos de portal também coincidiram com harness C#/.NET 10 usando
MonoGame 3.8.4: projeção >=8, clamp com vários crossings, offsets positivos e
negativos e dois nós. ODH falha fechado no domínio em que as somas inteiras de
quadrados da referência transbordariam. Próxima unidade: investigar a ordem
implementação de frame de rota ligada ao Route45 real, com estado-pai,
`PortalTurns` e continuidade de execução. Não promover a saída geométrica a
clearance, execução de waypoints ou movimento.

## Probe resource-backed Door Route45 — 2026-10-21

No commit-base `330a2e5`, `tools/bhav.cpp` ganhou `--route-slot-probe` para
carregar o OBJD3114 (GUID985, MasterID1) e fornecer o SLOT128 real a Route45
Literal sem instanciar serviços Snap nem alterar os dados do jogo. O probe
executou `Doors.iff:4113` e `4115` no VM ODH, com atributo1 da StackObject
controlado. Valor0 selecionou a chamada no pc2/SLOT type3 índice1; valor1
selecionou pc1/índice0. Ambos resolveram o provider uma vez, moveram o avatar
no grid construído e retornaram true (53 e 48 ticks). O teste manual
`tests/door_route_probe.py --game-root '../referencias/The Sims'` valida as
duas ramificações. Build `odh-bhav`, 134 checks de routing e CTest 159/159
passaram; X11 foi pulado sem display.

Escopo da prova: BHAV e SLOT comerciais executados/consumidos no ODH; StackObject,
grade e clearance são construídos. Não valida o valor runtime do atributo da
porta, hierarquia multitile, interação natural, clearance dinâmico, Portal15 ou
equivalência ao executável original. Próximo: localizar e vincular uma instância
Door real no mundo/salvamento suportado, preservando a resolução de code owner e
MasterID, e testar a rota na sala ativa antes de integrar travessia de portais.

## Route45 Literal em Door multitile restaurada de House07 — 2026-10-21

House07 real contém Door25 (GUID985, master_guid458; grupo `[25,26]`). O
provider de `odh-person` agora permite parte multitile quando GUID/MasterID/
SubIndex do OBJD conferem com a definição indexada, existe master único e o
`master_guid` restaurado bate com ele; continua falhando fechado em dados
ausentes/ambíguos. Um Main de avatar temporário chamou Route45 Literal nessa
instância restaurada usando SLOT128. Bob (`105`) saiu de `[392,440,1,3]` e
chegou a `[408,424,1,1]` na Room3: 16 movimentos, 1 resolução e 1 carga de SLOT.
Com obstáculo movido durante a tentativa, o runtime cancelou após um passo e
retornou false (`next segment blocked during route`). O script é
`tests/house07_route45_integration.py --binary ./odh-person --game-root
'../referencias/The Sims' --stack-object-id 25 --literal-slot`.

Evidência: mundo/save House07 restaurado, geometria/snapshot dinâmicos e Door
multitile real; a chamada ainda é injetada em Main temporário. Não é interação
natural nem execução de Doors.iff:4113 dentro do save; Portal15 e equivalência
ao jogo original seguem abertos. Próximo: roteamento a partir da interação
normal selecionada, preservando a frame tree e code owner, depois portal filho
e reconstrução da grade após mudança de sala.

## Probe de Portal15 Door e perfil estrito de opcode 27 — 2026-10-21

Com `odh-bhav` e os recursos externos originais, a execução bounded de
`Doors.iff:4105` foi iniciada no VM sobre mundo construído com caller word 9=0,
StackObject OBJD15089 (GUID1017, SubIndex1, SLOT128) e atributo1=1. O trace
executa 4105, chama 4112 e 4115, depois chega a `Doors.iff:4112` pc5:
opcode27, operandos `[0,0,254,0,0,0,2,0]`. A referência FreeSO fixa
identifica opcode27 como `goto_relative`: cria SLOT type3 standing e agenda
uma nova frame de routing para o StackObject; aqui location `-2` é OnTopOf,
direction 0 SameDirection e flag 2 NoFailureTrees. O adaptador ODH usa uma meta
distinta de ponto exato; não equivale ao Route45 Literal.

O CLI `odh-bhav` ganhou opções de probe para inicializar palavras explícitas de
Caller e StackObject no mundo construído; os valores são apenas estado sintético
do harness, não valores restaurados do save. Testes comprovam execução
assíncrona somente quando o host valida um ponto exato livre; destino bloqueado
falha sem movimento e operandos fora do perfil são recusados. Na primeira
análise, o trace comercial foi interpretado como se parasse no opcode11; a
reexecução e a correção dessa leitura estão registradas abaixo. O probe ainda
não completa a interação nem valida a rota com o clearance de uma Door
restaurada do save.

### Reexecução headless da árvore de Portal15 (2026-10-21)

Reexecutei `odh-bhav` contra `Objects.far:Doors.iff:4105` e a ligação
`DoorGlobals.iff`, com OBJD15089, caller word 9=0 e atributo1=1 no StackObject
sintético. O trace completo (135 instruções) confirma que `GetDistanceTo`
(`4114` opcode11) é executado e a árvore segue até Route45; portanto a anotação
acima de que a execução parava no opcode11 estava incorreta. O probe termina
`returned_false`, com o diagnóstico apontando a falha anterior: o host rejeita
o ponto exato do StackObject em `[64,64]` porque essa célula pertence à região
bloqueada pela grade deliberadamente sintética. Essa grade não usa o lote
House07, footprint real da porta ou clearance resolvido por EntryPoint5; não é
evidência de que o portal comercial falha no jogo.

`tests/door_route_probe.py --binary ./odh-bhav --game-root '../referencias/The Sims'`
passou novamente com BHAV/SLOTs reais de `Doors.iff:4113`: atributo1=0
seleciona o ramo pc2 e termina em `[64,80,1,3]` após 53 ticks; atributo1=1
seleciona pc1 e termina em `[64,48,1,1]` após 48 ticks. Isso valida o roteamento
isolado de 4113 no mundo construído, não conecta a frame Portal15 nem prova uma
travessia entre salas. A investigação posterior com Door25 restaurada está
registrada na seção seguinte; não altere a semântica de opcode27 com base no
bloqueio da grade sintética.

## GotoRelativePosition sobre Door restaurada de House07 — 2026-10-21

`odh-person` não encaminhava opcode27 ao `Routing`, embora o controlador já
implementasse esse opcode; o Main de diagnóstico falhava como `unsupported
primitive 27`. O host agora encaminha 27 e 45 ao mesmo controlador, com teste
CLI sintético cobrindo a dispatch boundary. O script
`tests/house07_route45_integration.py --binary ./odh-person --game-root
'../referencias/The Sims' --stack-object-id 25 --goto-relative` injeta somente
um Main de diagnóstico; o World, avatar e Door25 vêm do House07 restaurado.
Depois da correção, o probe chega a `Routing` e retorna false sem movimento:
`candidate room 1 differs from prepared route room 3`. Portanto o alvo real da
Door25 está na sala1 enquanto a malha ativa do avatar está na sala3. Isso
confirma o limite de roteamento local, não bloqueio de clearance/EntryPoint5.
O caso mostra que mover a validação de sala ou aceitar a meta fora da malha não
resolve a travessia; falta planejar o grafo de portais e executar EntryPoint15
do próximo portal. A mesma ferramenta mantém `--literal-slot` e seu percurso
Route45 anterior.

`Routing` também separa diagnóstico de alvo fora da grade e rejeição pelo
validador de sala/clearance; os testes unitários cobrem ambos. Build de
`odh-person`/`odh-routing-tests`, CTest focal `routing` e `person_cli` passaram;
o probe House07 com opcode27 agora termina sem falha de VM e reporta o mismatch
de salas. Nenhuma travessia de portal foi executada.


## Diagnóstico completo do retorno Portal26 (2026-10-21)

O probe `tests/house07_route45_integration.py --portal-id 26 --ticks 214` agora
para no resultado terminal da ação e mantém trace separado pelo CodeOwner do
Portal15. Foram 49 instruções para `Doors.iff:4105`. A cadeia chega a
`4105 pc2 → 4113 pc1 opcode45`; o Route45 retorna falso porque o validador
recebe candidatos da sala1 enquanto a grade ativa é da sala3
(`candidate room 1 differs from prepared route room 3`). `4105` retorna falso
sem fault; ator termina em `[408,424,1,0]`, sem travessia. Portal25, a outra
peça do par, falha antes de mover (`routing start or pre-ranked goal input
invalid`) e deixa o ator em `[392,440,1,3]`. O código de processo 0 no teste da
peça26 significa probe completado, não sucesso da ação. Esta prova é do ODH com
House07 restaurado e ação host-scheduled, não interação natural nem resultado
do executável original. Próxima dependência: integrar a cadeia de salas ao
Route45/Portal15 e reconstruir a grade após a transição.

## Route45 com planejamento diferido entre salas (2026-10-21)

O Route45 agora tem uma fase suspensa por ticket raiz que consulta o inventário
live de portais, resolve a rota no grafo, valida e agenda EntryPoint15, confirma
a sala observada após o retorno e exige refresh da grade antes de continuar.
Retornos falsos excluem a aresta direcionada `(portal, sala destino)` e
recalculam a alternativa. Os contextos são limitados e preservam chamadas
Route45 aninhadas. `IgnoreRooms` evita a fase de portais e continua pelo
validador local da grade ativa, conforme o perfil Simitone examinado.

Build Debug limpa em `/tmp/odh-route-integrated`: compilação completa, CTest
160/160 (159 executados; `linux_window_backend` pulado sem DISPLAY), 308
checks em `odh-routing-tests` e `git diff --check` passaram. Isso cobre host e
fixtures sintéticas, não o executável original.

O probe real `tests/house07_route45_integration.py --portal-id 26 --ticks 214`
agora observa o Route45 chegar ao SLOT `[408,408,1,0]` em 32 movimentos,
partindo de `[392,440,1,3]`. O destino continua classificado como sala 1, mas
está dentro da grade de obstáculos preparada para a sala 3. O host agora usa
essa grade e sua máscara de walkability como gate geométrico, coerente com o
`AttemptWalk()` do Simitone, que reúne obstáculos da sala atual. O percurso não
agenda Portal15 (`portal_planned_calls=0`), portanto isso não demonstra
travessia entre salas.

Depois da chegada, o BHAV comercial continua; o leitor `RoomByTemp0` agora
consulta o cômodo principal calculado a partir das ligações entre cômodos no
mapa preparado. Para os índices 0 a 4, reproduz os campos usados pelo
Simitone; usa `clamp(Temp0 + 1)` e recusa mapas preparados sem identidade de
piso resolvida. O índice 1 consulta o estado externo do cômodo principal, não
o estado geométrico do cômodo selecionado.

O probe `--portal-id 26 --ticks 214 --expect-route-goal 408 408` termina a
chamada Portal26 com `returned_true`, sem erro, e mantém o avatar em
`[408,408,1,0]`. O host não agendou outra chamada Portal15 e registrou zero
mudanças de cômodo. Assim, validamos o caminho até a porta e o retorno positivo
da ação diagnóstica; ainda não validamos o Sim atravessando a porta para outro
cômodo nem uma interação natural pela janela do jogo. `--expect-route-goal`
continua afirmando a chegada separadamente do resultado posterior da ação.

Na revisão seguinte, a grade da chegada passou a usar uma janela de 256×256
unidades quando a sala inteira excede o limite de memória. A nova grade é
montada com os obstáculos do retrato completo da sala e recusa destinos fora
da janela. O probe House07 continuou chegando a `[408,408,1,0]` em 32
movimentos; a grade final corresponde à sala 1 e está marcada como reduzida.
Portal26 retornou true, mas o avatar continuou no mesmo ponto e não houve
mudança de cômodo.

### Retry limitado de EntryPoint externo (2026-10-21)

`Routing` agora oferece `RoutePortalRetryProvider`: após retorno false de uma
BHAV externa, o host pode recalcular uma alternativa com base na chamada e nas
posições atuais. Até 32 novas chamadas podem ser agendadas sob o ticket Route45
original; cada retorno continua validando geração da VM, ticket e CodeOwner.
Sem provider/alternativa, ou após o limite, o job completa false. Se a chamada
falha depois de mover o avatar, o host precisa confirmar refresh da grade antes
de planejar outra tentativa; sem refresh válido, o provider nem é consultado.
Depois desse refresh, o Route45 pai marca o trecho local como obsoleto e o
replaneja após o primeiro retorno bem-sucedido, mesmo se a tentativa alternativa
não mover o avatar. A fixture cobre o movimento x=4→8, retorno false, refresh,
alternativa estacionária true, retomada em x=9 e chegada ao goal original x=20.
Os testes sintéticos cobrem retorno false → alternativa com outro CodeOwner →
retorno true → rota local concluída, ausência de provider, limite de 32
alternativas, movimento sem refresh e invalidação do ator dentro do callback de
refresh; o controller revalida a identidade e geração após o callback.
`odh-routing-tests` passa 280 checks.
Isso implementa a fronteira de retry
descrita pelo Simitone, não o planejador de salas: o host ainda precisa excluir
a ligação falha, escolher o portal alternativo, resolver EntryPoint15 e fornecer
a chamada. A travessia
natural em House07 permanece pendente.

Validação histórica do checkpoint abaixo, anterior ao refresh de sala e à
janela local: build Debug completo e CTest 160/160 sem falhas;
159 executaram e `linux_window_backend` foi ignorado porque não há DISPLAY X11
acessível. `person_cli.py` passou. Reexecuções dos probes House07 mantêm o
resultado: Portal25 falha antes de mover com entrada de rota inválida, enquanto
Portal26 retorna false após 16 movimentos locais; nenhum dos dois cruza salas.

## Inventário de colisão incompleto na restauração — 2026-10-23

O teste integrado `avatar_candidate_cli.py` agora inclui um objeto cuja OBJD
mantém o prefixo e `LevelOffset`, mas termina antes dos campos de colisão.
EntryPoints permanecem disponíveis em OBJf, permitindo restaurar o lote e
chegar ao probe. O ODH marca RoomInfo como `unsupported/inventory_incomplete`,
não inventa interseções e recusa a auditoria de posicionamento; a grade de
movimento também permanece `unsupported`. Isso prova o bloqueio seguro para
esse caso sintético, não o comportamento do executável original. O CTest focal
`avatar_candidate_cli` passou.

## Texto de interface sem PangoCairo — 2026-10-23

A configuração local `ODH_LINUX_TEXT_RENDERER=OFF` compilava os painéis sem
qualquer desenho de texto; por isso seletores podiam mostrar só fundos e faixas.
O CMake agora usa uma fonte bitmap incorporada quando PangoCairo está desligado
ou ausente. A fonte cobre ASCII e translitera acentos latinos comuns; outros
alfabetos aparecem como `?`, portanto Pango continua necessário para a interface
multilíngue completa. Os nomes no seletor ficam transliterados nesse modo.

A build Debug completa com fallback passou; 219 testes headless passaram. Os
três testes que criam janelas X11 foram deixados de fora para não interagir com
o desktop. O teste isolado valida pixels de texto, clipping, UTF-8 malformado e
caracteres sem suporte. Uma prévia sintética foi inspecionada e as letras ficaram
legíveis. Isso explica e corrige os rótulos ausentes nos painéis, mas ainda não
confirma a imagem real da vizinhança ou a janela do jogo; validação visual em
X11 permanece pendente.
