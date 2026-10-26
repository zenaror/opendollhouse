# Port completo para C/C++

## Direção vigente

O usuário esclareceu que quer **todo o jogo baseado no Simitone portado para
C/C++**, e não um jogo permanentemente em C# com algumas bibliotecas nativas.
Esta decisão substitui a restrição anterior de não iniciar a tradução integral.
O resultado precisa executar sem C#, CLR, .NET ou MonoGame gerenciado.
Migrar por etapas é a forma de execução, não uma redução desse destino.

O Simitone e sua revisão FreeSO continuam sendo as referências de comportamento
e de código. Portar o jogo não exige reproduzir serviços exclusivos de The Sims
Online que não participam do TS1. Todas as funcionalidades do Simitone para TS1
e os requisitos adicionais do original permanecem no escopo. Preservar licenças
e atribuições de código derivado. Assets comerciais continuam externos.

## Execução vigente (2026-10-26)

A instrução mais recente é não usar C# nem apenas corrigir a referência.
O trabalho segue diretamente no motor C++ existente: integrar os componentes
migrados ao seu caminho de execução, preservar os dados e evitar implementações
paralelas do mesmo sistema. O código Simitone é referência para a implementação,
não o aplicativo em que novas funções serão desenvolvidas. A ponte gerenciada
anterior e seu trabalho pendente ficam preservados fora desta frente.

## Estado real

O candidato anterior baseado no Simitone ainda depende principalmente de
C#/.NET e não é o produto final. A frente atual executa diretamente no
`odh-person` C++: o `Vm` existente já chama o executor compartilhado dos 21
operadores, sem hospedar C# nesta execução. A comparação inicial isolou uma
falha anterior de idioma, seguida por uma limitação na fila dos outros moradores.
Ambas foram corrigidas no programa nativo. No teste final da Casa 07, a ação
da geladeira retornou verdadeiro, a fila esvaziou, a sessão foi salva e reaberta
e continuou rodando. Isso ainda não comprova uma casa plenamente jogável.

Os novos bancos TEMP/TempXL nativos foram validados separadamente; o `Vm`
existente continua usando seus próprios arrays C++, sem cópia gerenciada.
Não apresentar esses bancos como já integrados ao aplicativo. Relatórios locais:
`reports/portability/native-state/`, `reports/native-vm-house07/`,
`reports/native-vm-expression-comparison/` e `reports/native-vm-resident-continuation/`.
Detalhes do checkpoint em [native-state-integration.md](native-state-integration.md).
Os resultados anteriores com ponte C# são históricos:
[evidência](native-expression-integration.md).

O C++ existente em `src/`, `include/` e `tools/` deve ser aproveitado quando seu
comportamento servir ao port. Não tratar o antigo motor parcial como equivalente
ao Simitone nem manter dois motores completos. Consolidar a implementação de
cada sistema portado; preservar o WIP anterior até a integração correspondente.

## Frentes do port e prova de conclusão

| Frente | Fontes e apoio atuais | Resultado exigido |
| --- | --- | --- |
| Execução das ações e estado do mundo | `tso.simantics`, `native/simulation`, antigo `src/vm.cpp` | Instruções, chamadas, variáveis, entidades, filas, necessidades, relações, inventário e relógio executados e armazenados em C/C++. |
| Rotas, objetos e construção | SimAntics, LotView e leitores/geometria C++ existentes | Sims andando e usando objetos; posicionamento, paredes, pisos, terrenos e alterações de lotes sem serviços gerenciados. |
| Dados e persistência | `tso.files`, `tso.content`, leitores, catálogos e codecs C++ existentes | Carregamento dos dados externos e CC; famílias, vizinhança, viagens e saves preservados pelo programa nativo. |
| Gráficos e animação | LotView, shaders, Simitone Client; malhas, esqueletos, sprites e renderizadores C++ existentes | Cenas e Sims desenhados pelo programa nativo, com recursos gráficos limitados por plataforma. |
| Interface e entrada | Simitone Client, recursos e textos originais | Menus, famílias, vizinhança, compra/construção, ações, idioma e controles nativos; interface original como destino final. |
| Áudio | HIT, leitores XA/PCM e mixer C++ existentes | Música, efeitos e vozes no programa nativo, com saída de áudio substituível por plataforma. |
| Aplicativo e plataformas | Bootstrap, caminhos, logs, janela e adaptadores | Executável completo sem runtime gerenciado; serviços de SO fora do núcleo e pacotes verificáveis por alvo. |
| Experiência completa | Escopo jogável e matriz de funcionalidades existentes | Sete expansões, viagens/atividades, famílias, CC, logs e aparência original, sem reduzir o objetivo a uma casa ou ação. |

Essas frentes representam dependências reais, não estimativas de porcentagem.
Uma frente só termina quando funciona no executável nativo, não quando existe
uma interface vazia ou um teste sintético isolado.

## Ordem prática

Prioridade reafirmada pelo usuário: migrar os sistemas TS1 do Simitone e sua
revisão FreeSO para C/C++ como trabalho principal. Testes servem para verificar
essas entregas; componentes isolados não substituem a migração do aplicativo.
Cada etapa deve conectar uma funcionalidade ao programa nativo e registrar o
que ainda falta para a paridade. Não retomar desenvolvimento da ponte C#.

1. Migrar a execução e o estado compartilhado, aproveitando BCON e fila já usados.
   O primeiro passo executado foi `VMExpression`: cálculos e comparações nativos,
   mantendo temporariamente o acesso às variáveis pela ponte de comparação.
2. Migrar acesso à memória, controle das rotinas e demais instruções; levar
   entidades, carregamento e salvamento para o mesmo núcleo. As chamadas à ponte
   precisam diminuir até desaparecer, não virar uma API permanente dependente
   do motor C#.
3. Integrar o núcleo em um aplicativo nativo desde que consiga carregar e avançar
   um lote. Migrar renderização, interface, áudio e entrada para esse aplicativo.
   Aproveitar os componentes C++ existentes quando validados; não recomeçar tudo.
4. Fechar fluxos completos e expansões no aplicativo nativo, com comparação à
   referência e investigação do original quando a referência estiver incompleta.
5. Validar e empacotar por plataforma, mantendo as restrições dos alvos limitados
   presentes durante toda a migração. A implementação Linux não define o núcleo.

Nos checkpoints anteriores, o candidato C# chamava componentes C++ para
comparação. A orientação atual encerra essa frente: implementar e executar
no aplicativo C++ existente. Preservar os resultados anteriores como evidência
histórica, sem ampliar a ponte ou tentar levar o CLR aos consoles.

## Verificação e limites

Cada sistema migrado precisa substituir o caminho real e conservar os efeitos
observáveis, incluindo erros, ordem das ações e estado salvo. Testes diferenciais
usam o código de referência; a execução integrada verifica ações do jogador.
Testes nativos ARM ajudam a detectar problemas de arquitetura, mas não provam
que o jogo inteiro roda no console. Gráficos, som, controles, memória e desempenho
continuam precisando de adaptações e validações próprias.

Linux, Windows, macOS Intel/Apple Silicon, Android, New Nintendo 3DS, Vita e
Switch permanecem alvos; Wii U permanece desejado. C/C++ remove a dependência
do runtime gerenciado, mas não promete automaticamente suporte a todos eles.
O escopo funcional continua em [linux-playable-scope.md](linux-playable-scope.md).
