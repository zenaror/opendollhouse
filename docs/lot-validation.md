# Contexto de lote e dados SIMI

Checkpoint de 2026-10-13. O host `odh-person` carrega SIMI1 de uma casa externa,
recebe explicitamente o número do lote escolhido e consulta zoning de STR#1
do bairro. Main e Init nativos atravessam suas primeiras consultas de lote.
Isso ainda não carrega arquitetura, entidades salvas, terreno ou uma simulação
completa da casa.

## Estrutura e procedência

Referência FreeSO fixa `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2,
consultada no clone local. Arquivos sob `TSOClient/`:

- `tso.files/Formats/IFF/Chunks/SIMI.cs:31`: header, globais e campos fixos;
  linhas72–75 e136–147: seis registros de orçamento com presença condicional.
- `tso.simantics/Utils/VMTS1Activator.cs:61`: lê SIMI1; linhas77–93 distinguem
  globais salvos de valores impostos pela inicialização do runtime.
- `tso.simantics/VM.cs:590`: leitura dos globais; alguns vêm do relógio.
- `tso.content/TS1/TS1NeighbourProvider.cs:85`: zoning em STR#1.
- `tso.simantics/Primitives/VMGenericTS1Call.cs:264`: modo28;
  linhas407–412: seleção pelo primeiro byte do operando.

`read_simi` lê pad bruto, versão, assinatura IMIS, exatamente32 ou64 palavras
assinadas, campo unknown1, oito campos int32 de identidade/valoração e seis
registros de orçamento. Valid zero significa ausência dos oito valores do
registro; não se transforma em oito valores zero. O leitor preserva extensão
de cauda desconhecida e não calcula valor de compra ou aplica defaults.

Foram lidos, sem gravação,648 blocos SIMI em `UserData*/Houses/House*.iff`:

| Versão hex | Ocorrências | Globais serializados |
|---|---:|---:|
| 33 | 8 | 32 |
| 38 | 8 | 32 |
| 3e | 34 | 32 |
| 3f | 46 | 32 |
| 45 | 400 | 64 |
| 48 | 72 | 64 |
| 49 | 80 | 64 |

Somente essas versões são aceitas atualmente. As de versão33 conservam168
bytes de cauda após o perfil de orçamento da referência; sua interpretação
não foi estabelecida. O writer FreeSO tem um limiar de versão diferente do
reader e não foi usado para inferir estrutura.

`UserData/Houses/House01.iff`, SHA256
`99b7a9cd585904f1a474f46e0c5f8df9089db57a16ff7535ea15b2d82b29607d`,
contém SIMI1 no offset64, payload326, versão3e,32 globais, seis registros
válidos e cauda zero. Global10 salvo é1; global23 é56. Valorações salvas:
lote10500, objetos0, arquitetura0. Leitura estrutural não valida a simulação.

## Globais e zoning na VM

`LotContext` mantém até38 globais opcionais. Os campos estáticos serializados
são carregados, inclusive zero e valores negativos. Global10 vem da seleção
explícita do host, conforme a origem de HouseNumber na referência; o JSON
mostra também o número salvo, permitindo reconhecer divergência. Não se infere
identidade a partir do nome do arquivo.

Índices0/1/4/5/6/7/8 continuam unsupported: a referência deriva suas leituras
de um relógio vivo. Índices3/9/17/20/25/32/33 também ficam ausentes porque
o activator sobrescreve esses campos com identidades e escolhas do runtime.
ODH não copia seus defaults para atravessar o programa. Campos32–37 não
serializados permanecem ausentes. Leituras/escritas fora do banco ou em um
campo ausente falham; campos presentes podem receber escrita da VM.

`read_lot_zoning` usa somente o grupo US0/1 de STR#1. Aceita labels explícitas
` residential` e ` community`, mapeadas para0 e1. Rejeita IDs duplicados,
texto desconhecido, ID fora de signed16 e mais de4096 lotes. FreeSO mapeia
qualquer label não-community para0; ODH diagnostica labels desconhecidas
até haver contrato para elas. Não mistura traduções no dicionário.

`UserData/LotZoning.iff`, SHA256
`b5feb255ada8cecb7cb20c475f72b916ce038a2f37b20219df139cc09356438c`,
tem STR#1 formato−3,1000 entradas em20 grupos de50. O grupo US contém
lote1 residential, community nos58/61/70–75/93–98, e não contém81–89.

Opcode1/modo28 lê temp0 como número do lote e publica o resultado em temp0,
retornando true. Com dicionário carregado, chave ausente usa o fallback da
referência:81–89→2, outros→1. Sem recurso zoning, permanece unsupported.
O log distingue valor carregado de fallback, inclusive quando ambos valem1.
Os outros modos GenericTS1Call continuam fora de cobertura.

A VM agora aceita `PrimitiveOutcome::temp_write` para branches síncronos.
Valida disposition, índice e eventual output de frame antes de publicar os
outputs. WAIT/call/unsupported com output temporário são recusados. O handler
de zoning devolve o output à VM; não modifica seus registradores diretamente.

## Uso e resultados nativos

```sh
./build/odh-person \
  --avatar '../referencias/The Sims/GameData/Objects/Objects.far' 'People\TemplatePerson.iff' 128 \
  --semiglobal '../referencias/The Sims/GameData/Global/Global.far' PersonGlobals.iff \
  --global '../referencias/The Sims/GameData/Global/Global.far' Global.iff \
  --lot '../referencias/The Sims/UserData/Houses/House01.iff' 1 \
  --zoning '../referencias/The Sims/UserData/LotZoning.iff'
```

`--routine 4097` seleciona o Init. Casa requer SIMI1 único; zoning requer STR#1
único. Recursos inválidos dão exit2, fault da VM dá exit3 com diagnóstico.
O programa usa somente leitura; não instancia as entidades salvas da casa.

| Probe | Antes | Com contexto de House01 |
|---|---|---|
| Main4096 | 6 dispatches/1 tick, Global320 pc0 scope6/index10 ausente | 11 dispatches/1 tick, Person8211 pc3 opcode31 ausente |
| Init4097 | 19 dispatches/0 ticks, Global321 pc1 scope6/index10 ausente | 24 dispatches/0 ticks, Person8325 pc2 opcode8 ausente |

Main lê current lot1 e sai da consulta Downtown. Init consulta zona1→0,
retorna de Global321 como residencial e lê BCON semiglobal8195[0]=50 antes
de pedir RandomNumber. Suas duas consultas anteriores continuam
Global257[4]=1 e[7]=7. O contexto não substitui RNG ou SetToNext por sucesso.
Relatórios locais ignorados: registro arquivado não distribuído e
registro arquivado não distribuído. A inicialização completa, o Main permanente
com ação real, o relógio, terreno/arquitetura e entidades salvas seguem abertos.

## Verificação

5670 checks sintéticos do leitor cobrem versões, todos os prefixos truncados,
palavras assinadas, registros com validade mista e caudas.80 checks de contexto
cobrem globais presentes/ausentes, limites, zoning US, carregado/fallback e
execução pela VM.37 checks de output temporário cobrem publicação e rejeição
atômica de outputs inválidos. A integração CLI cobre seleção diferente do
valor salvo, zero residential, community, fallback, ausência, duplicação,
recursos inválidos e campos sem backend.

Debug e ASan/UBSan:45 testes CTest passaram;1 teste X11 foi pulado porque este
gate não abre janela. LeakSanitizer ficou desativado pelo ambiente ptrace.
Os JSONs nativos Main/Init coincidem byte a byte entre builds. Nenhum desses
resultados estabelece execução do jogo original, equivalência geral ou hardware.
