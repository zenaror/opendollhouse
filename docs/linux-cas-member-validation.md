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
resultado, não usa os assets comerciais e não mostra o Sim na tela. A tela do
OpenDollhouse ainda precisa obter essas escolhas e chamar o adaptador antes da
gravação da família.
