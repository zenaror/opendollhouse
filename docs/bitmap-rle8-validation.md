# Leitura de BMP RLE8 para fundos da vizinhança

`read_bmp` agora aceita bitmaps indexados de 8 bits com compressão BMP
`BI_RLE8`, além dos perfis `BI_RGB` já suportados. O leitor interpreta linhas,
runs repetidos, runs absolutos com alinhamento de dois bytes e saltos; pixels
que o fluxo não escreve recebem a primeira cor da paleta, por decisão deste
leitor. RLE8 top-down, índices inexistentes, comandos truncados, dados fora
da imagem e streams sem marcador final são recusados.

Os limites padrão são 1.048.576 pixels e 8 MiB de bytes comprimidos. RLE4,
paletas com transparência e outras variantes de DIB continuam fora do perfil.
O decodificador produz pixels RGBA opacos; ainda não implementa a transparência
por cor-chave da interface de The Sims nem apresenta uma janela de vizinhança.

## Verificação sintética

O teste `bitmap` agora cobre RLE absoluto e repetido, alinhamento, direção das
linhas, saltos e preenchimento com cor zero; também rejeita truncamentos,
overflow de linha, índice fora da paleta, salto fora do mapa, ausência do
marcador final e orçamento excedido. Os fixtures são criados em memória e não
contêm bytes comerciais.

## Imagem original consultada

Os seis fundos de bairro identificados na interface do jogo estão no
`UIGraphics.far` local. O leitor ODH decodificou cada um por `odh-bitmap` como
800×600; comparação independente com Pillow deu pixels RGBA idênticos em todos
os **480.000 pixels de cada mapa**: `Nbhd\\NScreen.BMP`,
`Downtown\\DScreen.bmp`, `VIsland\\visland.bmp`,
`Community\\NScreen_unleashed.bmp`, `Studiotown\\DScreen.bmp` e
`Magicland\\DScreen.bmp`.

O cabeçalho de `Studiotown\\DScreen.bmp` anuncia 480.002 bytes RLE, mas o
arquivo acaba após 474.894 bytes de dados desde o início da imagem. O leitor
limita a leitura ao tamanho declarado do próprio BMP e exige o marcador final
RLE dentro desse espaço; não reserva nem lê os bytes excedentes anunciados.
Arquivos de imagem foram extraídos somente para `/tmp` durante a comparação; o
jogo e o repositório não foram modificados com os recursos.

Este resultado permite usar o fundo original numa futura tela de vizinhança,
mas não prova que os lotes estejam desenhados ou selecionáveis numa janela.
Coordenadas e seleção continuam no marco separado
[catálogo da vizinhança](neighborhood-catalog-validation.md).
