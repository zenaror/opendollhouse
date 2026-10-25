# Avaliação da base Simitone no Linux

Receita atual: [integração do leitor nativo](native-data-integration.md).
Ela exige também `managed/`, `native/`, experimental22 e ODHNativeDataLibrary.
Receitas de checkpoints anteriores abaixo permanecem históricas.

## Estado e alcance

Retomada em 2026-10-25 após `get_goal` retornar `active`. A instrução humana
mais recente manda respeitar o status da meta; a pausa inicial não sobrepõe uma
retomada posterior na interface. O texto da meta ainda guarda a instrução
histórica de pausa, mas não foi editado nem falsamente marcado como concluído.

O fork foi compilado e executado numa tela Xvfb isolada. A captura da vizinhança
foi inspecionada. Dois cliques selecionaram a House07, mostrando Bob e Betty,
e entraram no lote. A entrada exibiu exceções de simulação em objetos: o fluxo
**não está aprovado como jogável**. Compra, interações, salvar/reabrir, som e
destinos das expansões ainda não passaram por esta base.

## Identidade e separação das fontes

| Componente | Revisão/local |
|---|---|
| Simitone alexjyong | `e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` |
| FreeSO correspondente | `7e76c1960252ddd033d8937d5edb47d326f5b69c` |
| Checkout de trabalho | `refs/work/Simitone-linux/`, ignorado no Git ODH |
| Publicação de teste | `build/simitone-linux/`, separada do build C++ |
| Fontes de consulta preservadas | `refs/upstream/` |
| Dados originais externos | `../referencias/The Sims/` |

O checkout de trabalho foi clonado sem hardlinks do fork local, teve o remote
ajustado para `https://github.com/alexjyong/Simitone.git` e materializou o
gitlink FreeSO exato. Os demais submódulos não foram necessários à compilação
Desktop observada. Nenhum setup upstream ou instalador automático foi executado.

## Resultado da compilação e inicialização

- SDK instalado: .NET `10.0.113`. `dotnet restore` e build Release do Desktop
  passaram; build inicial sem adaptações teve 788 avisos e zero erros.
- Executar diretamente a saída de build falhou por `Eto` ausente. A saída de
  publicação inclui essa dependência. Não confundir build com pacote executável.
- Publicação autossuficiente `linux-x64`, com runtime .NET/ASP.NET `9.0.21`,
  sem AOT nem remoção de código não referenciado. Isso valida apenas este
  pacote de teste, não execução sem runtime nos consoles.
- O restore com RID revelou conflitos NU1605 em dependências antigas de
  servidor herdadas. A receita de avaliação mantém esses avisos e segue a
  configuração upstream `TreatWarningsAsErrors=false`, `WarningsAsErrors=`.
  Não se trata de aprovação de dependências para distribuição; a publicação
  padrão sem essa configuração falhou e esse resultado foi preservado.
- `-nosound` expôs uma falha: `PlaySoundEvent` retornava null e a vizinhança
  chamava `AddOwner` sem verificar. A adaptação usa `?.AddOwner`, permitindo
  abrir o mapa com áudio desativado. Não foi validada saída física de áudio.
- O perfil explícito `ODH_USER_DIR` evita escrita no perfil pessoal. Apenas
  caminhos absolutos são aceitos. A escolha acontece antes de `GlobalSettings`.
- O teste copia os dados para uma pasta temporária, sem depender de executáveis
  do jogo. Perfil, dados copiados, aplicação e Xvfb são encerrados/removidos ao
  término. Os seis arquivos comerciais acompanhados mantiveram os hashes.

## Reproduzir após preparar o checkout

Os patches do frontend são aplicados na raiz de `refs/work/Simitone-linux`;
o patch de diagnóstico é aplicado na raiz de seu submódulo `FreeSO`.
São adaptações pequenas de avaliação, não uma incorporação do runtime ao ODH.

```sh
git -C refs/work/Simitone-linux apply ../../../tools/simitone/0001-isolated-profile.patch
git -C refs/work/Simitone-linux apply ../../../tools/simitone/0002-optional-neighborhood-audio.patch
git -C refs/work/Simitone-linux/FreeSO apply ../../../../tools/simitone/0003-vm-diagnostics.patch
git -C refs/work/Simitone-linux/FreeSO apply ../../../../tools/simitone/0004-saved-frame-resolution-audit.patch
git -C refs/work/Simitone-linux/FreeSO apply ../../../../tools/simitone/0005-native-code-signature.patch

dotnet publish refs/work/Simitone-linux/Client/Simitone/Simitone.Desktop/Simitone.Desktop.csproj \
  -c Release -r linux-x64 --self-contained true \
  -p:PublishAot=false -p:PublishTrimmed=false \
  -p:TreatWarningsAsErrors=false -p:WarningsAsErrors= \
  -o "$PWD/build/simitone-linux"

python3 tools/simitone/window_smoke.py \
  --runtime-dir build/simitone-linux --game-root '../referencias/The Sims' \
  --report-dir reports/simitone-linux-evaluation/new-observation \
  --seconds 40 --click 10:754:318 --click 15:100:610 --vm-diagnostics
```

O diretório de relatório deve ser novo. Requisitos do observador: Python3,
Xvfb, xwd, xwininfo, ImageMagick e xdotool. Os eventos são enviados apenas ao display
criado pelo teste. A pausa entre mover o mouse e apertar o botão é necessária
porque a UI processa os alvos do quadro anterior; cliques instantâneos chegaram
a selecionar a casa anterior sob o cursor. O log agora identifica o número
da casa selecionada. Coordenadas acima valem para o cenário 1024×768 observado.

Saída zero do observador significa processo vivo, confirmação do perfil
isolado e seis hashes preservados. **Não significa jogo correto:** a entrada
House07 retornou zero mesmo com diálogos de exceção visíveis. Conferir captura
e log. O relatório guarda argumentos, ambiente permitido, hashes do apphost,
assemblies principais e captura; não grava variáveis de ambiente indiscriminadas.

## Evidências e pendências

Relatórios locais ignorados em `reports/simitone-linux-evaluation/`:
`managed-startup/` registra a falha de áudio, `neighborhood-nosound/` o mapa,
`house07-stable-input/` a seleção correta e a exceção ao entrar. As tentativas
anteriores de cliques foram preservadas para distinguir problema da automação
de falha do motor. O lote apresentou `NullReferenceException`, com chamada
reportada em `VMThread.Pop/HandleResult` e rotina de impacto de sala de lâmpada.
O relatório `house07-unresolved-routines/` confirmou a causa imediata: frames
importados ficaram com `Routine == null`, e o retorno de uma rotina filha tenta
ler a instrução do pai ausente. Foram registrados oito frames não resolvidos:

| Objeto | Rotina privada | GUID do dono salvo |
|---|---|---|
| 110 | 4109 | 2564857523 |
| 133 | 4109 | 3749178898 |
| 136 | 4096 e 4113 | 1797669432 |
| 152 | 4109 | 744145731 |
| 153 | 4096 | 2099284406 |
| 157 | 4096 e 4109 | 3127372498 |

O objeto 136 coincide com a pendência PhoneLine do motor próprio; mudar de base
não resolveu automaticamente essa questão. A investigação seguinte confirmou
os arquivos carregados e as assinaturas completas de Init/Main, incluindo
dependências, e encontrou o tratamento nativo de incompatibilidade. O cálculo
foi implementado e validado em 17 casos. Naquele checkpoint a recuperação tinha apenas
um adaptador experimental que falhava antes da migração; o resultado atual está
na seção de entrada e primeiro comando abaixo. Os campos baixos do OBJT não são contagens de instruções;
comparar apenas `BHAV.Version` é insuficiente. Ver
[contrato de assinaturas e recuperação](simitone-save-code-signatures.md).
Não esconder erros, escolher outro dono por aproximação, descartar frames ou
simular retorno de sucesso. O próximo trabalho é adaptar a recuperação nativa,
preservando identidade, dados escolhidos, colocação e notificações aos Sims.

Captura de entrada com exceção:
`house07-stable-input/startup.png`, SHA256
`49c06b3b5c2d9582f54d9a89600f239b4f7f46838023ed076f1a3744f552cc0f`.
Captura do diagnóstico dos oito frames:
`house07-unresolved-routines/startup.png`, SHA256
`10a21f63e7627d33b525e7d8e1e360bece574cbf608130ebc46ac131a6d1696c`.
Esses testes terminaram com aplicação e Xvfb encerrados; seus perfis e cópias
temporárias foram removidos. Logs e capturas ficaram nos relatórios locais.

## Portabilidade investigada desde o início

| Fronteira | Evidência estática na revisão FreeSO acima | Ação de projeto |
|---|---|---|
| Runtime e bibliotecas | `tso.simantics/FSO.SimAntics.csproj` referencia net9.0, gráficos, áudio e mundo | Verificar ferramentas/runtime por alvo antes de consolidar a base; propriedades AOT não comprovam suporte |
| Interpretador e geração de código | `FSO.SimAntics.JIT/Runtime/VMAOTTranslator.cs` admite `VMRoutine`; Desktop só inicializa assemblies AOT com `-jit` | Começar pelo interpretador e manter geração dinâmica fora dos requisitos mínimos |
| Reflexão | `FSO.SimAntics.JIT/Runtime/AssemblyStore.cs` e leitura IFF criam tipos dinamicamente | Investigar registros explícitos e preservação de tipos quando houver uma rota AOT comprovada |
| Simulação/apresentação | `tso.simantics/VMContext.cs` guarda World; `VM.cs` acessa câmera/listener; `VM.UseWorld` já oferece uma separação parcial | Transformar essas fronteiras em serviços substituíveis sem abandonar entidades fora da tela |
| Desktop | Eto, System.Drawing, imports Windows/Discord e projetos de servidor aparecem no grafo herdado | Separar janela, entrada, arquivos, presença externa e serviços de desktop do núcleo compartilhável |

New 3DS, Vita, Android, Switch, Windows, macOS Intel/Apple Silicon e Wii U
permanecem alvos de planejamento. Nenhum foi compilado ou testado neste gate.
O uso de memória do processo Linux inclui runtime e renderização por software;
não é medida isolada da simulação nem prova de viabilidade nos portáteis.

O FreeSO inclui MPL 2.0 e componentes com notices próprios. Há cabeçalhos MPL
em arquivos do frontend, mas não foi encontrada uma licença geral na raiz do
Simitone. Essa procedência precisa ser esclarecida por componente antes de
distribuir uma base derivada; não presumir uma licença única por associação.


## Recuperação de saves: checkpoint anterior de preflight (2026-10-25)

`tools/simitone/experimental-saved-code-recovery.patch` é um incremento aplicado
**depois** dos patches0003/0004/0005 na raiz do FreeSO de trabalho. Não faz parte
da receita básica acima: é um checkpoint incompleto da migração de saves,
sem aprovação de jogabilidade. O replay em árvore temporária com os patches
anteriores reproduziu exatamente os cinco arquivos de trabalho.

```sh
git -C refs/work/Simitone-linux/FreeSO apply ../../../../tools/simitone/experimental-saved-code-recovery.patch
dotnet run --project tools/simitone/recovery-tests/RecoveryTests.csproj -c Release
dotnet run --project tools/simitone/recovery-tests/person-state/PersonStateTests.csproj -c Release
```

Os testes ligados ao código real passaram em 13 casos de política de recuperação,
com serviços simulados, e cinco casos dos snapshots de pessoa com tipos da DLL
FSO.Files real. Nenhum desses casos executa BHAV comercial nem prova colocação
no motor ou recuperação completa. O projeto desktop foi compilado e publicado
com as adaptações; os avisos antigos de dependências permanecem.

Teste integrado separado: `reports/simitone-linux-evaluation/house07-recovery-direct/`.
A captura antes do segundo clique mostra Bob/Betty, 7 Sim Lane; o log confirma
house=7. O plano compara 82 tipos (61 diferentes), todos com definições e raízes
obrigatórias disponíveis. A validação para **antes de migrar qualquer entidade**:
14 instâncias ainda exigem suporte, 12 peças de portas e dois Sims. Portanto,
nenhuma recuperação real de objeto foi validada nesse teste, e a casa não abriu.
`--require-clean-vm --vm-diagnostics` retorna falha quando há exceções fatais ou
de simulação. O gate de28s terminou antecipadamente com erro fatal da validação,
preservou os seis hashes protegidos e encerrou aplicação/Xvfb; a cópia dos dados
e o perfil temporários foram removidos. Não houve acesso ao teclado ou mouse físico.

Os caminhos implementados são objetos comuns, grupos não portal e um vínculo
direto de objeto apoiado em outro móvel, com verificações estruturais. A rotina
de pessoa ainda precisa desenrolar usos/ações e restaurar aparência/colocação;
as portas precisam das operações específicas de ocupação e rotas. Preservaram-se
os registros ordenados dos usos e ações da pessoa antes da conversão para que
a próxima implementação não dependa de reconstrução incompleta dos frames.
Contrato e limites em [simitone-save-code-signatures.md](simitone-save-code-signatures.md).


## Entrada e primeiro comando sem erros (2026-10-25)

O candidato com recuperação de pessoas/portas e normalização dos bancos antigos
compilou/publicou. O patch experimental continua fora da receita básica, aplicado
após 0003/0004/0005, mas agora contém onze arquivos e passou por replay exato.
A revisão conferiu o patch e os hashes das fontes. Os testes sintéticos são 22 de
política/portais e 5 de snapshots com tipos FSO.Files reais. Saídas, comandos e
hashes em `house07-recovery-avatar-slots/policy-tests.json`.

| Gate real isolado | Duração | Resultado observado |
|---|---:|---|
| `house07-recovery-avatar-slots` | 40 s | Casa renderizada com Bob/Betty; zero exceções de VM/fatais |
| `house07-first-command` | 40 s | Menu Go Here mostrado pelo clique no chão; zero exceções |
| `house07-first-go-here` | 45 s | Selecionar Go Here moveu Bob a outro ponto; zero exceções |
| `house07-first-door-walk` | 55 s | Destino no pátio fez Bob sair da cozinha e chegar fora; zero exceções |

Todos preservaram os seis hashes protegidos e encerraram aplicação/Xvfb,
removendo os dados/perfis temporários. A imagem final da caminhada ao pátio
comprova origem/destino, sem traçar cada passo ou provar o instante/animação da
porta. Nenhum teste usa o teclado ou mouse físico. `result.json` continua com
`gameplay_verified=false`, pois não há aprovação do ciclo completo ou do jogo.

A entrada registra 150 recuperações de objetos e 2 de pessoas. Os oito frames
antigos ausentes viraram zero frames ausentes nas recuperações registradas;
identidades e palavras retidas foram preservadas. Pessoas mantiveram sua
instância/thread e necessidades no diagnóstico de reset, antes do Init final.
Os primeiros testes após integrar a recuperação expuseram bancos antigos menores
que o modelo atual: PersonData80→pelo menos 101 campos e armazenamento de objetos
carregados com menos de três posições. A extensão preserva o prefixo salvo e
registros maiores. A correção de PersonData também cobre o caminho de vizinhos.
Não foram adicionados retornos fictícios nem suprimidos diálogos de erro.

A captura Linux registrou cerca de 750 MiB RSS/HWM com renderização por software.
Isso inclui o ambiente desktop e não valida viabilidade, orçamento de memória,
gráficos ou runtime de qualquer console. A arquitetura e os alvos futuros
continuam sob avaliação separada.

Pendências: salvar/reabrir, vizinhança, famílias,
compra/construção, viagens e atividades das sete expansões, idioma e áudio.
A equivalência nativa ainda requer validar UseCount aninhado, ordem de busca de
colocação, cache de paredes e o tipo especial cursor. Este é um avanço executável
do fluxo, sem declaração de build jogável segundo o escopo definido pelo usuário.


### Interação completa com a geladeira

`house07-first-fridge-menu` mostrou Have A Snack. O primeiro teste da ação,
`house07-first-snack`, não registrou erro e mostrou a cobrança de §5, mas isso
sozinho não provava que o Sim havia comido. O gate `house07-snack-state` de 60 s
acrescentou amostras somente leitura após o passo de simulação:

| Etapa observada de Bob | Evidência do log |
|---|---|
| Escolheu o lanche | Have A Snack, ação 2, geladeira 112 |
| Pegou comida | Objeto 192 carregado na mão |
| Comeu | Eat, ação 3; fome de 94 para 100 durante a ação |
| Terminou | Depois de Eat, ações vazias e mãos livres no tick 1200 |

O teste passou com zero exceções de VM/fatais, dados originais intactos e
aplicação/Xvfb encerrados. `snack-summary.json` preserva as transições e os hashes
das assemblies; `source-manifest.json` identifica as onze fontes e o patch.
O diagnóstico usa `ODH_VM_DIAGNOSTICS=1`, limita a amostragem a cada 30 ticks,
funciona na thread da simulação e não executa árvores nem ações adicionais.
A melhora de fome foi correlacionada com Eat e com o objeto carregado; o simples
desaparecimento de uma ação não seria prova suficiente, pois poderia ser cancelamento.

A revisão independente anterior cobriu a entrada e o Go Here local. O novo
resultado da comida é separado e não valida todos os objetos, o ciclo salvar/
reabrir, áudio, conteúdo personalizado ou qualquer expansão completa.


## Salvar, sair, voltar e continuar

O gate final `house07-saved-return-and-resume` usa o novo
`tools/simitone/session_smoke.py`: uma cópia dos dados, um perfil e uma tela
virtual para dois processos distintos. Primeiro escolhe o lanche, salva com Eat
em andamento, volta à vizinhança e reentra sem gravar novamente; depois encerra
o processo pelo harness e abre a mesma partida em outro processo. Não é uma
validação da interface Quit normal.

Resultado: 80 s + 60 s, zero exceções de VM/fatais ou erros de diagnóstico, seis hashes
dos originais preservados e limpeza dos processos/perfil/cópia. O arquivo da casa
mudou, continha FSOV e permaneceu com o mesmo hash nas duas fases. O conteúdo
interno FSOV salvo e lido nas duas reentradas coincidiu. Estado salvo: relógio no tick 889
(7:30), orçamento §660, Bob na mesma posição com comida 192 e Eat (ação 1). O estado inteiro
registrado foi restaurado sem diferenças antes de Load, exceto a normalização
de versão SIMI 62→64 ao ler o formato gravado. Os bancos de pessoa/necessidades
dos três Sims continuaram idênticos após Load. As 39 mudanças de objetos feitas
pelos callbacks não foram apagadas do relatório ou promovidas a prova de paridade.
No segundo processo Eat continuou, fome 94→100, terminou e liberou as mãos/fila.

Conferência independente: `final-review.json` nesse report, com hashes dos logs,
artefatos, fontes e amostras brutas. `source-manifest.json` registra o replay
exato das 15 fontes FreeSO e uma frontend. `gameplay_verified` continua falso
porque estes fluxos não validam todo o jogo.

### Problemas descobertos e corrigidos

- SIMI.Write substituía os globais por zeros, escrevia cabeçalho antigo com
  quantidade incoerente de palavras e confundia MiscExpense com MiscIncome.
  Agora preserva os 38 globais, não modifica a instância e escreve o layout
  coerente de versão mínima 0x40. Versões maiores são mantidas. Sete casos ligados
  à fonte e aos tipos reais de arquivo falharam antes e passaram depois.
- O post-load de FSOV executava Load em pessoas TS1, sorteando novamente seus
  interesses. A condição agora segue o fluxo nativo que pula pessoas; os objetos
  comuns e os modos não TS1 mantêm seus callbacks. O primeiro teste expôs 62
  diferenças, incluindo interesses; o teste corrigido preserva os bancos de pessoa.
- Não salvar deixava o orçamento alterado no cache:665→660 por um lanche e ainda
  660 ao reentrar sem salvar. O reload do perfil recuperou §665. A correção conserva
  ID/caminho do UserData atual e limpa apenas personagens de usuário, incluindo
  seus arquivos processados; preserva objetos globais, Downloads e catálogo.
  O teste combinado mostrou que uma gravação anterior de §660 também é respeitada,
  sem restaurar equivocadamente os §665 da instalação original.

A promoção de SIMI é uma normalização do formato aceito por este leitor, não
prova de compatibilidade com todas as revisões do executável original. O modelo
continua conservando 38 palavras do layout que comporta 64; palavras adicionais
não modeladas não ganharam suporte neste checkpoint.

### Reprodução e comparação

Aplicar o experimental na raiz do FreeSO após 0003/0004/0005. Depois aplicar o
incremento frontend, dependente do helper do experimental, após 0001/0002:

```sh
git -C refs/work/Simitone-linux apply ../../../tools/simitone/0006-save-and-discard.patch
dotnet run --project tools/simitone/save-tests/SimiTests.csproj -c Release
dotnet run --project tools/simitone/save-tests/DiscardTests.csproj -c Release
python3 -m unittest discover -s tools/simitone/save-tests -p test_compare_save_states.py
```

Os testes são 7 de SIMI, 2 de provedores reais com IFF sintéticos e 14 de comparação.
A coleta `VMTS1SaveDiagnostics` não executa BHAV nem serializa novamente a plataforma
inteira; amostra o estado e hashes de componentes. O runner com
`--require-save-state --require-clean-vm --vm-diagnostics` exige comparação completa
até a restauração e preservação dos bancos de pessoa após Load. Todos os demais
campos após callbacks continuam no relatório. `compare_save_states.py --strict`
compara explicitamente os estágios escolhidos, sem tolerância de relógio e sem
filtrar atores, globais ou grupos. Apenas SIMI.Version 0..63→64 recebe a classificação
de normalização de formato, sempre visível.

Pendências: salvar o conjunto casa/vizinhança/personagens ainda não é uma transação
atômica; o arquivo cas.weather é global; salvar durante viagens e RuntimeSubset
não foi validado. Esses limites não impedem o ciclo observado, mas impedem declarar
confiabilidade geral ou equivalência ao original. Compra/construção, famílias,
expansões, áudio/idioma e conteúdo personalizado continuam como próximos fluxos.

## Compra, uso e persistência observados (2026-10-25)

O ciclo `house07-chair-save-before-notice` compra a cadeira Werkbunnst por §80,
coloca o novo objeto 139, faz Bob executar Sit e salva/reabre a casa em outro
processo. O saldo §585, o objeto, a posição/direção e o preço permanecem iguais.
A mesma ação Sit e postura sentada foram registradas juntas antes de salvar;
o conforto não aumentou, e não foi testada uma segunda ordem Sit após reabrir.
Não houve erros de VM/fatais.
Os seis arquivos comerciais acompanhados conservaram os hashes; aplicação,
tela virtual e dados temporários foram encerrados/removidos.

`tools/simitone/compare_purchase.py` vincula compra/ação/estado/arquivo salvo a
`session_smoke.py` e exige `--session-result` para aprovar o ciclo completo observado.
Usar `--require-action-name Sit --require-posture 1` neste caso. O modo opcional
`--require-comfort-gain` é outro requisito e não foi aprovado por este ensaio.
Treze testes sintéticos cobrem rejeições de provas falsas. Um ensaio anterior
sem ação Sit foi rejeitado, apesar de compra e persistência corretas. Outro ensaio
foi bloqueado por um aviso automático do kit mágico antes de Save; o caso final
salva mais cedo. Um relatório intermediário perdeu os campos detalhados por erro
de cópia do resumo; o caso final mantém `result.json` integral e o resumo separado.

O patch experimental corrente tem 18 arquivos, SHA256
`bb7a35bfae36de0e56b8c8820dc5ff171fce8f0d5b65f9cc993000a9cd906ddf`,
reproduzido exatamente após 0003/0004/0005; frontend 0006 permanece obrigatório.
Diagnósticos de compra e ação só funcionam com `ODH_VM_DIAGNOSTICS=1` e não
alteram decisões do jogo. Falhas dos diagnósticos são contidas. O build foi
publicado localmente; distribuição binária e equivalência geral não estão aprovadas.
Os limites e a próxima etapa estão em [continuidade](current-handoff.md).
