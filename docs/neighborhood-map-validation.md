# Janela de mapa e seleção de lotes no Linux

Milestone sobre `0def05287ebdebfd90a067bd184f0e6841ee6456`, em 2026-10-22.
`odh-neighborhood-window` lê o bitmap diretamente de `UIGraphics.far`, desenha
marcadores nas coordenadas de `LotLocations.iff` e retorna a casa clicada. Não
abre o lote, executa VM, altera recursos/saves nem estabelece gameplay jogável.

```sh
cmake -S . -B /tmp/odh-neighborhood-build -DCMAKE_BUILD_TYPE=Debug -DODH_SDL_AUDIO=OFF -DODH_X11=ON
cmake --build /tmp/odh-neighborhood-build -j4
/tmp/odh-neighborhood-build/odh-neighborhood-window \
  --user-data '/caminho/The Sims/UserData' \
  --ui-graphics '/caminho/The Sims/UIGraphics/UIGraphics.far' --mode 1
```

Para conferir o quadro antes de abrir qualquer janela, acrescente
`--export-frame /tmp/neighborhood.ppm`. O programa salva o mesmo fundo e os
mesmos marcadores que seriam enviados à janela e imprime um resumo JSON; essa
opção não conecta ao X11. Abra o PPM em um visualizador comum. Também é possível
informar `--language 1..20` para compor os rótulos no idioma escolhido. A
exportação verifica o preparo do quadro, não a apresentação pelo sistema de
janelas.

O executável só é construído quando X11 está habilitado e disponível. A API
portável `odh_neighborhood_picker` define configuração, renderização CPU e
hit-test; janela, eventos X11, filesystem e leitura de arquivos ficam no host.
Não há dependência do executável original nem extração de assets para o checkout.

| Modo | Fundo FAR exato | Primeira camada estática | Escala do marcador |
| --- | --- | --- | ---: |
| 1 | `Nbhd\NScreen.BMP` | `Nbhd\DiffN1-N2_8.bmp` em (0,0) | 1 |
| 2 | `Downtown\DScreen.bmp` | `Downtown\dscreen00.bmp` em (0,0) | 1 |
| 3 | `VIsland\visland.bmp` | `visland_waves001.bmp`, `visland_port.bmp`, `visland_trees.bmp`, em (0,0) | 1 |
| 4 | `Community\NScreen_unleashed.bmp` | `NScreen_unleashed_waves001.bmp` em (0,0) | 2 |
| 5 | `Studiotown\DScreen.bmp` | `DScreen_top_layer.bmp` em (0,0) | 1 |
| 6 | Sem mapa: `unsupported` | — | — |
| 7 | `Magicland\DScreen.bmp` | `DScreen_waves1.bmp` em (0,62) | 1 |

As camadas vêm da configuração do Simitone em `UINeighbourhoodSelectionPanel.cs`
na revisão acima. O Simitone marca como transparente três cores de UI:
`#FF00FF`, `#FE02FE` e `#FF01FF` (`UIGraphicsProvider.cs`). O leitor BMP
genérico do ODH continua retornando pixels opacos; ao montar mapas, o host troca
essas cores do fundo por um matte escuro e ignora essas cores nas camadas. Cada
camada usa o primeiro quadro listado; animação, transição cruzada, som e zoom
continuam fora do escopo deste recorte.

A referência estática é o array `Neighborhoods` e `PopulateScreen` de
`Client/Simitone/Simitone.Client/UI/Panels/UINeighbourhoodSelectionPanel.cs`,
Simitone `4906db3156616bfaffc10ce0963cf08062975fef`, Ms-PL; revisão e créditos em
[references.md](references.md). No modo 4, `Scale=2` é passado ao botão da casa;
a posição e o fundo permanecem em pixels 800×600. ODH mantém essas posições,
mas usa quadrados próprios de raio 6/12, com borda preta. Não reproduz os
sprites, animações, efeitos, música, zoom ou UI de famílias do Simitone.

Verde indica arquivo HouseNN presente sem associação FAMI; âmbar indica FAMI;
cinza indica arquivo ausente e impede seleção. FAMI é evidência de associação,
não prova de ocupação runtime; arquivo regular não prova lote válido. Cada
quadrado inclui sua borda no hit-test e é recortado ao viewport. Marcadores são
ordenados por y crescente, mantendo ordem serializada em empates. O último
marcador desenhado ganha o clique sobreposto; um marcador cinza no topo bloqueia
o clique em marcadores atrás. Cliques fora do viewport ou no fundo não selecionam.
Sentinela 99, idioma 1, validação e limites CSV seguem o
[catálogo](neighborhood-catalog-validation.md). Modo 6 retorna sem abrir dados
ou display; modos fora de 1..7 falham. Argumentos ausentes/duplicados falham.

Um clique válido fecha a janela e produz JSON `evidence=map_selection_only`,
`status=selected`, `selected_house=N`; Esc ou fechar a janela produz
`status=cancelled`, `selected_house=null`. A seleção não inicia uma sessão de
lote. Ainda não há números/texto de lotes, hover, navegação por teclado, escolha
de modo na janela, idioma persistido, logs integrados ou gestão de famílias.

## Validação

### Investigação da tela “Loading” — 2026-10-24

- Capturas separadas mostram o cartão “Loading” às 00:12:51 e o mapa pronto às
  00:13:04. No código, a janela mostra o cartão antes de buscar rótulos em
  `UIText.iff` e preparar o mapa. Essas capturas confirmam uma espera durante
  trabalho síncrono, mas não identificam qual etapa levou mais tempo.
- A entrada agora decodifica as cinco tabelas de menu usadas em uma só passagem
  pelo `UIText.iff`. `linux_ui_strings` verifica cache, idioma solicitado,
  fallback inglês, tabela ausente/duplicada e bloqueio de link para fora da
  pasta do jogo.
- A build com Pango exportou quadros 800×600 usando dados comerciais somente
  para leitura: modos 1–5 e 7 tinham todas as camadas (`missing_layers: []`).
  Modo 6 permanece explicitamente sem seleção, conforme o fluxo de Simitone.
  Essa exportação não abre a janela X11 nem confirma a duração total da espera.
- CTest Debug completo: 224 testes configurados, 221 aprovados e três testes
  de janela pulados por falta de acesso ao display (`linux_window_backend`,
  `linux_neighborhood_screen_actions`, `linux_move_out_integration`).

### Reteste depois da instalação do Pango — 2026-10-23

- `cmake --build build/linux-checkpoint -j2` concluiu e reconstruiu `odh-person`
  com o suporte Pango/Cairo instalado.
- `env -u DISPLAY ctest --test-dir build/linux-checkpoint --output-on-failure`
  passou 222/222. Os testes 120, 121 e 122 foram pulados porque esta execução
  foi feita sem display; isso não valida a janela interativa.
- Os seis modos disponíveis foram exportados de novo usando os arquivos
  externos do jogo, em modo somente leitura. Todos produziram 800×600, e o
  resumo informou `missing_layers: []` para cada modo.
- O teste CLI também confirma que camada decorativa ausente aparece tanto no
  JSON do exportador quanto no log, em vez de desaparecer sem aviso.
- A janela interativa ainda não foi confirmada: na sessão gráfica, ela recebe
  repetidos Esc e cancela o seletor. A captura direcionada à janela mostrou
  apenas “Loading”. Não atribuir esses eventos ao teclado físico sem investigar.

- `neighborhood_picker`: 110 checks sintéticos; mapeamento/recurso e tamanho nos
  seis modos, centros/bordas/fora da borda, FAMI/sem FAMI/arquivo ausente,
  sobreposição, viewport, extremos int32, escala/modo/imagem inválidos, cores,
  clipping, preservação da imagem e arrays exigidos pelo backend.
- `neighborhood_window_cli`: 40 checks sintéticos sem janela; argumentos
  inválidos, modo 6 sem recursos/display e recurso ausente nos seis modos.
- A exportação headless usa FAR/IFF/BMP sintéticos; verifica tamanho PPM, fundo,
  marcador e resumo JSON sem conectar ao X11.
- Em 2026-10-23, exportamos os seis quadros reais com `odh-neighborhood-window
  --export-frame` e os arquivos comerciais externos, sem abrir o X11. Downtown,
  Vacation Island, Unleashed Community, Studio Town e Magic Town mostraram fundo
  e lotes; os primeiros quadros e camadas de cada modo foram aplicados. O export
  não substitui a validação da janela interativa.
- A captura anterior preservava exatamente os pixels magenta dos BMPs fonte de
  Vacation Island e Magic Town. Depois da máscara e das camadas, nenhum dos seis
  quadros exportados contém as três cores-chave; isso é evidência de composição
  offline, não comparação integral com o jogo original.
- Build Debug completa e CTest: 168/168 passaram. No sandbox o teste
  `linux_window_backend` foi pulado porque o display não estava acessível;
  executado separadamente com acesso ao display, passou 1/1.
- Regressões focadas: LotLocations, catálogo CLI, FAMI, NBRS e BMP passaram.
- Teste separado do backend com display acessível: janela própria 128×96,
  comparação exata dos 12.288 pixels RGB e eventos direcionados à janela passou.
- A regressão sintética do exportador verifica as três cores-chave, a substituição
  por matte, uma camada colorida sobre a base e o caso de camada decorativa
  ausente, sem conectar ao X11. Também verifica ordem waves→port→trees em
  Vacation Island, transparência de uma camada superior, offset y=62 em Magic
  Town e diagnóstico de camada ausente tanto no modo base quanto no modo mágico.

Os testes GUI exigem acesso ao display X11; no sandbox, `linux_window_backend`
é pulado explicitamente quando o display é inacessível. Não interpretar esse
skip como PASS do backend. O teste separado acima usou acesso ao display.

Seis execuções visíveis com os recursos originais externos, somente leitura,
carregaram e selecionaram por clique direcionado apenas à janela do aplicativo:

| Modo | Clique (x,y) | Casa retornada |
| --- | --- | ---: |
| 1 | 162,118 | 1 |
| 2 | 187,236 | 21 |
| 3 | 112,103 | 40 |
| 4 | 480,84 | 1 |
| 5 | 70,282 | 81 |
| 7 | 70,244 | 90 |

Não houve captura do desktop nem cópia/alteração de recursos comerciais. Esses
casos confirmam leitura, renderização em janela e seleção; não estabelecem
comparação visual integral com o original, simulação ou funcionamento dos ports.
