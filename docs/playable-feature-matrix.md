# Cobertura funcional

Esta matriz resume a situação experimental. “Parcial” indica comportamento
implementado ou observado em parte; não significa sistema completo.

| Sistema | Situação | Cobertura ainda necessária |
| --- | --- | --- |
| Dados e recursos | Parcial | Variantes, recursos ausentes e identidade em todos os consumidores. |
| Scripts, objetos e interações | Parcial | Ciclo completo de criação, execução, cancelamento e retirada em mais perfis. |
| Movimento e animação | Parcial | Escadas, obstáculos, personagens e ações nos percursos integrados. |
| Necessidades e autonomia | Parcial | Dia residencial completo, sem ações falhando silenciosamente. |
| Relações e serviços | Parcial | Perfis sociais e serviços além dos casos já observados. |
| Carreiras e habilidades | Parcial | Seleção, horários, pagamento, promoção e progressão mais amplos. |
| Famílias e vizinhança | Parcial | Completar edição de personagens existentes, mudanças pelas telas e progresso entre famílias. Criação de rascunho, renomeação, exclusão elegível e cancelamentos já observados em percurso delimitado. |
| Compra e venda | Parcial | Grupos de várias peças, posicionamento, cancelamento e orçamento. |
| Construção | Parcial | Ferramentas completas, pisos superiores, custo e atualização da circulação. |
| Salvamento | Parcial | Estados ativos e transições de família/lote/destino do conjunto completo. |
| Interface original | Parcial | Navegação, catálogos, diálogos, retratos e proporção em todas as telas. |
| Gráficos e áudio | Parcial | Apresentação, iluminação, animações, vozes, música e efeitos integrados. |
| Animais e ciclos de personagens | Em aberto | Aquisição, comportamento, serviços e persistência próprios. |
| Idiomas e conteúdo personalizado | Parcial | Uso real, fallback e continuação após salvar. |
| Gozando a Vida e Fazendo a Festa | Em aberto | Objetos, eventos, convidados e atividades pelas regras dos dados. |
| Encontro Marcado | Parcial | Percurso completo ao Centro com efeitos e retorno preservados. |
| Em Férias | Partida e reserva parciais: presença da família, confirmações, cobrança, escolha do destino e roupas de frio apropriadas observadas. | O destino foi ativado com três familiares; a partida salva reabriu e continuou até a mensagem de boas-vindas da Diretora de Férias, sem falha de execução, em teste sem janela. Validar atividades, estadia, outras reaberturas e retorno, além da apresentação e dos controles. |
| O Bicho Vai Pegar | Em aberto | Cidade Velha, animais e persistência. |
| Superstar | Em aberto | Estúdio, atividades, fama e retorno. |
| Num Passe de Mágica | Em aberto | Cidade Mágica, inventário, moedas, feitiços e retorno/mudança elegível. |
| Pacote jogável Linux | Em aberto | Primeiro uso, sessão prolongada e diagnóstico de falhas. |
| Demais plataformas | Não aceitas | Execução e desempenho próprios; veja [portabilidade](portability.md). |

A relação não substitui os [requisitos jogáveis](linux-playable-scope.md).
Uma atividade bem-sucedida não aceita uma expansão inteira; testes de componentes
não aceitam o percurso completo no aplicativo.
