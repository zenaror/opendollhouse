# Plano de desenvolvimento

A primeira versão Linux jogável precisa cumprir o [escopo completo](linux-playable-scope.md),
incluindo a experiência original, as sete expansões e o conteúdo clássico.
A paridade funcional com a base Simitone fixada é um marco intermediário da
migração para um único motor C/C++. Uma demonstração de uma casa ou a paridade
com uma parte da referência não encerra a primeira versão jogável. Limitações
da referência não eliminam requisitos do jogo original.

## Entregas integradas

| Entrega | Critério de conclusão | Dependências principais |
| --- | --- | --- |
| Vida residencial | Entrar na casa, controlar e cancelar ações, deixar a autonomia avançar por um dia, usar necessidades/interações/serviço e continuar após salvar e sair. | Scripts, movimento, personagens, interface e persistência trabalhando juntos. |
| Família e lote | Criar adulto e criança com orçamento normal, mudar, comprar/editar, sair, voltar e mudar novamente sem perder pessoas, dinheiro ou alterações. Cobrir renomear/excluir e cancelamentos. | Gestão de famílias, propriedade, transações, compra/construção e salvamento. |
| Viagens e expansões | Sair, chegar com os viajantes corretos, fazer atividade com efeito observável, salvar/reabrir no destino e voltar à mesma casa com progresso preservado. | Estado compartilhado, seleção/transporte, destinos e regras de retorno. |
| Experiência original | Interface, controles, imagem, áudio, idioma e conteúdo personalizado permitem os percursos anteriores. | Apresentação e serviços nativos ligados ao estado real. |
| Candidata jogável | Instalar/iniciar, jogar por tempo prolongado, salvar/continuar e obter diagnóstico útil quando algo falhar. | Conjunto completo dos requisitos, pacote e validação integrada. |

Essas entregas são frentes de cobertura e podem avançar em paralelo. Não é
necessário aperfeiçoar todos os objetos de uma casa antes de integrar viagens.

## Ordem de integração

1. Completar as dependências residenciais que interrompem os percursos. Incêndios
   precisam integrar relógio, criação e propagação, objetos afetados, combate e
   salvamento; tratar essas partes como um serviço compartilhado.
2. Fechar o percurso compartilhado de viagem ao Centro e de volta, reutilizando
   as partes existentes e conferindo familiares e convidados pelas regras de
   controle próprias de cada um. Preservar necessidades e progresso pessoal
   desde a saída, incluindo relações, habilidades, profissão, roupas e fama.
3. Em paralelo, fechar o percurso integrado de criação de família e edição do
   lote, aproveitando os serviços de propriedade e transação existentes.
4. Aplicar a base de viagem a Férias, Cidade Velha, Cidade Estúdio e Cidade Mágica;
   registrar diferenças reais de cada destino e os recursos próprios das expansões.
5. Completar as demais lacunas residenciais, animais, interface, áudio e conteúdo
   personalizado; reunir tudo em uma candidata com aceitação conjunta.

O progresso pessoal precisa de um serviço compartilhado pela leitura e gravação
dos dados, criação e retirada de personagens e viagens. Ajustar esses consumidores
juntos, com formatos de salvamento explícitos e retomada segura, em vez de copiar
dados somente no retorno de uma viagem. Diferenças entre a referência e o original
continuam como requisitos a resolver.

## Método

Conferir o conjunto de operações registrado pela referência TS1 e sua ligação
ao motor nativo antes de ampliar os objetos de uma área. A existência de um
tratador não comprova todas as formas de uso. Distinguir operações realmente
usadas pelo TS1 de serviços exclusivos de The Sims Online, e migrar as lacunas
com seus consumidores e estado, em vez de descobri-las objeto por objeto.

Até duas frentes de implementação podem trabalhar com arquivos próprios. Os pontos
compartilhados e os builds têm um responsável; a revisão é independente. Cada
frente entrega comportamento no aplicativo, não apenas uma biblioteca desconectada.

A unidade de acompanhamento é um resultado do jogador. Correções internas ficam
na mesma entrega. Duas rodadas sem avanço no percurso exigem reavaliar a abordagem.
Reutilizar provas pertinentes e executar novamente apenas o que mudou, falhou
ou permaneceu inconclusivo. Engenharia reversa é usada para uma lacuna concreta,
sem reiniciar investigações já resolvidas.

## Situação e prazo

As entregas permanecem abertas. O [painel](progresso.md) e a [matriz](playable-feature-matrix.md)
separam comportamento observado de cobertura restante. Não há percentual global
validado nem data de lançamento confirmada. Não declarar o marco por uma seleção
de testes isolados. Os [critérios jogáveis](linux-playable-scope.md) e a
[portabilidade](portability.md) definem a abrangência necessária.
