# Changelog

Mudanças relevantes para usuários e contribuidores, no formato
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
Ainda não há uma versão completa jogável publicada.

## A publicar

### Adicionado

- Persistência nativa para animações importadas, poses de objetos segurados e
  controle de olhar, com leitura dos formatos anteriores suportados.

- Pontos de retorno opcionais em testes sem janela, usando o salvamento nativo
  para preservar progresso aceito antes de uma falha.
- Serviços C++ de cadastro de personagens e mudança de família, com preservação
  de identidades e ações. A integração dos percursos originais segue em desenvolvimento.
- Motor experimental C/C++ com leitores de recursos, execução parcial de scripts,
  movimento, personagens, objetos e salvamento de sessões delimitadas.
- Interface Linux experimental para vizinhança, famílias, simulação, compra e
  construção, com reconstrução parcial da aparência original.
- Serviços iniciais de áudio, idiomas, conteúdo personalizado e viagens.
- Verificadores de continuidade de partidas e ferramentas de inspeção de dados.
- Registros de falhas com a identidade da ação, do alvo e da etapa de
  posicionamento envolvida.
- Guias de arquitetura, formatos, requisitos jogáveis e futuras extensões.
- Leitura compartilhada de arquivos BCF mistos e preparação da aparência de
  animais com a estrutura corporal da espécie, sem mãos humanas.

### Modificado

- Desenvolvimento orientado por percursos completos no aplicativo: vida em casa,
  gestão de famílias/lotes e viagens com ida, atividade e retorno.
- Documentação reorganizada em guias públicos autossuficientes, com cobertura e
  limitações explícitas; registros internos e resultados gerados fora do Git.

### Consertado

- Conferência dos arquivos do jogo acelerada nos sistemas compatíveis,
  preservando os resultados e a alternativa portátil.
- Entrada comum de uma casa original, com recursos conferidos antes da recuperação
  e salvamento com o relógio parado, sem opções experimentais obrigatórias nesse percurso.
- Reabertura de uma continuação curta da importação clássica, preservando o
  salvamento inteiro após respostas únicas aos diálogos observados e avanço da
  simulação. Um salvamento nativo anterior também foi reaberto sem alteração.

- Criação de objetos carregados na mão durante uma ação, preservando a ação
  do personagem e recusando mudanças inesperadas durante a preparação da animação.
- Reabertura de partidas com personagens sem registro de roupas, preservando
  os registros existentes sem gerar novos durante a leitura.
- Alturas de apoios em arquivos antigos, evitando interromper a colocação de
  objetos por um campo que o formato não grava.
- Diagnósticos de posicionamento preservam a etapa e o motivo informado pelo motor.
- Preparação de portas e janelas para compra, mantendo as verificações dos
  objetos já instalados.
- Posicionamento e preservação de objetos de parede ao salvar e reabrir.
- Abertura de perfis antigos que ainda não possuem uma pasta de personagens
  criados pelo jogador.
- Leitura das famílias da instalação original Complete Collection, evitando
  uma falha antes de abrir a casa.
- Carregamento dos recursos de ações entre Sims durante interações em andamento,
  incluindo pedidos de passagem, preservando a fila e o salvamento.
- Carregamento de roupas, rostos e outros recursos de aparência, evitando buscas
  repetidas nos arquivos do jogo.
- Ordem das entradas rápidas de teclado e mouse na janela nativa, preservando
  exclusões de texto e cliques sucessivos. Criação, renomeação e exclusão de
  famílias foram observadas pelas telas, com reabertura dos dados e encerramento
  normal. A versão de diagnóstico ainda aponta memória não liberada ao encerrar.
- Continuação doméstica no cenário de incêndio validado: criação do fogo e
  reação de pânico prosseguiram, com salvamento e reabertura da partida. A
  aparência, as consequências, o combate ao fogo e o atendimento dos bombeiros
  continuam incompletos.
- Gestos de mãos explicitamente vazios nos dados originais, usados por algumas
  fantasias, sem exigir peças inexistentes nem substituir o gesto pelo de repouso.
- Tratamento da cabeça opcional de animais quando sua aparência está
  comprovadamente ausente, mantendo os erros de recursos inválidos.
- Leitura da posição e direção de alvos que saíram do lote durante uma
  caminhada, preservando a referência original quando seu número é reutilizado.
- Tratamento da posição especial usada pelo jogo para retirar personagens do
  lote, preservando a direção e os vínculos dos objetos carregados.
- Referências de ações pendentes a objetos removidos, permitindo o encerramento
  previsto pelo jogo sem atingir outro objeto que reutilize o mesmo número.
- Continuação da simulação quando os dados originais pedem uma animação
  comprovadamente ausente, preservando erros reais de leitura e recursos.
- Posicionamento, circulação e retirada de objetos em casos de recuperação de
  partidas, permitindo continuidade de interações residenciais delimitadas.
- Preservação de vínculos e ações em cenários de salvar e reabrir uma sessão.
- Mensagens de emprego e horários localizados, incluindo acentos e valores
  monetários literais; retorno de escola/trabalho, promoção e pagamento já
  observados em um percurso residencial.

A cobertura continua parcial. Esses resultados não constituem paridade completa,
aceitação das sete expansões ou suporte comprovado aos demais aparelhos.
Veja a [situação funcional](docs/playable-feature-matrix.md).
