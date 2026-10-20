# Catálogo ODH de recursos de piso e parede

`discover_architecture_resources` indexa recursos de arquitetura que o caller
fornece explicitamente como arquivos loose ou arquivos FAR1. Ele reconhece as
extensões `.flr` e `.wll` sem diferenciar maiúsculas, reduz o caminho interno ao
basename e canonicaliza o nome para lowercase ASCII. Outros sufixos são
ignorados e contados. Nenhum diretório é enumerado e nenhum payload IFF é
copiado ou decodificado.

Para nomes com vários pontos, o catalogador mantém as duas regras observadas no
provider Simitone: arquivos loose usam o basename sem a extensão final;
entradas FAR usam o prefixo antes do primeiro ponto como chave, embora a
família continue sendo identificada pela extensão final `.flr`/`.wll`.

A ordem de `ArchitectureResourceSource` é a precedência deliberada: um recurso
posterior com mesma família e nome canônico substitui o anterior. Dentro do
manifesto de um FAR, a entrada de índice posterior substitui a anterior. As
duplicatas são contadas separadamente para pisos e paredes. Após escolher os
vencedores, os recursos de cada família recebem IDs ODH independentes,
começando em 256, em ordem lexicográfica canônica. Essa política deixa os IDs
estáveis mesmo que a ordem dos caminhos fornecidos varie, desde que a coleção
de vencedores não mude. Os IDs não pretendem reproduzir atribuições dinâmicas
do Simitone/TS1.

Cada record mantém nome, família e um handle com índice da fonte, índice de
entrada, nome original, offset e tamanho. `catalog.sources[source_index]`
identifica o caminho e se a origem é loose ou FAR1. Assim, os adapters podem
reabrir o recurso necessário sem manter bytes comerciais na memória do
catálogo. Para casar com FLRm/WALm, `floors()` e `walls()` projetam os recursos
para `ArchitectureCatalogResource`, aceito pelo resolver existente. A
[integração host](external-architecture-renderer-validation.md) aplica aliases e
preparação ao lote e reabre os vencedores nos renderers de piso/parede.

As limites cobrem fontes, caminhos, nomes, entradas de recurso e índice FAR1.
FAR1 é validado com o parser comum; assinatura, versão, manifestos, limites ou
layout inválidos rejeitam o catálogo. A API não confirma a semântica interna
dos assets nem seleciona ordem de expansão/provedor. O chamador escolhe as
fontes e sua precedência por perfil de instalação. O resultado é um catálogo
estrutural ODH; não estabelece comportamento equivalente ao jogo ou Simitone.

`tests/architecture_resource_catalog.cpp` usa arquivos sintéticos para cobrir
loose/FAR1, extensão/família, case-fold, basename, precedência de fontes e
entradas FAR duplicadas, ordem estável de IDs, handles de provenance, entradas
ignoradas, FAR malformado e limites.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target odh-architecture-resource-catalog-tests odh-architecture-alias-tests -j4
ctest --test-dir build -R 'architecture_(resource_catalog|aliases)' --output-on-failure
```
