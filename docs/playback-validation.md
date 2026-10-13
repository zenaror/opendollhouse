# Animação silenciosa ligada à VM — 2026-10-13

A primitiva44 agora inicia e acompanha um clipe principal no perfil play-and-wait.
O probe da cadeira entrega `xevt100` e toma sua branch false até a primitiva32.
Não conclui sentar/levantar, não mostra animação, não aplica CFP aos ossos e não
comprova comportamento do executável original.

## Implementação e contrato

`Playback` recebe World e um resolver explícito de identidade imutável do clipe.
Cada controlador atende um avatar e uma VM estáveis. Mantém frames float32,
eventos na ordem do arquivo, fila de códigos, contador e ticket de espera.
Usa o maior frame count entre motions e velocidade30/25 por tick, dobrada somente
com WalkStyle1 e hurryable. Backwards inicia no último frame; a fronteira negativa
usa conversão a int, como a referência. O perfil preserva seleção de IDFromParam,
destino Args0/local e ExpectedEventCount. Padding não é interpretado. Flags/modes
fora do perfil, reset, loop e carry produzem diagnóstico explícito.

Em cada tick consecutivo, o controlador primeiro entrega evento/fim enfileirado
anteriormente, depois avança frames e observa eventos. O host retoma a VM após
essa chamada. Assim um evento detectado no tick145 só ramifica no tick146. Isso
preserva a defasagem entre observação e entrega; ainda não é o scheduler completo
do original. Eventos simultâneos drenam em ordem, podendo ramificar de imediato
na reentrada da44. O clipe persiste depois da branch false. Quando chega ao fim,
ExpectedEventCount pode gerar códigos sequenciais antes de retornar true.
`xevt100` não incrementa o contador. Texto de código inválido produz0 conforme
o parse da referência. `footstep` é registrado como dado bruto; não há som ou
serviço de passos. Outras propriedades, como sound/dress, são recusadas.

A VM recebeu `VmFrameWrite` para argumentos/locais e `run_generation()`, que muda
em toda tentativa de start. Completion com ticket/index/bank inválidos não grava
nem consome a espera. Completion válida grava antes de tomar a branch. Outcomes
imediatos validam a escrita; wait/unsupported não podem carregar output. Reinício
invalida também o clipe persistente após um evento, sem depender do contador de
instruções que start zera. Reuso de entity ID exige a mesma geração do avatar.

Cancelamento explícito do host, remoção/reuso do avatar ou orçamento de ticks
esgotado limpam o estado e entregam false à espera ainda correspondente. Esse é
um contrato ODH de cancelamento; não se afirma equivalência com cancelamento de
ações no TS1. Callbacks não podem reentrar ticks/cancelamento. Não há timeout
que transforme uma animação incompleta em sucesso.

Budgets próprios: 1.048.576 frames/ticks por clipe e 4.096 eventos/propriedades
e itens de fila. Mais de255 xevts que incrementariam o contador é recusado, sem
imitar overflow silencioso de byte. O CLI limita a evidência a4.096 eventos, marca
`animation_event_limit_reached` e para de avançar; `animation_events_truncated`
indica apenas eventos efetivamente descartados. Status waiting permanece waiting.
Não são limites atribuídos ao original ou benchmarks de consoles.

## Resolução no probe

`--animation-probe FAR BCF_ENTRY --age N` exige o perfil route/snap e OBJD explícito.
O resolver da ferramenta usa apenas o IFF configurado como CodeOwner e a coleção
BCF fornecida, com identidade compartilhada estável. Source Object0 e
StackObject65536 (somente objeto2) são suportados. IDFromParam converte source0 em
65536; Misc3 é distinto e não é usado como substituto. Global/PersonStock e
outros objetos exigem provedores futuros.

Inicializa person word58 (idade) com21 por padrão e object word17 (WalkStyle) com0.
Idade<18 seleciona tabela AnimationTableID+1; o ID da tabela é uint16 com wrap.
Tabela ausente permite fallback129 adulto ou130 criança; índice/idioma ausente
não permite fallback. Idioma1 e índice zero-based são explícitos. Nome de clipe
usa ASCII casefold, com rejeição de duplicatas. Não há catálogo/precedência global
de recursos ou inicialização automática de avatar pelo original.

## Evidência verificada

Os 16 testes CTest passaram em Debug e ASan/UBSan. A suíte de serviços tem74
checks sintéticos; a nova suíte Playback tem35. Cobrem output atômico, respostas
antigas/duplicadas, restart, xevt100, local/argumento, fim após eventos, geração
sintética, simultâneos, backwards e fronteira negativa, haste, IDFromParam,
troca de clipe, entidade reutilizada, cancelamento, budgets e serviços ausentes.
Testes CLI usam apenas recursos sintéticos e cobrem adulto/criança, idade18,
fallback, wrap da tabela, idioma explícito, ambiguidades e teto de evidência.
ASan/UBSan não apresentou diagnósticos; LSan continuou desativado por ptrace.

O comando real abaixo consulta os mesmos recursos identificados em
[animation-validation.md](animation-validation.md), além de Global.iff SHA256
`bdfd8456abceaa3e44f0a9754815248ea1e80c7b0df4fbaff53843fae3731478`:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --snap-probe --object-def 16808 --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff --animation-probe '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf --age 21
```

Resultado: saída3/fault esperado `unsupported primitive 32`, 19 instruções,
3 frames VM e146 ticks de simulação. Há33 movimentos de roteamento, posição
final(64,64,1,direção2), recipiente2/slot0 e postura1. A animação adulta é
`a2o-sit-left`, com161 frames; no ponto de parada o frame float32 é135.599854,
endfalse, fila vazia. Todos os15 eventos brutos foram observados; último4466
`xevt100` no tick145, seguido da branch false para BHAV1003 PC15 no tick146.
Os operandos da32 começam com GUID59C389BC. O clipe ainda não terminou.

Relatórios locais ignorados: `reports/chair-playback.json` e versão sanitizada,
JSON byte a byte idêntico e stderr vazio. Não houve escrita nos originais.
Reprodução de eventos aqui significa execução deste perfil ODH com metadata
comercial; sons, skeletons, pixels e execução do original permanecem ausentes.

## Referências e próximo passo

Contrato investigado no FreeSO fixo
`4c6b3e8f5835b228723caea3c9f683c62f244f73`:
[VMAnimateSim.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Primitives/VMAnimateSim.cs),
[VMAvatar.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Entities/VMAvatar.cs),
[VMAnimationState.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Model/VMAnimationState.cs),
[VMAnimationScope.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.simantics/Engine/Scopes/VMAnimationScope.cs).
Licença declarada MPL2.0; nenhum runtime C# foi incorporado. A ordem de arquivo,
parse de xevt e temporalidade são contratos da referência ainda sem comparação
com `Sims.exe.bak`.

Próxima unidade: investigar criação32/posicionamento23 e os GUIDs da branch
de evento, preservando entidades e estado da animação. Depois completar reset,
demais serviços/recursos e cenário, carregar skeleton e compor visual Linux.
Teste silencioso não substitui a versão Linux funcional solicitada.
