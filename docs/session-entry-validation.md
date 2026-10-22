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
- `reconcile_session_household`: compara a família resolvida com avatares de um
  World restaurado por GUID exato. Preserva a ordem da família e separa avatar
  com posição, sem posição e ausente. Recusa avatares sem identidade legível e
  GUIDs duplicados; objetos com GUID coincidente não contam como Sims. A função
  só lê o World.
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
- O fluxo `odh-person --headless-main --activate-family` agora liga a FAMI ao
  World restaurado e emite `family_world_reconciliation` em JSON. O teste de
  integração de House07 confirma, usando os arquivos comerciais externos, que
  os GUIDs 850010198/844195932 correspondem aos ObjectIDs 105/137 e às posições
  `(392,440,1,3)`/`(424,440,1,3)`. Isso acontece antes da interação do teste.
  A conferência só identifica os avatares existentes; não cria nem move Sims.
- Build Debug completa e CTest completos: 171 testes passaram; o teste
  `linux_window_backend` foi pulado pelo sandbox.

Na execução integrada acima, o programa escreveu status de processo 3 porque o
Main ainda estava esperando ao chegar ao teto controlado de 512 ticks. O runner
aceitou esse estado previsto para a sonda, e confirmou que a interação sentar,
o levantar e o vínculo dos dois moradores passaram. Isso não significa que o
Main terminou nem que a casa foi jogada por tempo ilimitado.

O resultado ainda é uma conferência de identidade e restauração estrutural.
`reconcile_session_household` não cria Sims nem os move. A sessão executa o Main
do morador explicitamente escolhido, mas ainda não inicia um Main próprio para
cada membro da família, nem restaura suas filas salvas. O próximo passo é
preparar uma execução independente por morador, começando pelos dois de House07.
Casas com moradores ausentes exigem reproduzir com fidelidade a verificação de
família e o posicionamento junto à caixa postal, conforme o comportamento TS1 de
referência; nenhum spawn ou posição é inventado nesta etapa. Isso ainda não
demonstra uma sessão jogável nem equivalência com Simitone ou com o jogo original.
