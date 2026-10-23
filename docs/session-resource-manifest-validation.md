# Fingerprint do manifesto de recursos da sessão

O helper `odh_host::identify_session_resources` calcula o fingerprint que será
gravado em `META`. Recebe a pasta de dados e a lista de arquivos que a sessão
usou. Cada arquivo deve existir dentro dessa pasta e ser regular. O helper
resolve caminhos com segurança, recusa caminhos repetidos ou externos, ordena
os nomes relativos resolvidos e lê o conteúdo em blocos de 64 KiB. O limite
padrão é 4.096 arquivos e 16 GiB somados.

O SHA-256 de cada arquivo é colocado numa lista binária canônica com o caminho
relativo em UTF-8, tamanho e hash. O hash da lista é independente da ordem de
entrada; mudar o conteúdo ou o nome relativo muda o resultado. O formato usa
campos little-endian e domínio/versionamento próprios. Idioma e máscara das
expansões ficam em campos separados de `SessionResourceIdentity`, para que o
mesmo conjunto de arquivos possa ser usado em mais de um idioma sem fingir que
o idioma altera o conteúdo desses arquivos.

O SHA-256 foi conferido com os vetores conhecidos de entrada vazia, `abc` e um
milhão de bytes `a`, além de atualizações em blocos. Os testes de manifesto
usam somente arquivos sintéticos: conferem independência da ordem, mudança de
conteúdo e caminho, conexão com o payload `META`, metadados separados,
duplicatas, arquivo ausente, caminho externo, idioma inválido e limite de
bytes.

O modo `--plan-only` do launcher agora monta essa lista do plano de entrada e
imprime os caminhos relativos e o fingerprint para diagnóstico. Na instalação
real usada neste checkout, `--mode 1 --house 7 --language 14 --plan-only`
resolveu 55 arquivos e produziu fingerprint
`6251e2f4ec41fc56d97c0aa435a36b376c2154ebedfa139b43a94a9ddeb84f63`. A máscara
de expansão foi zero: o plano atual não passa nenhum `ExpansionPack*.far` ao
runtime, mesmo que esses arquivos estejam na instalação.

Esse resultado comprova a identidade do conjunto que o launcher resolveu,
mas não prova ainda que todos os arquivos acessados dinamicamente pelo runtime
foram enumerados. O jogo em execução também não grava nem compara esse valor;
nenhum save é aceito ou recusado com base nele neste checkpoint.
