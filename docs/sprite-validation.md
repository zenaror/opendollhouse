# PALT, SPR2 e grupos de desenho — 2026-10-13

ODH decodifica sprites paletizados com alpha e Z, e seleciona os grupos de
desenho do objeto por direção/zoom/rotação. Esta etapa fornece pixels brutos;
a composição com o avatar e a projeção do World ainda não estão implementadas.
A cadeira selecionada é a mesma definição usada no probe de sentar/levantar.

## Perfil dos leitores

`read_objd_graphics` lê o prefixo de10 bytes de OBJD136/138..142:
BaseGraphicID no offset6 e NumGraphics no8. Não valida o OBJD inteiro.
`read_dgrp` aceita versões20000..20004, preserva flags, offsets assinados,
legacy type e floatbits ObjectOffsetXYZ; limita4096 imagens e4096 sprites
cumulativos. `dgrp_image` rotaciona a direção byte por2 bits por quarto de
volta, exige correspondência exata de direção/zoom e recusa duplicatas.
Floatbits são dados brutos, sem presumir finitude antes da interpretação.

PALT lê version/count/reserved8/RGB triples,1..256 cores; version e extensão
restante são preservados. Não rejeita version1 nativa ou0 do writer de
referência. SPR2 aceita somente1000, tabela de offsets, headers16 e flags
1/3/5/7 com pixels presentes. Offsets podem estar fora de ordem ou repetir;
headers distintos não podem sobrepor. O frame escolhido é limitado ao próximo
offset distinto ou fim do chunk; toda tabela e todos headers são conferidos.
Limites padrão4096 frames,2048×2048,1048576pixels e1048576 comandos por decode.
Pixels RGBA usam4 bytes, índice1 e Z opcional1: até6MiB de payload por frame,
além de headers/tabela/overhead; isso não é medição de RSS ou orçamento console.

SPR2 v1000 usa sempre o default palette ID convertido para uint16; o ID cru do
frame continua no relatório. Requer PALT explícita encontrada no mesmo IFF;
fallback preto da referência não está implementado. Marcadores externos0
(row, comprimento inclui2 bytes),4(skip rows) e5(END) são suportados. Packets
1(Z/index),2(Z/index/alpha),3(skip) e6(index) são limitados à linha e dimensão;
padding de comprimento ímpar de2/6 é consumido. Alpha5bit usa floor(a*255/31).
Packets1/6 são opacos inclusive quando índice coincide com TransparentIndex.
Skip explícito usa RGB da cor transparente, alpha0 e Z255. O tail não codificado
de uma linha conserva RGBA/index zero e só preenche Z255; rows intocadas após
END ficam zero também em Z. END antecipado é permitido; EOF sem END somente
após completar a altura. Flags/commands desconhecidos, índices inválidos,
alpha>31 e budgets excedidos são erros explícitos. Sem FloorCopy/ZAsAlpha.

## CLI e reprodutibilidade

```sh
./build/odh-sprite '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 16808 --direction 1 --zoom 3 --output reports/chair-sprite-1-3 > reports/chair-sprite-1-3.json
python3 tools/compare_sprite.py '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff reports/chair-sprite-1-3.json reports/chair-sprite-1-3
```

`--graphic`, `--direction`, `--zoom`, `--rotation`, `--layer` fazem seleção
explícita. Default:graphic0,direction1,zoom3,rotation0,layer0. Nome FAR/IFF e
IDs devem ser únicos; nenhum fallback silencioso. Sem output só há relatório;
com prefix são exportados `.rgba`, `.idx`, `.z` se Z presente. Pixels seguem
raw frame unflipped, `flip_required` indica a flag DGRP; offsets/crop/origem não
são aplicados. Exit0 somente após fechar os outputs e emitir JSON; exit2 em
falha, stderr diagnóstico, possíveis arquivos parciais indicados. O loader
mantém Views vivas durante decode e não altera os inputs comerciais.

O oracle Python lê FAR/IFF/PALT/DGRP20004/SPR2 diretamente, reconstrói os pixels
e compara cada byte RGBA/index/Z e metadata selecionada, sem chamar o decoder
C++. É ferramenta host com biblioteca padrão, não backend do núcleo. Este gate
confere decodificação no perfil descrito; não é screenshot ou execução original.

## Evidência nativa e testes

DiningChairs.iff354456bytes, SHA256
cffff7b4e8b5d1b31351c32a2f8656f50dbf72a82217bf38b4828853fc6f90c7.
OBJD16808 (GUID26BFBB29), version138, BaseGraphicID1128, NumGraphics7;
o grupo128 pertence a outra cadeira. Seleção graphic0: DGRP1128 version20004,
12 imagens, um sprite por imagem; SPR2 id1128 version1000/seis frames,
flags7, raw paletteA3A3/default2, TransparentIndex0. PALT2 version1/256 cores.
Todos os seis frames foram comparados integralmente com o oracle e consumidos
sem extensão restante. SHA256 de chunks:

- DGRP1128:1a6a94f73ad55c5d7abfb7da1ea9d74160ce0a2e7391bbcd44e934763e608ce3
- SPR2 1128:71c2c7ed464709143722bfb5a8e9629bb7da8afa9958e4e670055b0a315852a1
- PALT2:40bc9314dda022542fd4d138e497128259df645e519a08e52257f462a81f203b

| Direção/zoom | Frame | Dimensões | RGBA SHA256 |
|---|---|---|---|
|1/3|0|70×98|271964afcd2cdb8987743d8bc3a45a3b286c0691f8a9ed37c5308d6ab5f968b7|
|4/3|1|70×116|5c3f31e53ece6ab6ec9c062568ee4ec2cb278f2b56de3334d30c4be6c6fd70f9|
|1/2|2|35×50|c34bd90f31d7ecad2b26d27c96228403c2bb74a7e4f7d15d6927381b8c401dd1|
|4/2|3|36×59|8e0563c7f4b9a583dd0227e17df32b5dcba20b70453a4046709cd04bc65421dd|
|1/1|4|18×25|b9b95954a914b09d5c9b622e9f0b46ba2d1d262f6e29782c4baef4ba7f3b6d47|
|4/1|5|19×30|1f39bc858f798f173c97d3abe518daf4832865a27ed6ee50df175da5331837a2|

Frame0 indexSHA2b8dbd9db32646a67e2e3abc1eb078e6b0b4216708b9c8832efa4c0f6f48a027,
ZSHAd8f3828514e4fd6f066fb643f69d436792949f6a6d7aba1831badb628babb968.
Direção16/zoom3 e direção1/rotação2/zoom3 selecionam frame1 com flip=true,
offset(-37,17); ambos passaram no oracle. PNG com as seis vistas inspecionado;
exports brutos e preview ficam em reports/, fora do Git. Controles negativos
com um byte RGBA ou Z adulterado foram recusados pelo oracle.

CTest32/32 passou em Debug e ASan/UBSan. Novos gates sintéticos:319 checks
SPR2/PALT,258 DGRP,476 formats incluindo OBJD graphics; CLI cobre seleções,
exports, erros e ausência de sucesso parcial. Execução sanitizada do frame0
produziu RGBA/index/Z byte-idênticos ao Debug e JSON igual normalizando só
output paths, exit0/stderr vazio. LSan desabilitado (`detect_leaks=0`) por
restrição ptrace do ambiente; nenhuma validação de leaks é afirmada.

## Referências e próxima etapa

FreeSO fixo4c6b3e8f5835b228723caea3c9f683c62f244f73, MPL2.0 declarada:
- [PALT.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/PALT.cs)
- [SPR2.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/SPR2.cs)
- [DGRP.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/DGRP.cs)
- [OBJD.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.files/Formats/IFF/Chunks/OBJD.cs)
- [DGRPRenderer.cs](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/Utils/DGRPRenderer.cs)
- [WorldState.cs/WorldSpace](https://github.com/riperiperi/FreeSO/blob/4c6b3e8f5835b228723caea3c9f683c62f244f73/TSOClient/tso.world/WorldState.cs)

Código ODH próprio, sem engine de terceiros. A referência posiciona o desenho
em (CadgeWidth/2 + DGRP.x, CadgeBaseLine - frameHeight + DGRP.y), mais projeção
de ObjectOffset e posição do objeto; não soma novamente SPR2.Position. Isso
orienta a próxima implementação e evita duplicar o crop. Cadges near/medium/far
136/68/34 e baselines348/174/87; tiles128×64,64×32,32×16. Ainda é evidência de
referência, sem câmera/depth calibrados contra o original. Falta conferir a
conversão Z/alpha do shader/compositor antes de integrar objeto e avatar.

Próximo: projetor reutilizável, compositor bounded, instâncias/direção World e
playback ligados à cena, piso e janela/input Linux. Scheduler/queue, áudio,
necessidades e persistência delimitadas continuam pendentes. Meta ativa; não
houve execução do original ou validação em consoles nesta etapa.
