# Auditoria agregada dos bloqueadores de GetAvatarPlace

Esta unidade agrega a regra por par existente em uma lista completa de
bloqueadores fornecida pelo chamador. A lista deve estar ordenada por
`ObjectID` ascendente, como `VMContext.GetAvatarPlace` faz quando encontra
mais de um obstáculo. Cada bloqueador que não é ignorado por grupo multitile,
isento por EntryPoint 5 ou permitido substitui `statusObj`; EntryPoint 26
presente interrompe a iteração. Um EntryPoint 26 desconhecido no bloqueador
final mantém o bloqueio identificado, mas deixa desconhecido se houve parada
antecipada. Se houver candidatos seguintes, o resultado é unsupported porque
eles podem substituir `statusObj`.

Antes de classificar pares, a API exige inventário completo, quantidade dentro
do limite explícito (4096 por padrão) e IDs positivos, únicos e estritamente
crescentes. Entrada inválida ou regra por par unsupported falha fechada.
`pair_classifications` conta somente os pares efetivamente avaliados; uma
parada por EntryPoint 26 exclui os candidatos seguintes.

Referência consultada: `VMContext.cs:1190-1235` no Simitone
`4906db3156616bfaffc10ce0963cf08062975fef`, usando o submódulo FreeSO
`f86bbd34b2112296ed786741f640a0535886d180`. Essas revisões são pins de
consulta, sem build ou execução dos projetos upstream.

`odh-saved-avatar-place-audit-tests` cobre entradas sintéticas para o
bloqueador final em ordem crescente, substituição de `statusObj`, parada por
EP26, EP26 desconhecido, pares unsupported, IDs malformados, inventário
incompleto, limite e lista vazia. Os testes validam apenas a agregação do
modelo de regras: não descobrem geometria, não executam BHAVs, não reproduzem
a colocação em runtime e não demonstram comportamento do jogo original.

## Histórico de alterações

- 2026-10-21 — Agregação sintética de bloqueadores ordenados com limite de
  candidatos, falha fechada para listas inválidas e cobertura específica de
  EP26 desconhecido.
