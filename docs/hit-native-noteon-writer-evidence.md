# Writer do wrapper no NoteOn global

Pesquisa estática de 2026-10-20 para a imagem TS1 PC
`../referencias/The Sims/Sims.exe`, PE32 i386, image base `0x400000`, SHA256
`6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
O executável e os dados comerciais não foram executados nem modificados.

## Contrato comprovado

No NoteOn global em `0x57f8dd`, o virtual do wrapper `+0x84` recebe
`(38,1,0)`. Para a implementação criada pela factory `0x5800bf`, o vtable
`0x671c84` aponta esse slot para `0x57c276`. O método encaminha o valor e o ID
ao writer de banco `0x5805fa`; o terceiro argumento não é lido.

O construtor `0x57c1e0` instala um banco local que cobre IDs 17–47, com os
valores em `wrapper+0x38`. Assim, esse writer grava o DWORD
`wrapper+0x8c = 1` para o ID 38 e retorna sucesso em AL. No caminho concreto
inspecionado, não chama o backend, não escreve no banco/flags do controller e
não altera os campos de ativo, dirty ou modo de streaming do wrapper. O caller
NoteOn não usa o retorno do writer.

Isso esclarece o callback `wrapper_write(38,1,0)` em
[hit-global-note-on-validation.md](hit-global-note-on-validation.md): ele
representa a gravação no banco local do wrapper. Continua distinto do getter
38 do controller, que condiciona a escrita, e do getter 38 do Patch vivo, que
depois do NoteOn condiciona A e B. A fórmula dos controladores A/B está em
[hit-native-control-formulas-evidence.md](hit-native-control-formulas-evidence.md).

## Limites e reprodução

A conclusão cobre a implementação do wrapper indicada pelo vtable concreto,
não todas as subclasses, consumers posteriores do ID 38, mutações ao longo de
todo o ciclo de vida ou equivalência acústica. Não deduz streaming nem DSP
dessa escrita. A evidência é estática e não demonstra comportamento executado
do jogo.

O relatório completo, extractor, assembly e manifests estão no workspace em
`reports/hit-native-noteon-providers-20261020/`; o relatório tem SHA256
`c45e268157aed92476a73dc6b8b96d7af82ce6f30020929b0343834b86e6e191`. O
extractor valida cinco ranges novos (329 bytes), cinco ranges previamente
documentados, mapeamento PE/GNU BFD e hash do executável antes/depois. A
evidência reutilizada dos helpers A/B conserva os limites registrados em sua
documentação. O relatório completo também foi importado como fonte OMM no
escopo `OpenDollhouse`.
