# Avaliação nativa da construção

O cálculo C++ desta etapa inspeciona uma `SavedArchitecture` bruta, sem editar
a casa. Ele separa a estrutura das paredes, seus revestimentos, pisos e
piscinas. Não soma móveis, terreno, dinheiro da família nem aplica o
multiplicador de revenda. Não habilita reembolso ou despejo.

## Regra de referência

A análise estática de `0047b178`, na imagem de comparação com SHA-256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`,
estabelece estas regras:

- Percorrer os dois andares, ignorando uma célula quando a flag `0x20` do
  próprio andar estiver marcada.
- Somar os preços estruturais das faces de parede consultadas e dividir
  esse total por dois uma única vez, descartando a fração em direção a zero.
- Somar revestimento somente nas faces cujo estilo estrutural seja 1, 7, 8 ou 9.
- Somar o piso comum uma vez. Quando o piso for 255 e houver uma diagonal,
  consultar os dois campos de piso diagonal e dividir cada preço por dois
  separadamente, antes de somar.
- No térreo, uma célula cujo byte bruto de piscina seja menor que 16 soma o
  preço configurado por célula. O valor 16 indica presença de construção sem
  acrescentar preço; valores maiores não acrescentam esse componente.

O percurso de faces preserva uma particularidade do executável: processa a
menor flag presente, inclusive 32 quando ela é a menor; as faces seguintes
somente são processadas enquanto a flag for menor que 32. Máscaras mistas
não devem ser trocadas por um percurso incondicional das seis faces.

## Faces e vizinhos

As rotinas `00479737`, `0053bb44`, `004633e3` e `0046341b` expandem uma parede
bruta. A máscara de segmentos permanece igual. Os nomes abaixo seguem
`SavedWallTile`, sem girar ou normalizar a casa.

| Face | Estilo estrutural | Revestimento |
| --- | --- | --- |
| 1 | top_left_style da célula | top_left_pattern |
| 2 | top_right_style da célula | top_right_pattern |
| 4 | top_left_style do vizinho em x+1, se esse vizinho tiver face 1 | bottom_right_pattern da célula |
| 8 | top_right_style do vizinho em y+1, se esse vizinho tiver face 2 | bottom_left_pattern da célula |
| 16 | top_right_style da célula | bottom_right_pattern |
| 32 | top_right_style da célula | bottom_left_pattern |

Para o piso diagonal, os códigos consultados são `top_left_style` e
`top_left_pattern` da célula, independentemente do estilo estrutural diagonal.
Uma face 4 ou 8 usada sem o vizinho correspondente não tem estilo comprovado;
a inspeção nativa deve retornar não resolvida.

## Preços e limites

Os preços são fornecidos explicitamente pelo chamador, no domínio dos IDs
brutos da casa. Não usar diretamente IDs remapeados de `PreparedArchitecture`.
A identidade e a precedência dos recursos precisam estar resolvidas antes de
certificar o catálogo de preços.

Os provedores originais de piso e revestimento (`004a15df` e `004c6571`)
retornam zero quando uma consulta não encontra entrada. Isso é diferente de
não ter carregado o provedor: um catálogo incompleto não autoriza inventar zero.
O preço estrutural vem de uma tabela separada, e o preço de piscina tem uma
atualização em tempo de execução; seus valores no arquivo executável não
substituem a carga dos recursos. A ferramenta abaixo liga esses preços aos arquivos selecionados. Isso ainda
não comprova que todo estado de catálogo em uma partida original foi reproduzido.

A representação preparada para desenho não serve como entrada desta regra:
ela normaliza estilos, remapeia pisos, pode recortar a casa, usa flags do
andar térreo e converte piscinas em marcadores de piso. A avaliação precisa
preservar os dados brutos de ambos os andares.

O cálculo verifica dimensões, extensão dos bancos e catálogos, geometria de
vizinhos e aritmética de 32 bits. Dados não resolvidos não autorizam uma venda.
Esta é uma tradução baseada em análise estática; o jogo original não foi
executado para comparar resultados.

## Validação desta etapa

O teste `property_architecture_value` passou, cobrindo máscaras de parede,
vizinhos, exclusões por andar, pisos diagonais, piscinas, catálogos incompletos,
limites e estouro de valores. Uma revisão independente comparou a implementação
com as rotinas analisadas e não encontrou defeitos concretos nesse alcance.

A leitura da geometria bruta das Casas01, 07, 21 e 91 também foi aceita pelo
cálculo, com os quatro arquivos preservados por SHA-256. Esse teste usou
**preços fictícios**, declarados como tais: comprova a ligação com os leitores
de casas, sem validar preços comerciais, total de venda ou reembolso real.

## Inspeção com preços dos recursos

```sh
cmake --build build/native --target odh-property-architecture-value
./build/native/odh-property-architecture-value "$GAME_ROOT/UserData/Houses/House21.iff" \
  --build-iff "$GAME_ROOT/GameData/Build.iff" \
  --floor-globals "$GAME_ROOT/GameData/floors.iff" \
  --wall-globals "$GAME_ROOT/GameData/walls.iff" \
  --resource-far "$GAME_ROOT/ExpansionPack3/ExpansionPack3.far"
```

Acrescente `--resource-far` ou `--resource-file` para fornecer os arquivos
necessários. A ordem é explícita: a precedência dos nomes segue o catálogo de
[recursos de arquitetura](architecture-resource-catalog.md). A ferramenta não
procura diretórios nem modifica a instalação. A lista exata fornecida faz parte
do alcance da inspeção; ela não comprova cobertura de todo conteúdo personalizado.

Os preços usam o grupo inglês dos textos de catálogo. A função compartilhada
`architecture_price_from_strings` permite selecionar outro grupo, com fallback
por conjunto de idioma. Ausência, texto vazio, preço malformado ou valor fora
de 32 bits são erros; não se tornam zero. O formato aceito é decimal ASCII com
sinal e espaços opcionais, mais restrito que um conversor numérico permissivo.

Preços estruturais usam o perfil inicial da tabela de 256 entradas da imagem
analisada: zeros iniciais e as escritas auditadas de `004cd2d8`. Estilos
1/2/13/12/14 recebem as posições 0/3/6/9/12 de STR#`0x81`; estilo255 recebe o
preço do estilo1. O preço de piscina vem da posição0 de STR#`0x85`. A inspeção
de referências não prova ausência de toda escrita indireta durante o jogo;
este perfil permanece identificado como `comparison_stock_catalog_initialization`.
Estilos fora dessa tabela ficam não resolvidos.

Pisos e revestimentos globais usam os textos STR#`0x82`/`0x83` e exigem o
recurso gráfico correspondente. Materiais nomeados usam STR#0, posição1, do
arquivo FLR/WLL exato selecionado. A resolução reutiliza os mapas FLRm/WALm.
Nos IDs avançados 256–511, o byte inferior escolhe a entrada do mapa; o preço
continua associado ao número completo gravado na casa. Não são IDs internos
intercambiáveis do catálogo. Outros intervalos permanecem não resolvidos.

Uma declaração de material não resolvida impede usar silenciosamente o preço
global com o mesmo número. Campos de revestimento que não participam da
avaliação não abrem arquivos. O ID zero permanece sem remapeamento. As consultas
não certificadas continuam sem preço, em vez de receberem zero por conveniência.

A saída informa IDs sem preço, quantos materiais foram lidos e os componentes
do cálculo. Saída0 indica que este perfil resolveu a construção; saída3 indica
componentes não resolvidos; saída2 indica erro de formato ou recurso. Todas
mantêm `refund_authorized=false`; não incluem terreno, móveis ou multiplicador.

Os testes `architecture_price_metadata` e `property_architecture_value_cli`
passaram. A inspeção com preços reais dos recursos selecionados resolveu as
Casas01, 07, 21 e 91, incluindo dez materiais nomeados na Casa21 e onze na
Casa91. Os 40 arquivos de entrada conservaram seus SHA-256. Essa é validação
da leitura e do perfil estático, sem comparação executando o jogo original.
