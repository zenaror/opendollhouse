# Contratos do motor

Estas regras ajudam a entender e revisar a implementação C/C++.
São contratos internos experimentais, não uma API pública estável.
Veja também [arquitetura](architecture.md), [formatos](formats.md) e [mods](modding.md).

## Identidade e recursos

Um objeto é identificado por número e geração. Referências antigas não podem
atingir um novo objeto que reutilizou o mesmo número. Ao atravessar saves ou
lotes, personagens também precisam ser autenticados por sua identidade de
vizinho, definição e origem do recurso. Não usar o Sim selecionado como dono
implícito de todos os serviços do lote.

Recurso desconhecido, ausente e presente mas vazio são situações diferentes.
A resolução de scripts locais, semiglobais e globais preserva a origem escolhida.
A existência de um arquivo com nome semelhante não autoriza trocar seu conteúdo.

## Objetos, posições e retirada

O estado espacial admite vários ocupantes no mesmo quadrado. A cadeia original
de objetos do lote e o registro espacial vivo têm funções diferentes; não
usar a cabeça de uma cadeia como substituto para todos os ocupantes.
Uma posição fora do lote comprovada também difere de metadados desconhecidos.

Contenção física e vínculos virtuais são distintos. Limpar um vínculo legado
não deve colocar um móvel na mão de um Sim nem copiar coordenadas ou andares.
Retirar um objeto com conteúdo precisa conservar sua identidade durante as
consultas e posicionar os filhos pelos serviços reais. Criação, inicialização,
movimento e retirada precisam atualizar os mesmos donos e registros.

## Scripts e tempo

Ações, consultas e filas têm estados próprios. Uma consulta descartável não
pode modificar a fila real por conveniência. Cancelar precisa respeitar a fase
da ação e liberar seus recursos. Um efeito de necessidade não equivale ao saldo
final depois de um longo intervalo: o desgaste natural também altera o valor.
Partes fracionárias e taxas fazem parte do estado salvo quando usadas pelo motor.

Diálogos devem usar o texto e idioma efetivamente selecionados. Todos os campos
selecionados precisam ser interpretados, inclusive botões não visíveis naquela
variante. Valor monetário literal não é uma macro desconhecida. Uma resposta
pertence à janela e ao pedido corretos, sem aceitar qualquer aviso pendente.

A lista de contatos do telefone usa as relações atuais do personagem e nomes
dos recursos de personagem no idioma escolhido. Mostrar uma pessoa nessa lista
não significa que ela aceitará um convite. O [provedor de contatos](../tools/phonebook_choices.hpp)
recebe esses dados como serviços separados; ele deve informar quando não consegue
montar a lista, sem inventar nomes ou esconder contatos silenciosamente.

## Persistência e publicação

O conjunto de sessão guarda entidades, referências, scripts, filas e serviços.
Versões e seções necessárias devem ser validadas antes da restauração. Campo
legado desconhecido não é zero conhecido. Não fabricar dados para tornar um
save antigo aparentemente compatível.

Mudanças de propriedade, compra e construção trabalham sobre um estado candidato.
Conferir identidade, custo, recursos e restrições antes de publicá-lo. A operação
não pode sobrescrever uma gravação mais recente ou deixar uma alteração parcial
como sucesso. Perfil completo inclui o registro de ocupação, além do save.

Ao conferir uma casa desocupada, mantenha uma cópia independente da importação
original, incluindo os dados dos objetos e seus grupos. Comparar a casa editada
com ela mesma pode fazer um objeto criado legitimamente parecer inválido.
Essa referência é usada tanto na saída quanto na entrada de outra família.

Por exemplo: se um sofá foi retirado na mudança, carregar a casa para o próximo
morador não pode recriá-lo. Primeiro restaure os objetos que ficaram, o relógio
e suas ações salvas; depois crie os novos moradores. O preço da compra deve vir
da avaliação da casa após a retirada, com a cobrança publicada uma única vez.
Os [serviços de propriedade residencial](../tools/residential_property_scene.hpp)
conferem a origem desses dados antes de admitir o novo estado.

## Viagem temporária

A casa suspensa permanece a referência até a ativação válida do destino. O lote
de destino é preparado com os recursos corretos, e os viajantes entram quando os
scripts os solicitam; não duplicar a família antecipadamente. Membros permanentes
e participantes da viagem podem ser conjuntos diferentes.

Inventário de viagem tem convenção e versão explícitas. A referência Simitone e
o executável original não têm necessariamente o mesmo protocolo: não detectar
um formato por adivinhação de chaves. Companheiros e efeitos dependem da marcação
real dos dados. Não aplicar efeitos a todos os familiares para compensar um
resultado ainda não explicado.

O retorno restaura a mesma residência e transfere somente o estado pessoal com
regra estabelecida. Preservar dinheiro, necessidades, relações e progresso sem
importar identidades de objetos do destino para a casa. Diferenças pessoais ainda
não resolvidas devem bloquear a publicação, em vez de serem descartadas.
Uma falha de chegada/retorno conserva o último estado recuperável.

## Construção e apresentação

Alterações de arquitetura precisam atualizar geometria, cômodos, passagem,
valores, materiais e desenho de maneira coerente. Móveis com várias peças têm
identidade, posição e preço de grupo; vender uma peça não pode deixar membros
sem dono ou cobrar o grupo novamente.

A apresentação usa o estado real: seleção, humor, fila, idade, aparência e destino.
Retratos fixos não são a cabeça móvel do menu de ações. A interface original
exige proporção dos recursos e comportamento observado; um protótipo não
estabelece regra visual. Gráficos e áudio permanecem substituíveis por plataforma.

## Conferir uma mudança

Testes de componentes delimitam regras. A aceitação exige consumidores ligados
ao aplicativo, efeitos observáveis e persistência quando afetada. Confira também
cancelamento e falha. A [matriz](playable-feature-matrix.md) registra a cobertura
parcial; nenhuma regra deste guia declara o sistema inteiro concluído.
