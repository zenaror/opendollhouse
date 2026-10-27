# Descoberta nativa dos grupos HIT: ordem e raízes

Investigação estática de 2026-10-17, complementar a
[origem das entradas HIT](hit-native-entry-map-evidence.md). O executável
`../referencias/The Sims/Sims.exe` tem SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Nenhum código original foi executado ou alterado. Os recortes de assembly,
hashes dos bytes e mapeamento VA/RVA/offset estão em
registro arquivado não distribuído, reproduzíveis por
`python3 registro arquivado não distribuído`.

## Arquivos de uma raiz

`0x5725c4` procura `*.hit` na raiz recebida. Constrói o enumerador em
`0x572609`, define o padrão em `0x57261e` e chama `0x5d8632` com flag `8`
em `0x57262f`. Esta rotina chama `FindFirstFileA` e `FindNextFileA` pelos
slots importados `0x666114/0x666118`. Para arquivos, copia o
`cFileName` devolvido por `WIN32_FIND_DATAA` e o acrescenta à cauda da
lista em `0x575805`; o iterador em `0x57263a–0x572829` percorre essa lista
na mesma ordem. O bit de diretório `0x10` desvia para ramificações que a
flag `8` deste caller não admite. Não há ordenação lexical, descida recursiva
ou leitura de índice de arquivo FAR nesse caminho de grupos HIT.
O setter `0x5d8ab4` faz uma varredura preliminar por `0x5d82fa` em uma
coleção interna `+0x24`; `0x5d8632` usa o caminho e padrão em `+0x28/+0x34`
e não consulta essa coleção ao produzir a lista percorrida pelo loader.

Para cada nome enumerado, o loader deriva o basename e processa `.hsm`,
`.hit`, `.hot` nessa sequência (`0x5726e3`, `0x572741`, `0x5727a2`). A
enumeração parte de **HIT existente**, não de HOT existente. O padrão
`*.hit` é delegado à API Win32, e os sufixos construídos são minúsculos;
o helper de componentes alcançado por `0x5729e0` copia as partes do caminho
sem converter letras para minúsculas. Correspondência de wildcard,
nomes curtos 8.3 e sensibilidade a maiúsculas do volume são condições do
Windows/volume, não uma regra lexical implementada pelo jogo.

## Ordem entre raízes

O primeiro caminho vem da base combinada com `SoundData\\` (`0x57169c`)
e é carregado em `0x57174e`. O loader percorre depois a lista
`owner+0xd0`, da cabeça ao sentinela, e carrega cada raiz por `0x5725c4`
(`0x571753–0x57177d`). A porta dinâmica `0x5725a0` acrescenta raízes
à cauda dessa lista enquanto o byte global `0x6f63bc` vale zero; após sua
ativação em `0x5724a2`, carrega a raiz imediatamente. Portanto a regra
estática é **ordem de chamada das raízes**, seguida da ordem devolvida por
Win32 dentro de cada raiz. Uma raiz dinâmica tardia é processada quando
chamada, e não reposicionada por nome ou número de expansão.

Um caller identificado (`0x414385–0x41447b`) passa condicionalmente a
`0x57a5e8 → 0x5725a0` os caminhos abaixo, nesta ordem:

| Posição de chamada | Caminho literal | Condição local |
| --- | --- | --- |
| 1 | `ExpansionPack\\` | bit `0x01` de `+0x66` |
| 2 | `ExpansionPack2\\sound\\` | bit `0x02` |
| 3 | `ExpansionPack3\\sound\\` | bit `0x04` |
| 4 | `ExpansionPack4\\sound\\` | bit `0x08` |
| 5 | `Deluxe\\sound\\` | bit `0x10` |
| 6 | `ExpansionPack5\\sound\\` | bit `0x20` |
| 7 | `ExpansionPack6\\sound\\` | bit `0x40` |
| 8 | `ExpansionPack7\\sound\\` | bit `0x80` |
| 9 | `ExpansionShared\\sound\\` | comparação signed `WORD(+0x66) > 3` |
| depois | elementos da lista do caller | se presentes |

Essa tabela prova a ordem das chamadas quando cada condição é verdadeira;
não prova que todas as raízes existem, nem que esse seja o único produtor
dinâmico. A função `0x57a5e8` só encaminha a raiz quando seu estado local
está habilitado. A raiz `SoundData\\Plugin\\` também aparece na rotina
de inicialização, mas o recorte inspecionado não a entrega a `0x5725c4`;
por isso ela não entra nesta lista de grupos HIT.

## Contrato para o runtime Linux

Para reproduzir a precedência de uma instalação observada, o loader deve
aceitar uma sequência explícita de raízes e grupos HIT na ordem observada.
Cada grupo mantém identidade própria; a HOT correspondente é processada
depois da HIT, e cada `TrackData` substitui a definição anterior da mesma
chave no catálogo global. A última definição **processada** vence. O mesmo
princípio de precedência vale para outros mapas compartilhados somente
quando sua inserção por grupo estiver demonstrada separadamente.

Uma descoberta automática Linux pode usar uma ordem estável documentada,
mas essa ordenação é uma política ODH. A API nativa não classifica os
arquivos: ela conserva a ordem de `FindFirstFileA`/`FindNextFileA`, que pode
variar com o sistema de arquivos e seu estado. A
[documentação da Microsoft para FindNextFileA](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilea)
confirma que a ordem não é garantida e que o aplicativo precisa ordenar os
resultados se isso for necessário. Mesmo uma ordenação estável
Linux pode escolher outro vencedor para chaves duplicadas. O caminho
inspecionado não fornece precedência derivada da ordem física de entradas
em FAR ou ISO; qualquer efeito da extração desses dados é indireto, por
alterar a enumeração posterior dos arquivos no disco.

Como comparação apenas, o submódulo FreeSO fixado pelo Simitone em
`f86bbd34b2112296ed786741f640a0535886d180` percorre
`TS1AllFiles` de forma recursiva em `Content.cs` e `TS1Audio.Init` seleciona
arquivos `.hot` por regex. Essa implementação não substitui a prova da
ordem do executável original. O rastreio presente tampouco valida uma
ordem concreta em execução do jogo, áudio físico ou hardware.
