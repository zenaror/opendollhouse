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

| Modo | Entrada FAR exata | Escala do marcador |
| --- | --- | ---: |
| 1 | `Nbhd\NScreen.BMP` | 1 |
| 2 | `Downtown\DScreen.bmp` | 1 |
| 3 | `VIsland\visland.bmp` | 1 |
| 4 | `Community\NScreen_unleashed.bmp` | 2 |
| 5 | `Studiotown\DScreen.bmp` | 1 |
| 6 | Sem mapa: `unsupported` | — |
| 7 | `Magicland\DScreen.bmp` | 1 |

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

- `neighborhood_picker`: 110 checks sintéticos; mapeamento/recurso e tamanho nos
  seis modos, centros/bordas/fora da borda, FAMI/sem FAMI/arquivo ausente,
  sobreposição, viewport, extremos int32, escala/modo/imagem inválidos, cores,
  clipping, preservação da imagem e arrays exigidos pelo backend.
- `neighborhood_window_cli`: 25 checks sintéticos sem janela; argumentos
  inválidos, modo 6 sem recursos/display e recurso ausente nos seis modos.
- A exportação headless usa FAR/IFF/BMP sintéticos; verifica tamanho PPM, fundo,
  marcador e resumo JSON sem conectar ao X11.
- Build Debug completa e CTest: 168/168 passaram. No sandbox o teste
  `linux_window_backend` foi pulado porque o display não estava acessível;
  executado separadamente com acesso ao display, passou 1/1.
- Regressões focadas: LotLocations, catálogo CLI, FAMI, NBRS e BMP passaram.
- Teste separado do backend com display acessível: janela própria 128×96,
  comparação exata dos 12.288 pixels RGB e eventos direcionados à janela passou.

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
