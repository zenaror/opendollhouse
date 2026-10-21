# Sobreposição de bloqueadores em GetAvatarPlace

`odh_saved_avatar_place_overlap` é uma função C++ pura para uma única regra de
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/VMContext.cs:1190-1235`
na revisão Simitone `4906db3156616bfaffc10ce0963cf08062975fef` e FreeSO
submódulo `f86bbd34b2112296ed786741f640a0535886d180`. O mesmo grupo multitile
é ignorado primeiro; EntryPoint 5 verdadeiro em qualquer lado isenta o par;
EntryPoint 5 desconhecido retorna `unsupported`. Valores inválidos também
permanecem unsupported. Com ambos falsos, avatares dependem da permissão do
ator e de `AllAvatarsSolid`; objetos dependem dos bits Allow (16) e Disallow
(2). Flags ou permissões ausentes permanecem unsupported quando podem mudar a
decisão. Para um bloqueio conhecido, a presença de EntryPoint 26 informa se a
busca deve parar; se esse fato estiver ausente, `break_on_blocker` permanece
desconhecido.

`odh-saved-avatar-place-overlap-tests` cobre dezesseis casos com entradas sintéticas.
Isso não executa BHAVs, descobre obstáculos dinamicamente nem prova a colocação
de avatares no runtime. Não cobre outras regras de `GetAvatarPlace`.
