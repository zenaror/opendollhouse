# Catálogo CAS Linux

## O que foi implementado

`scan_linux_cas_catalog()` procura arquivos CMX de aparência e nomes de
texturas BMP/TGA em `GameData/Skins`, `ExpansionShared/SkinsBuy`, `Downloads`
e nas pastas `Skins` das expansões. Também indexa essas extensões dentro dos
arquivos FAR. Para arquivos FAR, barras invertidas e barras normais são
tratadas como separadores antes de extrair o nome do asset; isso é necessário
porque os nomes empacotados usam caminhos Windows mesmo quando lidos no Linux.

As consultas de cabeça exigem textura da pele escolhida. Para corpo, o
catálogo segue o comportamento observado em `TS1CASScreen.PopulateSimType` do
Simitone: procura primeiro a textura da pele e depois aceita o primeiro
prefixo sem pele. A mão usa o arquivo `huao` que corresponde ao sufixo da
aparência; quando ele falta, usa o código da pele. A busca não extrai nem
modifica arquivos comerciais.

## Verificação

O teste sintético `linux_cas_catalog` cobre arquivos soltos, itens de
`ExpansionShared/SkinsBuy`, roupas em FAR, caminhos FAR com `\\`, escolha de
textura por pele, fallback de corpo, textura de mão, classificação de corpo e
recusa de entradas inválidas. `mesh_resources` verifica a leitura CMX usada
pelo catálogo. Os dois testes e oito testes relacionados passaram em
`/tmp/odh-family-overlay-build` em 2026-10-24:

```text
linux_text_codec, family_overlay, character_catalog_scan,
template_person_clone, linux_private_character_store,
linux_private_family_creation, linux_simitone_cas_member,
linux_cas_catalog, mesh_resources, formats: 10/10 passed
```

Depois disso, a compilação completa terminou e o CTest completo executou
236 testes: 233 passaram e três testes de janela foram ignorados porque não
havia display (`linux_window_backend`, `linux_neighborhood_screen_actions` e
`linux_move_out_integration`). Nenhum servidor de display virtual foi
iniciado.

Uma leitura somente de metadados da instalação `../referencias/The Sims`
encontrou 272 CMX soltos, 90 arquivos FAR, nenhum CMX dentro de FAR e 5.045
nomes distintos de texturas BMP/TGA. Exemplos de consultas produziram 21
cabeças adultas masculinas de pele clara e 47 corpos para cada um dos três
tons; uma cabeça infantil feminina e duas opções de corpo infantil masculino
também foram encontradas. Essas contagens dependem da instalação e não
confirmam que todos os itens sejam exibidos ou renderizados corretamente.

Os hashes dos quatro arquivos comerciais consultados antes e depois
permaneceram iguais:

| Arquivo | SHA-256 |
| --- | --- |
| `GameData/Animation/Animation.far` | `acd29d2124d9ede479a586d40122a5896e264447cedd60c08ba54078c48eb35f` |
| `GameData/Textures/Textures.far` | `d4848bdf01c8e4e01067204d4a5c89011948c7ce7c1bf97b39202eafd002993e` |
| `GameData/Objects/Objects.far` | `68cadf2f250a50a6227540ee9175cbb937016eed54770bed12ec8c55047ed0c5` |
| `UserData/Neighborhood.iff` | `e585615fed1f9ddfb15ff8f5ebe7a09ea48a1369e9a3106f8331d3d9f5dde323` |

## Limites e próximo passo

O catálogo só resolve nomes de assets. Ele não decodifica nem mostra as
texturas e ainda não fornece os itens à janela CAS. BCF não é usado para
descobrir as roupas nesta etapa. O próximo marco é ligar estas escolhas à
tela de criação, gravar a família no perfil privado e testar a entrada no
lote com save e retomada em processo novo.
