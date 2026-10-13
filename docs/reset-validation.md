# Reset de postura e loops — 2026-10-13

Playback44 agora resolve o reset ID0 e mantém um clipe de postura em loop.
A cadeira chega ao snap seguinte, ainda fora do perfil. O probe continua saída3;
isso não conclui sentar/levantar nem mostra uma animação renderizada.

## Contrato implementado

`AnimationResetResolver` recebe o frame e a postura normalizada: person word0
igual1/2 permanece1/2 para seleção; outros valores selecionam3. Não muda a
postura armazenada. Para ID0 modos0/1/2, reset resolve o clipe antes de alterar
estado, exige hand slot0 explicitamente conhecido e vazio, e valida person
data até HeadSeekState42. Se esse campo é1, passa a4 depois da validação.
Não supõe que ausência de um slot significa ausência de objeto carregado.
Modo3, carry, gestos e estados de mão ocupada continuam recusados.

O reset inicia loop no frame0, ou NumFrames backwards, com speed30/25 sem
dobrar por hurryable; retorna true de imediato. Não usa uma espera temporizada
para fingir reset completo. Modo1 com ID não zero também inicia/substitui loop,
honrando backwards/WalkStyle/hurryable e retornando true. Progress expõe `loop`.

Loops persistem além do retorno da VM. Cada tick aceita apenas sequência
consecutiva, avança framefloat32 e envolve uma vez por +/-NumFrames quando o
frame truncado chega ao fim. Eventos mantêm ordem de arquivo e, consumidos, não
reaparecem em cada volta, conforme a referência. Fila/eventos/ticks continuam
limitados; conversões de frame fora do range int32 são recusadas antes de
qualquer conversão insegura. Reinício da VM ou reuso da entidade invalida o loop.
Cancelamento e orçamentos preservam o contrato ODH já descrito em Playback.

Recurso/resolver ausente causa unsupported sem alterar clipe ou HeadSeekState.
Essa recusa é mais estrita que o fallback silencioso da referência; não se
afirma que seja comportamento do executável original. A definição de um
personagem real e seu carry ainda exigem implementação.

## Resolução explícita e arquivos consultados

No FreeSO, InitBodyData usa STR150 global para adulto,151 criança (idade<18),
com override não vazio de STR150 do recurso do avatar. O probe é um avatar
construído sem recurso próprio: não aplica override nem usa STR150 da cadeira
como se pertencesse ao personagem. Idioma1 e índice de postura são explícitos.

Global.iff, SHA256
`bdfd8456abceaa3e44f0a9754815248ea1e80c7b0df4fbaff53843fae3731478`,
STR150 formato-3 tem39 entradas/39 bytes restantes no snapshot consultado.
Índices1/2/3: `a2o-sit-loop`, `a2o-sitting-floor-loop`, `a2o-standing-loop`.

O clipe sentado está em `Animation.far/a2o-sit-loop.cmx.bcf`,1199 bytes, SHA256
`239aee03d984a245295dd8b7e6ac0ef2d6dd4f7c53a3678401bef1dfed9ec599`.
Nome `a2o-sit-loop`, xskill `xskill-a2o-sit-loop`,29 motions,116 frames máximos,
232 translations/3364 rotations, duração float32 3833ms, zero eventos/tail.
As amostras desse CFP não foram decodificadas nesta unidade. Não se afirma
reprodução do skeleton a partir da progressão silenciosa.

`--reset-probe BCF_ENTRY` carrega somente o BCF extra selecionado no mesmo FAR
de `--animation-probe`, rejeitando nomes duplicados após casefold entre coleções.
`--post-ticks N` (0..4096, padrão0) avança os controladores depois de retorno ou
fault da VM, sem run adicional e mantendo status/trace/instruções. A separação
`execution_ticks`/`post_ticks` fica no JSON; `simulation_ticks` e `route_ticks`
incluem ambos. Isso testa o loop isolado após uma parada, não a retomada de uma
primitiva ausente ou o avanço completo da simulação.

## Validação

CTest18/18 passou em Debug e ASan/UBSan. Playback agora tem90 checks sintéticos,
cobrindo seleção1/2/outros, modos de reset, HeadSeekState, falhas atômicas,
recurso ausente/inválido, mão ocupada/desconhecida, loop após retorno,
substituição/restart, backwards fracionário, eventos consumidos uma vez,
budgets e limite de frames. CLI cobre idade/postura, ausência/idioma/índice,
rejeição de override da cadeira, ambiguidades, requisitos de opções e ticks
depois do retorno. Fixture com10000 instruções esgotadas não recebe post-ticks
que transformem seu status em conclusão. LSan desativado por restrição ptrace.

Probe comercial, recursos originais externos abertos somente para leitura:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff --animation-probe '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf --age 21 --sound-probe --reset-probe a2o-sit-loop.cmx.bcf --post-ticks 240
```

Resultado: saída3/fault `snap operand profile not implemented`,28 instruções,
3 frames VM,169 ticks de execução +240 post-ticks =409. Última instrução:
BHAV1003 PC13 opcode46 decimal, operandos `06 00 03 00 00 00 00 00`.
Reset mantém `a2o-sit-loop` com looptrue/endfalse; após240 ticks isolados,
framefloat32 55.9998131,116frames e fila vazia. Posição(64,64,1,direção2),
recipiente2/slot0, postura1 e33 movimentos permanecem os da cena construída.
O pedido de som metal continua `skipped_no_backend`; não há áudio real.

Debug/sanitizers tiveram JSON byte a byte idêntico, saída3 e stderr vazio.
Relatórios ignorados `reports/chair-reset.json` e versão sanitizada;
`global-walk-150.json`/`sit-reset-clip.json` guardam consultas de leitura.
Não houve execução do original, validação visual ou hardware.

## Referências e próxima unidade

FreeSO fixo `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2.0 declarada:
[VMAnimateSim.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMAnimateSim.cs),
[VMAvatar.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs),
[VMAnimationState.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Model/VMAnimationState.cs),
[VMPersonDataVariable.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Model/VMPersonDataVariable.cs),
[Animator.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.vitaboy.engine/Animator.cs).
Código C# não incorporado; temporização/reset do original permanece por verificar.

Próximo snap usa SLOT128 tipo3 índice6, record7: offsetzero, flags4096,
snapTarget0, height0/facing-2/resolution16. Seus operandos flags0 diferem do
primeiro snap contido flags4. Investigar o contrato antes de tratar ambos como
equivalentes. Depois completar a cadeia sentar/levantar e iniciar skeleton,
interpolação/compositor Linux, áudio real, lote/tempo/necessidades/persistência
e comparação original. A meta funcional permanece maior que estes probes.
