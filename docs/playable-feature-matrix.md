# Matriz de recursos para a primeira build jogável

Retrato da frente nativa com troca residencial e checkpoints familiares (2026-10-26). A frente vigente é o
aplicativo C/C++ existente, sem C#/.NET em sua execução. Simitone e sua revisão
FreeSO são referências; a ponte gerenciada anterior é histórica e não deve ser
ampliada. Ver [plano nativo](native-port-plan.md), [arquitetura](architecture.md)
e [checkpoint e limites](current-handoff.md).

O objetivo continua sendo The Sims 1 completo, sete expansões, conteúdo
personalizado e interface/controles originais. Paridade Simitone é um marco
intermediário. Esta matriz organiza trabalho; não reduz
[linux-playable-scope.md](linux-playable-scope.md). “Parcial” indica componentes
ou cenários demonstrados, sem afirmar o fluxo inteiro. Evidência headless,
clique sintético, PCM dummy e fixtures não equivalem a partida manual, audição
física, fidelidade ao original ou execução em console.

## Aplicativo, família e lote

| Área | Cobertura atual e procedência | Fluxo completo ainda pendente |
| --- | --- | --- |
| Dados, configuração e entrada | Pasta de dados e idioma persistentes; seletor inicial com 20 códigos. Entrada, mapa, família e lote no mesmo aplicativo nativo ([entrada Linux](linux-session-entry-validation.md)). | Validar primeiro uso visual, permitir mudanças posteriores e confirmar fallback/expansões ausentes. |
| CAS e família | Criação privada de personagens/família e compra de House01; gate comercial curto de dois adultos com save/resume headless. Mapa/seletor têm observações X11 ([CAS](linux-cas-screen-validation.md), [restauração](family-restore-validation.md)). | Criar → editar/renomear → mudar → jogar → sair → continuar pela janela; crianças, perfis de chegada adicionais e sessão longa continuam sem cobertura completa. |
| Mudança e exclusão de família | Overlay privado confirma compra e despejo de família originalmente alojada. Picker exclui logicamente família-fonte sem casa/sem histórico de mudança e com NBRS único; arquiva família criada sem histórico, retendo IDs e arquivos privados. Originais permanecem intactos ([overlay e limites](family-overlay-linux-validation.md)). Reconstrução nativa retira moradores originais somente com prova de ocupação encerrada ([contrato e evidência](native-occupancy-reconstruction.md)). | Casa07→Casa09→Casa07 passou pela janela, com compras, despejos e reabertura no mesmo fluxo ([gate gráfico](native-family-window.md)); generalizar objetos carregados e outros perfis, liquidação original e edição/renomeação ([transferência](native-family-transfer.md)). Exclusão continua sujeita aos controles de identidade, vínculos e histórico. |
| Vizinhança e troca residencial | M retorna ao mapa; voltar à mesma casa conserva a sessão viva. Trocar de casa salva e abre a família do destino; há regressão A→B→A sintética ([mapas](neighborhood-map-validation.md), [entrada](linux-session-entry-validation.md)). | Gate comercial Casa07→Casa05→Casa07 passou com partidas em300/300 e retorno até812, Sim ainda sentado, 13 seções iguais ao controle, Stand após reabrir e recuperação de destino inválido ([troca residencial](neighborhood-return-validation.md)). Gestão de mudança da família já passou pela janela em outro gate ([gestão](native-family-window.md)); ainda faltam viagem entre outras famílias pela janela e partida longa. Troca residencial não transporta a família de origem nem comprova viagem de expansão. |
| Simulação, interações e autonomia | VM, filas e moradores nativos; Sit/Stand/Go Here em cenários comerciais limitados. Relógio, sete necessidades, frações e pontuação de sala alimentam o runtime/HUD. Set Motive Change tem formas imediata/contínua ([interações](interaction-validation.md), [relógio](house07-clock-validation.md), [host](person-host-validation.md)). | Jogar sessões longas com autonomia, relações, necessidades e variedade de objetos; resolver serviços reais ainda ausentes, callbacks/RunImmediately, lifecycle e RunTreeByName gerais. Leitura de Beds.iff não demonstra ação de cama alterando necessidades ponta a ponta. |
| Ordens do jogador e interface | `b23bae1`: várias ordens por Sim, páginas, troca de seleção, cancelamento e reconstrução das pendências após carga ([fila nativa](native-player-queue.md)). Há HUD e painel provisório; X11 sintético mostrou cadeira House07 e save/resume ([janela](linux-saved-window-validation.md)). | Completar menus, telas, controles, visuais e localização originais; painel provisório e exemplos de objetos não comprovam interface final. |
| Salvar, sair e continuar | Save/resume de moradores em processo novo. `40057ae`: SEAT v3 por ator, moradores sentados/em pé misturados; House05 banco173 e House07 têm continuação documentada. v1/v2 permanecem legíveis ([assentos](native-household-seated.md), [projeto](session-save-design.md), [mundo](saved-world-validation.md)). | Generalizar fronteiras de execução/posturas/rotas e referências vivas fora dos perfis aceitos; partida longa e mais famílias/lotes. O gate de assento exige Sit concluído e fila ociosa, sem Sit/Stand em andamento. |
| Checkpoint da família | Novas compras usam ocupação identificada, dinheiro vivo no overlay e arquivo imutável. Retomada segue o último save confirmado; saves antigos ou de outra ocupação não podem substituir progresso ([contrato e testes](native-family-checkpoints.md)). Compras passam também por validação completa de save antes da cobrança. | Perfil portátil transfere dados pessoais, necessidades/frações e relações para moradores novos; reconstrução exige prova da ocupação original encerrada. Objetos carregados, transformações/props e dados desconhecidos incompatíveis continuam limitados ([perfil](native-family-transfer.md), [reconstrução](native-occupancy-reconstruction.md)). |
| Construir e comprar | Recursos, catálogos e geometria podem ser lidos; ciclo de edição ainda não ligado ([arquitetura](architecture-resource-catalog.md), [posicionamento](floor-placement-validation.md)). | Comprar → posicionar/remover → atualizar saldo/mundo → salvar → reabrir; também paredes, pisos e demais alterações de construção. |
| Áudio | `6a3c889`: pedidos BHAV/FWAV e eventos de animação chegam a GameAudio/HIT/XA/PCM; sink injetado, SDL/Linux no host. Cadeira/vozes produziram PCM não nulo com SDL dummy, sem alterar estado/RNG de jogo ([áudio nativo](native-session-audio.md)). | Música, efeitos e vozes com cobertura completa; loops, espacialização, HIT geral e compartilhamento de donos. Estado sonoro reinicia na carga; não há prova de audição física nem continuidade no meio da nota. |
| Conteúdo personalizado (CC) | Downloads FAR/IFF entra no plano, identidade e leitores de definições; FARs entram nas animações. House81 tem prévia estática com fontes de expansões/Downloads ([recursos](linux-download-resources-validation.md)). | Usar conteúdo personalizado em interações, compra/construção e save/resume; leitura/catálogo/prévia não comprovam compatibilidade geral. |
| Idioma | Codec/inventário, escolha inicial persistente, textos reais de mapa, lotes e diálogos; observações em português ([inventário](language-resource-inventory.md), [mapas](neighborhood-map-validation.md)). | Mudança posterior, CAS/rótulos restantes, diálogos/seletor visuais, acentos e fallback em mais textos reais. |
| Logs e recuperação | Log local compartilhado pelo launcher/runtime; rotação, permissões, falhas e encerramento têm regressões ([critérios](linux-playable-scope.md)). | Erro pela janela orienta até o log e oferece contexto suficiente para reproduzir falhas de gameplay. |
| Plataformas | Linux é a primeira validação. Fixtures nativas ARM/3DS são evidência limitada de componentes ([portabilidade](portability.md), [plano](native-port-plan.md)). | Adaptar e validar memória, gráficos, som, entrada e armazenamento em New 3DS, Vita e Android; Switch, Windows e macOS Intel/Apple Silicon seguem futuros, Wii U desejado. Nenhum port completo é estabelecido pela build Linux. |

## Sete expansões e viagem

Os nomes brasileiros distinguem House Party (“Fazendo a Festa”) de Unleashed
(“O Bicho Vai Pegar”), conforme a [lista oficial da EA](https://www.ea.com/pt-br/games/the-sims/the-sims-25th-anniv-edition).

Acesso a mapa, leitura de FAR ou máscara de expansão não comprova regras ou
atividades jogáveis. O transporte da família de origem, a estadia e o retorno
preservando a residência exigem um fluxo próprio, além da troca residencial.

| Expansão | Cobertura demonstrada | Fluxo completo pendente |
| --- | --- | --- |
| Livin' Large | Leitores/conteúdo disponíveis; sem gate de jogo específico. | Identificar atividade/regra no TS1 ativo e recursos; executá-la até resultado persistido. Não inventar cenário a partir de contagem de arquivos. |
| House Party / “Fazendo a Festa” | Lotes/objetos no escopo; sem demonstração integrada específica. | Identificar e concluir fluxo de festa, observar resultado no lote e salvar. |
| Hot Date / Centro | Mapa acessível ([mapas](neighborhood-map-validation.md)). | Família sai de casa, visita Downtown, conclui interação social/atividade identificada e retorna com estado preservado. |
| Unleashed / “O Bicho Vai Pegar”, Cidade Velha | Mapa desenhável; referência apresenta seletor de pet. | Confirmar aquisição/adoção no TS1 ativo, executar rotina do animal e persistir; também viagem/cidade e retorno. Seletor de gato/cachorro sozinho não comprova essas regras. |
| Vacation / Férias | Mapa desenhável; referência distingue trânsito/família e destino Vacation. | Escolher destino elegível, chegar, permanecer, concluir atividade, salvar/retomar e voltar. Confirmar regras de ocupação onde a referência é limitada. |
| Superstar / Cidade Estúdio | Mapa desenhável; referência tem seletor e catálogo Studio. | Identificar e concluir atividade/carreira própria, persistir resultado e retornar. Condições 4116/4127 de assentos de diretor não foram demonstradas numa partida. |
| Makin' Magic / Cidade Mágica | Mapa desenhável; referência tem MoveInMagic e saldos Simoleons/Magicoins. | Validar mudança elegível e bloqueio por saldo, preservar estado e concluir atividade mágica própria; mudança sozinha não cobre a expansão. |

Referências Simitone consultadas, sob
`refs/upstream/Simitone/Client/Simitone/Simitone.Client/UI/`: CAS em
`Screens/TS1CASScreen.cs:440–452,581–586,829–867`; mudança e viagem em
`Screens/TS1GameScreen.cs:196–246,391–405`; seletores de destino/pet em
`Panels/UILotControl.cs:241–285`; catálogo Studio em
`Panels/UIMainPanel.cs:259–264`; compra mágica em
`Panels/UIHouseSelectPanel.cs:202–257`. Trânsito TS1 está em
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Primitives/VMGenericTS1Call.cs:97–115,230–263`.
Esses trechos orientam a investigação, sem provar implementação nativa ou
fidelidade ao executável original.

## Prioridades por fluxo

1. Separar persistência da propriedade do save familiar e conservar alterações
   ao mudar de casa e voltar; gestão de mudança já passou pela janela, sem
   estabelecer viagem de expansão ou troca visual entre outras famílias.
2. Completar gestão de famílias, incluindo edição/renomeação e despejo após
   compra pelo jogador; integrar alterações ao save e à seleção visual.
3. Ligar compra/construção ao mundo e à persistência; completar simulação e
   interações necessárias aos cenários de jogo, corrigindo falhas demonstradas.
4. Integrar viagem da família e atividades das sete expansões, preservando
   residência/estado e explicitando limites da referência.
5. Completar interface original, CC, idioma, primeiro uso e recuperação;
   auditar todos os critérios do [escopo](linux-playable-scope.md) e dos ports.

As etapas se apoiam em regressões, mas uma coleção de microprobes não substitui
um fluxo jogável. Detalhes e histórico permanecem nos documentos vinculados e
no Git; resultados de checkpoints anteriores não descrevem automaticamente a
cobertura atual nem a validam em outro alvo.
