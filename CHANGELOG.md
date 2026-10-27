# Changelog

Mudanças relevantes para usuários e contribuidores. Este arquivo segue
[Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/).
Ainda não há uma versão completa jogável publicada.

## A publicar

### Adicionado

- Motor C/C++ com leitores de recursos, execução parcial das ações dos Sims,
  rotas e interpretação dos dados de lotes. A cobertura do jogo permanece incompleta.
- Interface experimental Linux com vizinhança, seleção de moradores, criação
  de famílias e mudanças entre casas nos cenários suportados.
- Fila de ordens por Sim, cancelamento e salvamento/reabertura de interações
  compatíveis, incluindo casos com vários moradores.
- Desenho de lotes, objetos e Sims, com animações e objetos carregados nos
  perfis suportados; menus e seleção ainda usam uma interface provisória.
- Reprodução inicial de sons do jogo, vozes e eventos de animação, com
  diagnósticos para formatos e situações ainda não suportados.
- Escolha de idioma e leitura dos textos do jogo; cobertura e desenho de
  alfabetos dependem dos recursos e do renderizador de texto disponível.
- Ferramentas de inspeção, testes sintéticos e cenários de integração separados
  dos dados comerciais. [Funcionalidades e limites](docs/playable-feature-matrix.md).
- Inventário das referências e plano de migração para C/C++, com dependências
  e critérios de conclusão. [Plano](docs/migration/README.md).

### Modificado

- Desenvolvimento concentrado no motor C/C++; a implementação gerenciada
  anterior permanece apenas como referência, sem requisito de CLR no produto final.
- Documentação inicial reorganizada em apresentação, instruções de build/uso
  e documentos técnicos específicos.

### Consertado

- Objetos criados durante a partida podem ser desenhados e selecionados nos
  cenários validados, inclusive comida carregada pelo Sim. A seleção do morador
  é conservada ao salvar/reabrir.
- Compra e mudança de casa verificam a preparação do estado antes de confirmar
  a cobrança; estados incompatíveis são recusados para preservar o progresso.
- Cancelamento e retomada de ações preservam dados necessários à limpeza do
  objeto e à continuação, incluindo cenários de geladeira e de sentar/levantar.

A cobertura de construção/compra, viagens, sete expansões, conteúdo personalizado,
interface original e versões para outras plataformas continua em desenvolvimento.
Contratos e resultados históricos estão no [índice técnico](docs/README.md).
