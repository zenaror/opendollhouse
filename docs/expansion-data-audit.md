# Auditoria de cobertura dos dados por expansão

Este relatório junta somente os catálogos de lotes do `odh-neighborhood` para os modos 1 a 7 e uma contagem de pastas `Template*`/arquivos FAR encontrados na instalação. Ele informa linhas de lote, casas presentes/ausentes, FAMI associados e quantidade de membros registrada. Modo 6 é relatado como não selecionável pelo catálogo atual.

```sh
python3 tests/expansion_data_audit.py \
  --binary /caminho/do/build/odh-neighborhood \
  --installation '/caminho/The Sims'
```

A ferramenta só lê os caminhos informados e escreve JSON no terminal. Para guardar um relatório local, redirecione a saída para `/tmp/odh-expansion-data-audit.json`. O teste usa apenas arquivos sintéticos:

```sh
python3 tests/expansion_data_audit_cli.py \
  tests/expansion_data_audit.py /caminho/do/build/odh-neighborhood
```

Os nomes e contagens de diretórios `Template*` e o total FAR indicam presença no disco, não identificam com certeza qual expansão forneceu cada arquivo. As contagens FAMI são associações do catálogo, e membros são entradas declaradas, não personagens carregados. Casas e arquivos são checados pela existência; o conteúdo não é validado por este relatório. Portanto, isto mede **cobertura catalogada/presença de dados**, nunca cobertura jogável, fidelidade, validade de recursos ou compatibilidade completa.
