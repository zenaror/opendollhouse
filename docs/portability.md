# Portabilidade

O OpenDollhouse usa C/C++ para o motor, a interface e os serviços do jogo.
C#/.NET não é um requisito do produto. Simitone e FreeSO são referências para a
migração; seus runtimes gerenciados não são uma camada obrigatória nos aparelhos
futuros. A [arquitetura](architecture.md) descreve a separação entre a simulação
compartilhada e os serviços de cada plataforma.

Linux é o primeiro ambiente de desenvolvimento e validação. Isso não significa
que o código deva depender de Linux, nem que uma função aprovada nele esteja
automaticamente pronta para outro sistema. O [progresso](progresso.md) informa a
situação funcional; o [escopo jogável](linux-playable-scope.md) define os limites
da primeira versão.

## Plataformas previstas

Esta tabela distingue o alvo desejado daquilo que uma ferramenta permite fazer.
Ela não anuncia versões prontas para baixar.

| Plataforma | Caminho técnico previsto | O que ainda exige validação do jogo |
|---|---|---|
| Linux | Compilador C++ e CMake; adaptadores desktop já usados pelo projeto. | Percursos completos, interface, estabilidade e distribuição da versão jogável. |
| Windows | Compilação nativa ou cruzada com uma cadeia MinGW compatível. | Janela, texto, áudio, entrada, armazenamento e execução real no Windows. |
| macOS Intel | Clang e SDK macOS para x86_64; OSXCross pode ajudar na compilação a partir de Linux. | Dependências nativas, aplicação empacotada e execução em Mac Intel. |
| macOS Apple Silicon | Clang e SDK macOS para arm64, com build separado do Intel. | Execução em hardware Apple Silicon, bibliotecas e distribuição próprias. |
| Android | NDK, aplicação Android e adaptadores de apresentação, entrada e armazenamento. | Instalação, suspensão/retomada, controles, consumo de recursos e execução no aparelho. **Retroid Pocket 5** é o dispositivo de referência planejado. |
| New Nintendo 3DS | devkitARM/libctru; apresentação e limites adequados ao aparelho. | Simulação, dados reais, gráficos, áudio, controles e salvamento em conjunto. Experimentos de componentes não são um port do jogo. |
| PlayStation Vita | VitaSDK e bibliotecas nativas adequadas ao frontend. | Aplicativo completo, memória, renderização, áudio, entrada e salvamento no alvo. |
| Nintendo Switch | Cadeia nativa apropriada ao ambiente escolhido, com adaptadores próprios. | Build e execução do jogo, recursos, controles e distribuição. Ferramentas homebrew e SDKs licenciados são caminhos distintos. |
| Wii U | Alvo desejado, usando ferramentas nativas PowerPC e serviços da plataforma. | Viabilidade do conjunto, ordem dos bytes, recursos e todos os adaptadores do jogo. |

A intenção de suportar uma plataforma não comprova viabilidade completa.
Compilar um exemplo com seu SDK comprova apenas uma parte do ambiente; executar
um leitor de arquivos também não comprova uma versão jogável.

## Fronteiras do código compartilhado

- **Simulação:** scripts, necessidades, filas de ações, relações e relógio não
  devem depender de janela, dispositivo gráfico ou diálogos do sistema.
- **Arquivos e estado:** usar tipos com tamanho definido, ordem dos bytes
  explícita e identidades estáveis. Arquivos salvos não podem depender de
  endereços de memória, objetos de uma biblioteca gráfica ou caminhos pessoais.
- **Apresentação e serviços:** desenho, som, controles, fontes, configuração,
  armazenamento e logs passam por adaptadores substituíveis. O jogo precisa de
  implementações reais desses adaptadores, não somente interfaces vazias.
- **Recursos:** caches, carregamento e alocações precisam de limites e erros
  definidos. O orçamento do desktop não é o orçamento de um portátil.
- **Comportamento:** trocar um backend ou reduzir a resolução não autoriza
  remover regras da simulação para fazer o jogo caber no aparelho.

A preparação para portes inclui diferenças de arquitetura, alinhamento e ordem
dos bytes, especialmente entre ARM, x86 e PowerPC. O salvamento e sua reabertura
precisam manter o mesmo significado mesmo quando a representação interna muda.
Os [formatos](formats.md) e o [guia de extensões](modding.md) devem acompanhar
mudanças nos contratos de dados e conteúdo.

## Ferramentas e comandos existentes

Para preparar uma compilação nativa do projeto, a partir da raiz do checkout:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 2
```

As dependências e a execução do aplicativo são descritas no
[guia de ferramentas](tools.md) e no [README](../README.md). Esses comandos não
selecionam automaticamente um backend para Android ou consoles.

O repositório também contém pequenos programas de portabilidade para 3DS.
Eles usam `DEVKITPRO`, `DEVKITARM` e, opcionalmente, `CXX`, sem exigir um diretório
pessoal fixo. Com a cadeia devkitPro já instalada e essas variáveis configuradas:

```sh
export DEVKITARM="$DEVKITPRO/devkitARM"
bash tools/portability/3ds-native-data/build.sh
bash tools/portability/3ds-native-expression/build.sh
bash tools/portability/3ds-native-state/build.sh
bash tools/portability/3ds-native-wake/build.sh
```

Esses scripts compilam e executam uma pequena versão host e produzem um programa
3DSX para o componente correspondente. Não iniciam uma partida completa. Os
experimentos de leitor de dados e fila nativa tiveram execução limitada em
emulador; isso não é validação em um New Nintendo 3DS físico nem de todo o motor.

Para os demais alvos, escolher a cadeia C/C++ é apenas o começo. Ainda é
necessário integrar as dependências e os adaptadores próprios do aplicativo.
Não há um comando universal que transforme o executável Linux em todos esses
portes. As fontes públicas das ferramentas estão nas
[referências](references.md).

## Quando um alvo pode ser considerado suportado

A validação precisa avançar por etapas com resultados distintos:

1. O compilador e as bibliotecas produzem um componente real do projeto.
2. Esse componente executa no alvo e conserva seus dados corretamente.
3. O aplicativo combina simulação, apresentação, som, controles e armazenamento.
4. Um percurso jogável salva e reabre o estado sem perda, dentro dos limites de
   memória e processamento do aparelho.
5. O pacote é instalado e testado no sistema e no hardware anunciados.

Registrar separadamente compilação, execução em emulador e execução física
impede que um teste de SDK vire uma promessa de suporte ao jogo. O avanço de um
alvo pode reutilizar o motor comum, mas não substitui essas verificações nos
outros sistemas.
