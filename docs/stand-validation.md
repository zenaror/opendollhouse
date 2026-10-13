# Sequência de sentar e levantar — 2026-10-13

O host de teste executa sentar e a função Stand do OBJD no mesmo World.
Ambas as fases retornam true; o avatar deixa a cadeira e o BHAV grava postura0.
Isso não estabelece uma ação contínua do jogo original, scheduler completo,
pose visual correta ou primeira versão funcional Linux.

## Identificação da cadeia

TTAB128 da cadeira barata tem apenas action1001/test1002. Stand é uma função
do objeto: OBJD16808 legado (UsesFnTable0) informa Main1000, Sit1004 e Stand1006
(IDs hex; o JSON usa números decimais4096/4100/4102). O leitor de funções
confere o prefixo de68 bytes: Main offset10, UsesFnTable44, Sit64, Stand66.
Definições UsesFnTable!=0 exigem OBJf, ainda não implementado neste dispatcher.

No IFF original, Stand4102 tem40 instruções/4locals. Usa PersonData37 para
selecionar/repetir uma direção de saída, chama4106 para snap, anima STR129
índice5/6/7/8 conforme o lado, faz reset44 ID0 e grava Posture0. Saídas bloqueadas
podem levar a Global370 e serviços ainda ausentes; não são convertidas em sucesso.

Global382 (“ensure Sim is standing”) é outra entrada que usa opcode20 Function9;
FreeSO mapeia esse código ao entrypoint27/Stand do objeto. Seu routing frame
também despacha entry27 ao sair de um recipiente sentado e guarda STAND_FUNC.
O CLI atual seleciona diretamente a função legada do recurso explícito, com
Calleravatar1/Calleechair2/StackObjectchair2; não implementa ainda20, queue ou
retomada de uma rota suspensa pelo dispatcher completo.

A Main1000 da cadeira é distinta: Global280(idle), MySlot0, teste de definição
do ocupante tipo2 e410D (“inc comfort”). Threads, scopes de definição/tuning,
Sleep/RNG/interrupts e motivos desse loop ainda exigem implementação; `no_thread`
da notificação anterior continua sendo a descrição honesta do probe.

## Snap de saída e estado atômico

SLOT128 tipos3 índices2/3/4/5 são saídas no chão, SnapTargetSlot-1,
flags4097/4112/4160/4100 e offsets norte/sul/oeste/leste. A conferência raw
float32 mostrou **±16**, bits41800000/C1800000. O relato inicial de±15 estava
errado e foi corrigido antes do checkpoint. O próprio probe real recusou o
perfil errado; não se alteraram os assets para encaixar na fixture.

Snap aceita somente esse perfil v10 exato, objeto em direção cardinal,
offsetZ0, height0/Facing-2/Resolution16, proximity16 e demais campos conhecidos.
Rotaciona posição/direção em inteiros, pede validação antes de alterar World,
e faz `place_on_floor`: valida limites, aloca a lista de descendentes, depois
esvazia slot antigo, solta attachment e atualiza posições. Descendentes
posicionados seguem a mudança; direções próprias e relações internas continuam.
Não modifica postura; quem a grava é o BHAV.

Callback inexistente é unsupported. Destino bloqueado/inválido retorna false
sem soltar o avatar nem alterar pose/slot. Identidade, geração e pose são
reconferidas após o callback; reuso do ID não aceita movimento antigo.
NaN/outros offsets, diagonais, transferências de outro recipiente e perfis
geométricos diferentes são recusados. O CLI fornece somente walkability do grid
construído e limites de nível/direção. Não valida todos os footprints, avatars,
raycasts/rooms ou regras do original.

## Host e recursos explícitos

`--stand-after` exige as opções de animação/reset/som/snap e função Stand
legada disponível. Só começa a fase seguinte se a anterior retornar true.
World e endereços dos controladores permanecem; Vm.start cria a próxima execução,
com nova geração/tickets e temps zerados. Esse restart de VM não é uma queue
de ações fiel; efeitos sobre loops em falhas e retomadas ainda exigem o scheduler.

`--animation-extra ENTRY.bcf` permite até16 coleções extras no mesmo FAR,
rejeitando seletores repetidos e nomes ambíguos após ASCII casefold. Todas as
entradas aparecem na proveniência. Trace tem phase/total_step; budget10000
instruções e relógio são globais entre as duas fases. post-ticks só seguem a
fase final terminal e não transformam budget_exhausted em conclusão.

Arquivo novo consultado: `Animation.far/adult-chair-stand-new.cmx.bcf`,4472 bytes,
SHA256 `dedc09de80f1c1b505ef566ed7af34d286d9806aef8dc09569c5112bbb7d1fb0`.
Contém `a2o-stand-behind`206frames, `a2o-stand-left`158, `a2o-stand-right`148,
`a2o-stand-front`115; nenhum evento temporal nesses quatro clipes. CFP de Stand
não foi decodificado nesta unidade. IFF/Global/demais BCFs mantêm os hashes
registrados nos checkpoints anteriores. Nenhum asset comercial foi versionado.

## Evidência verificada

CTest19/19 passou em Debug e ASan/UBSan. Snap/prefix tem332 checks sintéticos,
incluindo16 combinações de saída/rotação cardinal, atomicidade, descendentes,
callbacks e limites. Formats tem387 checks, incluindo truncamento do prefixo
funcional e seleção de UsesFnTable. CLI valida duas fases, interrupção em false/
fault, slot retido em falha, metadata ausente/OBJf não suportado, recursos extras
e orçamento compartilhado. Correções de fixtures/valores foram revalidadas.
LSan permanece desativado por limitação ptrace.

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff --animation-probe '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf --age 21 --sound-probe --reset-probe a2o-sit-loop.cmx.bcf --post-ticks 240 --stand-after --animation-extra adult-chair-stand-new.cmx.bcf
```

Probe nativo: saída0, stderr vazio,56 instruções totais. Fase sentar1001:
29 instruções/169ticks, posição(64,64,1,dir0), recipiente2/postura1.
Fase Stand1006:27 instruções/133ticks, posição(48,64,1,dir6), recipiente0/
postura0. Total302 ticks de execução +240 post-ticks =542. A saída escolhida
usa RouteEntryFlags64 e stand-left, não front. Reset ocorre antes de Posture0:
o loop final observado continua `a2o-sit-loop`, frame55.9998131/116, endfalse.
Não foi forçado um standing-loop para esconder a ordem dos dados.

Debug e sanitizers produziram JSON byte a byte idêntico, ambas fases true,
sem diagnósticos em stderr. Relatórios ignorados em
`reports/chair-stand-sequence.json` e versão sanitizada. Nenhuma execução do
original, áudio, rendering ou teste em hardware foi realizado.

## Referências e continuidade

FreeSO fixo `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2.0 declarada:
[OBJD.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/OBJD.cs),
[VMRunFunctionalTree.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMRunFunctionalTree.cs),
[VMRoutingFrame.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMRoutingFrame.cs),
[VMSnap.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMSnap.cs),
[VMSlotParser.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/VMSlotParser.cs).
Essas referências não provam temporização/pose do executável original.

Próximos passos: main-thread/queue/scheduler e retomada funcional de rota;
esclarecer reset antes de Posture0/standing-loop no original; skeleton,
interpolação/compositor Linux; depois áudio/HIT e lote/tempo/necessidades/
persistência. A meta funcional inteira continua ativa.
