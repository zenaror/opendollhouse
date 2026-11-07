# Plano de desenvolvimento

O primeiro marco é paridade funcional com a base Simitone fixada, em um único
motor C/C++. Uma demonstração de uma casa não encerra esse marco. A experiência
do The Sims 1 original, as sete expansões e o conteúdo clássico continuam como
destino do projeto; limitações da referência não eliminam esses requisitos.

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

1. Fechar o percurso compartilhado de viagem ao Centro e de volta, reutilizando
   as partes existentes e conferindo todos os familiares participantes.
2. Em paralelo, fechar o percurso integrado de criação de família e edição do
   lote, aproveitando os serviços de propriedade e transação existentes.
3. Aplicar a base de viagem a Férias, Cidade Velha, Cidade Estúdio e Cidade Mágica;
   registrar diferenças reais de cada destino e os recursos próprios das expansões.
4. Completar as demais lacunas residenciais, animais, interface, áudio e conteúdo
   personalizado; reunir tudo em uma candidata com aceitação conjunta.

## Método

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
