# Vínculos duráveis dos objetos: alcance da revisão

A revisão usa o FreeSO fixado em
`7e76c1960252ddd033d8937d5edb47d326f5b69c`, correspondente à base alexjyong
avaliada. As fontes foram lidas no objeto Git dessa revisão. Isso é evidência
da referência em C#, não uma prova independente do executável original.
As revisões e atribuições estão em [referências](references.md).

| Palavra ObjectData | Evidência na referência | Consequência para a propriedade |
| --- | --- | --- |
| 12, `TargetId` | Nome em `Model/VMStackObjectVariable.cs`; sem uso pelo nome encontrado na busca delimitada | Preservar; destino, sentinela e limpeza ainda não estabelecidos |
| 26, `ParentId` | `Entities/VMEntity.cs`, `GetValue`, linhas 797–801, lê o contêiner atual como a palavra 2 | Exige política explícita no nativo, que ainda trata a palavra 26 armazenada como dado comum |
| 54, `PrimitiveResultID` | `Primitives/VMSnap.cs`, linhas 99 e 127, grava o bloqueador ou zero; `Engine/VMRoutingFrame.cs`, linhas 573–574, procura esse objeto | Zero significa ausência; isso não autoriza apagar qualquer valor não nulo ao desocupar |
| 6, `RouteId` | Enumeração marca sem uso e TODO; sem uso pelo nome encontrado | Não assumir referência só pelo nome |
| 72, `GroupID` | `Utils/VMWorldActivator.cs`, linhas 135–139, atribui número de grupo | Não confundir com número de objeto |
| 52, `PrimitiveResult` | `VMSnap.cs`, linhas 98 e 126, grava código de resultado | Não confundir resultado com referência |

As palavras derivadas de contenção, lugar ocupado e identidade própria já
têm tratamento estrutural nas palavras 2, 3, 11 e 67 do World. Isso não resolve
os outros campos nem transforma bancos salvos antigos em estado atual.

`VMEntity.Reset`, linhas 525–550, limpa execução e chama ações de reinicialização;
não faz uma limpeza geral das palavras acima. `VMEntity.Delete`, linhas
1352–1430, retira referências de relações entre objetos, mas não estabelece
uma regra geral de zerar ObjectData. As rotas consultadas de
`VMContext.RemoveObjectInstance` e `VM.RemoveEntity` também não estabelecem
essa regra.

O [arquivo estrutural](neutral-property-format.md) preserva esses valores e
reserva os números retirados. A futura ativação precisa de uma regra comprovada
de reconstrução ou reinicialização para os vínculos relevantes. Não se deve
apagar palavras, atributos ou dados de jogo apenas porque seu valor numérico
coincide com o de um objeto retirado.
