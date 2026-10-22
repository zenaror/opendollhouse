# Prévia do launcher Linux e ligação de identidade

`tools/play_linux.py` encadeia a escolha de lote, a resolução da família e a
preparação da prévia residencial com `odh-person`. O vínculo entre um morador
na vizinhança e sua entidade no lote é feito pelo GUID: os números não são os
mesmos. Em House07 da instalação original, por exemplo, a vizinhança lista os
moradores como 48 e 47; o World salvo guarda esses mesmos GUIDs nos objetos
105 e 137. O launcher consulta `odh-saved-world`, exige uma única entidade do
tipo avatar para o GUID escolhido e falha se não encontrar correspondência
única.

## Verificação reproduzível

```sh
cmake -S . -B /tmp/odh-play-linux-build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/odh-play-linux-build --target odh-session-entry odh-saved-world odh-neighborhood-window odh-person -j4
ctest --test-dir /tmp/odh-play-linux-build --output-on-failure -R 'session_entry_cli|play_linux_cli|neighborhood_window_cli'

python3 -B tools/play_linux.py \
  --game-root '../referencias/The Sims' \
  --binary-dir /tmp/odh-play-linux-build --mode 1 --house 7 --plan-only
python3 -B tools/play_linux.py \
  --game-root '../referencias/The Sims' \
  --binary-dir /tmp/odh-play-linux-build --mode 1 --house 7 \
  --resident-id 47 --plan-only
```

Em 2026-10-22, a build Debug e os três testes de CLI passaram. Com os dados
originais externos, a preparação escolheu por padrão `User00000.iff`, ligou o
ID da vizinhança 48 ao objeto salvo 105 e encontrou dois membros. Ao escolher
o ID de vizinhança 47, usou `User00001.iff` e o objeto salvo 137. Os testes
sintéticos também confirmam que mapeamento ausente ou duplicado é recusado.
Uma captura headless do primeiro morador desenhou a casa e o Sim, mas o Main
terminou incompleto (código 3); isso não comprova um ciclo de jogo concluído.

## Limites desta etapa

A janela do mapa e a prévia do lote ainda são processos/janelas separados. O
launcher executa somente o Main do morador escolhido; não implementa a sessão
contínua dos outros moradores, retorno à vizinhança, criação ou edição de
famílias, salvar/continuar, destinos e sistemas das expansões. A janela
interativa do mapa→lote ainda não foi validada aqui. Esta etapa remove apenas
a troca incorreta de identidade e torna reproduzível a preparação do caminho;
não altera o critério completo de [primeira build jogável](linux-playable-scope.md).
