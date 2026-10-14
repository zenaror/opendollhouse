# Apresentação nativa dos snapshots no Linux

Checkpoint adicional 2026-10-14. O backend X11 existente foi verificado no
servidor DISPLAY=:0 fora do sandbox. Dentro dele, o acesso ao display falhava e
o teste era pulado; isso não demonstrava ausência do servidor.

`odh-window-tests` passou em Debug e ASAN/UBSAN: os12.288 pixels RGB da janela
própria coincidiram, inputs inválidos foram rejeitados e teclas/actions/hold/
release/câmera/quit responderam a eventos sintéticos enviados somente à própria
janela. Não são eventos físicos de teclado. O teste fecha sua janela ao sair.

Um harness adicional ignorado, `reports/saved-window-smoke.cpp`, leu os PPM
publicados da House07: paredes erguidas com junções e paredes abaixadas no andar1,
ambos640x400. Apresentou os dois frames na própria janela Linux e leu apenas esse
drawable. **Todos os256.000 pixels RGB de cada frame coincidiram byte a byte**,
em Debug e ASAN/UBSAN. Nenhum input/snapshot/original foi escrito. O helper
somente apresenta RGB exportado; não executa o renderer por eventos de câmera,
a VM ou uma interação do lote. Não é um novo oracle da imagem do jogo original.

Arquivos usados:
`reports/house07-wall-caps-r0-z3.ppm` SHA
`d985015ebe010d9173bd6ca6c850ea5b98c604faf4fb9675b97defb9a71ca5eb` e
`reports/house07-wall-view-r0-z3-l1-down.ppm` SHA
`9f721977151d35dc4e15c4cd7066290f376d69a87c65b4134bbc48639711cf95`.
As imagens e bins/harness permanecem ignorados, com os recursos comerciais fora
do Git. LSan desativado por ptrace; não houve leak check. Essa evidência prova
apresentação/input do backend Linux sob o servidor local, não desempenho/execução
New3DS, Vita ou Android. A suíte88 da unidade734bff0 teve87pass1X11skip dentro do
sandbox; agora o target X11 antes pulado passou separadamente nos dois builds.
Não foi repetida toda a suíte fora do sandbox.

Próximo passo de UI: integrar câmera/input/story/walls ao renderer do World salvo,
com os dados lógicos preservados. Playback/threads/VM do lote continuam pendentes;
o objetivo funcional Linux permanece ativo.
