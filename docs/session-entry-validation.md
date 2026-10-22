# Resolução da família antes de abrir um lote

O `odh-session-entry` recebe uma pasta `UserData`, o modo da vizinhança e o
número da casa. Ele confere a casa em `LotLocations.iff`, localiza a família em
`Neighborhood.iff`, liga cada morador ao registro NBRS e procura o mesmo GUID
em um arquivo de personagem de `Characters/`. Também confere se o lote contém
um SIMI1 que o leitor atual reconhece.

Os arquivos de `Characters` são percorridos em ordem estável e dentro de limites
de quantidade e tamanho. Só entram no catálogo OBJD138 do tipo Person. Arquivos
IFF malformados, membros repetidos, vínculos incompletos e resultados ausentes
ou ambíguos causam erro explícito. Os arquivos comerciais são apenas lidos;
nenhum asset é copiado para o projeto.

## Verificação

- `character_catalog_scan`: leitor de IFFs sintéticos, ordem previsível,
  filtragem de OBJD e limites de arquivo/quantidade.
- `session_entry_resolution`: ligação sintética casa → família → vizinho →
  arquivo de personagem, incluindo entradas ausentes e ambíguas.
- `session_entry_cli`: caminho completo sintético pela ferramenta de linha de
  comando.
- Instalação `../referencias/The Sims/UserData`: casas 5 e 7 resolvidas com 3 e
  2 membros, respectivamente.
- Instalação `../referencias/The Sims Legacy Collection/UserData`: casas 5 e
  7 resolvidas com 3 e 2 membros, respectivamente.
- No House07 da instalação `../referencias/The Sims`, `odh-session-entry`
  resolveu os dois moradores (Neighbor IDs 48 e 47; arquivos
  `User00000.iff` e `User00001.iff`). `odh-saved-world`, usando `Objects.far`
  e esses arquivos de personagem como fontes de definições, restaurou 182
  entidades e encontrou dois avatares no lote: ObjectIDs 105 e 137, com GUIDs
  850010198 e 844195932. Esses GUIDs correspondem aos dois membros resolvidos
  da família. Ambos já têm posição salva no lote. Esta é uma conferência real
  de dados, não uma execução simultânea dos dois Sims.
- Build Debug completa e CTest completos: 171 testes passaram; o teste
  `linux_window_backend` foi pulado pelo sandbox.

O resultado é uma conferência de identidade dos arquivos. `odh-session-entry`
não cria Sims no World, não executa Main ou filas, não desenha nem abre o lote
e não salva progresso. O leitor de lote restaura registros `ObjM` existentes,
mas o host ainda escolhe e agenda um avatar por vez; a ligação entre a família
resolvida e os avatares restaurados não está automatizada. Para House07, os
dois moradores já existem no save e devem ser reutilizados, sem duplicação.
O próximo passo é reconciliar os GUIDs da família com os avatares do World,
preservar seus ObjectIDs/posições e preparar o Main de cada membro. Casas com
membros ausentes do `ObjM` exigirão a lógica correta de `VerifyFamily` e de
posicionamento antes de poderem ser declaradas compatíveis. Nada disso, por si
só, demonstra uma sessão jogável ou equivalência com Simitone ou com o jogo
original.
