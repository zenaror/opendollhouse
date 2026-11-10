# Cobertura funcional

Esta matriz resume a situação experimental. “Parcial” indica comportamento
implementado ou observado em parte; não significa sistema completo.

| Sistema | Situação | Cobertura ainda necessária |
| --- | --- | --- |
| Dados e recursos | Parcial; entrada comum e salvamento com o relógio parado observados em uma casa original. | Outras casas, variantes, recursos ausentes e identidade em todos os consumidores. |
| Scripts, objetos e interações | Parcial | Ciclo completo de criação, execução, cancelamento e retirada em mais perfis. |
| Movimento e animação | Parcial | Escadas, obstáculos, personagens e ações nos percursos integrados; ampliar a conferência de animações já em andamento na importação clássica. |
| Necessidades e autonomia | Parcial | Dia residencial completo, sem ações falhando silenciosamente. |
| Incêndios | Parcial | Fogo, pânico e combate manual de um foco observados; a ação continuou após reabrir a partida, terminou com sucesso e o foco selecionado desapareceu. Ainda havia outros focos. Completar a cobertura de propagação, consequências, bombeiros por telefone ou alarme e apresentação. |
| Roubo, polícia e alarme contra roubo | Em aberto | Chegada do ladrão, detecção, roubo, chamada manual ou por alarme, intervenção policial e consequências sobre os bens ao continuar a partida. |
| Bebês | Em aberto | Nascimento ou adoção conforme as regras originais, cuidados, crescimento para criança, intervenções e continuidade da família após salvar. |
| Morte e tentativa de recuperar o Sim | Parcial | Morte de uma moradora, urna e continuação após salvar/reabrir observadas em um percurso. Validar as demais causas, a chegada e interação com a Morte, a tentativa de recuperar o Sim e os demais resultados persistentes. |
| Relações e serviços | Parcial | Perfis sociais e serviços além dos casos já observados. |
| Carreiras e habilidades | Parcial | Seleção, horários, pagamento, promoção e progressão mais amplos. |
| Famílias e vizinhança | Parcial | Criação com adulto e criança, cancelamentos, renomeação e exclusão elegível observados pelas telas, com reabertura dos dados e encerramento normal do aplicativo. O diagnóstico ainda aponta memória não liberada ao encerrar. Completar edição de personagens existentes, mudanças pelas telas e progresso entre famílias. |
| Compra e venda | Parcial | Grupos de várias peças, posicionamento, cancelamento e orçamento. |
| Construção | Parcial | Ferramentas completas, pisos superiores, custo e atualização da circulação. |
| Salvamento | Parcial; continuação curta de importação clássica e reabertura de um salvamento nativo anterior preservaram os arquivos inteiros. | Estados ativos e transições de família/lote/destino do conjunto completo. |
| Interface original | Parcial | Navegação, catálogos, diálogos, retratos e proporção em todas as telas. |
| Gráficos e áudio | Parcial | Apresentação, iluminação, animações, vozes, música e efeitos integrados. |
| Animais e ciclos de personagens | Gambá original criado, salvo e reaberto em percurso curto; aparência por espécie e desenho observados em verificação delimitada. | Aquisição, comportamento, controle, serviços e persistência nos demais estados; validar os animais dentro dos percursos completos. |
| Visitas e eventos de animais | Chegada e reabertura de um gambá observadas; restante em aberto. | Urso, gambá, guaxinim e demais visitantes com comportamentos próprios, lixo, reações da casa, serviços e desfechos persistentes. |
| Idiomas e conteúdo personalizado | Parcial | Uso real, fallback e continuação após salvar. |
| Gozando a Vida e Fazendo a Festa | Em aberto | Objetos, eventos, convidados e atividades pelas regras dos dados. |
| Encontro Marcado | Parcial | Percurso completo ao Centro com efeitos e retorno preservados. |
| Em Férias | Partida, aluguel e retorno parciais: os três familiares viajaram com reserva e roupas de frio; o aluguel de uma vara de pesca cobrou §45, foi salvo e reaberto, e a família voltou à casa de origem com pessoas e dinheiro preservados. A chegada foi salva, reaberta e continuou. | A caminhada até o píer falhou, sem efeito da pesca nas necessidades. Validar outras atividades, hospedagem, estadia completa e apresentação/controles. |
| O Bicho Vai Pegar | Em aberto | Cidade Velha, animais e persistência. |
| Superstar | Em aberto | Estúdio, atividades, fama e retorno. |
| Num Passe de Mágica | Em aberto | Cidade Mágica, inventário, moedas, feitiços e retorno/mudança elegível. |
| Pacote jogável Linux | Em aberto | Primeiro uso, sessão prolongada e diagnóstico de falhas. |
| Demais plataformas | Não aceitas | Execução e desempenho próprios; veja [portabilidade](portability.md). |

A continuação da importação clássica chegou a 64 passos após respostas únicas
aos diálogos observados; a reabertura foi conferida sem avançar o relógio.
O pedido de refeição apenas entrou na fila e parou num diálogo sobre táxis,
sem comprovar preparo ou consumo. Esses casos sem janela não aceitam a
interface, o áudio ou a continuidade de todos os estados de scripts originais.

A relação não substitui os [requisitos jogáveis](linux-playable-scope.md).
Uma atividade bem-sucedida não aceita uma expansão inteira; testes de componentes
não aceitam o percurso completo no aplicativo.
