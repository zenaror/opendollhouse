# Prévia do launcher Linux e seleção de morador

`tools/play_linux.py` encadeia a escolha de lote, a resolução da família e a
prévia residencial com `odh-person`. Para abrir um membro da família, o
launcher passa o GUID ao `odh-person`; o runtime resolve o número correto
quando já está restaurando o World do lote. Assim não se confunde o número do
morador na vizinhança com o número de seu objeto salvo, nem se lê o lote uma
segunda vez num processo auxiliar.

A tela do mapa foi extraída para `show_neighborhood_picker`, que recebe uma
janela Linux já aberta. O backend agora redimensiona a mesma janela sem trocar
seu identificador nativo, preservando a imagem e a entrada ao mudar de tamanho.
Isso deixa as peças prontas para o futuro fluxo bairro→lote numa única janela;
`odh-person` ainda não chama esse seletor nem reutiliza essa janela.

Em House07 da instalação original, a vizinhança lista os moradores como 48 e
47; os objetos correspondentes no World são 105 e 137. O smoke de 32 ticks
selecionou cada um pelo GUID e confirmou o GUID, o objeto e o arquivo de
personagem correspondentes. A saída 3 indica que o teste curto chegou ao seu
limite enquanto Main continuava aguardando; o relatório confirma ausência de
falha do host e da VM. Isso prova a seleção e uma execução limitada, não uma
ação jogável completa.

## Verificação reproduzível

```sh
cmake -S . -B /tmp/odh-play-linux-build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/odh-play-linux-build --target odh-session-entry odh-neighborhood-window odh-person -j4
ctest --test-dir /tmp/odh-play-linux-build --output-on-failure -R 'person_cli|session_entry_cli|play_linux_cli|neighborhood_window_cli'

python3 -B tools/play_linux.py \
  --game-root '../referencias/The Sims' \
  --binary-dir /tmp/odh-play-linux-build --mode 1 --house 7 --plan-only
python3 -B tools/play_linux.py \
  --game-root '../referencias/The Sims' \
  --binary-dir /tmp/odh-play-linux-build --mode 1 --house 7 \
  --resident-id 47 --plan-only
python3 -B tests/house07_resident_main_smoke.py \
  --binary /tmp/odh-play-linux-build/odh-person \
  --game-root '../referencias/The Sims' --ticks 32
```

Os testes de CLI cobrem GUIDs inválidos, o uso fora de um World salvo, seleção
no launcher e cancelamento do mapa. O smoke com dados comerciais externos
confirma que os GUIDs dos dois membros selecionam os objetos salvos 105 e 137.
Esses dados permanecem fora do repositório.

O build dos alvos `odh-neighborhood-window`, `odh-person` e
`odh-window-tests` passou. `neighborhood_window_cli` e `person_cli` passaram;
`linux_window_backend` foi pulado porque esta sessão não tem um servidor X11
acessível, então o resize ainda precisa de confirmação numa sessão gráfica.

## Limites desta etapa

A janela do mapa e a prévia do lote ainda são processos/janelas separados. O
launcher executa somente o Main do morador escolhido; não implementa a sessão
contínua dos outros moradores, retorno à vizinhança, criação ou edição de
famílias, salvar/continuar, destinos e sistemas das expansões. A janela
interativa do mapa→lote ainda não foi validada aqui. Esta etapa evita a leitura
duplicada do World e resolve o morador no runtime que já o restaura; não altera
o critério completo de [primeira build jogável](linux-playable-scope.md).
