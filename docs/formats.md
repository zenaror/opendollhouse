# Formatos de dados

O OpenDollhouse lê os recursos clássicos do The Sims 1 para reconstruir o jogo
em C/C++. Os leitores são experimentais e trabalham com variantes explícitas.
Um formato reconhecido não significa compatibilidade com todas as suas versões,
nem execução completa dos comportamentos descritos por ele.

## Recursos e alcance dos leitores

| Formato | Uso e leitura disponível | Limites importantes |
| --- | --- | --- |
| FAR | Arquivos de recursos; versão 1, assinatura `FAR!byAZ`, nomes com comprimento de 32 bits e comprimentos de dados iguais | Outras variantes precisam de tratamento próprio; nomes repetidos não podem determinar precedência por acaso |
| IFF | Contêiner de recursos 2.0/2.5; cabeçalho de 64 bytes e cabeçalhos de bloco de 76 bytes | Ler a estrutura não interpreta automaticamente blocos desconhecidos ou seu mapa de recursos |
| GLOB | Nome do conjunto de comportamentos compartilhados; variantes Pascal e texto terminado em zero | A ligação depende do contexto e da identidade dos recursos, não de uma busca aproximada por nome |
| BHAV | Comportamentos nas versões `0x8000`/`0x8001`/`0x8002`; instruções de 12 bytes, oito operandos e metadados de argumentos e variáveis locais | A execução depende dos comandos e serviços já implementados; outras versões não são presumidas equivalentes |
| TTAB | Interações, condições, textos, permissões e valores anunciados; versões 4–8 e versão 9 com codificação 0/1 | As versões antigas 2/3 têm tratamento limitado; ler os campos não comprova a execução de uma interação |
| SLOT | Lugares de uso e apoio; layouts 4–10, tipos, índices e extensões conhecidas | A colocação exige interpretar unidades, orientação, regras e geometria; registros desconhecidos não viram posições padrão |
| STR# e textos relacionados | Nomes, descrições e traduções; formatos 0 a−3, com texto, comentário e código de idioma | Os leitores conservam bytes originais; conversão de caracteres e escolha de idioma são etapas separadas; formato −4 não é coberto por esse perfil |
| BCON | Constantes dos comportamentos; contagem, indicadores e palavras de 16 bits | A constante depende da tabela local, compartilhada ou global selecionada pelo script |
| OBJD / OBJf | Definições de objetos e pontos de entrada dos comportamentos; campos conhecidos de identidade, funcionamento, aparência e colocação | Os campos interpretados variam por versão; uma definição reconhecida não torna todo objeto executável |
| SPR2 / PALT | Imagens com paleta, cor, transparência e profundidade; SPR2 versão 1000 e seus comandos de compressão | Variantes adicionais e indicadores não interpretados exigem suporte explícito; a composição visual é outro serviço |
| SPR# | Imagens clássicas com paleta; cabeçalhos e decodificação por quadro nas versões 502/504/505/1000 | Paleta, dimensões, comandos e posição dos quadros são verificados; outras variantes exigem suporte explícito |
| DGRP | Conjuntos de imagens; versões 20000–20004, direção, zoom, rotação e deslocamentos | Seleção de imagem e leitura dos indicadores não comprovam composição correta em todos os casos |
| BMP | Imagens com cabeçalho DIB40, sem compressão, de 8 ou 24 bits; paleta, alinhamento e orientação | Outros cabeçalhos, compressões e profundidades de cor precisam de leitores próprios; transparência depende do uso do recurso |
| BCF | Leitura compartilhada de estruturas corporais, aparências e animações, inclusive combinadas no mesmo arquivo binário; os leitores próprios extraem a seção solicitada | Contagens e texto têm limites cumulativos; amostras CFP continuam externas. Ler o contêiner não comprova execução de todos os seus recursos nem suporte a variantes desconhecidas |
| CMX / SKN | Descrições de aparência e malhas textuais clássicas; conversão de malha para uma representação comum | Outros segmentos e regras completas de seleção de roupas continuam sujeitos ao alcance do motor |
| BMF | Malhas, ossos, faces, coordenadas de textura, pesos e transformações | Materiais e alguns campos brutos ainda exigem interpretação conforme o consumidor |
| CFP | Amostras de posição e rotação comprimidas; valores literais, repetições e diferenças | Leitura das amostras não substitui transformação de coordenadas, interpolação e aplicação à estrutura corporal |
| FWAV | Nome do evento de áudio e extensão do recurso | O nome sozinho não contém a amostra nem comprova reprodução; áudio também depende dos eventos, bancos e decodificadores |
| CARR | Carreiras, níveis, salários, horários, requisitos e efeitos nas necessidades | Textos traduzidos e variantes também usam tabelas de texto; nomes internos não substituem essas traduções |
| FAMI / NBRS | Famílias, vizinhos, dados pessoais e relações nos perfis conhecidos | Ler os registros não cria personagens nem resolve sozinho ocupação, identidade e vínculos |
| SIMI e recursos do lote | Globais, dados financeiros e partes da descrição da casa | Arquitetura, objetos e personagens exigem os recursos associados e preparação compatível |
| OBJM / OBJT | Estado e referências de objetos de partidas clássicas | Recuperar uma partida exige reconciliar definições, comportamentos e vínculos; dados desconhecidos não recebem valores inventados |

## Estrutura, identidade e texto

Os blocos IFF são identificados por quatro caracteres, preservando maiúsculas e
minúsculas. Os tamanhos e campos externos usam a ordem de bytes do formato;
os leitores não aplicam uma única ordem a todos os dados. Cada bloco é lido
dentro de sua própria extensão, sem ultrapassar o recurso seguinte.

No perfil FAR atual, os dados precedem o índice e o índice termina no fim do
arquivo. Uma variante diferente recebe uma mensagem de formato não suportado;
isso não significa, por si só, que o arquivo esteja danificado. Nomes de entradas
não autorizam escrita ou extração em caminhos arbitrários.

A leitura estrutural conserva campos brutos e extensões não interpretadas quando
o contrato do leitor permite. Conhecido, ausente, vazio e desconhecido são estados
diferentes. Um trecho incompleto não deve ser preenchido silenciosamente com zeros.

Nos textos, a escolha do grupo de idioma e a conversão para texto visível têm
regras próprias. O serviço de seleção clássico pode usar inglês quando o grupo
pedido está ausente; um grupo existente conserva seus índices, inclusive entradas
vazias. Um consumidor que ofereça outro tipo de fallback deve declará-lo.

## Limites de leitura

Os limites abaixo protegem o leitor contra tamanhos inválidos ou consumo excessivo.
São padrões configuráveis do projeto, não limites atribuídos ao jogo original.

| Recurso | Padrão de proteção |
| --- | --- |
| Índice FAR | 200.000 entradas; nome individual até 4.096 bytes; total de nomes até1 MiB |
| IFF | 100.000 blocos |
| TTAB | 4.096 interações e 256 registros de necessidades por interação |
| SLOT | 4.096 registros |
| Texto e animação | 65.536 entradas de texto; 4.096 clipes e 4.096 movimentos acumulados; limites próprios de propriedades e 4 MiB de texto por leitura |
| CFP | 1.048.576 amostras de ponto flutuante no conjunto de posições e rotações |
| Decodificação de imagens SPR2 | Limites próprios de quadros, dimensões, pixels e comandos; a leitura do índice não autoriza expandir todas as imagens |

Os leitores verificam tamanhos antes de alocar memória e rejeitam truncamento,
referências fora dos limites e valores inválidos conforme cada formato. Imagens
podem ser decodificadas por quadro, evitando carregar um arquivo inteiro sem
necessidade. O acesso de baixo nível compartilha um fluxo de leitura e não deve
ser usado simultaneamente por várias tarefas sem coordenação.

Os saves nativos do OpenDollhouse têm contrato próprio; ler um save clássico
não implica gravá-lo de volta no mesmo formato. Compatibilidade de conteúdo,
execução e salvamento precisam ser avaliadas em conjunto. Consulte o
[escopo jogável](linux-playable-scope.md), a [arquitetura](architecture.md) e as
[ferramentas](tools.md) para entender esses usos.

## Salvamentos nativos

Os arquivos ODHS guardam o estado nativo da sessão. Suas estruturas internas
possuem versões independentes; os números abaixo não são versões do jogo nem
do IFF. O leitor confere também referências, recursos e estados dos serviços.

| Estrutura | Extensão | Quando o gravador a utiliza |
| --- | --- | --- |
| World v13 | Segundo banco de indicadores dinâmicos de sprites, completando os índices 0–127 | Quando algum indicador do banco alto está ativo; os demais estados continuam escolhendo os layouts anteriores conforme seus campos |
| VM v9 | Metadados dos quadros importados de uma partida clássica, incluindo estado original da primitiva, mapeamento de retorno e usos de objetos | Quando a pilha contém esses metadados; a anotação de uma primitiva não cria uma espera nativa |
| PLAY v3 | Estado de animação importado, identidade por nome, peso e quadro estático da pose de transporte (`carry`) | Quando esses dados precisam da extensão; as animações de caminhada mantêm seu próprio conjunto de pistas |
| PLAY v4 | Estado do controlador de olhar: identidade, peso, rotações, fração e último passo | Quando o controlador possui estado, inclusive depois de seu peso voltar a zero |

O leitor aceita World nas versões 1–13, VM nas versões 1–9 e PLAY nas versões
1–4, dentro dos respectivos contratos e limites. A ausência do banco alto em um World antigo corresponde a
zero nesse banco; a ausência do novo controlador de olhar conserva seu estado
inicial. Outros metadados desconhecidos continuam sujeitos às regras de sua
versão. Isso não autoriza preencher campos arbitrários com zeros.

Retomar um ODHS restaura serviços e cursores já gravados. Não é uma nova
importação do save clássico, nem um pedido para repetir seus eventos de
animação. Uma versão antiga do motor pode recusar extensões mais novas;
conserve o perfil completo para voltar à versão anterior. Os contratos de
[importação e continuidade](engine-contracts.md#importação-de-partidas-clássicas)
explicam as verificações que acompanham esses formatos.
