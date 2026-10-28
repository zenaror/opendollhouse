# Histórico dos objetos criados e retirados

As sessões nativas de famílias agora incluem a seção obrigatória `OHST` v1
quando guardam histórico de objetos. Ela complementa `GEXP` v1, cujo formato
permanece igual. Leitores que não conhecem `OHST` devem recusar esse arquivo,
em vez de abrir a partida descartando as reservas dos números de objetos.

O histórico contém:

- uma marca explícita de histórico completo ou incompleto;
- a origem dos objetos criados e depois retirados: número, geração, GUID,
  arquivo, recurso e definição;
- os números reservados que não podem ser usados por objetos novos.

O limite é de 4096 origens retiradas e 4096 números reservados. Os números
devem ser positivos, únicos e ordenados, sem corresponder a objetos atuais.
As retiradas de objetos originais e as criações retiradas precisam ter seus
números reservados. Identidades e gerações contraditórias são recusadas.

## Durante o jogo e ao reabrir

`World::retire` prepara o espaço para a reserva antes de retirar o objeto.
Falta de capacidade ou de memória nessa preparação não altera o World.
A função retira somente um objeto; o chamador ainda é responsável por parar
sua execução e pela política de conteúdo e grupos.

O host registra a origem antes de iniciar as ações do objeto criado e transfere
essa origem para o histórico quando ele é retirado. Uma falha posterior na
inicialização também conserva a origem quando a retirada pode ser confirmada.
Se a criação ou a limpeza deixarem um resultado incerto, o host interrompe a
execução e recusa salvar esse estado parcial. O último save confirmado fica
disponível para recuperação.

Ao carregar, as reservas são instaladas antes de restaurar o World e as novas
criações continuam usando gerações posteriores. Objetos temporários da interface
seguem sua política própria; esse histórico cobre os objetos de jogo.

## Saves antigos

A ausência de `OHST` continua sendo reconhecida como formato antigo.
Uma partida nova começa com histórico completo. Reabrir um arquivo antigo
nunca inventa as origens apagadas: os próximos saves mantêm a marca de
histórico incompleto, preservando o que passou a ser conhecido. A marca
incompleta também não é promovida ao reabrir.

Arquivos antigos que já reutilizaram um número de objeto retirado podem ser
lidos pelo perfil anterior, mas essa sobreposição não pode ser gravada como
histórico novo válido. A preparação da propriedade após a saída da família
exige histórico completo; arquivos sem essa evidência são recusados nessa
operação. Isso não os torna equivalentes a uma casa desocupada válida.

## Verificação e próximos limites

Os testes `world`, `session_bundle`, `household_session_restore` e
`property_checkpoint` cobrem retirada e reserva, formato inválido, compatibilidade,
completude e criação seguida de exclusão antes de salvar e reabrir. A reconstrução
normal da sessão preserva a origem retirada e impede reutilizar seu número.

Na validação Linux com dados da Casa 07, o Sim 105 iniciou a ação da geladeira
com o Sim 47 selecionado. Salvar no passo 256, reabrir sem avançar e continuar
até o passo 4096 produziu as mesmas 14 seções do save que a execução contínua;
a ação terminou com sucesso nos dois caminhos. O histórico final continha
duas criações retiradas. Uma nova reabertura desse save final preservou todos
os bytes, incluindo as duas origens. Os 25 arquivos originais selecionados
para conferência mantiveram seus hashes. Nenhuma janela ou tela virtual foi
aberta. Isso valida esse cenário nativo; não comprova o ciclo de desocupação
e compra por outra família nem a paridade completa com o jogo original.

Esse histórico não resolve sozinho referências em dados desconhecidos,
reinicialização de visitantes, arquitetura ou publicação da casa desocupada.
Essas etapas e a atualização conjunta do dinheiro e da propriedade continuam
no [projeto de persistência](native-property-persistence-design.md).
