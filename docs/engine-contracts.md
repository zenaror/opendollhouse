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

Uma ação pendente pode continuar citando um objeto que já foi retirado. Nesse
caso, a referência conserva o objeto antigo e sua geração para seguir o
encerramento previsto pelo jogo. Por exemplo, se um carro vai embora antes de
o Sim embarcar, sua ação não deve passar a usar outro objeto que recebeu o mesmo
número. Um alvo antigo preservado e um alvo sem identidade recuperável são
situações diferentes; a segunda continua sendo um erro.

Recurso desconhecido, ausente e presente mas vazio são situações diferentes.
A resolução de scripts locais, semiglobais e globais preserva a origem escolhida.
A existência de um arquivo com nome semelhante não autoriza trocar seu conteúdo.

O cadastro compartilhado de definições de personagens pertence à vizinhança.
Quando presente no formato pessoal v2, preserva a ordem do provedor, a identidade
da definição, origem, entrada do recurso, código de conferência do conteúdo e vínculo com o
número de vizinho. A restauração deve conferir o conjunto admitido e seus vínculos;
ordenar novamente por identificador, escolher outra definição de mesmo nome ou
refazer a numeração não constitui restauração equivalente. O corpo vivo continua
identificado por número e geração, separadamente desse cadastro.

Instalar, retirar ou substituir um recurso de personagem usado pelo cadastro exige uma
migração explícita quando altera o cadastro salvo. O caminho v2 atual recusa
essas divergências; não oferece migração automática. O caminho v1 permanece sem
catálogo e não é promovido a v2 apenas por ser reaberto. O contrato interno
experimental está em [native_personal_catalog.hpp](../include/odh/native_personal_catalog.hpp);
a existência desse contrato não declara uma API pública ou paridade completa.

## Animações ausentes

Um pedido comum de animação pode continuar no próximo passo da simulação quando
o catálogo comprova que o nome pedido não existe, conforme a referência TS1.
Nesse caso, a animação atual e o objeto carregado pelo personagem são preservados.
Por exemplo, um nome reservado por um comportamento não deve ser substituído por
outra animação parecida.

Essa conclusão exige conferir todas as seções dos arquivos binários admitidos.
Um arquivo desconhecido que possa conter o nome impede afirmar sua ausência.
Arquivo danificado, nome ambíguo, erro de leitura e limite de recursos continuam
sendo falhas. Movimento, alcance de objetos e restauração de animações salvas
mantêm seus próprios contratos; essa regra não autoriza ignorar seus erros.

## Objetos, posições e retirada

O estado espacial admite vários ocupantes no mesmo quadrado. A cadeia original
de objetos do lote e o registro espacial vivo têm funções diferentes; não
usar a cabeça de uma cadeia como substituto para todos os ocupantes.
Uma posição fora do lote comprovada também difere de metadados desconhecidos.

Os dados originais representam a posição especial fora do lote por
`(-32768, -32768, 1)`. Na entrada dos serviços de posicionamento, esse valor é
convertido para ausência de posição no mapa antes de preparar objetos carregados
e atualizar os registros. O nível original e a direção permanecem preservados.
Outras coordenadas negativas, valores incompletos e a mesma dupla em outro
nível não recebem esse tratamento. As consultas e notificações anteriores e
posteriores ao movimento continuam usando a sequência original.

Contenção física e vínculos virtuais são distintos. Limpar um vínculo legado
não deve colocar um móvel na mão de um Sim nem copiar coordenadas ou andares.
Retirar um objeto com conteúdo precisa conservar sua identidade durante as
consultas e posicionar os filhos pelos serviços reais. Criação, inicialização,
movimento e retirada precisam atualizar os mesmos donos e registros.

Na criação comum por um script, o objeto precisa existir e terminar sua
inicialização e seu posicionamento antes de o script continuar. Apenas a próxima
execução própria do novo objeto fica agendada. Adiar a criação até todos os
outros objetos terminarem pode fazer várias ações pedirem algo que já deveria
existir. As restrições de posição e de duplicação continuam sendo conferidas.

Uma ação chamada logo após a criação recebe o número do novo objeto no primeiro
argumento e no temporário 0, antes de executar. A referência de objeto do script
que a chamou permanece preservada. Por exemplo: se a inicialização remover um
objeto e outro reutilizar seu número, isso não autoriza executar a ação no
substituto. Esses contratos são internos e experimentais; não definem uma nova
API pública para mods.

Uma caminhada pode continuar consultando a posição e a direção do alvo que
acabou de sair do lote. Essas leituras usam a referência exata preservada,
incluindo sua geração, mesmo se outro personagem reutilizar o mesmo número.
Isso não permite mover ou alterar o alvo retirado: o Sim que caminha precisa
continuar presente, e as verificações de posição, obstáculos e permissões
permanecem obrigatórias. A direção de um objeto fora do mapa também vem do
próprio objeto, nunca de seu substituto.

Planejar uma passagem entre cômodos e caminhar até ela são etapas diferentes.
A primeira usa o mapa de passagens e confere se as portas e escadas ainda são
as mesmas, nas posições esperadas. A caminhada verifica então os obstáculos
e as permissões de passagem. Uma informação ausente continua sendo uma falha
de dados; ela não pode ser tratada como um caminho livre ou como ausência
comprovada de passagem.

Por exemplo, escolher a porta que liga a sala ao corredor não deve antecipar
todas as consultas de colisão dos móveis. Isso também não autoriza atravessar
um móvel quando a caminhada começar.

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

Um texto pode citar quem fala, um vizinho ou outro objeto. O
[serviço de textos de diálogo](../tools/dialog_text_substitutions.hpp) recebe
explicitamente esses donos e lê nomes de recursos autenticados da sessão.
Nome de arquivo não substitui nome visível. Textos que incluem outros textos
compartilham um limite de trabalho, inclusive quando o resultado final é vazio.

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

A gravação de personagens entre lotes segue a classificação do lote e a família
ativa. Por exemplo, um lote comunitário da Cidade Velha usa uma regra diferente
da casa da família. O [classificador compartilhado](../include/odh/original_lot_classification.hpp)
distingue residencial, comunitário, sem classificação e dados indisponíveis;
os consumidores usam esse resultado em vez de reinterpretar números do arquivo.
Esse contrato ainda é experimental, sem uma API pública estável para mods.

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

## Reservas de férias compartilhadas

As reservas pertencem à vizinhança. Cada família guarda sua identidade de origem
e seu número de reserva nesse cadastro; a cópia dentro de uma sessão serve para
conferir a restauração. Uma casa guardada antes da viagem não pode desfazer uma
reserva mais recente quando for reaberta.

Por exemplo: sair de casa com reserva zero e chegar ao lote 40 produz uma mudança
validada de zero para 40. A casa suspensa conserva seus bytes originais. No retorno,
a liberação da reserva exige a transição de retorno autorizada; abrir um save
comum não basta. Os seis valores de magia da família são preservados separadamente.

O [serviço de reservas](../include/odh/family_vacation.hpp) valida a identidade,
a sequência das alterações e os registros de todas as famílias. A publicação da
reserva e da referência para a sessão aceita acontece numa única atualização;
uma gravação antiga ou concorrente deve ser recusada, preservando a anterior.
Gravações antigas precisam de admissão explícita: ausência de dados não comprova
que todas as famílias estão sem reserva. O contrato permanece experimental e não
substitui a validação de uma viagem completa no aplicativo.

## Roupas dos personagens

Personagens antigos podem não ter as opções de roupa adicionadas pelas expansões.
O motor prepara uma cópia dessas opções pelas regras do original, usando idade,
sexo, corpo e tom de pele do recurso de origem. Os arquivos comerciais permanecem
intactos. Algumas opções são atualizadas em grupo; não completar cada campo por
uma regra independente.

Por exemplo, uma menina que viaja para um destino frio precisa da roupa infantil
correta. Usar uma roupa adulta, a roupa cotidiana ou esconder o corpo não substitui
essa atualização. Uma escolha personalizada existente é preservada quando a regra
do original não autoriza substituí-la.

As opções aceitas pertencem ao mesmo serviço de dados pessoais e acompanham o
salvamento e as viagens. Cada escolha identifica os recursos realmente usados pelo
desenho do personagem. Adicionar outra roupa ao catálogo não deve refazer uma escolha
salva; retirar ou modificar um recurso escolhido exige tratamento explícito.

O [produtor de roupas](../include/odh/wardrobe.hpp) prepara e confere as opções;
o [serviço de dados pessoais](../include/odh/native_personal_state.hpp) conserva o
estado. A consulta que desenha a aparência apenas lê esse estado. Esses contratos
são experimentais e não oferecem uma operação pública para comprar ou editar roupas.

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
