# Fingerprint do manifesto de recursos da sessão

O helper `odh_host::identify_session_resources` calcula o fingerprint que será
gravado em `META`. Recebe a pasta de dados e a lista de arquivos que a sessão
usou. Cada arquivo deve existir dentro dessa pasta e ser regular. O helper
resolve caminhos com segurança, recusa caminhos repetidos ou externos, ordena
os nomes relativos resolvidos e lê o conteúdo em blocos de 64 KiB. O limite padrão é
4.096 arquivos e 16 GiB somados.

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

Este checkpoint ainda não prova uma identidade da instalação real. O launcher
precisa fornecer a lista completa e exata dos recursos que seus leitores e o
runtime abriram. Até essa ligação existir, nenhum save deve ser aceito ou
recusado com base nesse fingerprint.
