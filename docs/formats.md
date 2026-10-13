# Perfil inicial dos leitores

O protótipo verifica a estrutura de um corpus Complete Collection local. As
variantes abaixo são um perfil explícito, não cobertura geral de TS1 ou TSO.

| Leitor | Implementado | Ainda não interpretado |
|---|---|---|
| FAR | Assinatura `FAR!byAZ`, versão 1, nomes com comprimento uint32, duas extensões de tamanho iguais; índices e entradas dentro dos limites | Variante de nomes uint16, comprimentos diferentes, precedência de arquivos |
| IFF | Identificador 2.0/2.5, cabeçalho de 64 bytes, chunks com cabeçalho de 76 bytes, tamanhos e campos externos big endian | Semântica dos chunks desconhecidos e validade do mapa de recursos |
| BHAV | Versões 8000/8001/8002, instruções de 12 bytes, opcode little endian, oito operandos e oito bytes de metadata preservados; metadata 8002 de argumentos/locais/type/tree version | Metadata legada 8000/8001; cobertura funcional completa e scheduler; execução parcial 8002 descrita em vm-validation.md |
| SPR2/PALT | SPR2 1000, frame RLE, RGBA/index/Z; PALT RGB e version/raw; limites/seleção explícitos | SPR2 outras versões, palette fallback, FloorCopy/ZAsAlpha e composição |
| TTAB | Campos das versões 4..8 e versão 9 com coding 0/1; action/test, flags, índices e motivos; estados distintos para vazio, ausente e legado 2/3 | Semântica das flags/motivos, execução das interações, versões 2/3 e significado dos bits restantes |
| SLOT | Assinatura TOLS, layouts 4..10, campos brutos, extensões opcionais por versão, seleção por tipo/índice e bytes restantes | Normalização de unidades/defaults, destinos geométricos, flags e roteamento |
| STR# | Formatos 0..-3, bytes de texto/comentário, idioma e seleção explícita; estados ausente/header-only/decoded | Encoding Unicode, fallback automático de idioma e formato -4 |
| BCF | Leitores separados para animações, skeletons ou appearances, exigindo demais seções vazias; campos brutos, propriedades, ranges e extensões | BCF misto e formatos textuais CMX/SKN/ANIM |
| BMF | Nomes, bones, faces, bindings, UVs, pesos, transforms floatbits e vertex_count2 bruto | SKN textual, materiais/roupas completos e semântica geral de vertex_count2 |
| BMP | DIB40/BI_RGB8 ou24bpp, paleta/BGR, padding/orientação e RGBA opaco | DIBs/compressões/bpp restantes, alpha e color key |
| CFP | Sete planos float32, literal FF, repetição FE e deltas 00..FC; limite total e rejeição de FD/não finitos | Transformação de coordenadas, interpolação, aplicação ao skeleton e playback |
| FWAV | Nome bruto terminado em NUL, limite de texto, bytes restantes | HIT/eventos/amostras, mixing e reprodução de áudio |
| BCON | Contagem byte, flags brutas, constantes LE16 e extensão restante; máximo estrutural de255 palavras | TRCN, semiglobal/global/OTF e precedência geral de tuning |
| DGRP | Versões20000..20004, seleção direction/zoom/rotation, sprite refs/flags/offsets/raw floatbits | Composição geométrica, interpretação geral de flags e Z |
| OBJD/SPR# | OBJD prefixos de identidade/funções/graphics; SPR# só versões no inspector | OBJD inteiro/OBJf e SPR# pixels |

`read_objd_binding` lê somente o prefixo estável de 42 bytes nas versões
136/138/139/140/141/142, retornando GUID, AnimationTableID, SlotID, MasterID e
SubIndex assinado. Não valida
o restante do OBJD nem implementa uma fábrica completa de objetos.

`read_objd_functions` estende a leitura estável até68 bytes para Main/Sit/Stand
e UsesFnTable. Preserva a seleção legada versus OBJf; não interpreta a tabela
OBJf nem implementa despacho funcional genérico.

O leitor FAR deste perfil exige dados antes do manifesto e que o manifesto termine
no fim do arquivo. Uma variante diferente produz diagnóstico explícito, sem
supor automaticamente que todo arquivo comercial com outro layout está corrompido.
Nomes não são usados para escrever ou extrair arquivos.

Os FourCC são preservados, inclusive maiúsculas/minúsculas. Cada chunk entrega uma
view limitada à sua extensão, de forma que um leitor não ultrapasse o próximo
recurso. Os campos não interpretados permanecem no stream de origem.

Limites iniciais configuráveis na API: 200.000 entradas por FAR, 100.000 chunks por
IFF, 1.000.000 de frames por SPR2, 4.096 interações por TTAB e 256 motivos por
interação e 4.096 registros por SLOT. São limites de proteção deste protótipo, não
limites atribuídos ao original. O índice FAR e os BHAVs são alocados somente após
validação de tamanho; o inspector não expande sprites. O decoder separado
SPR2 expande somente o frame selecionado com budgets próprios; composição CPU
com depth/alpha em [composition-validation.md](composition-validation.md). Ver
[sprite-validation.md](sprite-validation.md).

Views compartilham um stream seekable e não são thread-safe. O inspector é uma
ferramenta Linux host, usando filesystem/iostream e mapas para relatórios. O
desenho final de I/O e orçamento dos consoles permanece por medir.

Animações usam limites próprios: 4.096 clipes, 4.096 motions cumulativas,
65.536 unidades de propriedades/grupos/eventos cumulativas e 4 MiB de texto por
leitura. STR# limita 65.536 entradas. CFP limita o total `3*T + 4*R` a 1.048.576
amostras float32; não implica que esses sejam limites do original.

## Lacunas relevantes encontradas no corpus

- TTAB versão 9 comprimida aparece em 1.398 ocorrências internas aos FAR,
  incluindo cadeiras comuns; seus campos agora são lidos. O codec usa bits MSB-first,
  campo zero ou seletor de largura com payload assinado. Truncamento causa erro;
  não inventar valores zero quando acaba a entrada. O float de atenuação é
  preservado como bits uint32, sem interpretação numérica nesta etapa.
- Existem TTAB versões 2/3 e dois payloads vazios. O tratamento original não
  foi estabelecido; não eliminar esses recursos silenciosamente.
- Há 115 nomes normalizados repetidos em FARs. Identidade, precedência e
  substituição de recursos ainda exigem investigação.
- A cadeia de chamadas diretas de `DiningChairs.iff` é um candidato para a VM
  inicial; dependências por nome, função, animação e áudio não estão fechadas.
