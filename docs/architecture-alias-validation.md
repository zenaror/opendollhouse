# Resolução explícita dos aliases de arquitetura

`odh_architecture_aliases` expõe `resolve_architecture_aliases` para ligar os
registros já parseados de FLRm/WALm a catálogos ordenados fornecidos pelo
caller. O resultado contém os arrays `ArchitectureResourceAliases` consumidos
por `prepare_saved_architecture`, além de contagens de duplicatas e nomes não
resolvidos. A função não abre arquivos, FARs ou chunks comerciais.

## Contrato de nomes e precedência

A referência consultada é Simitone `4906db3156616bfaffc10ce0963cf08062975fef`,
com FreeSO submodule `f86bbd34b2112296ed786741f640a0535886d180`. Em
`VMTS1Activator.BuildFloorDict/BuildWallDict`, cada nome de mapa é truncado no
primeiro ponto, convertido para minúsculas invariantes e procurado no catálogo
dinâmico (`VMTS1Activator.cs:482-507`). Só uma busca bem-sucedida escreve
`entry.ID`; entradas subsequentes para o mesmo alias substituem o valor somente
se resolverem. Um miss posterior não apaga um alias anterior.

O resolvedor mantém dois estados distintos. `unresolved_*_aliases` é o
diagnóstico histórico e marca qualquer ID que teve pelo menos uma entrada sem
match. `declared_unresolved_*_aliases` indica o resultado final: o ID foi
declarado no mapa, mas nenhuma de suas entradas produziu valor. Assim, uma
duplicata com uma entrada resolvida continua utilizável mesmo que outra
duplicata tenha falhado; só IDs sem qualquer resolução final são propagados ao
preparo como declarações ambíguas.

O catálogo recebe as chaves dinâmicas prontas do provider (basename sem a
extensão final) e as converte para minúsculas. Os nomes de mapa e catálogo
devem ser ASCII; bytes fora do ASCII são contabilizados como não suportados e
permanecem sem resolução. Isso evita uma aproximação locale-dependent de
`ToLowerInvariant`; a leitura original de FLRm/WALm conserva os bytes.

O vector do catálogo é uma política ODH explícita: em nomes canônicos
duplicados, o último registro vence e a duplicata é contada. O mesmo vale para
duplicatas bem-sucedidas de alias no mapa. IDs finais devem ser únicos e ficar
entre 256 e 65533; 65534/65535 são sentinelas de água/piscina. A função não
atribui IDs nem determina providers, ordem de arquivos, precedência de arquivos
comerciais ou fallback de chunk 1/0. O caller seleciona os dois mapas e passa
catálogos em ordem deliberada. IDs não são identidades estáveis do TS1 nem do
Simitone.

## Diagnósticos e limites

Os contadores distinguem por família (piso/parede) entradas resolvidas, misses,
aliases sobrescritos, duplicatas canônicas de catálogo e nomes não suportados.
`unresolved_*_aliases` aponta IDs que tiveram ao menos um registro sem match;
`declared_unresolved_*_aliases` aplica a precedência final de
`BuildFloorDict/BuildWallDict` e só fica marcado quando nenhuma entrada do ID
resolveu. Uma falha de lookup só prova que o nome não está no catálogo
informado, não que o asset esteja ausente da instalação.

`ArchitectureAliasResolverLimits` limita entradas de mapa/catálogo, tamanho de
cada nome e bytes totais examinados. IDs fora da faixa e colisões de IDs entre
nomes canônicos finais são rejeitados. Mapas e catálogos de entrada não são
alterados. Os dados comerciais e qualquer futura resolução de nomes permanecem
fora do Git.

`tests/architecture_aliases.cpp` cobre prefixo no primeiro ponto, lowercase
ASCII, ordem explícita e duplicatas, miss que preserva valor anterior,
diagnósticos independentes FLRm/WALm, nomes não ASCII, preservação dos mapas,
limites e colisões. São fixtures sintéticas da regra Simitone; não provam
comportamento do jogo original nem a precedência de um catálogo de instalação.

```sh
cmake -S . -B build-aliases -DBUILD_TESTING=ON
cmake --build build-aliases --target odh-architecture-alias-tests -j4
ctest --test-dir build-aliases -R '^architecture_aliases$' --output-on-failure
```
