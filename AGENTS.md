# OpenDollhouse

Reimplementação experimental do The Sims 1 em C/C++, com Simitone/FreeSO como
referências. O produto final não deve exigir C#/.NET/CLR. As fontes gerenciadas
preservadas servem como referência histórica; não ampliar essa implementação.

## Orientação local

- Leia [README.md](README.md), [arquitetura](docs/architecture.md) e o
  [plano nativo](docs/native-port-plan.md). O índice está em [docs/README.md](docs/README.md).
- Motor: `src/`, `include/odh/`, `native/`; programas: `tools/`; testes: `tests/`.
- Não distribuir ou modificar dados comerciais nem saves originais. Use cópias
  e perfis isolados para validações que gravam estado.
- Referências externas são de terceiros: preserve revisão, licença e créditos;
  não editar submódulos/clones de consulta como se fossem fontes do projeto.
- Mantenha simulação e dados separados de gráficos, áudio, entrada e arquivos.
  Sucesso no desktop não comprova funcionamento em consoles.
- Preserve alterações preexistentes. Relate o escopo e os limites da validação;
  não declare paridade com o jogo completo a partir de um cenário limitado.

## Comandos essenciais

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

Dependências e modo sem janela: [README.md](README.md). Para mudanças de código,
execute os testes pertinentes; não repetir cenários sem uma alteração ou falha
que justifique a repetição. Mudanças documentais não exigem compilar o jogo.
