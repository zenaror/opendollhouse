# Globals de bootstrap TS1

O [checkpoint seguinte de enumeração](entity-next-validation.md) remove os
faults de SetToNext registrados abaixo e documenta o avanço até scope35.

O host `odh-person --initialize` agora inicializa três globals de runtime
antes do Init privado do avatar:17=4,20=255,25=4. O banco é próprio e fixo,
com três palavras16 e sem alocação dinâmica. Ele persiste entre as entradas
Init/Dynamic/Main do mesmo host. Um novo host começa com novos valores.

## Referência e limites

FreeSO main `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL-2.0:

- `VM.cs:223–229`: Init da VM cria38 globals e define20=255,17=4,25=4.
- `VMTS1ActivatorNew.cs:423–433`: importação de IFF copia SIMI e aplica
  esses três overrides; o activator antigo faz o mesmo.
- `VM.cs:919–925`: restore de VMMarshal atribui os globals salvos, sem
  esses overrides. Esse contrato não é implementado por esta unidade.
- `VMExpression.cs:185–186`: IsFlagSet usa flags numerados a partir de1.

GameEdition255 é a política da referência de considerar todas as expansões.
Não foi deduzido do nome da instalação, do executável ou de pastas encontradas;
não prova qual máscara o jogo original produziria. O submódulo FreeSO de
Simitone contém as mesmas atribuições, mas tem sua revisão própria.

Esta unidade fornece somente três globals. Clock e entidades continuam
sem seus backends; não se oferece um array de38 zeros para mascarar campos
não inicializados como SelectPerson/CurrentFamily. O estado não é um
carregamento completo de VM nem da casa.

## Roteamento e proveniência

Scope6 consulta primeiro o banco de bootstrap, quando inicializado. Outros
índices seguem para LotContext, se uma casa foi fornecida. Writes de17/20/25
mudam somente o banco de runtime; valor zero continua um valor válido, sem
cair para SIMI. Outros writes seguem a cobertura anterior de LotContext.
Bancos pertencem ao host, sem mudanças nos recursos comerciais.

Init da VM também existe sem casa na referência: o host oferece os três
globals com `--initialize` mesmo sem `--lot`. O modo de probe explícito
continua sem bootstrap; seu global20 permanece unsupported, mesmo com SIMI.

O JSON inclui `global_bootstrap`, perfil e valores iniciais/atuais dos três
globals. Cada leitura scope6 inclui origem `runtime_bootstrap` ou
`lot_context`. Este último identifica o contexto mutável do lote, sem afirmar
que um valor depois de escrita ainda coincide com a palavra serializada.

## Gates nativos

Usar o comando de [avatar-neighbor-validation.md](avatar-neighbor-validation.md),
com House01/zoning/`--initialize --neighborhood --seed 1`. Para o segundo
personagem, trocar User00000.iff por User00003.iff. Os dois continuam visitantes
sem família ativada; a casa ainda não é restaurada como entidades.

Resultados:

- Bob: Init180+Main110=290 instruções/1 tick, antes264.
- Vizinho45/família3: Init180+Main111=291/1 tick, antes265.
- Ambos leem GameEdition20=255, origem runtime_bootstrap. O flag1 do gate
  PersonGlobals8193 pc26 testa máscara0x01 e segue true.
- As consultas seguintes de lote usam global10=1 do LotContext.
- Main chega a PersonGlobals8286 pc99, opcode31, operandos
  `[193,215,99,137,132,10,0,0]`, GUID `0x8963d7c1`, modo4 com destino
  StackObjectID. Essa busca ainda é unsupported; saída3, host_failed=false.
- Seed final16610512366814951206. Os três globals conservam seus valores
  iniciais nesses probes.

## Verificação

Fixtures CLI verificam os três globals desde Init até Main, com/sem lote;
SIMI com valores runtime diferentes não substitui o bootstrap. Escrita zero
permanece durante Main, sem alterar o IFF; novo host reinicializa seu estado.
Probes antigos continuam sem esse perfil. Todos os índices de clock/entity
anteriormente unsupported continuam falhando, inclusive com palavras SIMI.

Debug e ASan/UBSan:56 testes passaram e1 X11 foi pulado (57 configurados).
CLI foi repetido após a correção da fixture de zoning e do campo de proveniência.
LeakSanitizer ficou desativado por ptrace. Não houve execução comparativa do
original, lote jogável ou teste de consoles.

Relatórios Debug/ASan completos coincidem byte a byte, nos arquivos ignorados
registro arquivado não distribuído:

- bob: `e4a5a98eef0e54516b48d95f87c7df2e6431abff9e0d88c965ec36c1306e2986`.
- family3: `e50ea5f8058a0b11ed19bc020a75b60d66274bba562a9ed55599ae8baeb76236`.

## Próxima busca de entidade

Leitura estática posterior identificou GUID0x8963d7c1 como
`Objects.far / HelpSystem.iff / OBJD16807 "Help System"`, versão138, MasterID0.
A consulta é SetToNext ObjectOfType4, não enumeração de vizinhos.

Referência: `VMSetToNext.cs:84–85,105–173,263–307` seleciona o menor ObjectID
estritamente maior que o valor atual, sem wrap em modo4. Ausência retorna false
e preserva o destino. `VMObjectQueries.cs:182–193,268–272` indexa o GUID exato
do OBJD; não usa MasterDefinition como alias nem expande o mestre em partes.

O GUID não aparece nos tipos/mapeamentos serializados de House01 ou House03.
Isso é ausência no save, não prova de ausência numa casa runtime: controladores
de startup ainda podem criar objetos. Não foram examinados todos os BHAVs de
bootstrap. O World atual do host é construído, não uma restauração de ObjM.

BHAV8286 "begin visiting": pc98 zera StackObjectID; o gate pc99 false segue
pc80. Se encontra Help System, pc100–102 preparam argumentos e pc103 chama
PushInteraction28. Esses fluxos são análise estática, não avanços já executados.
