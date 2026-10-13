# Referências e atribuição

FreeSO, Simitone e seus autores/contribuidores oferecem investigação e
implementações importantes de formatos, SimAntics e integração TS1. ODH usa esses
trabalhos como conhecimento e inspiração. Este checkout não inclui nem depende
do runtime ou implementação C# desses projetos.

Revisões de referência identificadas em 2026-10-13:

- [FreeSO — 4c6b3e8f5835b228723caea3c9f683c62f244f73](https://github.com/riperiperi/FreeSO/tree/4c6b3e8f5835b228723caea3c9f683c62f244f73).
- [Simitone — 4906db3156616bfaffc10ce0963cf08062975fef](https://github.com/riperiperi/Simitone/tree/4906db3156616bfaffc10ce0963cf08062975fef).
- O submódulo FreeSO desse Simitone aponta para
  `f86bbd34b2112296ed786741f640a0535886d180`. Essas referências são distintas.

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
sem aplicar defaults ou escalas do runtime. `Engine/VMSlotParser.cs` foi consultado
para identificar a próxima etapa de geração/validação de destinos; sua política
de colisões, scoring e rotação ainda não está implementada no ODH.

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
