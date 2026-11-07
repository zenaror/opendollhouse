# Referências e atribuição

O OpenDollhouse reimplementa em C/C++ a experiência de The Sims 1 para PC.
Simitone e FreeSO fornecem referências de formatos e comportamento; o jogo
original continua sendo a referência de fidelidade. Os fontes C# consultados
não tornam C# ou .NET uma dependência do motor nativo.

Simitone é a referência funcional prioritária porque ativa os caminhos de The
Sims 1. FreeSO também contém regras próprias de The Sims Online: encontrar uma
rotina compartilhada não significa que seu comportamento se aplique ao TS1.
Uma implementação ausente ou incompleta na referência também não define o
limite do OpenDollhouse. Consulte a [arquitetura](architecture.md) e o
[escopo jogável](linux-playable-scope.md) para distinguir objetivo e cobertura.

## Fontes fixadas

Estas revisões identificam fontes públicas consultadas. São identificadores de
código upstream, não resultados de testes do OpenDollhouse. Cada adaptação deve
conservar a revisão efetivamente usada, inclusive quando existe um arquivo de
mesmo nome em outra versão.

| Projeto e autores | Revisão | Uso |
|---|---|---|
| [Simitone, de riperiperi e colaboradores](https://github.com/riperiperi/Simitone/tree/4906db3156616bfaffc10ce0963cf08062975fef) | `4906db3156616bfaffc10ce0963cf08062975fef` | Frontend e integração do TS1. |
| [FreeSO fixado por esse Simitone](https://github.com/riperiperi/FreeSO/tree/f86bbd34b2112296ed786741f640a0535886d180) | `f86bbd34b2112296ed786741f640a0535886d180` | Submódulo correspondente; distinto do FreeSO principal. |
| [Fork Simitone de alexjyong e colaboradores](https://github.com/alexjyong/Simitone/tree/e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab) | `e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` | Referência da integração desktop usada na migração nativa. |
| [FreeSO fixado pelo fork](https://github.com/riperiperi/FreeSO/tree/7e76c1960252ddd033d8937d5edb47d326f5b69c) | `7e76c1960252ddd033d8937d5edb47d326f5b69c` | Contratos TS1 de simulação e serviços adaptados. |
| [FreeSO principal, de riperiperi e colaboradores](https://github.com/riperiperi/FreeSO/tree/4c6b3e8f5835b228723caea3c9f683c62f244f73) | `4c6b3e8f5835b228723caea3c9f683c62f244f73` | Consulta inicial de formatos, VM, áudio e comparação de implementações. |
| [FSOMonoGame e colaboradores](https://github.com/riperiperi/FSOMonoGame/tree/74f7ef558111e62a7d0a2228b6f9714980e000fa) | `74f7ef558111e62a7d0a2228b6f9714980e000fa` | Semântica de operações gráficas e matemáticas consultadas. |

A [discussão de limitações do Simitone](https://github.com/riperiperi/Simitone/issues/8)
ajuda a identificar perguntas de compatibilidade. Ela não substitui a leitura
do código fixado nem a observação do jogo original. As referências não são
baixadas ou executadas automaticamente para compilar o motor nativo.

## Origem dos contratos técnicos

Os nomes abaixo são caminhos dentro dos repositórios públicos da tabela.
Eles permitem localizar a origem de uma regra sem depender de uma instalação
particular do projeto.

| Área | Fontes consultadas no FreeSO |
|---|---|
| Arquivos e objetos | `TSOClient/tso.files/FAR1/FAR1Archive.cs`; `Formats/IFF/IffFile.cs`; chunks `BHAV`, `OBJD`, `TTAB`, `OBJf`, `GLOB`, `SPR`, `SPR2`, `DGRP`, `STR` e `SLOT`. |
| Tabelas compactadas | `TSOClient/tso.files/Formats/IFF/Chunks/TTAB.cs` e `IffFieldEncode.cs`. |
| Execução dos scripts | `TSOClient/tso.simantics/Engine/VMThread.cs`, `VMStackFrame.cs`, `VMMemory.cs`, `Scopes/VMVariableScope.cs`; primitivas `VMExpression.cs` e `VMSubRoutine.cs`. |
| Entidades e posições | `Entities/VMEntity.cs`, `VMGameObject.cs`, `Model/VMStackObjectVariable.cs`, `VMPersonDataVariable.cs`, `VMContext.cs`, `Engine/Scopes/VMSlotScope.cs` e `Engine/VMSlotParser.cs`. |
| Caminhada e interações | `Engine/VMRoutingFrame.cs`; primitivas `VMGotoRoutingSlot.cs`, `VMSnap.cs` e `VMNotifyOutOfIdle.cs`. |
| Animação e personagens | Leitores `BCF.cs`, `CFP.cs`, `BCFReadProxy.cs`, `Animation.cs`; `TS1BCFProvider.cs`, `VMAnimateSim.cs`, `VMAvatar.cs`, `VMAnimationState.cs`, `VMAnimationScope.cs` e `Animator.cs`. |
| Áudio | Implementação HIT, `FWAV.cs` e primitivas de reprodução de som. |
| Seleção dos dados TS1 | Provedores `TSOClient/tso.content/TS1/` e `VMTS1Activator.cs`. |
| Famílias e viagens | `VMGenericTS1Call.cs`, inventário TS1 e `Model/TS1Platform/VMTS1LotState.cs`, em conjunto com o frontend Simitone. |

Os leitores e serviços nativos acrescentam limites de memória, validação de
entradas e erros explícitos. Um formato lido corretamente não comprova todas as
regras do jogo que o utilizam. Diferenças entre regras TS1, regras TSO e edições
do jogo devem continuar identificáveis; não devem virar alternativas silenciosas.
O [guia de formatos](formats.md) explica as estruturas tratadas pelo projeto.

## Dados comerciais e edições do jogo

The Sims e suas expansões são obras da Maxis e da Electronic Arts. Seus
executáveis, textos, imagens, músicas e demais recursos não fazem parte da
distribuição do OpenDollhouse. O jogador fornece seus próprios dados do jogo.

The Sims Complete Collection é a referência principal de conteúdo. A Legacy
Collection pode servir para comparação adicional de recursos e interface.
Edições diferentes não são presumidas idênticas: um endereço de executável,
layout ou comportamento observado em uma delas precisa ser confirmado antes
de ser aplicado à outra. Recursos modificados pelo jogador também devem ser
distinguidos dos recursos originais.

## Licenças e créditos

- **FreeSO:** a revisão consultada declara
  [Mozilla Public License 2.0](https://github.com/riperiperi/FreeSO/blob/7e76c1960252ddd033d8937d5edb47d326f5b69c/LICENSE.md).
  Adaptações devem conservar os avisos, a origem e as obrigações aplicáveis aos
  arquivos derivados. O crédito pertence aos autores e colaboradores upstream.
- **Simitone e o fork alexjyong:** conservar os créditos próprios dos frontends.
  A licença do submódulo FreeSO não estabelece, por si só, a licença de todos os
  arquivos do frontend. Conferir os avisos do componente antes de incorporá-lo.
- **FSOMonoGame:** a revisão consultada declara
  [Microsoft Public License, Ms-PL](https://github.com/riperiperi/FSOMonoGame/blob/74f7ef558111e62a7d0a2228b6f9714980e000fa/LICENSE.txt),
  com crédito à equipe MonoGame. Consultar suas operações de interpolação não
  torna esse runtime gerenciado uma dependência do renderizador nativo.
- **Demais bibliotecas e ferramentas:** suas licenças continuam próprias.
  Reutilização de código, shaders ou recursos deve registrar autor, licença e
  revisão; uma licença de código não concede direitos sobre os dados comerciais.

O projeto não afirma uma metodologia clean-room nem equivalência com o jogo
completo apenas por ter consultado essas fontes.

## Outras referências públicas

- [Don Hopkins: sistema original e ferramentas de personagens](https://www.donhopkins.com/home/movies/).
- [3dbrew: mapa de memória](https://www.3dbrew.org/wiki/Memory_map) e
  [pipeline PICA200](https://www.3dbrew.org/wiki/Nintendo_OpenGL).
- devkitPro: [libctru](https://github.com/devkitPro/libctru),
  [Citro2D](https://github.com/devkitPro/citro2d),
  [Citro3D](https://github.com/devkitPro/citro3d),
  [libnx](https://github.com/switchbrew/libnx) e
  [wut](https://github.com/devkitPro/wut).
- [VitaSDK](https://vitasdk.org/) e [libvita2d, de xerpi e colaboradores](https://github.com/xerpi/libvita2d).
- [Android NDK: suporte a C++](https://developer.android.com/ndk/guides/cpp-support).
- [SDL3: notas para Nintendo 3DS](https://wiki.libsdl.org/SDL3/README-n3ds).
- [OSXCross](https://github.com/tpoechtrager/osxcross) e
  [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw), ferramentas de compilação cruzada.

A pesquisa de alternativas gerenciadas também consultou
[ReSharp3DS](https://github.com/saiitanaa/ReSharp3DS/tree/9ddfe53fc0900a0b561aebeec5ff1e66527edef6),
[VitaMonoLoader](https://github.com/GrapheneCt/VitaMonoLoader) e a documentação de
[plataformas MonoGame](https://docs.monogame.net/articles/getting_started/platforms.html).
Essas consultas não definem o motor atual, que é C/C++, nem constituem prova de
execução do jogo nesses aparelhos. Veja a [portabilidade](portability.md).
