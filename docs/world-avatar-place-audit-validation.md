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
de 255 exige um resolver explícito. O CLI executa em World temporário apenas a
forma BHAV estritamente validada `TestObjectType(StackObject)` seguida de
`StackObjectAttributes[16] == 1`. O formato tem dois passos, não escreve
memória e usa orçamento de duas instruções; qualquer outra estrutura permanece
`unsupported`. O teste de tipo consulta GUID e master GUID explícitos. A
comparação lê o atributo vivo do StackObject; se ele estiver ausente e o tipo
corresponder, o resultado é `unsupported`, sem inferir zero. Isto não fornece
estado ghost genérico nem implementa o restante do interpretador BHAV.
EntryPoint 26 é lido da tabela do bloqueador para determinar se a varredura
termina; se essa informação for desconhecida e houver bloqueadores posteriores,
o resultado também fica `unsupported`.

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
O teste `entry_point5_readonly` cobre o perfil BHAV isolado, resultados true e
false, short-circuit de tipo, atributos ausentes e rejeição de escrita/formas
extras. Os casos são sintéticos e não executam BHAVs comerciais.

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

Dois probes adicionais exercitam bloqueio e EntryPoint 5 com dados comerciais
reais. A caixa `(456,504,1,3)` encontrou os objetos 35 e 101 em RoomInfo
completo; a auditoria retornou `blocked`, `final_blocker_id=35`, dois pares e
`early_stop_known=false`. Para `(280,376,1,3)`, RoomInfo encontrou o flamingo
155. O callback carregou BHAV 4108 de `Objects.far/flamingo.iff` e executou a
forma read-only permitida em um World temporário: a identidade de Bob 105 não
corresponde ao GUID testado, então o resultado foi false após uma instrução,
sem ler atributo 16. A posição permaneceu bloqueada pelo flamingo; JSON marca
`bhav_executed=true`, mas `placement_decision=not_evaluated` e
`route_clearance=false`. Ambos são probes de candidato sobre o World restaurado;
não movem o avatar nem provam resposta ou colisão no executável original.

O frame capturado foi `/tmp/odh-house07-place-audit-02.ppm` para `(400,440)` e
`/tmp/odh-house07-place-audit-04.ppm` para `(392,440)`. Esses frames comprovam
renderização CPU do preview House07 nesta execução, não equivalência visual ao
jogo nem teste de input/display.
