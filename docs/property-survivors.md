# Preparação estrutural da casa após a saída

`prepare_property_survivors` prepara uma cópia privada dos objetos que ficam
na casa. Usa o World **atual**, as relações atuais, definições explícitas e a
lista completa de grupos, incluindo objetos de uma só parte. Não usa os
registros antigos importados como substitutos do estado atual.

A seleção inicial dos objetos a retirar é uma entrada explícita. Quem chama
deve obtê-la pela política de destinação auditada e manter fixos o salvamento,
os recursos e a autorização da família. Esta função ainda não faz essa ligação
com o salvamento da família nem decide o reembolso.

`prepare_property_checkpoint` faz a ligação com os bytes exatos do último
salvamento confirmado da ocupação atual. Reproduz o histórico da vizinhança,
confere o SHA-256, a família, a casa, o período de ocupação, o dinheiro e a
identidade dos recursos. Salvamentos antigos preservados para transferência
de moradores são recusados como fonte da casa atual.

Essa camada compara os moradores do salvamento com os GUIDs da família e só
então produz as aprovações de retirada com número, geração e GUID. A seleção
usa os dados atuais de permanência de **cada parte** dos objetos. Uma parte
qualificada pode retirar o grupo inteiro, mesmo quando sua parte principal
tem a marca de permanência. Isso segue o laço de exclusão identificado na
análise estática; a regra que examina somente a parte principal pertence ao
cálculo de valor, não a esse laço.

O resultado tem acesso somente de leitura ao candidato e guarda a identidade
do salvamento e da ocupação. `verify_overlay` compara o histórico completo
esperado com o observado; até uma alteração em outra família exige nova
preparação. Essa comparação em memória ainda não substitui a confirmação
atômica no armazenamento.

A preparação também exige a base inicial da ocupação, reconstruída pelo
chamador a partir da casa e dos recursos fixados. Compara os objetos atuais
com essa base: um objeto que desapareceu precisa ter registro de retirada;
um objeto novo precisa ter origem e reconstrução de recursos verificadas.
Partes de um grupo original devem permanecer ou desaparecer juntas. Os grupos
atuais são derivados dessa conferência, em vez de aceitar uma lista atual
montada separadamente.

Para objetos criados, a origem deve corresponder exatamente ao arquivo,
recurso e definição selecionados. Um construtor confiável confere os dados
fixos, como quantidade de lugares disponíveis e ações de entrada. Valores,
posição e conteúdo atuais continuam vindo do salvamento. A implementação de
criação suportada aqui produz objetos de uma só parte; novas criações com
várias partes exigem suporte próprio.

O resultado preserva separadamente os objetos criados que ficam, a origem
dos criados que são retirados, as retiradas de objetos da base e a retirada
dos Sims. Os números das retiradas anteriores conhecidas também ficam
reservados. Não é permitido conservar um objeto novo que já reutilizou o
número de um objeto retirado da base: dados antigos podem ainda usar esse
número sem guardar sua geração.

## Regras implementadas

- Retirar uma parte retira seu grupo completo. Retirar um objeto retira também
  todos os objetos que ele contém, com seus respectivos grupos e descendentes.
  A marca de permanência de um filho não interrompe essa retirada recursiva.
- Qualquer Sim atingido exige aprovação com número, geração e GUID exatos.
  Tanto o tipo nativo de avatar quanto o tipo 2 da definição ativam essa
  proteção. Visitantes não recebem autorização por serem avatares.
- Sims que permaneceriam na casa são recusados neste perfil: ainda precisam
  de uma política de reinicialização para a próxima ocupação.
- O candidato mantém os dados dos sobreviventes, grupos inteiros, gerações e
  reservas anteriores. Todos os números retirados ficam reservados.
- Relações entre objetos perdem somente fontes retiradas e destinos que
  correspondem exatamente aos números retirados. As matrizes de vizinhos são
  preservadas, inclusive chaves de 32 bits com o mesmo valor numérico.
- Os espelhos conhecidos de contenção e do próprio número do objeto nos
  bancos salvos são ajustados somente na cópia. Outros números iguais ao de
  um objeto retirado não são apagados por suposição.

O perfil exige relações de objetos com fontes atuais e destinos positivos
existentes de 1 a 32767. Fontes de gerações antigas, destinos fora desse domínio,
recursos ambíguos, grupos incompletos, objetos temporários da interface e limites
excedidos causam recusa antes de devolver um candidato. Os limites da origem
são conferidos antes das exclusões; uma retirada grande não encobre uma origem
que já excedia o orçamento. Nenhuma falha modifica a origem.

## Verificação e limite de uso

O teste `property_survivors` cobre cadeias de contenção que atravessam vários
grupos, proteção de atores, preservação de vizinhos e dados arbitrários,
referências inválidas, origem grande demais, esgotamento das reservas e
retirada de todos os objetos sem reiniciar a sequência de gerações.

O teste `property_checkpoint` usa arquivos de sessão sintéticos codificados
pelo formato normal para verificar a ligação ao salvamento atual. Cobre
alterações nos valores e marcas atuais sem mudar seus bancos antigos,
qualificação de uma parte secundária, recursos incorretos, dinheiro e ocupação
incompatíveis, moradores diferentes, corrupção, salvamento anterior, saída e
reentrada, além de alterações em outra família.

Os testes de proveniência incluem objetos novos preservados e retirados,
origem ou construtor incorretos, objeto sem origem, retirada sem registro,
grupo parcialmente retirado, reutilização de número antigo, mudanças nos
dados fixos e reservas herdadas. As definições de grupos já completamente
retirados também são conferidas, antes de descartá-los.

Esta é uma preparação **estrutural**, ainda sem autorização para ativar a casa.
Não copia filas, pilhas de execução ou serviços externos da sessão anterior.
Os campos duráveis que ainda podem conter referências precisam de auditoria;
não se afirma que a simples cópia do World reproduz os callbacks de exclusão
do original. Ainda faltam o histórico completo de objetos criados/apagados, a
arquitetura, a reconstrução de execução nova, o formato persistente e a
publicação conjunta com a saída e o dinheiro da família. Consulte o
[projeto de persistência](native-property-persistence-design.md).

Os grupos da base, suas reservas de números e as definições ainda precisam ser
reconstruídos pelo chamador a partir da casa e do conjunto de recursos fixados.
A identidade dos recursos no arquivo não autentica um índice arbitrário criado
em memória. A camada nova não dispensa essa proveniência, o vínculo com a revisão
persistente da propriedade ou a validação de execução nova.

O [histórico OHST](native-object-history.md) agora registra criações retiradas
e reservas durante as novas partidas. A preparação exige a marca de histórico
completo e preserva as origens retiradas herdadas do salvamento. Arquivos antigos
sem esse histórico, ou com a marca de incompleto, são recusados para preparar
a nova propriedade; as lacunas antigas não são preenchidas por suposição.
