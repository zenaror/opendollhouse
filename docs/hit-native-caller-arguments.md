# Argumentos do caller nativo de PlaySound

Análise estática de2026-10-15, sem executar o binário ou alterar dados/Ghidra.
Contextualiza o modo diagnóstico de áudio. A regra bruta abaixo já pode ser
usada por --audio-caller-args; ver hit-caller-resolution-validation.md.
Não adotar os antigos argumentos de
fixture[origem,1024,0] como defaults do caller.

## Identidade e método

Entrada externa Sims.exe SHA256
6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2,
PE32/x86, base0x400000. Equivalência ao arquivo .bak permanece não comprovada.
HOT externo SimsGeneratedHitSource.hot SHA256
cf324a77668c04f15ec56e78e905e0b8b3219807ce959c60bca68b9e81afb21d.
Para os endereços listados nesta imagem, offset de arquivo=VA-0x400000;
não é regra geral de PE. Exportações anteriores em registro arquivado não distribuído foram
conferidas com leituras objdump e bytes atuais. Trechos selecionados novamente
em registro arquivado não distribuído; não são execução original.

## Cadeia observada

| Ponto | VA | Offset |
| --- | --- | --- |
| Dispatcher BHAV mascara opcode com0x7fff |0x452288|0x052288|
| Entrada23 da jump table aponta para0x452433 |0x452506|0x052506|
| Case chama handler0x452862 |0x452437|0x052437|
| Handler chama wrapper de nome0x57a7f7 |0x452afd|0x052afd|
| Wrapper chama resolver0x57348c |0x57a8c9/0x57a950|0x17a8c9/0x17a950|
| Resolver aplica overrides não zero |0x573718–0x573735|0x173718–0x173735|
| Resolver chama dispatch0x57376b |0x573746|0x173746|
| Dispatch invoca vtable+0x30 |0x5738e4/0x573917|0x1738e4/0x173917|
| Entrada da vtable contém0x57dc59 |0x671aa4|0x271aa4|
| Inicializador escreve variáveis1/2/3 |0x57dcc4/0x57dcd4/0x57dce4|0x17dcc4/0x17dcd4/0x17dce4|

Em0x452873–0x452896, flags vêm de operand+4. Se flags!=0xff e bit0x02 está
ativo, seleciona word do frame+4; caso contrário word da instância+0xf0.
O valor int16 é estendido com sinal e resolvido via0x407498. FWAV vem de
0x582e2a; em0x452af1–0x452afd passa origem e nome resolvido ao wrapper.
Este também reconhece source=-2 como sentinela de origem em cache; implementar
a API geral exige esse contexto, sem inventar fallback para source0.

No wrapper, EBX é zero. Em0x57a8b8/0x57a8bc faz push EBX para argumentos3/2;
0x57a8b9 estende DI com sinal e0x57a8bd faz push da origem como argumento1.
Após preparar o nome, chama o resolver. O caminho alternativo faz o mesmo.
Portanto o caller fornece a tripla bruta `[origem_int16,0,0]` nesse caminho.

## Overrides EventMapping

Registro0x5713dc–0x5713f9 associa seção EventMapping (string0x6ceb54) ao
callback0x5712a2, que chama0x5712b5. O parser lê até6 campos separados por
vírgula, preenche campos finais omitidos com zero em0x571372–0x571383 e chama
registro0x5724f5. Alocação/cópia0x576bb5/0x577039 produz este layout:

| Campo CSV após `=` | Offset no node | Uso demonstrado |
| --- | --- | --- |
|0|+0x10|Tipo de evento|
|1|+0x14|Track/event identifier|
|2|+0x18|Override do argumento1, se não zero|
|3|+0x1c|Override do argumento2, se não zero|
|4|+0x20|Override do argumento3, se não zero|
|5|+0x24|Armazenado, sem interpretação concluída|

A substituição é independente por componente: override não zero substitui
caller; zero conserva caller. O read_hot agora também preserva/resolve2–4 com
sinal e diagnóstico para campos não resolvidos. O sexto campo segue sem
interpretação; não converter simbolismo ausente em valores adivinhados.

HOT linha5036: hungry_sated_vox=kSndobPlay,3979,0,0,0,0;
equate kSndobPlay=1 na654. Para esse registro resolvido, a tripla continua
`[origem,0,0]`. O1024 na linha5778 pertence ao Track3979, campo Volume;
kArgsIdVolPan=2 está na704. Não é R2 fornecido pelo caller dessa chamada.

## Limites de interpretação

Raw arguments e controles efetivos são camadas distintas. Dispatch ArgStyle2
registra origem por0x574581, computa valores espaciais em0x57488f, chama o
inicializador com a tripla inalterada e depois usa vtable+0x40/+0x48 em
0x5738ee/0x5738f8. Setters0x57d873/0x57d899 escrevem variáveis19/20.
Inicializador também possui escritas condicionadas por estilo em0x57de08–0x57de27.
R2 bruto zero não demonstra ganho final zero. Style2 exige argumento1 não zero
em0x5738af; não extrapolar para origem0/outros estilos.

Helper0x57320a contém0x400/0x200, bounds e consultas de posição. Unidades e
interpretação DSP completa permanecem abertas. Próximos pontos:0x57488f,
0x57320a e escritor pela vtable+0x84. Preservar source callback comprovado em
hit-source-callback-validation.md, distinto do GetSrcDataField do Simitone
que ignora src. Não afirmar equivalência dinâmica ou fidelidade do áudio.
