# Orientação do OpenDollhouse

- Base comum: papel OMM `project-base`. Mapa local no README; planejamento
  canônico em `docs/architecture.md`, evidência nos documentos de validação
  e em `reports/`. Preservar as particularidades e autorizações abaixo.
- O desenvolvimento inicial foi autorizado em 2026-10-13. Avançar em unidades
  pequenas e verificáveis, mantendo as decisões humanas e os limites da evidência.
- Git de trabalho: https://git.home.zsrv.com.br/zenaror/opendollhouse.
- Checkout ativo: `/home/rafael/_PROJETOS/TheSims1/opendollhouse`. O build
  incremental válido fica em `build/linux-checkpoint`; não usar o cache legado
  em `build/` como diretório de compilação.
- Meta ativa: primeira versão funcional e testável no Linux antes dos ports
  New 3DS/Vita/Android (decisão humana de 2026-10-13). Seguir autonomamente em
  milestones, com testes e checkpoints. New 3DS segue orientando restrições
  arquiteturais; resultados Linux não validam consoles.
- Nintendo Switch também é um possível alvo futuro de build, depois do Linux e
  dos ports prioritários; toolchain, requisitos de distribuição e validação em
  hardware ainda não foram confirmados. Windows e macOS (Apple Silicon e Intel)
  também permanecem alvos futuros de build.
- Consultar OMM no escopo `OpenDollhouse` nas retomadas. Ler o handoff antes de
  atualizar; usar o SHA atual em `expected_sha256`. Registrar revisão, checks,
  pendências e referências. Notas históricas de planejamento não revogam a
  autorização posterior de desenvolvimento.
- Prioridade humana de 2026-10-14: Simitone é a referência técnica principal
  para The Sims Complete Collection. Consultar primeiro seu frontend em
  `refs/upstream/Simitone/Client/` e o núcleo da revisão que ele usa em
  `refs/upstream/Simitone/FreeSO/`, verificando os caminhos TS1 efetivamente
  ativados. O FreeSO principal é referência secundária para comparação,
  formatos compartilhados e correções; não importar comportamentos de TSO
  como se fossem do TS1. As referências antigas mantêm sua proveniência;
  não relabelar evidência anterior sem comparar as implementações.
- Simitone/FreeSO são inspiração e referências. ODH tem implementação própria.
  Identificar revisão e licença de referências; não afirmar equivalência ao
  original apenas porque uma implementação de terceiros se comporta assim.
- Fontes FreeSO/Simitone locais em `refs/upstream/`, ignoradas no Git ODH,
  com revisões fixadas em `docs/references.md`. Consultar primeiro com `rg` e
  leitura de trechos; conector/web quando faltar material ou para nova revisão.
  Preservar distinção FreeSO principal/submódulo Simitone; não rodar setup/build
  ou atualizar os checkouts de referência durante consulta rotineira.
- Os recursos comerciais, ISO, executáveis e saves ficam fora do checkout.
  Instalação local atual: `../referencias/The Sims`; ISO em `../referencias/`.
  A instalação foi movida pelo usuário em 2026-10-13; revalidar caminhos nas retomadas.
  Não modificar os originais ou outros projetos. Fixtures versionadas devem ser
  sintéticas ou ter licença/procedência adequada.
- Decisão humana de 2026-10-14: o runtime exige dados do jogo, sem depender
  de executável original, disco inserido ou checagem de CD. Priorizar uma pasta
  de dados copiados; ISO serve como fonte de preparação/importação. Estado e
  limites da importação ficam em `docs/architecture.md` e README.
- Separar leitura de estrutura, interpretação semântica, execução de VM,
  comportamento do original e testes em hardware. Não promover contagens,
  compilação ou testes sintéticos a prova de compatibilidade.
- Não descartar estado/simulação de entidades fora da tela como otimização.
  Caches gráficos podem descartar representações dispensáveis.
- Ghidra: conferir versão, loader PE e arquitetura x86 para este alvo. Usar um
  projeto próprio sob `reports/ghidra/`, ignorado pelo Git. Preservar hashes e
  distinção entre offset de arquivo, RVA e endereço virtual. Não reutilizar ou
  alterar projetos e configurações do Open-GBP.
- Rodar os checks adequados ao código alterado e atualizar a documentação de
  cobertura. Não instalar dependências sem necessidade técnica demonstrada.
- Nesta etapa não há testes de 3DS, Vita, Android ou Switch. Não declarar esses
  ports funcionais a partir do build Linux.
