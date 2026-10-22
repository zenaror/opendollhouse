# Escopo da primeira build jogável Linux

Este documento define o que significa “primeira build jogável” para o
OpenDollhouse. Um lote de demonstração que abre e permite uma interação é um
marco de desenvolvimento, mas não satisfaz esse objetivo.

## Definição

A build candidata deve oferecer, no Linux, o ciclo principal de The Sims 1 com
os dados externos fornecidos pelo jogador e incluir os sistemas e destinos das
sete expansões. A referência prática mínima é a cobertura funcional do
Simitone para The Sims Coleção Completa. A equivalência precisa ser avaliada por
uma lista de recursos e fluxos de jogo; abrir um lote ou executar alguns BHAVs
não basta.

O objetivo admite bugs conhecidos e limitações documentadas. Não admite que
uma expansão ou uma parte central do ciclo de jogo esteja ausente e ainda assim
seja chamada de build jogável equivalente. Não exige que todos os textos sejam
auditados: uma seleção curta de textos reais, incluindo caracteres acentuados
e um caso de tradução ausente, é suficiente para o primeiro gate de idioma.

## Recursos e fluxos que precisam estar presentes

- **Dados e configuração:** localizar os dados instalados, detectar as
  expansões presentes, escolher um idioma disponível na primeira execução e
  guardar a escolha. O jogador pode alterar idioma e pasta de dados depois.
- **Vizinhança e famílias:** mostrar o overworld/vizinhança com seus lotes;
  criar, escolher, renomear e excluir famílias; mudar uma família para outro
  lote; entrar em um lote e voltar à vizinhança sem perder o estado salvo.
  Excluir uma família inteira significa remover a família escolhida e seus
  vínculos com confirmação, mantendo as demais famílias. Apagar um Sim durante
  a edição ou desocupar uma casa não conta como excluir a família. Esse fluxo
  continua obrigatório para o OpenDollhouse mesmo que o caminho de exclusão
  esteja incompleto na referência Simitone consultada.
- **Lotes residenciais e comunidade:** abrir, jogar e salvar lotes da edição
  base e das expansões. “O Bicho Vai Pegar” (House Party) entra nesta cobertura
  de lotes e objetos.
- **Destinos das expansões:** oferecer acesso jogável ao Centro (Hot Date),
  às Férias (Vacation), à Cidade Mágica (Makin' Magic) e à Cidade Estúdio
  (Superstar), incluindo ida, permanência e retorno ao lote residencial.
- **Demais expansões:** incluir os recursos de Livin' Large e Unleashed no
  fluxo de jogo e nos dados disponíveis, sem presumir que ler seus arquivos
  equivale a implementar suas regras.
- **Ciclo de vida do Sim:** simulação contínua, necessidades, relações,
  autonomia e interações de objetos suficientes para jogar os cenários
  correspondentes. Construir/comprar, pausar, salvar, sair e continuar também
  precisam estar ligados ao fluxo, dentro do escopo observado no Simitone.
- **Conteúdo:** resolver e usar dados da edição base e das expansões presentes,
  mantendo a instalação comercial fora do repositório. A ausência de uma
  expansão deve ser explicada, não convertida silenciosamente em um destino
  vazio.

Esta lista define áreas obrigatórias, não afirma que elas já estejam
implementadas. Para cada item, o plano de validação deve indicar o caminho de
uso, as referências Simitone consultadas, os dados de teste e o que ainda não
foi comparado com o jogo original.

## Critérios de validação

1. Fazer uma matriz de recursos do Simitone por expansão e ligar cada linha a
   uma demonstração reproduzível ou marcar explicitamente a lacuna.
2. Percorrer os fluxos de vizinhança, gestão de família, mudança, entrada e
   saída de lote, salvar e retomar.
3. Entrar e voltar de cada destino: Centro, Férias, Cidade Mágica e Cidade
   Estúdio. Conferir regras específicas de cada expansão, não só a troca de
   cenário.
4. Exercitar conteúdo representativo de House Party, Livin' Large e Unleashed,
   além da edição base.
5. Confirmar a escolha de idioma, persistência após reiniciar, textos de
   interface reais e fallback inglês sem corromper bytes acentuados.
6. Registrar bugs conhecidos com passos para reproduzir e anexar o log local da
   sessão. O fato de a build ainda ter bugs não dispensa esses registros.

Testes sintéticos continuam úteis para formatos e regras. Eles não substituem
uma execução integrada com os dados comerciais externos nem a passagem manual
pelos fluxos acima. Um cenário pequeno como House07 continua sendo uma boa
prova rápida de regressão, não o critério de conclusão do objetivo.

## Logs e diagnóstico

A build jogável deve gravar logs locais automaticamente e indicar ao jogador
onde encontrá-los. No Linux, o caminho padrão será
`$XDG_STATE_HOME/opendollhouse/logs/odh-person.log`, ou
`~/.local/state/opendollhouse/logs/odh-person.log` quando `XDG_STATE_HOME` não
estiver definido. Cada sessão deve registrar versão/commit, horário, idioma,
expansões detectadas, etapas de inicialização, avisos, erros e contexto útil de
VM/interação. Uma falha fatal deve deixar ao menos um registro da última etapa
conhecida e indicar encerramento inesperado na próxima inicialização.

Os logs devem ter rotação com até três cópias anteriores e limite de 1 MiB por
sessão, para não crescerem sem controle. Não devem incluir conteúdo bruto dos
arquivos comerciais nem enviar dados pela rede. Um modo de diagnóstico pode acrescentar traces mais
detalhados quando o jogador quiser reproduzir um problema. Captura de stack ou
core dump nativo pode ser opcional, pois precisa de limites e consentimento
explícitos de armazenamento.

A prévia Linux já tem um logger inicial com caminho local, rotação, limite de
tamanho, identificação de build e etapa de inicialização, exceções e falhas por
sinal. A indicação de encerramento inesperado e a gravação da última etapa são
checadas no próximo início. Ligar o logger a todos os fluxos do jogo e registrar
idioma/expansões detectadas ainda é trabalho pendente; esta implementação não
significa que exista um jogo completo ou captura de stack nativa.
