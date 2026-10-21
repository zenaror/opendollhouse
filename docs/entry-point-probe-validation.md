# Inventário estático de EntryPoints por entidade

`odh-wall-appearance --entry-point-probe` abre a OBJD e a tabela de funções
selecionadas de cada entidade salva, usando a origem concreta da definição
resolvida pelo índice. Para `UsesFnTable == 0`, registra as EntryPoints geradas
da OBJD legada; caso contrário, exige a OBJf de mesmo chunk ID. O inventário
também verifica se as ações de Main (EP1), EP5 e EP26 aparecem como chunks BHAV
no mesmo asset de definição. Entidades compartilham o resultado por GUID.

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
1, 5 e 26 com condição, ação e presença do BHAV no asset. Isso fornece a
procedência estática necessária para planejar a execução bilateral de EP5 e
classificar EP26; não é validação de placement ou equivalência com o jogo.
