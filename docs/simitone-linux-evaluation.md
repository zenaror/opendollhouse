# Avaliação da base Simitone no Linux

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
foi implementado e validado em 17 casos, mas a recuperação ainda não foi
adaptada ao Simitone. Os campos baixos do OBJT não são contagens de instruções;
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
