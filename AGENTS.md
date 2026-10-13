# Orientação do OpenDollhouse

- O desenvolvimento inicial foi autorizado em 2026-10-13. Avançar em unidades
  pequenas e verificáveis, mantendo as decisões humanas e os limites da evidência.
- Git de trabalho: https://git.home.zsrv.com.br/zenaror/opendollhouse.
- Meta ativa: primeira versão funcional e testável no Linux antes dos ports
  New 3DS/Vita/Android (decisão humana de 2026-10-13). Seguir autonomamente em
  milestones, com testes e checkpoints. New 3DS segue orientando restrições
  arquiteturais; resultados Linux não validam consoles.
- Consultar OMM no escopo `OpenDollhouse` nas retomadas. Ler o handoff antes de
  atualizar; usar o SHA atual em `expected_sha256`. Registrar revisão, checks,
  pendências e referências. Notas históricas de planejamento não revogam a
  autorização posterior de desenvolvimento.
- FreeSO/Simitone são inspiração e referências. ODH tem implementação própria.
  Identificar revisão e licença de referências; não afirmar equivalência ao
  original apenas porque uma implementação de terceiros se comporta assim.
- Os recursos comerciais, ISO, executáveis e saves ficam fora do checkout.
  Não modificar os originais ou outros projetos. Fixtures versionadas devem ser
  sintéticas ou ter licença/procedência adequada.
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
- Nesta etapa não há testes de 3DS, Vita ou Android. Não declarar esses ports
  funcionais a partir do build Linux.
