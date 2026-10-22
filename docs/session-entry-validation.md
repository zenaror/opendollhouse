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
- Build Debug completa e CTest completos: 171 testes passaram; o teste
  `linux_window_backend` foi pulado pelo sandbox.

O resultado é uma conferência de identidade dos arquivos. A ferramenta não cria
Sims no World, não executa Main ou filas, não desenha nem abre o lote e não
salva progresso. Portanto, ainda não demonstra uma sessão jogável nem a
equivalência com Simitone ou com o jogo original. O próximo passo é entregar
essa família ao carregamento normal do World e verificar a criação simultânea
de seus membros.
