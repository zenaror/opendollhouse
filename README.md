# OpenDollhouse

Reimplementação independente de The Sims 1 para PC, com suporte progressivo às
sete expansões e ao conteúdo personalizado clássico. FreeSO e Simitone são
referências técnicas e inspiração; a referência de comportamento é o jogo original.

Estado: leitores de recursos, núcleo de execução BHAV e modelo inicial de
entidades/slots em C++17,
com testes sem janela e traces. Ainda não há jogo executável, serviços completos
de simulação, renderizador ou validação em consoles.

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
irmã `referencias/The Sims`. Feche o jogo enquanto registra o corpus de referência.

```sh
mkdir -p reports
python3 tools/fingerprint.py '../referencias/The Sims' > reports/input-manifest.json
./build/odh-inspect '../referencias/The Sims' > reports/corpus.json 2> reports/corpus-errors.txt
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
./build/odh-ttab '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 128
```

O nome da entrada é exato; o ID aceita decimal ou prefixo `0x`. A ferramenta
rejeita seleção ambígua. Saída 0 indica tabela decodificada ou contagem vazia,
3 indica payload ausente ou versão legada não suportada, 2 indica erro.
O JSON preserva bits restantes e não executa os BHAVs indicados pela tabela.

Para inspecionar um BHAV ou fazer uma execução limitada sem serviços do mundo:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run
```

O modo `--run` emite trace e status JSON. Sem entidades, a cadeira para com
diagnóstico de scope não suportado, saída 3. Saída 0 indica retorno true/false
da rotina; 2 indica erro de uso/recurso. Há um orçamento de 10.000 instruções,
distinto de ticks do jogo. A biblioteca preserva o estado para retomada ao
esgotar o orçamento. Recursos globais opcionais são fornecidos explicitamente
com `--global ARCHIVE.far ENTRY.iff`; não há busca nem precedência automática.

Um probe com entidades construídas permite investigar as branches da cadeira:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1002 --run --world-probe
```

O cenário tem caller 1, objeto 2 e outro avatar 3, com um slot no objeto.
`--occupant 0|1|3`, `--posture 0..32767` e `--autonomous` configuram o estado
inicial. O relatório identifica `ODH_VM_constructed_world`: os BHAVs vêm do
arquivo selecionado, mas as entidades são construídas no ODH. Não carrega
um lote/save nem comprova comportamento do executável original.

O registro de roteamento indicado pela cadeira pode ser consultado em leitura:

```sh
./build/odh-slot '../referencias/The Sims/GameData/Global/Global.far' Global.iff 100 3 1
```

Os últimos seletores significam tipo 3 e índice 1 dentro desse tipo. Sem eles,
a ferramenta lista todos os registros. Os valores são brutos; floats são bits
uint32 e campos ausentes da versão são `null`. Saída 0 indica leitura, 2 erro.

Para investigar a ação com percurso e ticks no cenário construído:

```sh
./build/odh-bhav '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 0x1001 --run --world-probe --route-probe --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff
```

O JSON inclui movimentos e posição final. O perfil inicial de roteamento
suporta o SLOT selecionado, com política de percurso e velocidade provisórias.
A ação ainda para em `snap`, saída 3; não é sentar/levantar completo.

Adicione `--snap-probe --object-def 16808` para usar o OBJD explícito da
cadeira selecionada. Esse perfil liga a ocupação ao SLOT privado; sem a opção
de animação abaixo, a ação para na44. Não mostra uma animação de sentar.

Adicione `--animation-probe '../referencias/The Sims/GameData/Animation/Animation.far'
adult-chair-sit-new.cmx.bcf --age 21` para avançar frames e eventos silenciosamente.
O probe adulto entrega `xevt100`, testa o GUID da cadeira e para no som23
sem a opção adicional abaixo, saída3. Os opcodes do JSON são decimais.
O relatório distingue eventos observados, progresso e ticks da simulação;
`route_ticks` nesse modo também conta o tempo das animações. Ainda não há ossos,
som ou renderização de personagem. A idade explícita evita selecionar criança
a partir do zero inicial do probe antigo.

Com `--sound-probe`, o CLI resolve FWAV e registra pedidos não bloqueantes,
com backend `none` e status `skipped_no_backend`; não reproduz áudio.
O probe adulto termina o clipe e, sem recursos de reset, para na44 ID0.

Adicione `--reset-probe a2o-sit-loop.cmx.bcf` para resolver a postura sentada
pela STR150 global e manter seu loop. `--post-ticks 240` avança apenas os
controladores após o fim/parada da VM, preservando seu status; não conclui a ação.
O probe real com reset completa o BHAV selecionado, saída0/returned_true,
com avatar no slot e alinhado à direção da cadeira. A notificação final é
`no_thread` quando a thread principal não é solicitada.
Personagens carregando objetos e gestos das mãos ainda exigem implementação.

`--stand-after --animation-extra adult-chair-stand-new.cmx.bcf` executa depois
a função Stand do OBJD legado selecionado, preservando o mesmo World.
O probe barato retorna true nas duas fases, esvazia o slot e termina com postura0.
O despacho é do host de teste; ainda não é o scheduler/queue completo do jogo.
As fases e o orçamento global aparecem no JSON. O loop após Stand é registrado
conforme a ordem efetiva do BHAV, sem forçar uma pose visual de pé.

Acrescente `--main-probe --hold-ticks 120` à sequência para executar também
a Main da cadeira, acordá-la pela notificação ao sentar e testar o conforto
enquanto o avatar permanece sentado. O JSON separa as duas VMs e informa
conforto, espera e orçamento compartilhado. Esse host ainda não implementa
queue geral, decaimento de motivos ou RNG real; veja [a validação](docs/main-validation.md).

`reports/` e os diretórios de build são ignorados pelo Git. Não inclua executáveis,
ISOs, recursos comerciais, saves, extrações ou bancos de análise do original no Git.

Para identificar e decodificar os recursos da animação adulta da cadeira:

```sh
./build/odh-strings '../referencias/The Sims/GameData/Objects/Objects.far' DiningChairs.iff 129 1 3
./build/odh-animation '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf A2O-sit-left --cfp xskill-A2O-sit-left.cfp > reports/chair-animation-cfp.json
python3 tools/compare_cfp.py '../referencias/The Sims/GameData/Animation/Animation.far' xskill-A2O-sit-left.cfp reports/chair-animation-cfp.json
```

Essas consultas preservam texto bruto e bits float32; não reproduzem os ossos ou
executam eventos. A seleção de adulto é explícita: a idade ainda não foi
inicializada no probe anterior. `odh-strings` suporta formatos 0..-3; `odh-animation`
suporta BCF binário com seções skeleton/appearance vazias. Ambos retornam 2 em erro.

`odh-pose` lê o skeleton binário, aplica os canais de um clipe/CFP e compõe as
transformações dos ossos. A seleção dos recursos é explícita. Pode exportar um
SVG de diagnóstico; isso ainda não desenha as meshes, roupas ou o cenário do jogo.

```sh
./build/odh-pose '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf adult --animation '../referencias/The Sims/GameData/Animation/Animation.far' adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --svg reports/pose-adult-80_5.svg > reports/pose-adult-80_5.json
python3 tools/compare_pose.py '../referencias/The Sims/GameData/Animation/Animation.far' adult-skeleton.cmx.bcf reports/pose-adult-80_5.json reports/chair-animation-cfp.json
```

O verificador compara campos brutos exatamente e poses numericamente; o CFP do
relatório deve ter passado pelo gate `compare_cfp.py` acima. Detalhes e limites
estão em [pose-validation.md](docs/pose-validation.md).

`odh-mesh` lê uma appearance/BMF e aplica skinning CPU à pose. Exporta JSON e,
opcionalmente, OBJ com vertices/UVs/normals; ainda sem texturas ou renderer de jogo.

```sh
./build/odh-mesh '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf b001mafit_01 xskin-b001mafit_01-PELVIS-BODY.bmf adult-skeleton.cmx.bcf adult --animation adult-chair-sit-new.cmx.bcf a2o-sit-left xskill-A2O-sit-left.cfp --frame 80.5 --obj reports/mesh-body-80_5.obj > reports/mesh-body-80_5.json
python3 tools/compare_mesh.py '../referencias/The Sims/GameData/Animation/Animation.far' b001mafit_01.cmx.bcf xskin-b001mafit_01-PELVIS-BODY.bmf reports/mesh-body-80_5.json
```

`preview_mesh.py OUTPUT.png REPORT...` produz uma prévia diagnóstica sem
texturas usando Pillow no host. Contratos e gates em
[mesh-validation.md](docs/mesh-validation.md).

## Documentação

- [Arquitetura e próximos experimentos](docs/architecture.md)
- [Perfil dos formatos suportados e lacunas](docs/formats.md)
- [Validação do primeiro protótipo](docs/validation.md)
- [Validação das tabelas de interação](docs/ttab-validation.md)
- [Núcleo da VM e limites de validação](docs/vm-validation.md)
- [Entidades, slots e probe da cadeira](docs/world-validation.md)
- [Dados SLOT e recurso de roteamento](docs/slot-validation.md)
- [Suspensão da VM para serviços](docs/services-validation.md)
- [Cálculo de percurso na malha](docs/route-validation.md)
- [Percurso ligado à VM e ao World](docs/routing-validation.md)
- [Snap, definição do objeto e ocupação](docs/snap-validation.md)
- [Recursos de animação e próximo contrato da VM](docs/animation-validation.md)
- [Skeleton, interpolação e transformações da pose](docs/pose-validation.md)
- [Appearances, BMF e skinning CPU](docs/mesh-validation.md)
- [Execução silenciosa dos frames/eventos](docs/playback-validation.md)
- [Teste de tipo e pedidos de som](docs/object-sound-validation.md)
- [Reset de postura e loops persistentes](docs/reset-validation.md)
- [Snap de direção e retorno do BHAV da cadeira](docs/facing-validation.md)
- [Sequência de sentar e levantar no mesmo mundo](docs/stand-validation.md)
- [Referências e atribuição](docs/references.md)

O contexto, decisões e handoffs também são mantidos na OMM, escopo
`OpenDollhouse`. Revalidar informações que dependam de arquivos ou revisões.

## Licenciamento

A licença do código do ODH ainda será definida. Não presumir que a licença de uma
referência se aplica a este projeto. Reutilização futura de código de terceiros
requer verificar a licença dos arquivos e dependências. Dados comerciais do jogo
devem ser fornecidos pelo usuário e não são distribuídos com o ODH.
