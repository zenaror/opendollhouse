# Inventário estático de EntryPoints por entidade

`odh-wall-appearance --entry-point-probe` abre a OBJD e a tabela de funções
selecionadas de cada entidade salva, usando a origem concreta da definição
resolvida pelo índice. Para `UsesFnTable == 0`, registra as EntryPoints geradas
da OBJD legada; caso contrário, exige a OBJf de mesmo chunk ID. O inventário
também verifica se as ações de Main (EP1), Load (EP2), EP5 e EP26 aparecem como
chunks BHAV no mesmo asset de definição. Entidades compartilham o resultado por
GUID.

O probe é somente leitura: presença de um chunk BHAV não significa que ele foi
decodificado, que suas dependências Global/SemiGlobal foram resolvidas ou que
foi executado. `bhav_executed` e `runtime_placement_semantics` são sempre
`false`. Entradas fora do limite declarado da tabela ficam `null`; recursos
ausentes, ambíguos ou malformados ficam `state: "unknown"` com uma razão.

Exemplo reproduzível no House07:

```sh
build/odh-wall-appearance '../referencias/The Sims/UserData/Houses/House07.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff' \
  --entry-point-probe > /tmp/odh-house07-entry-points.json
```

O JSON associa cada registro ao ID/GUID da entidade, caminho e asset da
definição, chunk OBJD, `UsesFnTable`, origem e tamanho da tabela, e as entradas
1, 2, 5 e 26 com condição, ação e presença do BHAV no asset. EP2 é o ponto de
entrada Load executado pelo Simitone ao restaurar objetos do lote; este campo
apenas localiza a ação e não executa o BHAV nem suas dependências. O inventário
fornece procedência estática para planejar EP2, a execução bilateral de EP5 e a
classificação de EP26; não é validação de placement ou equivalência com o jogo.

## Inventário em lotes reais (2026-10-24)

O probe foi executado em modo somente leitura nos saves House05 e House07,
usando os 90 arquivos FAR e 20 IFFs de personagens disponíveis na instalação
(110 fontes explícitas, dentro do limite de 128 fontes e do orçamento de
definições). Todos os registros foram resolvidos: 245/245 em House05 e 182/182
em House07. A EP2 tinha ação não zero em 88 entidades de House05 e 60 de
House07, cobrindo 17 e 19 IDs de ação. Nenhuma definição usada nesses lotes
veio de Downloads.

A inspeção estática dos BHAVs imediatos nos FARs exatos encontrou 25 pares
distintos de recurso/ação. As rotinas contêm expressões, chamadas a outros
BHAVs e operações diretas com os opcodes 7, 10, 23, 31 e 49. Uma EP2 de
`FlowersOutdoor.iff` (ação 4107) não aparece como BHAV nesse mesmo asset. Isso
não identifica a cadeia completa de chamadas, o significado dessas operações
ou o resultado em execução; a ausência no asset também não prova que a ação
não tenha outro caminho de resolução.

O inventário confirma que o carregamento do lote precisa tratar as EP2 na
ordem dos objetos e com o dono correto dos recursos. A execução futura deve
usar uma cópia ainda não publicada do World, recusar esperas, yields, falhas,
donos ausentes e estouro do orçamento, e publicar o lote inteiro somente
quando todas as ações suportadas terminarem. O ODH ainda não executa essas
EP2; estes resultados são evidência estática.
