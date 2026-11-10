# Contratos do motor

Estas regras ajudam a entender e revisar a implementação C/C++.
São contratos internos experimentais, não uma API pública estável.
Veja também [arquitetura](architecture.md), [formatos](formats.md) e [mods](modding.md).

## Consultar, preparar e executar

O mesmo serviço pode ser usado em momentos diferentes, com permissões diferentes:

| Momento | Estado usado | Efeitos permitidos |
| --- | --- | --- |
| Consultar uma ação no menu | Cópia da partida usada pela consulta | Ler os dados necessários e produzir o resultado da consulta. A consulta não altera a partida do jogador. |
| Preparar compra, movimentação ou venda | Cópia autenticada da partida | Executar as etapas síncronas admitidas e preparar o estado inteiro para confirmação. |
| Executar a simulação | Estado atual, filas e serviços próprios de cada personagem ou objeto | Executar as ações e persistir seus efeitos pelos serviços correspondentes. |

Ter uma operação implementada no motor não basta: cada consumidor precisa estar
ligado ao serviço e ao estado correto. Na preparação de objetos, relações,
consultas de distância e direção usam os dados da cópia, junto dos demais serviços
admitidos. Espera, diálogo ou outra operação sem suporte nesse contexto continua
sendo uma limitação explícita; sua presença no modo de simulação não autoriza
executá-la durante a preparação.

Os leitores usados pelo menu precisam observar a mesma cópia consultada. Isso
inclui os estados visuais dos objetos e os níveis necessários às expressões dos
scripts. Funções de escrita não podem ser emprestadas da partida em andamento.
Conferir essa origem faz parte da integração; copiar uma função, por si só, não
comprova isolamento.

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

## Roupas e leitura de uma partida

Um registro de roupas salvo precisa corresponder ao personagem e aos arquivos
que o originaram. A ausência desse registro também faz parte do estado: abrir
a casa ou recriar um visitante não deve gerar roupas, escolher outras peças
nem alterar o catálogo. Criar ou atualizar o registro exige uma operação
explícita, com as mesmas verificações de identidade e recursos.

A consulta [verify_wardrobe_source](../include/odh/native_personal_state.hpp)
confere um registro existente ou informa sua ausência. Ela não muda o estado.
O registro retornado pertence ao dono dos dados pessoais; depois de uma
alteração nesse dono, consulte novamente antes de usar a referência.

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

## Importação de partidas clássicas

A importação confere as definições, os recursos e as assinaturas de código antes
de escolher o procedimento de recuperação de cada classe. Uma classificação
interna de recuperação não substitui o tipo real do OBJD. Carga, retirada de
vínculos, reset e inicialização continuam exigindo seus serviços correspondentes.

Quando uma pessoa e suas dependências admitem continuidade, os planos preparam
a pilha da VM, a fila e os controladores do próprio personagem. Preparar não executa Main nem avança o
relógio. As rotinas de carga e recuperação pertencem às etapas do coordenador.
A publicação volta a conferir identidade, geração, recursos e estado anterior;
uma falha exige descartar a candidata, preservando a sessão aceita e os dados
originais. Uma pilha opaca não é substituída por uma espera fictícia de caminhada
ou animação.

Os valores atuais das necessidades e suas taxas contínuas são dados distintos.
Importar uma taxa não significa executar novamente a interação que a produziu.
Na retomada nativa, as frações e o relógio dos serviços vêm do estado salvo.

### Continuidade de animação

A animação atual conserva quadro, velocidade, direção, repetição, peso e cursor
de propriedades. Propriedades já atravessadas, inclusive no instante exato do
quadro salvo, são descartadas sem executar novamente som, troca de roupa ou gesto.
Um peso zero pausa o avanço e essas propriedades. Um nome de recurso preenchido
exige resolução válida; uma animação salva ausente não recebe um clipe parecido.

O clipe de transporte (`carry`), usado na pose de segurar objetos, conserva seu
próprio quadro. Ele é amostrado de forma estática, com peso visual um, sem avançar
ou disparar propriedades a cada passo. O campo de animação base do registro clássico não acrescenta uma pista:
o conversor TS1 de referência não o usa. O primeiro pedido real da mesma animação
atual reutiliza o recurso autenticado e o cursor, criando sua própria espera.
Um nome alternativo admitido pelo catálogo continua ligado à mesma identidade
de recurso, mesmo que o cabeçalho contenha outro nome canônico.

Esses estados têm versões próprias no [formato nativo](formats.md#salvamentos-nativos).
A retomada não reinicia quadros, filas ou eventos só para simplificar a carga.

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

A altura de um apoio tem um valor gravado e um valor usado pela simulação.
Os arquivos SLOT antigos não gravam esse campo; nesses casos, e quando o campo
original vale zero, a referência usa a classe padrão 5. O leitor conserva os
dados originais e [source_slot_height](../include/odh/slot_height.hpp) faz essa
conversão separadamente. Dados desconhecidos continuam sendo recusados. Os pontos
de apoio dos Sims seguem a regra própria dos personagens.

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

Ao iniciar um personagem, a fila de ações precisa reconhecer a mesma execução
do script antes de importar a continuação original. Essa admissão aceita somente
um início ainda não executado, com o personagem correto e os controles sem uso.
Ela não avança o relógio, executa instruções ou apaga ações. Uma ligação incorreta
é recusada antes de publicar a continuação.

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

Os scripts dessa preparação usam os serviços da cópia candidata, incluindo
relações, distância, direção, alvo da cabeça e alterações nas necessidades.
Alterações contínuas preservam a ordem normal do personagem; preparar uma compra
não avança a simulação para forçar um efeito. Virar o corpo exige o serviço de
caminhada e não recebe uma conclusão artificial nessa preparação síncrona.
O perfil finito de compatibilidade do Simitone continua explícito: uma operação
sem efeito na referência não comprova o recurso correspondente do jogo original.

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

## Continuidade após a saída de personagens

A casa pode continuar com visitantes e objetos, mesmo sem um morador ativo.
O salvamento deve distinguir os familiares cadastrados, os personagens ainda
presentes no lote e aquele que o jogador pode controlar. Não criar um morador
substituto para conseguir gravar ou reabrir a partida.

Retirar o corpo de um personagem também deve encerrar sua execução de ações.
Se outra ação ainda faz referência a ele, essa referência precisa continuar
identificando o mesmo corpo, sem autorizar novas ações nem confundi-lo com um
personagem criado depois. Essa retenção temporária não pode crescer a cada saída.

Por exemplo: depois que o último morador sai, o relógio e os objetos podem seguir
funcionando. Reabrir a casa não deve trazer esse morador de volta. Alterar o
cadastro da família é uma operação própria dos scripts do jogo; retirar um corpo
do lote não equivale a excluir sua família ou declarar sua morte.

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

## Aparência de personagens

Os dados do personagem escolhem sua estrutura corporal. Os tipos `adult` e
`child` usam as estruturas humanas correspondentes; `dog`, `cat` e `kat` usam
recursos de animais. `cat` e `kat` pertencem à mesma espécie, mas conservam o
nome literal do recurso solicitado. O motor não substitui uma estrutura ausente
pela de outra espécie.

A estrutura pode vir de um arquivo de expansão, enquanto as animações e as
texturas vêm de outros arquivos. Cada parte conserva sua origem. Estruturas,
aparências e animações de um BCF misto passam pelo mesmo leitor limitado;
nenhuma seção intermediária é pulada sem verificação.

Somente humanos recebem o conjunto de gestos das mãos. As seis posições desse
conjunto permanecem identificadas por lado e gesto. Um campo explicitamente
vazio não acrescenta uma peça naquele gesto, e não reutiliza a mão de repouso.
Campos preenchidos continuam exigindo aparência, malha e textura válidas.
Animais usam seu corpo e, quando disponível, uma cabeça separada. Uma aparência de cabeça opcional
comprovadamente ausente não acrescenta geometria. Isso exige um catálogo de
nomes completo: metadados BCF ou CMX não interpretados impedem essa conclusão.
Uma aparência presente continua exigindo malha e textura válidas, e nomes
ambíguos continuam sendo erro. O corpo obrigatório, a cabeça humana e os recursos
de gestos preenchidos permanecem estritos.

Esses serviços de criação e apresentação não definem adoção, comportamento,
controle ou participação do animal na família. Cada um desses fluxos precisa
de implementação e validação próprias.

### Aparência ao carregar e consultar

A restauração de um personagem confere seus recursos de aparência mesmo sem
abrir uma janela. Nesse caso, os arquivos vêm da instalação selecionada. Opções
de captura ou de desenho não são necessárias para localizar esses dados; um
recurso ausente continua causando uma falha de preparação.

Na carga TS1, a roupa atual passa pelo serviço de seleção de roupas e a cabeça
vem da tabela corporal da definição (BodySTR). Esses valores reaplicados durante
a carga prevalecem sobre os campos brutos de roupa e cabeça do registro OBJM.
Os acessórios admitidos são resolvidos como recursos reais. As tabelas de
caminhada e nado usam a idade viva dos dados pessoais; derivar a aparência não
reescreve idade, gênero ou o registro de roupas.

O controlador de olhar conserva alvo, transição, timeout e peso. Sua progressão
não exige uma janela; o desenho usa a estrutura corporal e a geometria disponíveis.
Consultar ossos para um efeito não despacha scripts nem avança a interpolação
visual. Ocultar o personagem com Hidden impede seu desenho, mas conserva a
instância e as consultas de ossos de um personagem posicionado.

Os indicadores de exibição e de fantasma vêm do World atual. ShowAsGhost tem
precedência sobre IsGhost na cor e transparência. Transparência não elimina os
critérios de profundidade e identificação usados na composição. O alinhamento
ao solo usa a inclinação real do terreno e o relógio de apresentação; um desenho
oculto não interpola essa orientação.

No perfil gráfico 2D, a altura usada para olhar um objeto vem de seus recursos
DGRP/SPR2. Os limites geométricos de malhas RC3D e seus caches no FreeSO não são
importados para fornecer essa altura. Isso delimita o provedor gráfico atual,
sem atribuir a ele equivalência com a reconstrução 3D da referência.

## Conferir uma mudança

Testes de componentes delimitam regras. A aceitação exige consumidores ligados
ao aplicativo, efeitos observáveis e persistência quando afetada. Confira também
cancelamento e falha. A [matriz](playable-feature-matrix.md) registra a cobertura
parcial; nenhuma regra deste guia declara o sistema inteiro concluído.
