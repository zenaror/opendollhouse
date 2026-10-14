# CFP com prefixo no entry FAR

Checkpoint 2026-10-14. `odh-animation` agora aceita o entry completo do CFP,
como `Animations\xskill-clip.cfp`, sem confundir seu diretório com a identidade
do xskill. O argumento original continua sendo o seletor exato no FAR; não há
busca automática por basename ou escolha implícita entre colisões.

A validação compara apenas basenames, normalizando separadores `\`/`/` e caixa
ASCII. Nome diferente do `AnimationHeader.xskill + .cfp` continua falhando.
Formato dos decoders, plano CFP, JSON e seleção do BCF/clip não mudam. Entradas
flat antigas mantêm o mesmo resultado.

Debug e ASAN/UBSAN passaram no teste `animation_cli`: paths Windows/POSIX,
uppercase, basename errado, entry exato ausente e igualdade do JSON com fixture
flat. Compilação sem avisos e diff check limpo. LSan off por ptrace, sem leak check.
Não foi repetida a suíte completa após este guard; a unidade anterior passou88
configurados/87pass1skip nos dois builds.

Recurso nativo confirmado em `ExpansionShared/ExpansionShared.far`:
BCF `Animations\a2o-idle-neutral-lstart.cmx.bcf`, clip interno
`a2o-idle-neutral-trans-stand-lstand`, CFP
`Animations\xskill-a2o-idle-neutral-trans-stand-Lstand.cfp`.
Metadata e CFP foram decodificados com zero bytes restantes. Os sete planos têm
90/90/90/1305/1305/1305/1305 elementos, total5490. JSON Debug/ASAN byte idêntico,
SHA256 `baed4510ddbe60f0b193aa53d78e38d43defccade58c9761c3c6a4b379589132`.
Hash do FAR original permaneceu igual antes/depois. Reports ignorados
`misc281-animation-cfp-{debug,asan}.json` e `animation-prefix-input-before.sha256`.
Nenhum recurso comercial é versionado.

Essa evidência corrige a hipótese de CFP ausente: uma busca anterior confundiu
basename e fullentry. O recurso existe com prefixo. O host `odh-person` ainda não
registra opcode44, então o fault do Main não prova falta da animação. Resolver
Misc281 requer Global STR157/156 por idade, catálogo de nomes internos BCF e fontes
FAR explícitas: skeleton adulto em Animation.far, este clip/CFP em ExpansionShared.
`SavedAppearanceResources` atualmente usa um FAR e lookup fullentry; catálogo
multi-source/CFPbasename e ligação Playback/ticks/eventos continuam pendentes.
A referência FreeSO4c6b3e8 tem providers de basename separados, com overwrite por
ordem; ODH precisa política explícita, sem presumir ordem original.

Esta unidade não executou Main/VM, não gerou pose/figura do novo clip e não prova
continuação dos atores salvos105/137. O objetivo Linux funcional continua ativo.
