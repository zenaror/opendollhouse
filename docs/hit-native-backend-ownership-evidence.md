# Ownership do backend HIT e identidade compartilhada

Investigação estática de 2026-10-17 sobre `../referencias/The Sims/Sims.exe`,
PE32 x86, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
Complementa [engine e backend](hit-native-engine-backend-evidence.md).
O original não foi executado ou modificado. Os 37 recortes em
registro arquivado não distribuído têm 5.772 bytes de
instruções comparados com o arquivo; o manifesto registra VA, RVA, offset
raw e SHA256 de cada trecho, derivados dos cabeçalhos PE. Reprodução:
`python3 registro arquivado não distribuído`.

## Alias espacial: um objeto, um contador, duas liberações

A factory `engine+0x70` (`0x603ec1`) devolve `base+0x174`, interface
secundária com vtable `0x6762e4`. Seus slots `+0/+4` ajustam `this` em
`-0x174` (`0x5fd5d3/0x5fd5de`) e alcançam o mesmo contador `base+0xcc`
usado pela vtable principal `0x6763e0`. Não há dois contadores independentes.

O construtor zera esse contador (`0x5f8161`). A factory configura a fonte,
chama `+8` e faz **um AddRef** (`0x603f12`). O slot `+8`, `0x5fab9d`, apenas
chama `+0x54`, `0x5f8056`, que guarda `this` em `base+4`; não incrementa
referências. Os setters secundários `+0xec/+0xf4`, chamados pela patch,
tampouco acrescentam uma referência ao backend.

Com ID26 não zero, a inicialização guarda o retorno em `patch+0xe4` e copia
o mesmo ponteiro para `patch+0xe0` (`0x57ff58–0x57ff64`) sem AddRef.
A limpeza chama Release em **ambos**, primeiro `e4` (`0x580010`), depois
`e0` (`0x580029`), sem comparar os endereços. Os getters somente leem campos.
O primeiro Release que atinge zero chama limpeza e destrutor com liberação
da alocação (`0x5f8352 → 0x5faaef`).

Portanto, a sequência espacial examinada contém um desequilíbrio estático:
um crédito da factory para duas chamadas de Release. Se não houver retenção
externa interposta, o primeiro pode destruir o objeto e o segundo usa um
alias inválido. Isso não demonstra que uma execução real do jogo tenha
percorrido esse cenário. A pesquisa não inventa uma referência compensatória
nem promove a anomalia estática a um crash observado. Com ID26 zero, `e4`
fica nulo, a factory `+0x5c` fornece uma referência e a limpeza faz um Release.

## Patch, wrapper e recursos têm vidas diferentes

| Estado | Transição observada | Efeito |
| --- | --- | --- |
| `patch+0x14` | `+0x78`, zero para um | Inicializa backend; depois incrementa |
| `patch+0x14` | `+0x7c`, um para zero | Limpa backend; não deleta a patch |
| `patch+0xd4` | `+0x60`, zero para um | Chama carga do recurso no backend |
| `patch+0xd4` | `+0x64`, um para zero | Limpa recursos, sem Release do objeto |
| `wrapper+0x14` | `+0x78/+0x7c` | Contador local; não adquire a patch |
| `backend+0xcc` | `+0/+4` | Retém/libera a alocação do backend |

A patch começa com `d4/d8/e0/e4` zerados. A limpeza normaliza `d4` não zero
para um e chama a liberação de recurso uma vez antes de liberar os backends.
O destrutor da patch zera `+0x14` e força a limpeza. O significado de `d8`,
contado por `+0x68/+0x6c`, permanece aberto.

A factory do wrapper (`0x5800bf`) cria somente o wrapper. `0x580229` copia
a patch para `+0xbc`, seu `e0` para `+0xc0` e o alias espacial para `+0xc4`.
Não chama AddRef do backend nem `patch+0x78`. Essa aquisição pertence ao
binding do controller `0x57f91e`, antes de criar o wrapper. Dois wrappers
da mesma patch observam o mesmo backend.

A destruição do wrapper encaminha uma parada, sem Release do backend ou
`patch+0x7c`. A parada (`0x5804ba`) consulta o `e0` atual da patch; se ele
for nulo, evita consultar o ponteiro antigo do wrapper. Isso é relevante
porque o rebinding libera a patch antes de destruir o wrapper antigo.
Mesmo rebinding para o mesmo ID percorre a liberação e a aquisição; não há
atalho por igualdade. Parar ou destruir um wrapper pode parar o backend
compartilhado com outro wrapper ainda vivo.

## Contrato para o registry do adapter

Um registry deve possuir uma identidade de backend por **instância de patch
ativa**, com geração quando ela é reinicializada. Os wrappers são views do
mesmo estado. Contagem de bindings, carga dos recursos e vida da alocação
são operações diferentes; não transformar cada controller em dono exclusivo
de uma reprodução. Stop deve afetar o backend compartilhado sem apagar os
bindings dos outros controllers. Após a última liberação, uma nova aquisição
inicializa uma nova geração; a patch registrada pode continuar existindo.

Os campos espacial e comum devem ser views de um único owner gerenciado.
Reproduzir duas deleções porque existem dois campos seria importar a
anomalia de memória, não preservar o compartilhamento. Handles com geração
e rejeição de views aposentadas são uma política segura do ODH; não são
uma alegação de ABI original. O registry também precisa preservar a ordem
de clear, rebinding e destruição do wrapper.

Esta unidade não fecha retenções externas de todo o programa, concorrência
de callbacks ou decodificação streaming. Não estabelece PCM físico,
contagem global de vozes ou conversão Q15.

## Namespace numérico: Patch e Track podem substituir um ao outro

O callback HOT `Patch`, `0x57b5e6`, converte a chave à esquerda de `=` pelo
helper numérico `0x5d81cf` (`0x57b61d`), rejeita zero e registra o objeto por
`0x57d0ce` em `0x57b8cf`. O callback `Track`, `0x57b921`, faz a mesma
conversão (`0x57b958`), rejeita zero e usa o mesmo registro (`0x57bb52`).
Esse é o helper já documentado na
[evidência de entradas](hit-native-entry-map-evidence.md), e não uma leitura
restrita a decimal ou resolução simbólica do LHS.

`0x57d0ce` substitui objetos em `manager+0x80` sem particionar por tipo:
limpa/deleta o anterior e guarda o novo. O tipo final da Patch é
`0xa3cd96b7`; o da Track é `0xa3cd96cf`, gravado em `0x57d784` após o
construtor da base. A consulta `0x573466` também não filtra tipos. O binding
ID50 primeiro escolhe o objeto por número, só então testa se é Patch
(`0x57f9a8`); um Track vencedor não provoca fallback para uma Patch anterior.
O ramo de tipo incorreto emite diagnóstico e retorna sucesso sem criar o
wrapper, enquanto chave ausente retorna falha.

Logo, o catálogo deve conservar ordem de registros e tipo vencedor para
colisões **entre Track e Patch**, inclusive dentro de um HOT com seções
intercaladas. A ordem de cadastro dos callbacks não reordena as linhas:
o parser despacha conforme lê o arquivo, como já demonstrado na evidência
de entradas. `TrackData` permanece no mapa separado de entry points.

`GlobalHitList` também converte o LHS por `0x5d81cf` (`0x57bcc2`), mas não
rejeita zero nesse callback. Chama `0x57d50a`, que aloca uma lista de `0x14`
bytes e a registra em **`manager+0xd8`**, não em `+0x80`. Ela não deve ser
introduzida como um tipo que colide com Track/Patch no catálogo de objetos.
O caminho de substituição/liberação de listas duplicadas não foi fechado
nesta unidade. Os recortes e a cadeia completa dos endereços estão em
registro arquivado não distribuído.
