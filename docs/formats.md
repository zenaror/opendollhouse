# Perfil inicial dos leitores

O protótipo verifica a estrutura de um corpus Complete Collection local. As
variantes abaixo são um perfil explícito, não cobertura geral de TS1 ou TSO.

| Leitor | Implementado | Ainda não interpretado |
|---|---|---|
| FAR | Assinatura `FAR!byAZ`, versão 1, nomes com comprimento uint32, duas extensões de tamanho iguais; índices e entradas dentro dos limites | Variante de nomes uint16, comprimentos diferentes, precedência de arquivos |
| IFF | Identificador 2.0/2.5, cabeçalho de 64 bytes, chunks com cabeçalho de 76 bytes, tamanhos e campos externos big endian | Semântica dos chunks desconhecidos e validade do mapa de recursos |
| BHAV | Versões 8000/8001/8002, instruções de 12 bytes, opcode little endian, oito operandos preservados, destinos ordinários dentro da lista | Execução, operandos, retornos especiais FD/FE/FF e scheduler |
| SPR2 | Versão 1000, tabela de offsets, cabeçalhos de 16 bytes, dimensões e flags | Pixels RLE, paletas, alpha/Z decodificados e renderização |
| DGRP/OBJD/SPR# | Somente contagem de versões nos cabeçalhos pelo inspector | Decodificação completa e validação dessas versões |

O leitor FAR deste perfil exige dados antes do manifesto e que o manifesto termine
no fim do arquivo. Uma variante diferente produz diagnóstico explícito, sem
supor automaticamente que todo arquivo comercial com outro layout está corrompido.
Nomes não são usados para escrever ou extrair arquivos.

Os FourCC são preservados, inclusive maiúsculas/minúsculas. Cada chunk entrega uma
view limitada à sua extensão, de forma que um leitor não ultrapasse o próximo
recurso. Os campos não interpretados permanecem no stream de origem.

Limites iniciais configuráveis na API: 200.000 entradas por FAR, 100.000 chunks por
IFF e 1.000.000 de frames por SPR2. São limites de proteção deste protótipo, não
limites atribuídos ao original. O índice FAR e os BHAVs são alocados somente após
validação de tamanho; sprites não são expandidos em memória.

Views compartilham um stream seekable e não são thread-safe. O inspector é uma
ferramenta Linux host, usando filesystem/iostream e mapas para relatórios. O
desenho final de I/O e orçamento dos consoles permanece por medir.

## Lacunas relevantes encontradas no corpus

- TTAB versão 9 comprimida aparece em 1.398 ocorrências internas aos FAR,
  incluindo cadeiras comuns; ainda não há leitor desses campos neste protótipo.
- Existem TTAB versões 2/3 e dois payloads vazios. O tratamento original não
  foi estabelecido; não eliminar esses recursos silenciosamente.
- Há 115 nomes normalizados repetidos em FARs. Identidade, precedência e
  substituição de recursos ainda exigem investigação.
- A cadeia de chamadas diretas de `DiningChairs.iff` é um candidato para a VM
  inicial; dependências por nome, função, animação e áudio não estão fechadas.
