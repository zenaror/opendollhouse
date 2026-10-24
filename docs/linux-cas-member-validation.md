# Conversão de escolhas do CAS para personagem Linux

O adaptador `make_simitone_cas_member` prepara a entrada que a criação privada
de famílias já aceita. Ele recebe nome e biografia UTF-8, idioma, gênero,
idade, tom de pele, classe de roupa, identificadores de corpo/cabeça/texturas,
conjunto de mãos e cinco controles de personalidade. As saídas são seis
substituições de mãos, os demais índices usados de STR# 200 e 88 palavras de
PersonData para o registro NBRS. A função não lê nem grava arquivos.

O mapeamento foi conferido nestes trechos da revisão Simitone fixada em
`docs/references.md` (`4906db3156616bfaffc10ce0963cf08062975fef`) e do submódulo
FreeSO próprio do Simitone (`f86bbd34b2112296ed786741f640a0535886d180`):

- `refs/upstream/Simitone/Client/Simitone/Simitone.Client/UI/Screens/TS1CASScreen.cs`:
  `CASToNeighGen` e `BuildMember` convertem as escolhas da tela em aparência,
  texturas, valores de personalidade e tipo do Sim.
- `refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Utils/SimitoneNeighbourGenerator.cs`:
  `SimTemplateCreateInfo` mapeia roupas e campos de PersonData; `CreateFamily`
  chama `MakePersonData` para cada pessoa.
- `refs/upstream/Simitone/Client/Simitone/Simitone.Client/UI/Panels/CAS/UISimCASPanel.cs`:
  os cinco controles permitem de 0 a 10 pontos, com limite total de 25.

Os testes `linux_simitone_cas_member` usam resultados aleatórios repetíveis e
verificam adulto feminino, criança masculina, nomes de assets recusados,
limites dos pontos, os campos de idade/pele/gênero, necessidades e a
distribuição de dez traços. Isso é teste sintético contra o contrato de código
consultado; não demonstra que o original ou o Simitone produzem o mesmo
resultado.

## Integração com a tela Linux

A vizinhança abre o criador de famílias com `C`. A tela lê as escolhas de corpo
e cabeça do catálogo de skins instalado, coleta nomes da família e dos Sims e
os demais campos do CAS, converte cada membro por
`make_simitone_cas_member` e grava arquivos privados de personagens mais um
overlay de família do jogador. `UserData/Neighborhood.iff` é apenas fonte de
leitura; não é o destino dessas gravações.

Verificado no Xvfb com a instalação comercial: a tela listou escolhas reais de
corpo e cabeça; preencher nome de família e de Sim e salvar criou arquivos em
`XDG_DATA_HOME` temporário e estado de família em `XDG_CONFIG_HOME` temporário,
e voltou à vizinhança. O perfil temporário foi removido ao fim. O teste gráfico
focado também abre e cancela o CAS duas vezes antes de continuar o fluxo
sintético de mudança de lote. Isso valida menu -> CAS -> gravação privada, mas
não a mudança dessa família para um lote nem sua aparência animada completa. A
tela mostra nomes e escolhas de assets; ainda não oferece a prévia final do Sim
nem prova paridade visual com The Sims 1 ou Simitone.
