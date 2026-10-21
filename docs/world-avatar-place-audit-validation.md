# Auditoria de sobreposição do avatar no World restaurado

`audit_world_avatar_place_overlaps` compõe uma consulta RoomInfo completa com
entidades, `ObjectData[8]` e tabelas EntryPoint já ligadas ao `World`. Ela usa a
ordem crescente de ObjectID e as regras de sobreposição observadas no Simitone
TS1. O resultado cobre somente entidades que intersectam a caixa do avatar;
parede, portal, clearance de rota e decisão final de posicionamento continuam
fora do escopo.

Para cada par, o chamador fornece a relação de grupo multitile e
`AllAvatarsSolid`. O adaptador considera EntryPoint 5 falso quando a tabela
existe e o índice não está presente, ou quando seu ActionFunction é menor ou
igual a 255. O código Simitone retorna falso nesses casos. ActionFunction acima
de 255 exige um resolver explícito de resultado; sem ele, a auditoria retorna
`unsupported` em vez de fingir que executou o BHAV. O callback de EntryPoint 5
fica responsável por estabelecer os argumentos/runtime inputs necessários,
incluindo o estado ghost. EntryPoint 26 é lido da tabela do bloqueador para
determinar se a varredura termina; se essa informação for desconhecida e houver
bloqueadores posteriores, o resultado também fica `unsupported`.

Referência: Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, FreeSO
submodule `f86bbd34b2112296ed786741f640a0535886d180`,
`VMContext.cs:1190-1235` e `Entities/VMEntity.cs:567-650`. Isso fundamenta o
perfil de referência, não prova compatibilidade com o executável original.

## Verificação

O teste sintético `world_avatar_place_audit` cobre consulta incompleta,
autointerseção na coleção estática, objeto sólido, Allow/Disallow com Disallow
prioritário, parada por EntryPoint 26, skip de grupo, BHAV EntryPoint 5 sem
resolver e com resultado explícito, `AllAvatarsSolid` e EntryPoint 26
desconhecido antes de um bloqueador posterior. Um caso confirma que o callback
do bloqueador posterior não roda após uma parada conhecida por EntryPoint 26.
Os casos usam `World` sintético e não executam BHAVs.

Em House07, `odh-person --saved-world-main --ticks 214` restaurou as tabelas
EntryPoint de todas as entidades presentes no lote a partir do recurso exato
resolvido por GUID, preservando os limites de 256 definições únicas, 256
entradas por tabela e orçamento total de palavras do `World`. A execução
terminou em `waiting` no tick 214 sem fault de VM (código de saída 3 é o estado
de espera já usado por este host).

Com as 23 fontes loose FLR/WLL documentadas, a caixa candidata `(400,440,1,3)`
resolveu RoomInfo completo sem interseções e o diagnóstico de sobreposição
retornou `allowed` com zero pares. A caixa `(392,440,1,3)` encontrou apenas a
pegada estática do próprio avatar 105 (`[389,437,395,443]`); o adaptador a
ignorou e também retornou `allowed` com zero pares. Ambos são resultados
limitados a entidades no snapshot/restauração, com `AllAvatarsSolid=false` do
perfil Default; `placement_decision` permanece `not_evaluated`, pois a auditoria
não inclui arquitetura, portal, deslocamento entre posições nem mutações de
grupo que o World atual ainda não modela.

O frame capturado foi `/tmp/odh-house07-place-audit-02.ppm` para `(400,440)` e
`/tmp/odh-house07-place-audit-04.ppm` para `(392,440)`. Esses frames comprovam
renderização CPU do preview House07 nesta execução, não equivalência visual ao
jogo nem teste de input/display.
