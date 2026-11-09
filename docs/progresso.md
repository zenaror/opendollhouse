# Situação do primeiro marco

Situação em 9 de novembro de 2026: motor nativo experimental, com cobertura
parcial. Ainda não há uma versão completa jogável publicada.

| Área | Resultado observado em percursos delimitados | O que falta para aceitar o conjunto |
| --- | --- | --- |
| Motor e dados | Leitura de dados, execução de ações, movimento e estado salvo ligados ao aplicativo. | Completar caminhos ausentes e entrada comum sem opções experimentais obrigatórias. |
| Casa | Alimentação, sono, conversa, banheiro, banho e serviços em cenários registrados. Uma continuação doméstica avançou cerca de quatro horas dentro do jogo e salvou o progresso. Em outro percurso, o fogo foi criado e o Sim reagiu em pânico; a partida salvou, reabriu em outro processo e continuou. | Sessão residencial completa e estável com o mesmo conjunto de código. Completar propagação, combate ao fogo, bombeiros e consequências do incêndio, incluindo cinzas e mortes. |
| Escola e carreira | Retorno de escola/trabalho, promoção, salário e bônus observados. | Ampliar carreiras, horários e progressão. |
| Salvamento | Partidas delimitadas salvam e reabrem preservando o estado. Uma visita ao Centro também reabriu com o cadastro de personagens e o salvamento inteiro idênticos. A continuação residencial com fogo reabriu preservando os três familiares e retomou a simulação. | Continuidade também após viagens, edição e gestão de famílias. |
| Famílias e lotes | Adulto e criança criados pela interface, com edição e cancelamento do rascunho. Renomear, cancelar, excluir uma família sem casa e reabrir a lista preservaram as famílias anteriores e seu salvamento. Em percurso sem janela, compra, construção, mudança e reocupação preservaram casa, pessoas e dinheiro; a nova família continuou e reabriu o salvamento inteiro idêntico. A ordem das teclas e dos cliques rápidos foi corrigida e conferida em testes da janela nativa. | Completar mudanças pelas telas, edição de personagens já criados, demais dados pessoais e continuidade residencial; conferir as entradas rápidas no percurso completo pelas telas e a aparência no original. |
| Centro | Em percursos sem janela, um Sim visitou o Centro sozinho e acompanhado. O Sim controlado realizou atividades e conversou com a acompanhante, com efeitos nas necessidades e na relação. A estadia, o retorno à mesma casa e a continuação residencial salvaram e reabriram preservando o estado conferido. | Ampliar atividades conjuntas, apresentação e regras próprias do destino. |
| Férias | Em percurso sem janela, os três familiares viajaram com reserva e roupas de frio apropriadas. O aluguel de uma vara de pesca cobrou §45 e foi salvo e reaberto. A família voltou à casa de origem com os três moradores e o dinheiro preservados; a chegada foi salva, reaberta em outro processo e continuou sem falha. | A caminhada até o píer falhou e a pesca não produziu efeito nas necessidades. Validar outras atividades, hospedagem, estadia completa, apresentação e controles. |
| Demais expansões | Serviços compartilhados de dados, objetos, scripts e viagens parcialmente implementados. | Completar os percursos da Cidade Velha, Cidade Estúdio e Cidade Mágica, além dos recursos próprios de todas as expansões. Nenhuma expansão foi aceita como completa. |
| Personagens e animais | Gambá original criado durante a rotina da casa, salvo e reaberto em outro processo; seu aviso foi confirmado e houve uma continuação curta. Estrutura, malhas e texturas também desenhadas em verificação delimitada. O progresso de uma entregadora de jornais vinda de um pacote original foi gravado quando ela saiu do lote e reaberto com o salvamento inteiro idêntico. | Completar comportamento dos animais, adoção, controle, salvamento nos demais estados, NPCs, bebês e ciclos completos. A reabertura curta e o desenho isolado não aceitam esses fluxos. |
| Interface e áudio | Implementação nativa e reconstrução parcial da interface original. | Todas as telas, acabamento, animação, iluminação e áudio. |
| Idiomas e conteúdo personalizado | Leitura de textos e partes do suporte a conteúdo clássico. Uma interação com um banheiro de conteúdo personalizado terminou e melhorou a necessidade Banheiro; o salvamento reabriu inteiro idêntico. | Conferência representativa dentro dos percursos completos e uso/salvamento de conteúdo personalizado. |
| Outras plataformas | Preparação de ferramentas e separação de serviços. | Compilar e executar o jogo em cada alvo, medir recursos e adaptar entrada/apresentação. |

Os resultados acima vêm de cenários e revisões diferentes. Não são uma prova de
que todos os recursos já funcionam juntos na revisão atual.

A continuação com fogo foi conferida pelos registros disponíveis e pelo estado
salvo, sem validação visual do incêndio. Parte dos registros do percurso mais
longo ficou incompleta, e alguns objetos já tinham falhas registradas no estado
inicial. O resultado não confirma uma casa livre de falhas nem o ciclo completo
do incêndio. Os testes de entrada da janela também não substituem a validação
da gestão de famílias inteira pelas telas.

As próximas entregas são **completar os eventos residenciais que interrompem a partida,
começando pelos incêndios**, **ampliar as viagens aos participantes corretos e aos demais
destinos** e **completar a gestão de famílias e a preservação da casa desocupada**.
Incêndios incluem suas consequências, combate ao fogo e atendimento dos bombeiros;
bebês, morte, roubo e visitas de animais também pertencem ao primeiro marco.
Veja os critérios no [plano](native-port-plan.md).
Atualizar esta página quando mudar uma entrega ou limitação relevante, sem
transformá-la em diário de tentativas. Não medir conclusão por arquivos ou testes.
