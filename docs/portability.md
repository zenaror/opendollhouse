# Portabilidade como requisito da base

## Decisão vigente

O código deve considerar os demais alvos desde agora, por determinação humana.
Linux é o ambiente inicial de execução e comparação. Um fluxo funcionando no
Linux não aprova a arquitetura para os portáteis. Não acrescentar funcionalidades
sobre a hipótese de que o mesmo programa .NET será posteriormente recompilado
para todos os aparelhos.

O Simitone permanece como candidato funcional e referência executável. Seu
conjunto atual `net9.0`/DesktopGL **não está aprovado como núcleo compartilhado
obrigatório dos consoles**. Extrair componentes realmente usados, com fronteiras
explícitas, e provar a cadeia de execução dos alvos antes de consolidar essa
escolha. Após nova cobrança humana, priorizar a migração incremental desse núcleo para
C++, substituindo execução real e mantendo C# apenas como ponte temporária de
validação. Próximo corte: operadores BHAV Expression TS1, com acesso a memória
ainda fornecido pela ponte, sem cálculo duplicado ou fallback. Não manter dois
motores completos em paralelo. A viabilidade do runtime legado continua aberta;
interfaces de gráficos/áudio não fornecem um runtime ausente.

## Evidência atual, conferida em 2026-10-25

A revisão corresponde ao frontend `e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`
e FreeSO `7e76c1960252ddd033d8937d5edb47d326f5b69c`, com as adaptações locais
identificadas no handoff. A inspeção do restore anterior à separação do primeiro
componente encontrou 29 projetos no aplicativo e dez na dependência transitiva
da simulação. O programa publicado inclui .NETCore.App e ASP.NETCore.App 9.0.21;
a presença deste último não prova que suas funções sejam necessárias ao TS1.

`FSO.SimAntics.csproj` referencia diretamente DesktopGL, Files, HIT e LotView.
Files, Content e Common também referenciam DesktopGL. `VM` e `VMContext`
conhecem diretamente áudio, mundo gráfico e o provedor global de conteúdo.
`UseWorld=false` não elimina essas dependências da compilação. Portanto,
a separação necessária ainda não existe para o motor inteiro.

A propriedade `IsAotCompatible` não constitui validação. O bootstrap desktop
contém carregamento dinâmico de assemblies; o tradutor de instruções também
constrói operandos por reflexão. Cada caminho precisa ser avaliado e substituído
ou preservado explicitamente conforme o alvo. Native AOT compila para um ambiente
específico e não acrescenta automaticamente uma plataforma. Veja a
[documentação do Native AOT](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/).

| Alvo | Base disponível | Situação do OpenDollhouse |
| --- | --- | --- |
| Linux x64 | DesktopGL/.NET | Fluxos observados no candidato; não jogo completo. |
| Windows | DesktopGL/.NET | Caminho publicado pelo framework; build deste candidato ainda não validada. |
| macOS Intel e Apple Silicon | .NET x64/Arm64 e DesktopGL | Bibliotecas nativas e aplicação ainda não validadas em cada arquitetura. |
| Android | .NET Android e backend MonoGame Android | Exige alvo/aplicativo próprio; o projeto DesktopGL atual não é o port Android. |
| New Nintendo 3DS | Ferramentas nativas ARM11/ARMv6K; experimentos com nanoCLR | Leitor nativo executado em emulador. O módulo C# inalterado é incompatível com o ReSharp fixado; runtime do motor continua sem prova. |
| PlayStation Vita | Ferramentas nativas ARMv7; loader Mono legado experimental | O perfil antigo não estabelece compatibilidade com este motor net9. Bloqueio de runtime/API. |
| Nintendo Switch | MonoGame oficial por acesso licenciado; ferramentas homebrew distintas | Não confundir suporte privado com DesktopGL público nem prometer build homebrew. Caminho ainda não demonstrado. |
| Wii U, alvo desejado | Ferramentas nativas PowerPC/Espresso | Nenhuma cadeia .NET9/MonoGame demonstrada. Requer decisão de runtime e atenção à ordem dos bytes. |

Fontes: [plataformas MonoGame](https://docs.monogame.net/articles/getting_started/platforms.html),
[macOS .NET](https://learn.microsoft.com/en-us/dotnet/core/install/macos),
[.NET Android](https://learn.microsoft.com/en-us/dotnet/android/),
[libctru](https://github.com/devkitPro/libctru),
[ReSharp3DS/nanoCLR](https://github.com/saiitanaa/ReSharp3DS),
[VitaMonoLoader](https://github.com/GrapheneCt/VitaMonoLoader),
[acesso aos consoles MonoGame](https://docs.monogame.net/articles/console_access.html)
e [wut](https://github.com/devkitPro/wut).
São fontes de suporte ou ferramentas, não execuções nossas em hardware.
Ausência de suporte publicado não é prova de impossibilidade de um novo port.

A versão .NET 9 aproxima-se do fim de suporte em 2026-11-10, conforme a
[política da Microsoft](https://dotnet.microsoft.com/en-us/platform/support/policy).
A atualização do host deve ser planejada separadamente; trocar para .NET 10
não resolve os consoles sem runtime compatível.

## Fronteiras que o código precisa respeitar

- Simulação: ordem de execução, necessidades, interações e estado sem acesso
  direto a janela, dispositivo gráfico, reprodução de áudio ou diálogos do SO.
- Dados: formatos e estados explícitos, inteiros de tamanho definido e ordem dos
  bytes declarada; acesso por fontes substituíveis. Não serializar a identidade
  de objetos CLR como contrato entre plataformas.
- Apresentação e serviços: desenho, áudio, controles, armazenamento do usuário,
  configurações e logs fornecidos pelos adaptadores de cada alvo.
- Recursos: medir memória e processamento por componente. A observação de cerca
  de 750 MiB no desktop com gráficos por software não é orçamento de console,
  nem pode ser atribuída integralmente ao .NET sem medição separada.

Componentes extraídos devem substituir o caminho real no candidato Linux e ter
comparação de comportamento. Criar interfaces não usadas não atende ao requisito.
Uma biblioteca C# sem gráficos é uma fronteira útil, mas continua exigindo um
runtime compatível; `netstandard` também não constitui suporte ao 3DS ou Vita.
Se o runtime necessário não puder ser demonstrado, rever sua obrigatoriedade
antes de ampliar o núcleo. Uma solução nativa incremental deve reaproveitar
contratos e evidências, sem criar um segundo jogo completo em paralelo.

## Primeira separação usada pelo jogo

`managed/OpenDollhouse.Simulation` contém `WakeQueue<T>`, extraída do scheduler.
A versão Linux realmente usa esse módulo por referência de projeto; ele não
referencia FreeSO, MonoGame, áudio, arquivos ou reflexão. O alvo .NET Standard 2.0
com C# 7.3 reduz dependências da biblioteca, mas não disponibiliza um runtime
compatível nos consoles. Os pacotes implícitos de referência do SDK continuam
necessários para compilar a biblioteca.

A fila conserva referências, ordenação por ID e listas modificáveis durante a
execução. O adaptador existente conserva os efeitos sobre entidades, desenho,
remoções e aleatoriedade. Doze cenários diferenciais contra o código upstream
fixado passaram, inclusive interrupção, reentrância, reutilização de ID e relógio.
A origem/licença estão no [README do módulo](../managed/OpenDollhouse.Simulation/README.md).

A compilação real e o fluxo comprar/sentar/salvar/reabrir também passaram com
o novo módulo. A evidência está em
`reports/simitone-linux-evaluation/house07-shared-scheduler/`. Isso valida uma
separação concreta e esse fluxo Linux; o motor completo ainda referencia gráficos,
áudio e serviços, agora com 11 projetos na sua dependência transitiva e 30 no
aplicativo. A verificação conhecida de dependências aprova somente o módulo de
fila, enquanto o relatório continua declarando runtime de console não provado.
O experimento seguinte substituiu essa fila por uma implementação nativa; veja
o corte compartilhado abaixo.

## Experimento concreto no 3DS (2026-10-25)

O ReSharp3DS fixado em `9ddfe53fc0900a0b561aebeec5ff1e66527edef6` marca
`DATATYPE_VAR`, `DATATYPE_GENERICINST` e `DATATYPE_MVAR` como não suportados.
Isso bloqueia `WakeQueue<T>`, `Dictionary<uint,List<T>>` e `Func<T,short>`
inalterados nesse candidato. O build recompila contra CoreLibrary 1.9/nanoFramework,
não fornece compatibilidade com a DLL netstandard existente. Veja o
[código fixado](https://github.com/saiitanaa/ReSharp3DS/blob/9ddfe53fc0900a0b561aebeec5ff1e66527edef6/source/clr/TypeSystemLookup.cpp#L90).
O nanoFramework atual tem uma prévia de genéricos; atualizar firmware, ferramentas
e bibliotecas seria trabalho de runtime, não uma simples mudança de projeto.
Não concluir que toda implementação C# para 3DS seja impossível.

A alternativa nativa teve uma execução limitada bem-sucedida: o mesmo
`src/formats.cpp` já existente leu FAR→IFF→BCON sintéticos no host e em um
programa ARMv6K executado por Azahar/RetroArch. Marcador SVC do convidado e
captura mostram PASS. Nenhum parser ou código do motor C++ foi alterado.
Ferramentas devkitARM/libctru já estavam instaladas; o core de teste foi obtido
do release oficial com hash conferido, sem instalação de aplicativo no sistema.
Não houve teste em aparelho físico.

Isso demonstra uma opção de implementação para um componente de dados, não o
motor do jogo. A etapa seguinte já ligou BCON ao Simitone por uma API C pequena, com tipos,
memória e erros definidos, e repetiu a execução no 3DS emulado. A ligação e seus
limites estão em [integração nativa](native-data-integration.md). Não
transportar objetos CLR ou tipos internos de STL pela fronteira. O scheduler C# original fixado permanece como oráculo; a implementação atual
usa a fila nativa descrita abaixo, sem ampliar obrigação universal de CLR.

Continuam faltando execução de simulação e save/retomada no alvo, gráficos,
áudio, entrada e orçamento de recursos. A estratégia é substituir componentes
usados e compartilhar código, sem criar outro motor completo nem começar uma
tradução integral. A cadeia do jogo completo continua reprovada/não demonstrada.

A primeira tentativa do emulador escreveu dois arquivos no perfil real do
RetroArch por causa das regras XDG do Flatpak. Eles foram preservados no relatório
e removidos com autorização humana exata. A repetição final manteve iguais os
147 arquivos de configuração acompanhados e encerrou instância própria/Xvfb.
Não transformar isso em afirmação sobre todo o estado interno do Flatpak.
Evidência: `reports/portability/3ds-managed-probe/report.json`,
`3ds-native-data/report.json` e `3ds-native-execution-final/final-review.json`.
Receita e limites em [tools/portability/3ds-native-data/README.md](../tools/portability/3ds-native-data/README.md).

## Fila nativa usada no Linux e executada no 3DS emulado

A extração seguinte substitui o armazenamento e ordenação da fila por
`native/simulation`, usada pelo VMScheduler real. Preserva identidades distintas
de IDs mutáveis, listas vivas/reentrância, referências e migração do relógio.
Tetos explícitos recusam excesso de recursos sem descartar ações. A ponte permite
carregamento serializado em outra thread e libera recursos com SafeHandle.

Passaram 19 comparações com o código original, 13 grupos de ownership/recursos,
82 verificações nativas, CTest e sanitizers. A mesma fonte executou numa fixture
3DSX com avanço e reconstrução sintéticos; o candidato Linux comprou/usou a cadeira,
salvou e reabriu em outro processo com a biblioteca realmente carregada.
Picos observados da fila foram 9.680 e 9.440 bytes nativos; o resto do jogo,
registro gerenciado e gráficos não entram nessa medida.

Receita e gates separados em [native-simulation-integration.md](native-simulation-integration.md).
A fixture ARM não executa entidades/BHAVs do jogo, CLR ou saves comerciais. O
núcleo completo, gráficos/áudio/entrada e orçamento total continuam pendentes,
assim como os outros alvos e hardware físico. Esta prova libera continuar os
fluxos visíveis de famílias enquanto a separação segue vinculada a sistemas usados.

## Verificações antes de consolidar a arquitetura

1. Definir, para cada alvo, compilador, runtime/API, bibliotecas nativas e método
   de distribuição disponíveis. Distinguir ferramentas públicas de acesso
   licenciado e distinguir experimento de uma base mantida.
2. Compilar e executar um componente real do projeto nesse ambiente: carregar
   dados, avançar estado e conservar/recuperar esse estado. Um exemplo vazio ou
   uma biblioteca compilada apenas no PC não aprova esta etapa.
3. Demonstrar uma cena com gráficos, áudio, entrada e armazenamento próprios do
   alvo, preservando a mesma simulação. Medir custos; não reduzir a simulação
   fora da tela para fazer o teste caber.
4. Registrar separadamente compilação, execução em ambiente de teste e hardware.
   Há execução emulada aprovada somente para o leitor sintético 3DS; não há
   execução física nem motor de console aprovado. Não foram instalados SDKs ou
   runtimes. Os demais alvos ainda não tiveram build/executável neste projeto.

Não adiar essas verificações até o jogo inteiro estar pronto no desktop. Antes
de retomar a ampliação das funcionalidades, iniciar a separação usada do núcleo
e resolver o plano de execução dos alvos prioritários com base nas restrições
acima. Famílias, expansões, idioma e interface original continuam no escopo.

## Inventário reproduzível

`tools/simitone/audit_dependencies.py` lê os manifests do restore existente;
não baixa pacotes, não compila nem altera o checkout de referência. Exemplo:

```sh
python3 tools/simitone/audit_dependencies.py \
  --source-root . \
  --entry refs/work/Simitone-linux/FreeSO/TSOClient/tso.simantics/FSO.SimAntics.csproj \
  --output reports/portability/dependencies/simulation.json \
  --require-data-core
```

O código de saída 1 significa dependências incompatíveis com a fronteira de um
núcleo de dados isolado. O inventário atual deve reprovar essa tentativa; não
retirar dependências da lista para obter aprovação. Condições de pacotes não são
avaliadas pelo script e o restore pode estar desatualizado: ambos os hashes são
registrados. Mesmo uma fronteira limpa não prova compatibilidade de runtime.

Evidência detalhada, com fontes e limites:
`reports/portability/desktop-android/report.json`,
`reports/portability/runtime-targets/report.json` e
`reports/portability/dependencies/`.
