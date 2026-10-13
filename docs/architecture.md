# Arquitetura em investigação

ODH pretende manter um núcleo C/C++ compartilhado, com renderizadores e integração
de sistema específicos por plataforma. C++17 é a escolha do primeiro protótipo;
toolchains e orçamentos dos consoles ainda não foram validados.

O núcleo deve poder executar testes sem janela, GPU ou áudio. A interface envia
ações lógicas. Simulação, scheduler e renderização têm relógios e responsabilidades
separados. Resources resolve origem/precedência e fornece representações com cache
limitado; Audio recebe eventos; Platform abstrai I/O, entrada e serviços.

A VM inicial deverá interpretar os BHAVs. Entidades, roteamento, ocupação,
autonomia, necessidades, relacionamentos e serviços de famílias não se reduzem
ao despacho de opcodes. Não introduzir pausas arbitrárias de execução ou
primitivas desconhecidas que retornam sucesso silenciosamente.

Classic e Extended compartilham mecanismos, com regras, conteúdo habilitado e
persistência identificados. SDK, Studio e formato `.simpack` ficam para interfaces
estabilizadas. Apartamentos não fazem parte do primeiro escopo.

## Experimentos e critérios

1. **Identidade e leitores:** tornar a leitura estrutural reproduzível, preservar
   inputs e diagnosticar variantes não suportadas. Primeiro protótipo implementado.
2. **Interação controlada:** resolver TTAB comprimida e dependências da cadeira
   `DiningChairs.iff`, GUID `0x26BFBB29`. Antes da execução, determinar o significado
   dos operandos, contexts e retomadas usados pelo cenário.
3. **VM:** traces de instruções, chamadas e estados com fixtures sintéticas e
   cenários originais identificados. Comparação com FreeSO serve de referência
   auxiliar; decisões de fidelidade precisam de evidência do original.
4. **Composição gráfica cedo:** sprites com profundidade, personagens animados,
   paredes e sobreposição. Comparar software, híbrido CPU/GPU e geometria derivada
   no New 3DS antes de escolher o backend. PICA200 não oferece fragment shader
   programável equivalente ao das APIs modernas.
5. **Lote mínimo:** tempo, interação completa, necessidades, compra/construção
   delimitada e persistência ODH. Separar importação de saves, persistência ODH e
   exportação consumível pelo original.
6. **Cobertura:** sistemas clássicos, expansões e conteúdo personalizado por
   cenários/versionamento; depois extensões e ferramentas.

Metas sugeridas para o primeiro cenário New 3DS: 30 FPS, p95 de frame até 33,3 ms,
simulação sem descartar ticks e memória até o menor valor entre 96 MiB e 80% do
orçamento medido. São critérios propostos, não resultados ou limites universais.

Riscos principais: oclusão fiel na PICA200; serviços implícitos em BHAV; ordem de
resolução; persistência e viagens; diferenças de edições/expansões; picos de
decodificação. Fora da tela pode-se descartar representação gráfica, preservando
estado e comportamento das entidades.
