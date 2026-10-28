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
substituem a carga dos recursos. Esses provedores ainda precisam ser ligados
aos dados comerciais para validar valores reais de uma casa.

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
