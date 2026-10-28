# Inspeção da destinação dos objetos ao desocupar uma casa

## Alcance

`odh-property-disposition` inspeciona os registros legados `ObjM/objt` de uma
casa, sem executar o jogo, ativar uma família, remover objetos ou gravar dinheiro
e saves. Não lê `FSOV`, o save atual de uma família nem um World em execução.
Um arquivo pode conter estado mais recente nessas outras representações; o
resultado não deve ser apresentado como a situação atual da partida.

É uma etapa do pacote P01 do [plano](migration/README.md), não uma implementação
do despejo. A [auditoria de referência](property-liquidation-reference-audit.md)
fundamenta uma regra candidata por entidade: conservar a flag de retenção;
caso contrário, retirar atores do tipo 2 e objetos cuja categoria efetiva de
construção seja zero. Objetos de construção com categoria não zero permanecem.
Essa regra inclui objetos de outros tipos, não só móveis do tipo 4.

## Dados e identidades

- `ObjectDefinition::build_mode_type` lê o campo bruto de 16 bits em `0x8a`
  das versões OBJD 136/138, somente quando os dois bytes estão disponíveis.
  Um campo ausente é diferente de um campo presente com valor zero.
- A definição deve ser única para o GUID e concordar com o tipo da instância.
  Para objetos com várias peças, `DefinitionIndex::unique_master` resolve a
  definição principal no mesmo recurso, com o mesmo MasterID e SubIndex=-1.
  MasterID é um número de grupo, não um GUID.
- O inspetor recebe explicitamente a palavra de movimento ObjectData43.
  A ferramenta usa a cópia do prefixo legado. Em uma futura integração ao
  runtime, deve ser usado o valor atual do World, não seu banco salvo antigo.
- A flag `0x10` tem precedência sobre a classificação de ator. A permissão de
  apagar `0x08` não a substitui. A classificação de ator usa a definição da
  própria peça, antes de consultar a definição principal. Os tipos da peça e
  do principal podem diferir. Para os demais objetos, a categoria vem da
  definição efetiva. Recursos ambíguos ou campos necessários ausentes produzem
  `unresolved`, sem inventar zero.
- A leitura dos recursos e a importação do SavedWorld comparam também a nova
  categoria com a definição selecionada, impedindo substituir esse dado sem
  concordância de identidade.

Layout e resolução usam as fontes FreeSO fixadas em
`7e76c1960252ddd033d8937d5edb47d326f5b69c`: `OBJD.cs`,
`VMStackObjectVariable.cs`, `VMEntity.cs`, `VMContext.cs`,
`VMMultitileGroup.cs` e `VMArchitectureStats.cs`. Consulte
[referências](references.md) para obter essa revisão.

## Uso

```sh
cmake --build build/native --target odh-property-disposition
./build/native/odh-property-disposition "$GAME_ROOT/UserData/Houses/House07.iff" \
  --object-definitions "$GAME_ROOT/GameData/Objects/Objects.far" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00000.iff" \
  --object-definition-file "$GAME_ROOT/UserData/Characters/User00001.iff"
```

Defina `GAME_ROOT` para sua instalação externa. Acrescente as fontes exatas das
expansões, personagens e objetos necessários. A ferramenta não adivinha a
precedência entre cópias de GUIDs. Aceita até 128 fontes, 8192 definições e 1 MiB
de texto de proveniência; o leitor de prefixos limita a casa a 4096 entidades.
Ela emite uma linha por objeto e um resumo, sempre com `eviction_authorized=0`.
Saída 0 significa que todas as classificações candidatas foram resolvidas;
3 indica classificações não resolvidas; 2 indica erro de uso, formato ou recurso.
Nenhuma dessas saídas autoriza efetivar um despejo.

## Limites para a próxima integração

A inspeção não comprova a transformação completa de uma propriedade. Ainda é
necessário validar grupos inteiros e flags divergentes entre peças, objetos
carregados/contidos, limpeza de referências, definição efetiva e ordem de remoção.
A análise estática do predicado `0043557a` confirmou a ordem flag → tipo da
própria peça → categoria efetiva. O mapeamento completo dos campos serializados
ainda precisa ser fechado antes da integração que altera a propriedade.

A rotina de remoção `00440f2b` também pode alcançar o grupo inteiro e remover
objetos contidos, sem repetir a decisão de retenção para cada peça. Portanto,
os resultados individuais desta ferramenta não podem ser usados como uma lista
independente de remoções. A limpeza de referências ainda precisa de validação.

Tipo 2 inclui visitantes e NPCs: o resultado `remove/actor` não autoriza retirar
pessoas fora da família cuja saída está comprovada. Inventário NGBH é um conjunto
de registros por vizinho, não uma lista de móveis, e não é alterado pelo inspetor.
Valores, depreciação, reembolso, arquivo neutro e publicação atômica continuam
separados no [desenho de persistência](native-property-persistence-design.md).

Os testes `object_definition_metadata`, `property_disposition`,
`object_runtime_resources`, `object_definition_index` e `saved_world_factory`
verificam limites de leitura, precedência, definição principal e rejeição de
metadados alterados. Esses testes não comprovam paridade de despejo com o original.

## Inspeção dos dados instalados

Uma passagem pelos prefixos legados de oito casas, usando 99 fontes explícitas
de definições, produziu os resultados abaixo. São classificações candidatas,
não objetos efetivamente retirados ou mantidos por um despejo executado.

| Casa | Entidades | Conservar | Retirar | Não resolvidas |
| --- | ---: | ---: | ---: | ---: |
| 01 | 60 | 55 | 5 | 0 |
| 05 | 245 | 158 | 87 | 0 |
| 07 | 182 | 101 | 81 | 0 |
| 11 | 16 | 10 | 5 | 1 |
| 21 | 760 | 322 | 438 | 0 |
| 40 | 532 | 251 | 281 | 0 |
| 81 | 327 | 131 | 196 | 0 |
| 91 | 205 | 160 | 45 | 0 |

Na Casa11, ID5/GUID `0xa8b7b894`, nome `Outside`, tipo7, ficou sem definição
nas fontes selecionadas. Não foi criada uma exceção para adivinhar sua categoria.
As 107 entradas comerciais selecionadas conservaram seus SHA-256 antes/depois;
o binário do inspetor também permaneceu fixo durante a passagem final.
Nomes legados com bytes fora de ASCII são apresentados com escapes em
`name_bytes`, sem emitir texto UTF-8 inválido ou perder a identidade dos bytes.

## Valor atual dos grupos

A inspeção separada [de valores](property-values.md) usa o World importado e
permanece sem autorização para vender objetos ou calcular o reembolso completo.
