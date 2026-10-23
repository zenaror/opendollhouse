# Resolução da família antes de abrir um lote

O idioma recebido pelo carregador é validado, mas não escolhe o grupo usado
para localizar casas. `LotLocations` também carrega coordenadas de mapa; os
grupos não ingleses inspecionados têm uma entrada opaca no lugar da primeira
coordenada. O OpenDollhouse usa o grupo inglês para essas posições técnicas e
preserva o idioma escolhido para os textos do runtime.

`odh::load_session_entry` recebe uma pasta `UserData`, o modo da vizinhança, o
número da casa e o idioma. O executável `odh-session-entry` usa essa função.
Ela confere a casa em `LotLocations.iff`, localiza a família em
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
  comando, agora passando pelo mesmo carregador reutilizável destinado ao
  aplicativo Linux integrado.
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

Na execução integrada de referência para o morador 105, o programa escreveu
status de processo 3 porque o Main ainda estava esperando ao chegar ao teto
controlado de 512 ticks. O runner aceitou esse estado previsto para a sonda e
confirmou sentar, levantar e o vínculo dos moradores. A primeira repetição com
o morador 137 agendou o levantar no tick 241, antes de a ação de sentar terminar
no tick 255. Por isso, aquela prova não chegou a tentar o levantar. O runner
agora agenda essa etapa no tick 270; com esse intervalo, os dois moradores
passam Sit/Stand nas instalações The Sims e Legacy Collection. O status 3
continua significando apenas que o Main ainda esperava ao alcançar o teto de
512 ticks; não significa que a casa foi jogada por tempo ilimitado.

O teste complementar `tests/house07_resident_main_smoke.py` executa cada um dos
dois moradores separadamente, usando seu próprio arquivo `User00000.iff` ou
`User00001.iff`, GUID, ObjectID e Main. Ambos precisam receber `LotZoning.iff`:
sem esse dado, o Main do Sim 137 para com o diagnóstico “lot zoning resource
unavailable”. Com as regras do lote presentes, cada execução chega ao limite
controlado de 32 ticks sem falha, usando o próprio ObjectID como dono do trace.
Isso verifica o carregamento e a execução inicial de cada recurso de morador;
não os executa ao mesmo tempo.

O resultado ainda é uma conferência de identidade e restauração estrutural.
`reconcile_session_household` não cria Sims nem os move. A sessão executa o Main
do morador explicitamente escolhido, mas ainda não inicia um Main próprio para
cada membro da família, nem restaura suas filas salvas. O próximo passo é
preparar uma execução independente por morador, começando pelos dois de House07.
Casas com moradores ausentes exigem reproduzir com fidelidade a verificação de
família e o posicionamento junto à caixa postal, conforme o comportamento TS1 de
referência; nenhum spawn ou posição é inventado nesta etapa. Isso ainda não
demonstra uma sessão jogável nem equivalência com Simitone ou com o jogo original.
