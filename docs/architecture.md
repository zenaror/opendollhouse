# Arquitetura

OpenDollhouse é um único motor C/C++ experimental. Simitone e sua revisão
correspondente do FreeSO são referências de código e comportamento TS1;
o jogo original é a referência de experiência. O produto não deve exigir
C#, CLR, .NET ou MonoGame gerenciado. Consulte as [origens](references.md).

## Componentes

| Área | Responsabilidade | Código |
| --- | --- | --- |
| Dados | Ler arquivos, resolver recursos e preservar identidades e campos desconhecidos. | `include/odh/`, `src/` e adaptadores em `tools/` |
| Simulação | Executar scripts, interações, relógio, necessidades, relações e movimentação. | `src/`, `include/odh/` |
| Persistência | Gravar o estado, validar dependências e restaurar ações em andamento. | `src/session_bundle.cpp` e serviços de sessão |
| Apresentação | Compor lotes e personagens, desenhar interface, tocar áudio e receber comandos. | `tools/` e adaptadores nativos |
| Plataforma | Fornecer janela, arquivos, entrada, áudio e outros serviços do sistema. | Adaptadores em `tools/` e `native/` |

O aplicativo Linux liga essas áreas. Essa ligação ainda é parcial; existência de
um módulo não significa que todo o seu comportamento esteja disponível ao jogador.

## Dados e execução

Os dados comerciais são fornecidos pelo usuário e ficam separados do código.
A entrada atual é uma pasta extraída; leitura direta de ISO não está implementada.
O motor executa os scripts dos dados, em vez de criar uma regra diferente para
cada móvel. Recursos locais, semiglobais e globais precisam conservar a origem
que define o significado de cada comando. Conteúdo desconhecido não deve ser
convertido em sucesso silencioso.

Objetos e personagens têm identidade e geração. Reutilizar um número não torna
um objeto novo equivalente ao que foi removido. Movimento, uso de recipientes,
criação e retirada de objetos compartilham o estado espacial da simulação.
Os [contratos do motor](engine-contracts.md) detalham essas fronteiras.

## Salvamento e alterações

Um save precisa guardar personagens, objetos, scripts e pedidos ativos, relógio,
necessidades e o estado dos serviços usados. A restauração confere versão,
identidades e recursos. Não se deve completar metadados ausentes por suposição.
Compatibilidade parcial deve produzir um limite explícito ou rejeição explicada.

Compra, construção e mudanças de família usam uma alteração candidata: conferir
recursos, custo, propriedade e efeitos antes de publicar o novo estado. Falha
não deve deixar dinheiro cobrado e objeto ausente. O registro de sessão mantém
a relação entre família, residência e gravação atual.

Viagem temporária preserva a casa de origem e cria uma sessão de destino com os
viajantes elegíveis. Voltar de uma viagem não é mudar de residência. A transferência
de progresso pessoal precisa ter regras explícitas, sem descartar diferenças
que ainda não são compreendidas.

## Apresentação e portabilidade

A simulação não deve depender de X11, janela, relógio do sistema ou dispositivo
de áudio. Adaptadores recebem comandos e apresentam o estado real do motor.
Gráficos, áudio, entrada e armazenamento devem ser substituíveis nos demais alvos.
Orçamento de memória e processamento precisa ser medido; desempenho no computador
não estabelece viabilidade em um portátil.

No áudio Linux, `LinuxAudioRuntime` mantém o serviço da aplicação entre casas.
Cada `LinuxAudio` ou `LinuxPcmSink` fecha seu próprio dispositivo e libera sua
referência; a última referência encerra o serviço. O executável declara
`Ownership::application`. Um programa que hospede o adaptador junto a outros
consumidores SDL usa `Ownership::external` e continua responsável pelo
encerramento global. Essas classes pertencem ao adaptador, sem tipos SDL na
simulação ou no formato de salvamento. O acesso ocorre no mesmo thread do dono.

Retratos fixos da família e a cabeça móvel do menu de ações têm funções distintas.
A interface deve seguir o original em navegação, proporção e comportamento.
A fidelidade visual exige observação em janela, além dos testes da simulação.

## Situação e decisões

A cobertura é [parcial](playable-feature-matrix.md). A prioridade é integrar
serviços existentes em percursos completos, conforme o [plano](native-port-plan.md),
e evitar dois motores ou sistemas paralelos para a mesma responsabilidade.
Interfaces públicas de extensão ainda não são estáveis; veja [mods](modding.md).
