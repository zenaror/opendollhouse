# OpenDollhouse

Reimplementação independente de The Sims 1 para PC, com suporte progressivo às
sete expansões e ao conteúdo personalizado clássico. FreeSO e Simitone são
referências técnicas e inspiração; a referência de comportamento é o jogo original.

Estado: primeiro protótipo de inspeção de recursos em C++17. Ainda não há jogo
executável, VM funcional, renderizador ou validação em consoles.

Plataformas pretendidas: New Nintendo 3DS, PlayStation Vita, Android e Linux.
O primeiro alvo funcional é Linux, para permitir testes, depuração e comparação
antes dos ports. O New 3DS continua orientando consumo, carregamento e decisões
portáveis desde o início. Cada console exigirá validação própria em hardware.

## Compilar e testar

Requisitos desta etapa: compilador C++17 e CMake 3.16 ou superior. Os scripts
auxiliares usam Python 3.8 ou superior, somente com a biblioteca padrão.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

Os testes geram fixtures sintéticas na memória; não dependem dos dados comerciais.

## Inspecionar uma instalação fornecida pelo usuário

Mantenha a instalação fora deste checkout. O exemplo abaixo pressupõe uma pasta
irmã chamada `The Sims`. Feche o jogo enquanto registra o corpus de referência.

```sh
mkdir -p reports
python3 tools/fingerprint.py '../The Sims' > reports/input-manifest.json
./build/odh-inspect '../The Sims' > reports/corpus.json 2> reports/corpus-errors.txt
python3 tools/check_census.py reports/corpus.json
```

`odh-inspect` abre arquivos somente para leitura. Emite JSON com contagens e
diagnósticos em stderr. Saída 0 significa que os checks implementados passaram;
1 indica erros em entradas; 2 indica erro de uso ou preparação. Não extrai recursos
nem executa BHAVs. O relatório deve ser interpretado junto com seu status de saída.

O comparador reconhece o snapshot local de 2026-10-13. Outra instalação pode ter
contagens diferentes legitimamente. Igualdade de contagens não autentica arquivos:
o manifesto de hashes registra a identidade dos inputs efetivamente selecionados.
Ele não cobre todos os formatos da instalação, o executável ou a ISO.

Para consultar os campos de uma tabela de interação específica:

```sh
./build/odh-ttab '../The Sims/GameData/Objects/Objects.far' DiningChairs.iff 128
```

O nome da entrada é exato; o ID aceita decimal ou prefixo `0x`. A ferramenta
rejeita seleção ambígua. Saída 0 indica tabela decodificada ou contagem vazia,
3 indica payload ausente ou versão legada não suportada, 2 indica erro.
O JSON preserva bits restantes e não executa os BHAVs indicados pela tabela.

`reports/` e os diretórios de build são ignorados pelo Git. Não inclua executáveis,
ISOs, recursos comerciais, saves, extrações ou bancos de análise do original no Git.

## Documentação

- [Arquitetura e próximos experimentos](docs/architecture.md)
- [Perfil dos formatos suportados e lacunas](docs/formats.md)
- [Validação do primeiro protótipo](docs/validation.md)
- [Validação das tabelas de interação](docs/ttab-validation.md)
- [Referências e atribuição](docs/references.md)

O contexto, decisões e handoffs também são mantidos na OMM, escopo
`OpenDollhouse`. Revalidar informações que dependam de arquivos ou revisões.

## Licenciamento

A licença do código do ODH ainda será definida. Não presumir que a licença de uma
referência se aplica a este projeto. Reutilização futura de código de terceiros
requer verificar a licença dos arquivos e dependências. Dados comerciais do jogo
devem ser fornecidos pelo usuário e não são distribuídos com o ODH.
