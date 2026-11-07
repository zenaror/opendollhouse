# OpenDollhouse

Motor experimental do The Sims 1 em C/C++. Simitone/FreeSO são referências;
o produto não deve exigir C#/.NET/CLR nem manter outro motor em paralelo.

## Orientação

- Comece pelo [README](README.md), [documentação](docs/README.md),
  [arquitetura](docs/architecture.md) e [plano](docs/native-port-plan.md).
- Motor: `src/`, `include/odh/`, `native/`; programas/adaptadores: `tools/`;
  testes sintéticos: `tests/`; configuração: `CMakeLists.txt`.
- Preserve alterações existentes, origens, licenças e créditos. Não modifique
  dados comerciais ou saves originais; valide com cópias e perfis isolados.
- Separe simulação/dados de gráficos, áudio, entrada, arquivos e serviços do SO.
- Integre serviços completos e seus consumidores. Não crie exceções por objeto
  nem declare sistema concluído a partir de um teste isolado.
- Divida frentes independentes com dono de arquivos e revisão; pontos compartilhados
  e builds têm um responsável. Respeite o estado de execução definido pelo usuário.
- Confira aparência e comportamento da interface no original antes de implementá-los.
- Documentação pública deve ser legível e autossuficiente. Não incluir caminhos
  pessoais, diários de execução, resultados gerados ou continuidade entre agentes.
  Ferramentas reproduzíveis ficam versionadas; suas saídas permanecem ignoradas.
- Explique interfaces de extensão, exemplos e limites em [mods](docs/modding.md).

## Compilar e conferir

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

Requisitos e uso: [ferramentas](docs/tools.md). Execute verificações pertinentes
à mudança; documentação não exige build. Compilação e emulação não comprovam
funcionamento ou desempenho em outro aparelho.
