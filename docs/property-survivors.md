# Preparação estrutural da casa após a saída

`prepare_property_survivors` prepara uma cópia privada dos objetos que ficam
na casa. Usa o World **atual**, as relações atuais, definições explícitas e a
lista completa de grupos, incluindo objetos de uma só parte. Não usa os
registros antigos importados como substitutos do estado atual.

A seleção inicial dos objetos a retirar é uma entrada explícita. Quem chama
deve obtê-la pela política de destinação auditada e manter fixos o salvamento,
os recursos e a autorização da família. Esta função ainda não faz essa ligação
com o salvamento da família nem decide o reembolso.

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

Esta é uma preparação **estrutural**, ainda sem autorização para ativar a casa.
Não copia filas, pilhas de execução ou serviços externos da sessão anterior.
Os campos duráveis que ainda podem conter referências precisam de auditoria;
não se afirma que a simples cópia do World reproduz os callbacks de exclusão
do original. Ainda faltam a proveniência de objetos criados/removidos, a
arquitetura, a reconstrução de execução nova, o formato persistente e a
publicação conjunta com a saída e o dinheiro da família. Consulte o
[projeto de persistência](native-property-persistence-design.md).
