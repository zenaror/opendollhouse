# Integração do leitor nativo de constantes

O candidato Simitone Linux usa `native/data` para decodificar BCON, os valores
que o código dos objetos consulta. A implementação é o `odh::read_bcon` existente,
sem alterações em `src/formats.cpp`. O mesmo wrapper e leitor são compilados no
probe 3DS; esse caminho executou com sucesso em emulador. Os demais leitores
FAR/IFF do candidato ainda são gerenciados. Não se trata de outro motor completo.

## Contrato compartilhado

[odh_data.h](../native/data/include/odh_data.h) expõe ABI1 com dois símbolos C.
Tamanhos e estados de erro são explícitos, sem tipos CLR/STL, `bool` ou `size_t`
na fronteira. Buffers são válidos, alinhados, não sobrepostos e pertencem ao
chamador. Ponteiros não ficam retidos; exceções C++ viram códigos de erro.
Entrada é little endian; palavras tipadas de saída usam a representação numérica
do host. O wrapper usa stream emprestado, sem copiar todo o payload.

A [ponte C#](../managed/OpenDollhouse.Data.Interop/README.md) prende os buffers
somente durante a chamada. `BCON.Read` real a utiliza; o writer não mudou.
A ponte consome apenas o registro declarado, suporta streams não seekable e
leituras curtas, conserva trailer/stream aberto e lança EndOfStream no truncamento.
Em leitura inválida, campos são atribuídos somente após sucesso; a mutação parcial
antiga de Flags/Constants não é reproduzida. Não há parser alternativo escondendo
biblioteca, símbolo ou ABI ausente/incorreto.

## Reproduzir o candidato atual no Linux

Usar apenas o checkout canônico e os clones de trabalho fixados no handoff.
Na revisão inicial dos clones, aplicar frontend0001/0002/0006 e FreeSO0003/0004/0005
mais `experimental-saved-code-recovery.patch` atual (22 arquivos), na ordem
indicada no handoff. Não reaplicar patches sobre fontes já adaptadas. Os projetos
`managed/` e `native/` deste repositório são parte necessária da receita.

```sh
cmake -S native/data -B build/native-data -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-data
ctest --test-dir build/native-data --output-on-failure
cmake -S native/simulation -B build/native-simulation -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-simulation
ctest --test-dir build/native-simulation --output-on-failure

dotnet publish refs/work/Simitone-linux/Client/Simitone/Simitone.Desktop/Simitone.Desktop.csproj \
  -c Release -r linux-x64 --self-contained true \
  -p:PublishAot=false -p:PublishTrimmed=false \
  -p:TreatWarningsAsErrors=false -p:WarningsAsErrors= \
  -p:ODHNativeDataLibrary="$(pwd)/build/native-data/libodh_data.so" \
  -p:ODHNativeSimulationLibrary="$(pwd)/build/native-simulation/libodh_simulation.so" \
  -o build/simitone-linux
```

A biblioteca deve ser compilada para o destino. O parâmetro é um caminho absoluto
com basename `libodh_data.so`, `libodh_data.dylib` ou `odh_data.dll`; passar o alias
sem versão, não o arquivo `.so.1.0`. O SDK copia o arquivo ao publish transitivamente.
A receita observada usou o build em registro arquivado não distribuído;
os hashes do arquivo publicado coincidiram. Windows/macOS não foram compilados
nesta etapa. O CMake prepara nomes/exports desses hosts, não prova sua execução.

Não há instalação nova de SDKs/runtimes nesta etapa. O candidato continua sendo
self-contained .NET9.0.21 com avisos herdados de dependências; a atualização do
host é separada da portabilidade. A API nativa não depende de CLR para executar.

## Evidência e limites

- CTest nativo: duas entradas, incluindo 14 verificações de contrato e consumo
  do header por C. Dois exports dinâmicos, outputs sem escrita parcial, erro
  definido e limite de 255 constantes.
- Ponte: 12 grupos e 2.052 comparações com BCON/IoBuffer reais anteriores,
  incluindo todos os valores ushort, flags/counts, streams, falhas de diagnóstico,
  biblioteca ausente, propriedade/basename e cópia build/publish. Sintéticos.
- Linux real: `house07-native-bcon`, dois processos em cópia/perfil isolados,
  compra/Sit/save/reabertura. Ambos registraram chamada nativa ABI1 e o mapa da
  biblioteca publicada; hash nativo igual ao teste, nove hashes gerenciados.
  Peça/preço/saldo/estado observado conservaram-se, sem erros de VM/fatais/diagnóstico;
  seis arquivos comerciais acompanhados conservaram os hashes.
- 3DS emulado: `3ds-native-abi-execution`, nova fixture ARM que chama a mesma API
  e testa sucesso e erros. PASS SVC+imagem, hashes ligados às fontes comuns.
  147 configurações acompanhadas iguais, processos/perfil/Xvfb encerrados.

Relatórios em registro arquivado não distribuído,
registro arquivado não distribuído e
registro arquivado não distribuído. Binários nativos pequenos de cada
execução foram preservados para evitar sobrescrever a evidência aprovada.
O primeiro probe anterior foi reconstruído de `eea8a5b`, com hashes exatamente
iguais, e arquivado como `rebuilt-exact`, sem confundi-lo com captura original.

Não foram executados o jogo completo, fila gerenciada ou save de família no 3DS,
nem hardware físico. O restante do motor ainda depende de CLR/serviços desktop.
Este corte torna um leitor efetivamente compartilhado; os demais sistemas e os
ports continuam no escopo. A fronteira seguinte é a
[fila nativa de simulação](native-simulation-integration.md). A receita atual acima
inclui as duas bibliotecas; os resultados BCON descritos aqui continuam históricos
do checkpoint anterior, sem atribuir a eles a validação da fila nova.
