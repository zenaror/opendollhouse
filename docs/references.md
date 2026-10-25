# Referências e atribuição

FreeSO, Simitone e seus autores/contribuidores oferecem investigação e
implementações importantes de formatos, SimAntics e integração TS1. ODH usa esses
trabalhos como conhecimento e inspiração. Até o checkpoint desta decisão, o
código versionado do ODH não inclui nem depende do runtime C# desses projetos.
Em 2026-10-25, o usuário autorizou mudar o caminho principal para uma base
derivada do Simitone/FreeSO, com o original como destino de fidelidade.
A entrada Linux do fork alexjyong será avaliada primeiro em checkout de trabalho
separado. Adoção, revisões e licenças ainda precisam ser verificadas na prática;
os clones abaixo continuam sendo referências preservadas, não dependências
automaticamente incorporadas. Ver [plano vigente](architecture.md).

## Prioridade para The Sims Complete Collection

Por orientação humana de 2026-10-14, consultar primeiro o frontend Simitone
e seu submódulo FreeSO na revisão fixada. Identificar como o Simitone ativa
os caminhos TS1 antes de interpretar o código compartilhado. O FreeSO principal
serve como referência secundária para comparação, formatos comuns e correções;
comportamentos exclusivos de The Sims Online não definem o comportamento alvo.
A referência de fidelidade continua sendo o jogo original.

Esta prioridade vale para novas investigações e revisões de contratos quando
necessárias. Os documentos anteriores preservam a origem realmente consultada;
não se troca sua atribuição apenas porque um arquivo tem equivalente no
submódulo. A [verificação inicial desta decisão](reference-priority-2026-10-14.md)
registra os caminhos TS1 e a comparação do interpretador HIT.

Revisões de referência identificadas em 2026-10-13:

- [FreeSO — 4c6b3e8f5835b228723caea3c9f683c62f244f73](https://github.com/riperiperi/FreeSO/tree/4c6b3e8f5835b228723caea3c9f683c62f244f73).
- [Simitone — 4906db3156616bfaffc10ce0963cf08062975fef](https://github.com/riperiperi/Simitone/tree/4906db3156616bfaffc10ce0963cf08062975fef).
- O submódulo FreeSO desse Simitone aponta para
  `f86bbd34b2112296ed786741f640a0535886d180`. Essas referências são distintas.

## Checkouts locais de consulta

### Checkout de avaliação separado

Após a retomada da meta em 2026-10-25, a avaliação usa
`refs/work/Simitone-linux`, com frontend alexjyong
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab` e FreeSO
`7e76c1960252ddd033d8937d5edb47d326f5b69c`. Essa cópia pode receber patches
de desenvolvimento; não altera as revisões ou arquivos dos clones de consulta
abaixo. Patches reproduzíveis ficam em `tools/simitone/`. A avaliação e os
limites de atribuição por componente estão em
[simitone-linux-evaluation.md](simitone-linux-evaluation.md).

### Referência comercial adicional: Legacy Collection

Em 2026-10-15, o usuário adicionou `../referencias/The Sims Legacy Collection`
e identificou sua origem como a edição Steam lançada em 2025. Essa origem é
relato do usuário; ainda não foi verificada por manifesto ou versão de produto.
A primeira inspeção ocorreu durante a cópia. Após o usuário informar sua
conclusão, um novo inventário encontrou metadados estáveis antes/depois da
comparação. Isso registra a árvore fornecida, sem verificar um manifesto Steam.

Uma comparação SHA256 limitada encontrou identidade byte a byte nos três
arquivos `Downloads/BirthdayCake/` presentes nas duas instalações:

| Arquivo | SHA256 em ambas |
|---|---|
| `bcakesnd.HIT` | `1acbbcdbd62207b82d91de4b9c7606c18d83e3121b27d11a50e6970df10257a4` |
| `bcakesnd.hot` | `eacae1faac6b3c1edffbe59e43b9dc5d3bdb4da2688dd9a6f9818f147ccd1d00` |
| `bcakesnd.hsm` | `082b4e97bef77612fbc264c236710326952262bdb1212a10d6152aab91e43a61` |

Isso permite reutilizar esses recursos em comparações futuras, mas não comprova
equivalência das demais tabelas, do motor ou das instalações completas.

O inventário posterior está em
[legacy-reference-validation.md](legacy-reference-validation.md): os 81 arquivos
HIT/HOT/HSM comuns são idênticos; quatro dos cinco FAR amostrados diferem.

Usar essa edição como referência comparativa de formatos, recursos e rotinas.
A instalação `../referencias/The Sims` continua sendo a base das evidências
anteriores. Endereços, hashes e contratos obtidos de uma edição não se aplicam
automaticamente à outra: registrar o arquivo e sua identidade em cada análise.
Preservar ambas as instalações externas, sem executar ou modificar seus
binários durante consultas estáticas. Nenhum recurso comercial entra no Git.

### Fontes de terceiros

Por solicitação do usuário, as fontes estão materializadas em `refs/upstream/`,
ignorada pelo Git do ODH. Consultar primeiro esses arquivos com `rg` e ler
somente os trechos necessários. Usar conector/web quando o material não estiver
local ou quando for necessário verificar uma revisão externa nova. Os hashes
fixados evitam misturar consultas de versões diferentes.

| Checkout local | Revisão |
|---|---|
| `refs/upstream/FreeSO` | `4c6b3e8f5835b228723caea3c9f683c62f244f73` |
| `refs/upstream/Simitone` | `4906db3156616bfaffc10ce0963cf08062975fef` |
| `refs/upstream/FSOMonoGame` | `74f7ef558111e62a7d0a2228b6f9714980e000fa` |
| `refs/upstream/Simitone/FreeSO` | `f86bbd34b2112296ed786741f640a0535886d180` (submódulo) |

Os clones usam histórico raso e as duas raízes estão em HEAD destacado. O
submódulo FreeSO específico do Simitone é materializado na revisão própria;
outros submódulos não são inicializados. Nenhum build, setup ou runtime de
terceiros é executado para consulta. Os diretórios não são dependências de
compilação do ODH e não são publicados junto com seu código. Licenças e créditos
originais permanecem nos respectivos checkouts.

Uma pesquisa focada, sem percorrer arquivos de runtime/binários:

```sh
rg --no-ignore-vcs -n 'GetTuningVariable' refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics --glob '*.cs'
```

A economia esperada vem da reutilização dos arquivos e da leitura de trechos
focados. O clone não mede nem garante uma quantidade específica de tokens
poupados. Pesquisas que despejam arquivos inteiros continuam gerando contexto
extenso, por isso devem ser evitadas.

## Proveniência anterior

Consulta adicional de 2026-10-17: a issue8 e o fork comunitário alexjyong foram
avaliados em [auditoria de referências](reference-audit-2026-10-17.md).
O fork está isolado em `refs/upstream/Simitone-alexjyong`, revisão
`e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab`; seu gitlink FreeSO
`7e76c1960252ddd033d8937d5edb47d326f5b69c` não foi materializado.
Lacunas de Simitone/forks devem ser investigadas nos recursos e no executável
original identificado, sem transformar ausências de terceiros em limites ODH.

A pesquisa inicial também consultou links `master`; esse conjunto não foi
arquivado como snapshot integral. As revisões acima permitem consultas futuras
fixas, mas não provam que cada excerpt anterior veio dessas mesmas revisões.

Leituras relevantes no FreeSO:

- `TSOClient/tso.files/FAR1/FAR1Archive.cs`.
- `TSOClient/tso.files/Formats/IFF/IffFile.cs` e chunks `BHAV`, `SPR2`, `DGRP`,
  `SPR`, `OBJD`, `TTAB`, `OBJf`, `GLOB`.
- `TSOClient/tso.simantics/Engine/VMThread.cs`, `VMStackFrame.cs` e `VMContext.cs`.
- Provedores `TSOClient/tso.content/TS1/` e `VMTS1Activator.cs`.

O leitor TTAB usa como referência de formato `TTAB.cs` e `IffFieldEncode.cs`
de `TSOClient/tso.files/Formats/IFF/Chunks/` no FreeSO, revisão fixa
`4c6b3e8f5835b228723caea3c9f683c62f244f73`. A implementação C++ inclui
limites próprios, diagnóstico de truncamento e estados para layouts não lidos.
Ler campos conforme essa referência não comprova o tratamento do original.

O núcleo inicial da VM consultou, na mesma revisão fixa FreeSO, `VMThread.cs`,
`VMStackFrame.cs`, `VMMemory.cs`, `Scopes/VMVariableScope.cs` em
`TSOClient/tso.simantics/Engine/`, e `VMExpression.cs`/`VMSubRoutine.cs` em
`TSOClient/tso.simantics/Primitives/`. O layout 8002 usa também `BHAV.cs`.
Retornos, FD e convenção de argumentos são um perfil de referência que ainda
requer comparação com o original. Diferenças e falhas explícitas estão em
`vm-validation.md`; não se reproduzem silenciosamente fallback de primitivas
ausentes ou hacks de mundo da referência.

O primeiro bridge de entidades consultou `Entities/VMEntity.cs`,
`Model/VMStackObjectVariable.cs`, `Model/VMPersonDataVariable.cs`, `VMContext.cs`,
`Engine/Scopes/VMSlotScope.cs`
e `Primitives/VMGotoRoutingSlot.cs`, sob `TSOClient/tso.simantics/`, na mesma
revisão fixa. O campo de slot sem recipiente é -1 nessa referência. Layouts
de entidades no probe são construídos explicitamente; inicialização pelo
original ainda não foi estabelecida.

O leitor SLOT consulta `TSOClient/tso.files/Formats/IFF/Chunks/SLOT.cs` na
revisão FreeSO acima. Preserva floats como bits e extensões ausentes como opcionais,
sem aplicar defaults ou escalas do runtime. `Engine/VMSlotParser.cs` do
submódulo FreeSO fixado do Simitone foi consultado para enumeração, scoring,
direção e `FaceAnywhere`. O ODH implementa um perfil de faixa delimitado para
House07 Global SLOT100 tipo 3 índice 8; o host fornece a validação do destino e
outros perfis continuam fora do escopo. Evidência e limites em
[route-slot-range-lattice-validation.md](route-slot-range-lattice-validation.md).

Para a ligação inicial de roteamento foram também consultados `VMRoutingFrame.cs`
(`Engine/`) e `VMSnap.cs` (`Primitives/`) no mesmo FreeSO fixo. A referência
escreve person data RouteEntryFlags na chegada; snap Mode3 é Literal. O backend
ODH ainda tem regras provisórias descritas em `routing-validation.md`.

O perfil de contenção consultou `Entities/VMGameObject.cs` (`PlaceInSlot`) no
mesmo FreeSO fixo: posição física acompanha a do recipiente, com dados SLOT
de offset/altura disponíveis para representação. O prefixo OBJD foi conferido
em `TSOClient/tso.files/Formats/IFF/Chunks/OBJD.cs`. A aplicação visual da
altura ainda não está implementada.

Os leitores STR#/BCF/CFP consultaram `STR.cs`, `BCF.cs`, `Animation.cs`, `CFP.cs`,
`BCFReadProxy.cs` e `TS1BCFProvider.cs` na mesma revisão fixa. A pesquisa da
primitiva44 consultou também `VMAnimateSim.cs`, `VMAvatar.cs`, `VMAnimationState.cs`
e `Animator.cs`. As identidades dos recursos e o contrato ainda não implementado
estão em [animation-validation.md](animation-validation.md), com links fixos.

O controlador silencioso de animação consulta também `VMAnimationScope.cs`, na
mesma revisão: StackObject é65536, distinto de Misc3. Limites, scheduler parcial,
eventos e cancelamento ODH estão em [playback-validation.md](playback-validation.md).
Reset de postura e loops, incluindo STR150/151 global e HeadSeekState42, estão
em [reset-validation.md](reset-validation.md), com limites de carry/gestos.
SnapToDirection e a notificação49 foram conferidos em VMSnap/VMSlotParser/SLOT
e `VMNotifyOutOfIdle.cs`; ver [facing-validation.md](facing-validation.md).
Função Stand/saídas no chão e o host de duas fases estão em
[stand-validation.md](stand-validation.md); OBJf/dispatcher/scheduler completo
ainda não são inferidos a partir dos testes legados.

O registro `VMContext.cs` foi conferido para distinguir opcodes decimais de
hexadecimais. TestObjectType32 e PlaySound23 consultam seus handlers e `FWAV.cs`;
evidência/correção em [object-sound-validation.md](object-sound-validation.md).

Os leitores do protótipo ODH foram escritos para o perfil do corpus observado,
com verificações próprias de limites e fixtures sintéticas. A referência de
comportamento final continua sendo o executável original identificado.
Não se afirma metodologia clean-room, equivalência de implementação ou
compatibilidade funcional apenas a partir dessas leituras.

Outras fontes:

- [Don Hopkins: material do sistema original e ferramentas de personagens](https://www.donhopkins.com/home/movies/).
- [3dbrew: mapa de memória](https://www.3dbrew.org/wiki/Memory_map) e
  [pipeline PICA200](https://3dbrew.org/wiki/Nintendo_OpenGL).
- [libctru](https://github.com/devkitPro/libctru),
  [Citro2D](https://github.com/devkitPro/citro2d),
  [Citro3D](https://github.com/devkitPro/citro3d).
- [libvita2d](https://github.com/xerpi/libvita2d) e [VitaSDK](https://vitasdk.org/).
- [Android NDK: C++](https://developer.android.com/ndk/guides/cpp-support).
- [SDL3 no 3DS](https://wiki.libsdl.org/SDL3/README-n3ds).

FreeSO declara MPL 2.0. Qualquer reutilização futura de código, shaders ou assets
de terceiros deve registrar licença, origem e revisão. A licença do frontend
Simitone não deve ser presumida a partir da licença de seu submódulo.

Em 2026-10-14, foi clonado o [FSOMonoGame fixado pelo FreeSO](https://github.com/riperiperi/FSOMonoGame/tree/74f7ef558111e62a7d0a2228b6f9714980e000fa)
para conferir `Color.Lerp`/`MathHelper.Lerp`: operações float e conversão int por
truncamento antes de `ToVector4`. A licença declarada nessa revisão é Ms-PL.
Checkout shallow e destacado, apenas consulta; nenhum build/setup executado.
A implementação CPU própria não liga nem incorpora o runtime C# dessa biblioteca.
