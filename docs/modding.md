# Criar mods e expansões

O OpenDollhouse pretende permitir novos objetos, personagens, ações e lugares,
preservando a experiência do The Sims 1. Este guia explica as peças necessárias
para criar esse conteúdo e os cuidados que ele deverá seguir.

**Estado atual:** a compatibilidade com conteúdo clássico é experimental.
O kit público de criação e as interfaces para novas expansões são propostas
futuras. Ainda precisam ser definidos a linguagem de extensão, os pacotes,
as ferramentas de autoria e as regras de compatibilidade entre versões.
Os exemplos deste guia descrevem projetos de conteúdo; não são comandos
executáveis nem receitas de uma API já disponível.

## Conteúdo clássico e novas interfaces

Há dois caminhos relacionados:

- **Conteúdo clássico:** objetos e comportamentos nos formatos do The Sims 1.
  O motor precisa interpretar os recursos existentes, inclusive conteúdo
  personalizado, sem exigir uma alteração própria para cada objeto.
- **Novas extensões:** recursos que ultrapassam as possibilidades desses formatos,
  como novas regras de ocupação de lotes. Precisam de interfaces documentadas
  e de suporte correspondente na simulação, na interface e no salvamento.

Uma API é o conjunto de serviços oferecidos a quem cria conteúdo. Por exemplo,
pedir uma caminhada ou receber o resultado de uma escolha. As interfaces C++
internas do motor ainda podem mudar; sua existência não constitui um SDK público.
A separação entre simulação e serviços de plataforma é explicada na
[arquitetura](architecture.md).

## As peças de um conteúdo clássico

Os recursos trabalham em conjunto. Uma imagem pronta não torna um objeto usável.

| Peça | Para que serve |
| --- | --- |
| Definição do objeto, como OBJD | Identidade, categoria e referências aos demais recursos |
| Comportamentos BHAV | Condições, ações e etapas executadas pelo jogo |
| Tabela de interações TTAB | Liga as opções oferecidas ao jogador aos comportamentos |
| Textos STR# e tabelas relacionadas | Nomes, descrições, mensagens e traduções |
| Constantes BCON | Valores usados pelas regras do conteúdo |
| Desenhos, modelos e animações | Aparência, movimento e apresentação no catálogo |
| Posições de uso SLOT | Lugares de aproximação, apoio ou interação |

A [referência de formatos](formats.md) apresenta os leitores e suas limitações.
Conseguir ler um recurso não significa executar todos os seus comportamentos.
O alcance jogável está no [escopo do projeto](linux-playable-scope.md).

## O que você quer acrescentar?

| Conteúdo | O que preparar | Serviços necessários |
| --- | --- | --- |
| NPC, um personagem controlado pelo jogo | Identidade, aparência, função e rotina | Criação, procura, ações, horários e estado salvo |
| Objeto | Identidade, nome, aparência, preço, espaço ocupado e usos | Catálogo, colocação, compra, venda e interações |
| Ação | Participantes, condições, etapas e efeitos | Fila, caminhada, animação, conclusão e cancelamento |
| Opção de menu | Texto, condição de disponibilidade e ação chamada | Seleção, tradução e retorno da escolha |
| Cidade ou destino | Mapa, lotes, acesso e regras de visita | Viagem, chegada, atividades, retorno e salvamento |
| Tipo de lote | Ocupação, serviços, permissões e restrições | Regras compartilhadas pela simulação e pela interface |

### Um vendedor para uma loja

Descreva quando ele chega, o que vende e quando vai embora. Prepare sua aparência,
nome e falas. Dê ao jogo uma forma de reconhecer o vendedor já existente, para
não criar uma cópia a cada visita.

Sua rotina precisa encontrar o balcão, caminhar até ele, atender e esperar.
Também precisa lidar com balcão removido, caminho bloqueado e cliente que desiste.
Ao salvar e voltar à loja, sua identidade e suas tarefas devem continuar coerentes.

Adultos e crianças usam estruturas corporais próprias. Animais também exigem
aparência, ações e estado adequados: um gambá visitante não se torna membro
da família apenas por compartilhar características de outro animal.

### Uma cafeteira e a ação “Preparar café”

Prepare a definição, os desenhos, os textos, o preço e os lugares de uso.
A ação deve pedir ao motor que leve o Sim até a cafeteira, reproduzir a preparação
e aplicar o efeito definido pelo conteúdo.

O caminho pode estar bloqueado ou a máquina pode estar em outro andar. Use o
serviço de caminhada, incluindo portas e escadas, em vez de mudar a posição
manualmente. Defina o que acontece se a ação for cancelada ou o objeto for vendido.

Os efeitos têm suas próprias taxas e limites. Uma ação de higiene pode terminar
normalmente sem melhorar um Sim que já está acima do limite definido pelo objeto.
Não confunda “ação concluída” com “necessidade chegou ao máximo”.

### Uma opção de menu

“Conversar/Perguntar sobre a loja” pode organizar uma interação dentro da categoria
“Conversar”. São necessárias a frase traduzível, a condição para oferecê-la e a
ação chamada. Duas opções com o mesmo texto podem representar ações diferentes.

A condição que decide se a opção aparece é uma consulta. Abrir o menu não deve
gastar dinheiro, melhorar necessidades, avançar uma ação ou alterar os próximos
sorteios da partida. Por exemplo, verificar se o vendedor está disponível não
deve criá-lo nem começar o atendimento. Esses efeitos pertencem à ação escolhida.

A escolha e o nome mostrado na fila precisam acompanhar a ação ao salvar.
Uma opção no menu principal exige outro ponto de integração: uma interação de
objeto não concede acesso automático a todas as telas do jogo.

### Uma cidade e um novo tipo de lote

Uma feira precisa de mapa, lotes, acesso e regras de funcionamento. Defina quem
pode viajar, como chega e como volta. Uma loja da feira também precisa informar
seus serviços, horários e permissões de compra ou construção.

Imagine dois Sims visitando a feira e comprando um sorvete. A visita só está
completa quando ambos voltam para casa, o preço é cobrado uma vez e cada um
conserva seu inventário e suas necessidades após salvar e reabrir.

**Apartamentos são uma proposta futura.** Será necessário definir moradias e
áreas comuns, famílias residentes, portas, propriedade dos móveis, aluguel,
mudança e visitas. Acrescentar um nome à lista de lotes não implementa essas regras.

## Contratos que o conteúdo precisa respeitar

**Identidade.** Cada personagem e objeto precisa de uma identidade que não
colida com outro conteúdo. O número temporário de um objeto na partida pode
ser reutilizado depois de sua remoção; não o trate como identidade permanente.

**Móveis com várias peças.** Um sofá pode ter várias partes e um único preço.
Comprar, mover, girar e vender devem conservar o conjunto e cobrar uma vez.
A peça clicada, a peça usada como referência e a origem do preço podem ser
diferentes. Associações que mudam durante o jogo também precisam ser salvas.

**Posição e apoio.** Um abajur sobre uma mesa e um jornal na mão do Sim têm
vínculos de uso. Peça ao motor que coloque ou retire o objeto, para atualizar
posição, apoio, colisão e ocupação juntos. Apagar só a imagem deixa o estado incoerente.

**Dinheiro e efeitos.** Uma tentativa recusada não deve publicar uma compra
incompleta. O conteúdo precisa definir quando a cobrança ou o efeito acontece
e como responder a falhas posteriores. Não presuma que toda interrupção desfaz
automaticamente efeitos já executados.

**Salvamento.** Declare o estado que deve persistir, inclusive ações em andamento,
relações, inventário e escolhas. Ao atualizar um comportamento, o motor não pode
continuar cegamente uma ação no meio das instruções antigas. A ausência de um mod
precisa ter tratamento explícito, sem apagar silenciosamente o progresso.

**Erros.** Diferencie recurso ausente, dado desconhecido, ação indisponível e
resultado normal de falha, como um caminho bloqueado. Um texto realmente vazio
é diferente de um arquivo que não pôde ser lido. As mensagens devem ajudar o
criador a encontrar a causa sem inventar dados para esconder o problema.

**Apresentação.** Textos devem admitir tradução e descrições longas. Retratos e
indicadores leem o estado do personagem; desenhar uma imagem não deve avançar
a simulação nem alterar suas necessidades. Adultos, crianças e animais precisam
das imagens e estruturas correspondentes.

**Recursos e portabilidade.** Memória, processamento, arquivos e permissões
precisam de limites documentados. Uma extensão deve pedir serviços de som,
entrada e arquivos ao motor, sem depender de um sistema operacional específico.
Compatibilidade no computador não comprova funcionamento nos demais
[alvos de portabilidade](portability.md).

## Como será documentada uma interface pública

Cada interface deverá informar seu estado — proposta, experimental ou estável —,
versão, dependências, dados aceitos e resultados. Deverá trazer um exemplo
utilizável, explicar cancelamento e erros, mostrar o que fica no save e orientar
atualização ou remoção do conteúdo.

O exemplo deverá cobrir o ciclo completo: instalar, usar, interromper, salvar,
reabrir e remover. Recursos distribuídos pelo projeto precisam de licença
compatível; os dados comerciais continuam sendo fornecidos pelo jogador.
O [progresso](progresso.md) distingue o que já funciona das possibilidades futuras.

## Progresso dos personagens (experimental)

Um personagem pode sair de uma casa e aparecer em outra. Suas habilidades,
interesses, profissão e fama precisam acompanhá-lo. Já existe um serviço C++
compartilhado para esses dados; seu uso integrado no jogo ainda está em validação.
Ele é uma interface interna experimental, sem promessa de compatibilidade estável.

Imagine um objeto que ensina culinária. Ao terminar a aula, ele altera a habilidade
do participante e pede a gravação de seu progresso. A chamada clássica `Generic25`
faz esse pedido para o personagem que executa a ação. O motor confere quem é essa
pessoa e qual arquivo a define antes de aceitar a gravação. Retirar um personagem
do lote e preparar uma viagem familiar usam o mesmo serviço.

O registro pertence à vizinhança, para que abrir outra família não recupere uma
versão antiga do personagem. Atualizar seu progresso preserva a posição, o objeto
que ele segura, as ações em andamento e as necessidades atuais. Alterar quem mora
em uma família exige o serviço próprio de mudança; gravar uma habilidade não
transforma um visitante em morador.

Esse registro tem uma lista explícita de campos suportados. Ele não transporta
necessidades, roupas atuais ou referências a objetos de outro lote. O transporte
desses dados tem regras próprias; a existência do serviço não comprova o retorno
completo de acompanhantes ou a atualização visual de idade e aparência.

Os dados originais instalados permanecem intactos. O progresso é gravado no perfil
do jogador, com identificação de formato. Salvamentos antigos sem esse registro
continuam sem ele até uma gravação necessária. Uma versão antiga do motor pode
recusar um save que contenha um formato novo.

A interface interna está em [native_personal_state.hpp](../include/odh/native_personal_state.hpp).
A cobertura disponível continua indicada no [progresso](progresso.md).


### Viagens com acompanhantes (experimental)

Há uma implementação experimental das regras de transporte do jogo original,
ainda em validação dentro do aplicativo. Para experimentá-la, uma importação
nova pode usar `--original-companion-protocol`. A escolha fica registrada no
salvamento e não precisa ser repetida ao continuar essa partida.

Salvamentos nativos anteriores mantêm suas regras. A opção não converte um save
antigo: ela não consegue recuperar dados que deixaram de ser registrados no
passado. O objetivo da versão jogável é escolher o formato adequado sem exigir
uma opção de diagnóstico.

Imagine Ana convidando Bia para visitar uma feira. O jogo precisa reconhecer
Bia ao chegar e ao voltar, conservar seus dados e reaproveitar seu personagem
quando ele já existir. Criar outra Bia a cada chegada produziria uma duplicata.
Convidar para sair e convidar para casa também são escolhas diferentes; uma não
autoriza automaticamente a outra.

Uma ação de viagem deve usar o conjunto de operações compatível com o formato
da partida. Misturar marcadores antigos de ida e volta com os novos efeitos
pode confundir uma pessoa com uma informação sobre roupa ou fama. O motor
confere o formato em vez de tentar adivinhá-lo.

O espaço reservado aos efeitos da viagem é limpo pelas regras originais.
Um mod não deve usá-lo para guardar progresso permanente. Fora dessas ações,
os leitores preservam informações desconhecidas, sua ordem e a diferença entre
uma informação ausente e um zero registrado.

Trocar a roupa ao chegar exige o recurso correto de aparência. Se ele faltar,
o efeito é recusado sem consumir os dados como se a troca tivesse funcionado.
A implementação atual também recusa uma viagem familiar quando algum membro
selecionado não tem um personagem ativo autenticado; esse caso ainda precisa
ser ampliado. A existência do código não comprova todas as viagens, os efeitos
visuais ou a equivalência numérica completa ao jogo original.

A interface interna está em [original_companion_protocol.hpp](../include/odh/original_companion_protocol.hpp).
