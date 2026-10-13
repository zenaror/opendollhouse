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
