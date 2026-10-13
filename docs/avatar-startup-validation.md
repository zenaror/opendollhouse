# Sequência de inicialização do avatar no host Linux

Checkpoint de 2026-10-13. `odh-person --initialize` executa Init e a entrada
de atualização de multitile, empilha Main e aplica o estágio BodySTR antes da
primeira instrução de Main. World e RNG persistem entre os estágios. Isso liga
o Init privado concluído ao Main; aparências, tuning especial e restauração
de saves continuam fora desse perfil.

## Referência e seleção de recursos

FreeSO fixado em `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2,
consultado localmente em `refs/upstream/FreeSO/TSOClient/`:

- `tso.simantics/Entities/VMEntity.cs:444–456,485–520`: tabela de funções e ordem.
- Mesmo arquivo,589–601: Caller/Callee=this, StackObject fornecido pelo call.
- `tso.simantics/Engine/VMStackFrame.cs:37–43`: null StackObject significa ID0.
- `tso.simantics/Engine/VMThread.cs:92–100`: checks fora do tick clonam temps.
- `tso.simantics/Entities/VMAvatar.cs:290–421`: BodySTR, dados do corpo e pós-Init.
- `tso.simantics/Model/VMOutfitReference.cs:31–38`: referências TS1 de aparência.

O perfil usa OBJD138/type2, tabela direta (`UsesFnTable=0`) e BodySTR tipo adult.
O novo leitor `read_objd_startup` requer o prefixo158 e lê BodyStringID em38,
Init em86 e DynamicMultiTileUpdate em156. Não declara validar todo o OBJD.
Main continua vindo da tabela direta existente. Recursos e rotinas são exatos,
por owner, sem busca heurística ou precedência implícita.

Init é entrada0; atualização de multitile é entrada8. **Load é entrada2** e
não é executado como substituto da entrada8. IDs de entry<=255 são explicitamente
pulados, conforme a referência. Uma entrada executada pode retornar true ou
false; base.Init não usa esse retorno para escolher se continua a construção.
Fault, espera, yield ou budget interrompem este host antes de BodySTR/Main.
Checks com waits não são emulados até conclusão pelo host nesta cobertura.

Init e entrada8 usam Caller/Callee1, owner1 e StackObject0, com quatro argumentos
zero. Main recebe o mesmo contexto inicial, antes de BodySTR. O modo anterior
de probe de rotina conserva seu contexto de fixture com StackObject1. O novo
modo representa construção fora dos ticks normais: temps são zerados entre
estágios; não modela criação dentro de um tick com temps compartilhados.

As entradas síncronas usam a VM já limitada. Cada `start` preserva World e o
RNG externo, mas cria outro run. O budget total é10000 instruções, incluindo
Init, multitile, Main e eventual ação. Não é renovado em cada estágio.
O trace identifica init/dynamic_multitile/main; probes isolados são root_probe.
O JSON distingue passos totais, passos de startup e do run atual, resultados
das entradas, corpo aplicado, fault real e falha do host.

## BodySTR e dados de movimento

O core usa somente o grupo US0/1. BodySTR presente requer índices0–22 e age13
signed16. Gender12 reconhece male/female/dogmale/dogfemale/catmale/catfemale
sem diferenciar caixa; desconhecido preserva person65. Idade vai para person58.
O helper de dados suporta ausência de BodySTR conforme a referência, mas o
host `--initialize` exige a tabela selecionada para seu primeiro perfil.

Na política TS1 da referência, body1 usa o primeiro segmento antes de `;`,
head2 e hands17–22 ficam sem divisão. SkinTone fica Light; os campos de seleção
aleatória9/10/14 usados no TSO não consomem RNG aqui. O descriptor guarda essas
referências, sem resolver mesh, textura, skeleton ou appearance. O host ainda
não constrói uma representação visual a partir delas.

Name11 não vazio é dividido por `;`, descartando tokens vazios. A escolha
consome o mesmo RNG, inclusive quando há um único nome. Texto não vazio feito
só de delimitadores é recusado antes de mutação. O descriptor registra nome
selecionado e seed antes/depois dessa etapa, separadamente de opcode8/Sleep.

Walk e Swim têm50 posições. Idade<18 seleciona globals151/160; idade>=18 usa
150/158. STR150 privada sobrescreve apenas walk entries não vazios. Semiglobal
não participa dessa escolha. As referências resultantes são carregadas como
strings, sem executar animações ou mudar poses automaticamente.

Depois: todos os16 motives recebem100, exceto SleepState11=0; NeatPersonality7
recebe1000. Isso é política da referência, ainda sem equivalência original
demonstrada. O perfil TS1 mantém DisableAvatarCollision=false e não aplica
overrides de tuning especial; Flags não é alterado pelo helper de BodySTR.

Validação e alocação de descriptors precedem mudanças de World/RNG. Cada tabela
tem até65536 entries totais,50 US entries e64KiB de valores US cumulativos.
Plan de BodySTR tem limite64KiB e256 nomes. Índices/bancos/avatar são conferidos
antes da publicação. Esses limites tornam o consumo previsível; não medem RSS
ou estabelecem orçamento real de New3DS.

## Uso e gate nativo

Adicionar `--initialize` ao comando de [lot-validation.md](lot-validation.md),
com `--seed 1`. O modo é incompatível com `--routine`, `--age` e `--flags`, que
continuam opções de fixtures/probes separados.

TemplatePerson OBJD128 contém Init4097, DynamicMultiTile0 e BodySTR200. STR200
tem27 US entries: type adult, gender male, age27; body/head/hands/name vazios.
Nenhum RNG é consumido na etapa de BodySTR. Os globals de movimento selecionados
são150/158. Ambos conservam39 posições US, preenchendo as demais posições até50
com ausência de referência no descriptor.

Resultado com House01/zoning e seed1:

- Init4097 retorna true em179 instruções; entrada8 ID0 é pulada.
- Main é empilhado; BodySTR aplica age27/gender0, Neat1000 e motives pós-Init.
- Main executa11 instruções/1 tick e para no opcode31 NeighborId,
  PersonGlobals8211 pc3. Total190 instruções; o lote ainda não está jogável.
- Traits46–55 permanecem `[6,4,4,7,9,0,1,2,8,3]`, preservados do Init.
- Seed após Init9848121759223504888; após BodySTR igual; Sleep de Main avança
  para16610512366814951206. Flags permanecem264 depois do Init.

Relatório ignorado `reports/avatar-startup-native.json`. NBRS/scope32/Relationship
continuam a próxima dependência do Main. Aparências vazias deste template não
são substituídas por uma skin inventada. Família/vizinho, humano salvo real,
terreno, arquitetura e persistência ainda precisam ser ligados ao carregador.

## Verificação

112 checks do helper cobrem US, signed age, genders, body/head/hands, nomes e
RNG, adult/child tables, limites de entries/bytes/nomes, overrides privados,
motives/Neat e rejeição sem alteração.162 checks do prefixo OBJD cobrem campos
distintos, todos os prefixos truncados, versão e subviews.
Fixtures CLI verificam ordem/contexto StackObject0, retorno false de Init,
preservação de World/RNG, ação somente após body, erro/espera antes de Main,
budget compartilhado, seleção child e rejeição de opções incompatíveis.

Debug e ASan/UBSan:49 testes CTest passaram;1 teste X11 foi pulado. Depois das
correções de contexto/cap, os testes afetados foram repetidos nos dois builds.
LeakSanitizer desativado por ptrace. JSON nativo completo coincide byte a byte
entre builds, e seus estados RNG foram conferidos por oracle independente.
Não houve execução do original, restauração integral da casa ou teste de console.
