# Documentação

Comece por [compilação e uso](tools.md), [situação funcional](playable-feature-matrix.md)
e [arquitetura](architecture.md). O [plano nativo](native-port-plan.md) e o
[inventário da migração](migration/README.md) descrevem o trabalho ainda necessário.
[Referências e atribuições](references.md) identificam revisões e licenças upstream.

Os documentos de validação descrevem cenários e resultados históricos, não
garantias para qualquer instalação. Relatórios brutos e dados comerciais não
são distribuídos. Para reproduzir, use as fontes, testes, comandos e identidades
de entrada descritos; testes com dados comerciais exigem uma cópia própria.
Uma validação sintética, uma execução Linux e um teste em console são evidências
distintas. Consulte sempre os limites do cenário e a revisão indicada.

## Documentação

- [Ação de carregamento dos objetos salvos](saved-entity-load.md)
- [Formato estrutural da propriedade desocupada](neutral-property-format.md)
- [Revisão dos vínculos duráveis dos objetos](property-reference-audit.md)
- [Histórico dos objetos criados e retirados](native-object-history.md)
- [Preparação estrutural da casa após a saída](property-survivors.md)
- [Arquitetura e próximos experimentos](architecture.md)
- [Perfil dos formatos suportados e lacunas](formats.md)
- [Campos de colisão do OBJD](object-collision-validation.md)
- [Geometria de rota sobre arquitetura salva](route-geometry-validation.md)
- [Comparação da rota no cômodo com Simitone](same-room-routing-simitone-comparison.md)
- [Segmentação geométrica de salas salvas](room-map-validation.md)
- [Preparo de pisos/paredes salvos com aliases explícitos](prepared-architecture-validation.md)
- [Resolução explícita dos aliases FLRm/WALm](architecture-alias-validation.md)
- [Candidatos geométricos de obstáculos por sala](room-obstacles-validation.md)
- [Validação do primeiro protótipo](validation.md)
- [Validação das tabelas de interação](ttab-validation.md)
- [Núcleo da VM e limites de validação](vm-validation.md)
- [Entidades, slots e probe da cadeira](world-validation.md)
- [Dados SLOT e recurso de roteamento](slot-validation.md)
- [Suspensão da VM para serviços](services-validation.md)
- [Cálculo de percurso na malha](route-validation.md)
- [Percurso ligado à VM e ao World](routing-validation.md)
- [Snap, definição do objeto e ocupação](snap-validation.md)
- [Recursos de animação e próximo contrato da VM](animation-validation.md)
- [Skeleton, interpolação e transformações da pose](pose-validation.md)
- [Appearances, BMF e skinning CPU](mesh-validation.md)
- [BMP e rasterização texturizada CPU](render-validation.md)
- [PALT, SPR2 com alpha/Z e grupos DGRP](sprite-validation.md)
- [Projeção e composição CPU de avatar/objeto](composition-validation.md)
- [World/playback e preview Linux X11](live-view-validation.md)
- [Execução silenciosa dos frames/eventos](playback-validation.md)
- [Teste de tipo e pedidos de som](object-sound-validation.md)
- [Seleção HIT e limite do perfil FreeSO](hit-selection-validation.md)
- [Jump HIT no executável distinto e probe literal explícito](hit-jump-binary-validation.md)
- [Variáveis de origem dos pedidos de som no perfil Simitone](sound-source-variable-validation.md)
- [Callback HIT por origem/campo e diferenças no executável fornecido](hit-source-callback-validation.md)
- [Argumentos HIT explícitos e consulta à origem do World](hit-invocation-validation.md)
- [Saída PCM Linux opcional e validação do backend](linux-audio-validation.md)
- [Probe XA para saída Linux com fila limitada](xa-play-validation.md)
- [Lifecycle persistente HIT: NoteOn, WaitSamp e End](hit-thread-validation.md)
- [Origem de som ligada ao World com geração e leitura viva](sound-source-binding-validation.md)
- [Coleção de threads de som e cancelamento por remoção do dono](sound-thread-collection-validation.md)
- [Factory de eventos, tracks e inicialização HIT](sound-factory-validation.md)
- [Catálogo XA com fontes FAR/avulsas e proveniência](xa-patch-catalog-validation.md)
- [Mixer PCM portátil, limites e consumo por nota](pcm-mixer-validation.md)
- [Transporte PCM compartilhado e cancelamento na saída Linux](pcm-transport-validation.md)
- [Ownership de notas HIT e integração da factory XA com BHAV](pcm-hit-owner-validation.md)
- [Áudio diagnóstico no loop BHAV Linux](bhav-audio-validation.md)
- [Evidência estática dos argumentos nativos de PlaySound](hit-native-caller-arguments.md)
- [Resolução HOT/caller e uso no probe](hit-caller-resolution-validation.md)
- [Evidência estática dos controles HIT e ordem de inicialização](hit-native-controls-evidence.md)
- [Controles HIT brutos antes do primeiro tick e perfil ArgStyle2](hit-native-controls-validation.md)
- [Verificação do retrigger nativo e defaults 50–62](hit-native-retrigger-validation.md)
- [Evidência estática do retrigger HIT nativo](hit-native-retrigger-evidence.md)
- [Lifecycle nativo: execução, áudio corrente e Clock](hit-native-controller-lifecycle-evidence.md)
- [Controller HIT nativo: verificação e limites do subconjunto](hit-native-controller-validation.md)
- [Catálogo nativo de entradas e chamadas entre arquivos HIT](hit-native-entry-map-validation.md)
- [Banco proprietário HIT/HOT e namespace HSM nativo](hit-resource-bank-validation.md)
- [Objetos globais HIT: Track/Patch, colisões e origens distintas](hit-object-catalog-validation.md)
- [Binding global de Patch e backend compartilhado por geração](hit-global-patch-binding-validation.md)
- [Controles nativos de áudio antes do wrapper](hit-native-audio-controls-validation.md).
- [PCM reutilizável e Patch XA global](hit-reusable-pcm-validation.md).
- [Ordem de descoberta dos grupos no executável original](hit-native-load-order-evidence.md)
- [Backend original: pausa, retomada e compartilhamento](hit-native-engine-backend-evidence.md)
- [Opcode HIT 69 nativo: registro de tracks, fade e relógio](hit-native-group-kill-evidence.md)
- [Identidade e ciclo de TrackID no HIT nativo](hit-native-track-identity-evidence.md)
- [Tabela portátil de contribuições HIT por TrackID](hit-source-contributions-validation.md)
- [Evidência estática das contribuições nativas por TrackID](hit-native-source-contributions-evidence.md)
- [Reset de postura e loops persistentes](reset-validation.md)
- [Snap de direção e retorno do BHAV da cadeira](facing-validation.md)
- [Sequência de sentar e levantar no mesmo mundo](stand-validation.md)
- [Referências e atribuição](references.md)
