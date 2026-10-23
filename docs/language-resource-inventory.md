# Inventário inicial de idiomas

Este levantamento confirma que a instalação de referência contém textos de
interface em vários idiomas. Ele não comprova que todas as frases tenham
tradução, que o ODH já use esses idiomas no runtime ou que a interface esteja
pronta para lançamento.

## Arquivo verificado

- Origem: `../referencias/The Sims/GameData/UIText.iff` (fora do Git e preservado).
- Tamanho: 2.300.586 bytes.
- SHA-256: `e73b8f48ddfbbb9c476854b131aa68d4715495a1b7b88f4b23c841f399d95f44`.
- Revisão do ODH: `95ede6017c53aab4d2573dd4504d0997c8d6a988`.

## Resultado

O arquivo tem 74 chunks `STR#`, todos no formato `-3`, com 18.461 entradas ao
todo. Esse formato guarda um código de idioma em cada entrada. A leitura direta
do arquivo encontrou estes números de entradas por código:

| Código | Idioma conforme o enum STR do Simitone | Entradas |
|---:|---|---:|
| 1 | Inglês (EUA) | 1.071 |
| 2 | Inglês (Reino Unido) | 816 |
| 3 | Francês | 988 |
| 4 | Alemão | 988 |
| 5 | Italiano | 988 |
| 6 | Espanhol | 988 |
| 7 | Holandês | 988 |
| 8 | Dinamarquês | 988 |
| 9 | Sueco | 988 |
| 10 | Norueguês | 988 |
| 11 | Finlandês | 988 |
| 12 | Hebraico | 383 |
| 13 | Russo | 383 |
| 14 | Português | 988 |
| 15 | Japonês | 988 |
| 16 | Polonês | 988 |
| 17 | Chinês simplificado | 988 |
| 18 | Chinês tradicional | 988 |
| 19 | Tailandês | 988 |
| 20 | Coreano | 988 |

O enum do Simitone também define 21 como Eslovaco, marcado como acréscimo do
FreeSO; esse código não apareceu neste arquivo. Isso não prova sua ausência em
outros arquivos da instalação.

## Como foi contado e limites

A contagem percorreu os chunks do arquivo IFF e, em cada `STR#`, leu o formato
assinado, o número de entradas e o byte de idioma de cada entrada. A estrutura
confere com o leitor ODH em `src/animation.cpp` (`read_strings`): formato `-3`
contém idioma, texto e comentário terminados por NUL. Os nomes dos códigos vêm
do enum `STRLangCode` em
`refs/upstream/Simitone/FreeSO/TSOClient/tso.files/Formats/IFF/Chunks/STR.cs`.

As quantidades mostram quais idiomas aparecem, mas não garantem que cada tabela
tenha todos os textos em todos os idiomas. O inventário não avalia caracteres,
fontes, alinhamento ou aparência na tela. Uma amostra de entradas não ASCII
mostrou que os bytes não são UTF-8 de forma geral: o código 14 inclui
`53 6f 66 e1 73` (decodificado como `Sofás` com Windows-1252); o código 15
inclui `83 7c 81 5b 83 59` (CP932, `ポーズ`); o 17 inclui `d4 dd cd a3` (GBK,
`暂停`); o 18 inclui `bc c8 b0 b1` (Big5, `暫停`); o 19 inclui
`e0 a1 c1 cb c2 d8 b4` (CP874, `เกมหยุด`); e o 20 inclui
`c0 cf bd c3 20 c1 a4 c1 f6` (CP949, `일시 정지`). Essas decodificações são
amostras confirmadas, não uma auditoria de todas as tabelas ou um mapa completo
de codificação. Os parsers preservam os bytes sem converter para Unicode; a
borda Linux agora converte rótulos escolhidos para UTF-8 e usa PangoCairo para
desenhá-los na cena. Na primeira execução, um seletor percorre os 20 idiomas
encontrados com Tab/Shift+Tab e confirma com Enter; com Pango também aceita
clique numa linha. Sem Pango, o título da janela mostra o idioma enquanto o
jogador percorre a lista. O código escolhido fica na configuração privada do
usuário quando a sessão inicia. A janela X11 do seletor ainda não recebeu uma
validação visual nesta etapa.

A checagem rápida da primeira build deve usar poucos exemplos que representem
codificações diferentes (por exemplo, português, japonês e chinês simplificado),
confirmar os glifos na tela e verificar o fallback para inglês quando faltar
uma frase. Não precisa comparar todas as traduções.

Uma checagem automática preliminar foi executada no `UIText.iff` acima usando
o leitor de `STR#` do ODH e o conversor Linux. Ela percorreu os 74 chunks e
converteu quatro textos não vazios de cada grupo encontrado: 80 exemplos no
total, sem substituições U+FFFD. As amostras de português (`Pausado`,
`Clique para centralizar`, `Cadeiras de Jantar`), japonês (`ポーズ`,
`クリックで中央`, `ダイニング椅子`) e chinês simplificado (`暂停`,
`请点击屏幕中央`, `餐椅`) ficaram legíveis após a conversão. Isso confirma a
conversão dessas amostras, não a apresentação visual pela janela nem a
qualidade de todas as traduções. Os quatro exemplos dos grupos 12 e 13 eram
frases ASCII em inglês; portanto, a passagem sem U+FFFD não valida CP1255 ou
CP1251 nem confirma texto localizado nesses grupos. O primeiro grupo também
começa com valores numéricos, então a checagem da build deve escolher rótulos
reais de menu em vez de confiar apenas nas primeiras entradas da tabela.

Para isso, consultei os mesmos índices em tabelas de uso visível: `STR# 148`,
índice 0 (`Paused`), `STR# 156`, índice 0 (`Click to center`), e `STR# 200`,
índice 0 (`Dining Chairs`). Português, japonês e chinês simplificado retornam
respectivamente `Pausado`/`Pause`/`暂停`, `Clique para centralizar`/`クリックで中央`/
`请点击屏幕中央` e `Cadeiras de Jantar`/`ダイニング椅子`/`餐椅`. Também confirmei
um caso de ausência real para exercitar o fallback: `STR# 141`, índice 5 tem
`*Close Photo Album` em inglês e não tem entrada francesa; o lookup com
fallback retorna o texto inglês. Esses exemplos validam leitura, índice,
fallback e conversão dos bytes; a faixa atual ainda não mostra esse caso dentro
da cena porque o menu de pausa e outras telas do jogo seguem incompletos. A
escolha de idioma acontece antes de abrir a vizinhança.

## Decodificação inicial para apresentação no Linux

O código de idioma do STR# não é, por si só, uma codificação. O novo helper
`tools/linux_text_codec.cpp` converte bytes em UTF-8 somente na camada Linux;
os parsers e o núcleo continuam preservando os bytes originais. O mapa inicial
e sua força de evidência são:

| Grupos | Conversor usado | Evidência |
|---|---|---|
| 1–11 e 14 | CP1252 | Amostras legíveis nos grupos 3–11 e 14; grupos 1–2 têm amostras limitadas |
| 12 | CP1255 | Convenção provável para hebraico; ainda sem frase hebraica confirmada nos dados consultados |
| 13 | CP1251 | Convenção provável para russo; ainda sem frase cirílica confirmada nos dados consultados |
| 15 | CP932 | Amostra `ポーズ` decodificada |
| 16 | CP1250 | Amostra polonesa decodificada |
| 17 | CP936/GBK | Amostra `暂停` decodificada |
| 18 | CP950/Big5 | Amostra `暫停` decodificada |
| 19 | CP874 | Amostra tailandesa decodificada |
| 20 | CP949 | Amostra coreana decodificada |

A suíte `linux_text_codec` cobre uma amostra para cada família acima, bytes
inválidos, idioma desconhecido e limite de saída. A suíte `linux_text_renderer`
desenha textos de português, japonês, chinês simplificado, hebraico e tailandês
em um quadro sintético com PangoCairo; verifica pixels alterados, glifos ausentes,
UTF-8 inválido e limites geométricos. A prévia Linux também desenha o rótulo da
ação selecionada numa faixa sobre a cena. Esses testes provam composição e
fallback para as fontes instaladas neste computador, sem validar a captura da
janela X11, todas as traduções ou outros computadores. O seletor de primeira
execução já está ligado ao launcher e à configuração; ainda falta validá-lo
visualmente e oferecer uma forma dentro do jogo para mudar o idioma depois. Os
idiomas 12 e 13 exigem confirmação com texto
localizado real antes de declarar seus dados totalmente decodificados.

### Uso atual nas telas Linux

As confirmações de compra e despejo agora leem textos de `GameData/UIText.iff`:
o título de compra em `STR# 132`, índice 0, a pergunta de compra no índice 1,
o título de despejo em `STR# 131`, índice 2, e os rótulos Sim/Não em `STR# 142`,
índices 2 e 3. A busca usa o idioma escolhido e recorre ao inglês quando faltar
a frase; se `UIText.iff` não estiver disponível, usa os textos em inglês
embutidos. Os valores em português e francês foram conferidos na cópia
comercial. O seletor de famílias também usa `STR# 128`, índices 8 (título) e 3
(ação de selecionar a família); os valores em português e francês foram
conferidos. Essa ligação ainda não traduz o restante dessas telas: nomes,
preços, instruções de teclado e outros rótulos continuam em inglês. O texto de
despejo com patrimônio líquido não foi usado porque exige o nome real da família
e o total correto. Também não é uma validação visual da janela; essa checagem
aguarda a dependência gráfica Linux indicada acima.

## Referência de fontes

A leitura do frontend Simitone fixado encontrou fontes MSDF carregadas de
atlas MonoGame em
`refs/upstream/Simitone/Client/Simitone/Simitone.Client/SimitoneGame.cs:233–249`.
O `MSDFFont` do FreeSO
(`refs/upstream/FreeSO/TSOClient/FSO.UI/Framework/MSDFFont.cs`) prevê fontes de
fallback, mas os intervalos e atlas inspecionados não cobrem kana nem ideogramas
japoneses/chineses; os `.xnb` são
específicos de MonoGame e não são uma fonte pronta para o rasterizador CPU do
ODH. Isso é apenas uma observação da implementação Simitone/FreeSO, não uma
prova sobre as fontes usadas pelo jogo original. O Linux usa PangoCairo com
fallback para as fontes do sistema; os demais alvos ainda precisam de provedores
de fonte próprios e confirmação visual de glifos.
