# Matriz de recursos para a primeira build jogável

Esta matriz acompanha evidências de vários checkpoints, incluindo a entrada
integrada descrita em [linux-session-entry-validation.md](linux-session-entry-validation.md),
sem alegar equivalência. O alvo é o fluxo funcional do Simitone para The Sims Coleção
Completa no Linux, incluindo as sete expansões. “Parcial” significa que há uma
peça demonstrável, mas falta ligá-la ao caminho de jogo descrito.

| Área | Evidência atual no OpenDollhouse | Estado | Próxima demonstração necessária |
| --- | --- | --- | --- |
| Abrir o jogo e escolher dados/idioma | `--game-root` e `--language` são lembrados depois de iniciar uma sessão; a configuração fica na conta local e não altera os dados do jogo ([entrada integrada](linux-session-entry-validation.md)). Ainda não há tela de primeira execução para escolher os valores. | Parcial | Oferecer escolha na primeira execução, opção acessível para mudar depois e confirmar textos reais/fallback. |
| Vizinhança e lotes | A entrada integrada prepara mapa, família e runtime no mesmo processo. O controle M abre o seletor enquanto o runtime vivo fica pausado; o teste headless Sit→mapa→mesmo lote→Stand preserva tick, VM, avatar, attachment e contagens de relações ([retorno](neighborhood-return-validation.md)). A troca para outro lote é recusada e a entrega visual X11 ainda não foi testada. | Parcial | Validar a ida e volta com clique real; depois permitir trocar de lote sem descartar o estado e gravar as alterações. |
| Criar, escolher e excluir família | Depois de selecionar um lote, a entrada integrada permite escolher um morador da família pelo ID de vizinhança e arquivo do personagem na mesma janela. FAMI é lido e a família pode ser ativada; ainda não há fluxo de criar, editar ou excluir famílias ([entrada](linux-session-entry-validation.md), [restauração](family-restore-validation.md)). | Seleção de morador ligada; gestão de família só leitura | Criar, renomear, mudar e excluir uma família com confirmação, preservando as demais após reiniciar. |
| Restaurar e jogar lote residencial | World real pode ser preparado pelo launcher e desenhado numa captura; Main de House07 roda em provas curtas para um ou dois moradores ([World](saved-world-validation.md), [host](person-host-validation.md), [prévia](linux-launcher-preview-validation.md)). A prévia ainda não é uma sessão contínua. Os scripts salvos antigos continuam dormentes; o smoke de 32 ticks termina por limite enquanto Main aguarda (sem falha de host/VM). | Parcial | Jogar um ciclo residencial completo, com moradores ativos, controles, ações, estado salvo e retorno à vizinhança. |
| Interações, necessidades e autonomia | O teste House07 agora injeta clique e Space no mesmo tratamento de entrada da janela, escolhe a cadeira por um pixel real do lote renderizado e conclui Sit4097 ([interações](interaction-validation.md)). Ainda não verifica a entrega nativa de eventos X11; o Main também encontra serviços sem implementação em caminhos reais ([Main do avatar](person-host-validation.md)). | Parcial | Validar clique/tecla reais numa janela X11, alterar estado/necessidade pelo jogo e permitir que o Sim retome sua rotina. |
| Construir e comprar | Catálogos e recursos podem ser inspecionados; falta o ciclo de edição, compra, posicionamento, confirmação e persistência no jogo ([catálogo de objetos](hit-object-catalog-validation.md)). | Não ligado | Comprar/colocar/remover um item num lote e confirmar o resultado ao salvar e reabrir. |
| Salvar, sair e continuar | `WorldSnapshot`, `VmSnapshot` e `RelationshipSnapshot` agora copiam e restauram partes do runtime apenas em memória. O snapshot da VM inclui pilha, esperas, tickets e retornos; exige uma VM nova com os mesmos programas/owners e ligações de host equivalentes. Há agora um codec versionado e um leitor/escritor Linux para arquivos de seção, mas eles ainda não guardam snapshots de uma sessão real; faltam snapshots, filas, relógios, scheduler, animação, caminhada e bindings de host. Portanto não retoma uma partida ([World salvo](saved-world-validation.md), [snapshot da VM](vm-runtime-snapshot-validation.md), [snapshot de relações](relationship-snapshot-validation.md); o limite proposto para save/load durável está em [session-save-design.md](session-save-design.md)). | Parcial | Definir identidade estável dos recursos e gravar/reconstruir o estado completo após reiniciar o processo, incluindo VM, controladores e as outras partes da sessão. |
| Livin' Large | Formatos e conteúdo podem ser lidos conforme as ferramentas existentes; não há prova de cenário ou recurso da expansão jogável. | Sem gate de jogo | Demonstrar pelo menos um fluxo/recurso próprio selecionado na matriz de referência Simitone, do início ao resultado no lote. |
| House Party / “O Bicho Vai Pegar” | O escopo inclui seus lotes e objetos; não existe demonstração integrada dessa expansão. | Sem gate de jogo | Abrir e jogar um lote/conteúdo representativo da expansão e salvar o resultado. |
| Hot Date / Centro | O mapa do Centro pode ser desenhado e selecionado ([mapas](neighborhood-map-validation.md)); ainda não há viagem, estadia, regras sociais e retorno ligados à sessão. | Parcial | Sair de casa para o Centro, usar um fluxo próprio do destino e voltar com a sessão residencial preservada. |
| Unleashed / Cidade Velha | O mapa correspondente pode ser desenhado; lotes, conteúdo e regras ainda não estão ligados ao jogo. | Parcial | Entrar e jogar conteúdo representativo da expansão, incluindo o fluxo próprio identificado na matriz Simitone. |
| Vacation / Férias | O mapa de férias pode ser desenhado; estadia, atividades, salvamento e retorno não formam um fluxo de jogo. | Parcial | Viajar, permanecer, salvar/retomar e voltar para casa. |
| Superstar / Cidade Estúdio | O mapa do Studio pode ser desenhado; carreira, atividades e retorno não estão ligados. | Parcial | Jogar uma atividade/carreira própria do destino e retornar à família residencial. |
| Makin' Magic / Cidade Mágica | O mapa de Magicland pode ser desenhado; regras, objetos e viagem não estão ligados à sessão. | Parcial | Viajar, usar conteúdo próprio da expansão e voltar mantendo o estado da família. |
| Idioma | Existe codec e inventário dos recursos de texto ([inventário](language-resource-inventory.md)); a interface não oferece seleção persistente nem registra o idioma da sessão. | Parcial | Selecionar idioma na primeira execução, reiniciar, confirmar persistência, acentos e fallback em alguns textos reais. |
| Logs e captura de falhas | Logger Linux inicial grava estado local, rotação e falhas, conforme o escopo registrado ([critérios](linux-playable-scope.md)); não acompanha todos os caminhos nem mostra o local ao jogador. | Parcial | Um erro controlado deve registrar etapa, build, idioma e expansão; reiniciar deve apontar o encerramento anterior e manter rotação. |

## Ordem de integração para reduzir retrabalho

1. **Aplicativo e sessão única:** ligar configuração, localização de dados,
   logs, vizinhança, seleção de família/lote, renderer, entrada e relógio em um
   único processo. Isso troca vários probes desconectados por uma base jogável.
2. **Casa residencial completa:** ligar Main, controladores por morador,
   interação que termina, necessidades, autonomia básica e salvar/continuar.
   Usar House07 e a instalação original mais a Legacy como testes repetíveis.
3. **Gestão de famílias e lotes:** criar/editar/excluir família, mudar de lote,
   voltar à vizinhança e confirmar persistência, inclusive após encerramento.
4. **Viagem reutilizável:** criar um caminho comum de sair de casa, carregar um
   destino e voltar; depois cobrir Centro, Férias, Cidade Estúdio e Cidade
   Mágica com suas regras próprias.
5. **Matriz por expansão:** antes de declarar cada uma coberta, selecionar no
   Simitone seus fluxos/regras próprios e demonstrar pelo menos um caminho
   reproduzível com dados comerciais externos. Livin' Large, House Party e
   Unleashed também precisam de demonstrações próprias, não só leitura de FAR.
6. **Construir/comprar e acabamento de primeiro uso:** ligar catálogo e
   posicionamento ao lote, completar idioma persistente e verificar o ciclo de
   logs/crash/reinício. Só então auditar todos os gates de
   [linux-playable-scope.md](linux-playable-scope.md).

Probes de BHAV, parsers e renderizadores continuam como regressões baratas,
mas só entram na frente quando desbloqueiam um destes caminhos. Um teste
isolado que passa não muda o estado de uma linha da matriz; a demonstração
reproduzível é que muda.

As descrições dos mapas e limites da prévia estão em
[neighborhood-map-validation.md](neighborhood-map-validation.md); restauração
de família e mundo em [family-restore-validation.md](family-restore-validation.md)
e [saved-world-validation.md](saved-world-validation.md); os requisitos
completos da build em [linux-playable-scope.md](linux-playable-scope.md).
