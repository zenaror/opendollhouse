# PersonData por índice temporário e conclusão do Init privado

Checkpoint de 2026-10-13. A VM resolve scopes30/31 usando o valor do
registrador temporário escolhido pelo operando. O mesmo resolver atende
Expression e Random Number. O Init privado4097 do TemplatePerson agora retorna
true no probe nativo; o construtor completo com entrada8 e BodySTR ainda não
está integrado.

## Contrato e fonte

FreeSO fixado em `4c6b3e8f5835b228723caea3c9f683c62f244f73`, MPL2, lido do
clone local. `TSOClient/tso.simantics/Engine/VMMemory.cs:126–130,570–574` define:

| Scope lógico | Entidade | Índice PersonData |
|---|---|---|
| 30 MyPersonDataByTemp | Caller | temps[data] |
| 31 StackObjectPersonDataByTemp | StackObject | temps[data] |

O operando `data` seleciona um dos20 temps; não é o índice PersonData final.
`resolve_person_data_address` valida o seletor e traduz para scope18/19,
preservando os demais endereços. O backend World valida identidade/avatar e o
índice resultante no banco explicitamente alocado. Não se usa Callee ou code
owner como substituto de Caller/StackObject. Scope31 não é opcode31 SetToNext.

Leituras, atribuições e operações de Expression passam por essa resolução
em cada acesso. Zero presente e valores signed16 são válidos; índices negativos,
temp20+, PersonData fora do banco e entidade ausente/não-avatar falham. O
resolver não expande bancos ou inventa valores. O Word continua truncado como
nas escritas diretas existentes. World sozinho não recebe acesso implícito ao
estado de temps da VM.

Random Number usa o resolver para range e destino. A leitura ocorre antes da
escrita, inclusive se ambos apontam à mesma palavra. Falha de um índice ou do
writer mantém o RNG e a palavra de destino sem mudança. Seu log conserva o
endereço lógico dos operandos; o backend recebe scope18/19 e índice resolvido.

## Gate nativo

Comando de [lot-validation.md](lot-validation.md), mais `--seed 1 --routine 4097`.
Avatar `People\TemplatePerson.iff` OBJD128, semiglobal PersonGlobals e Global
explicitamente carregados. O cenário conserva o estado do construtor parcial,
sem antecipar a idade27 da BodySTR.

Antes:58 instruções, fault em privado4100 pc17, write scope30/index0.
Agora:179 instruções/0 ticks, returned_true, stack vazio, exit0. A rotina4097
chama o Init semiglobal8192 e o privado4100, cujo label nativo é `init traits`.
Há10 escritas indiretas em PersonData46–55; o operando escolhe temp0, que começa
em46 e é incrementado até56. Esses campos são interesses/slots de dados da
referência, não uma afirmação de personalidade completa do avatar.

Seed1 produz26 chamadas RNG:7 para motivos e19 tentativas para preencher os10
campos. As quotas da rotina são uma ocorrência de0, três de1–3, três de4–6 e
três de7–10; tentativas quando a quota já está esgotada são recusadas pelo BHAV.
Um oracle independente com máscaras unsigned64 e filtragem dessas quotas
confere todos os draws e os valores finais46–55:
`[6,4,4,7,9,0,1,2,8,3]`. Seed final9848121759223504888.

Outros campos PersonData não zero no probe:36=50 (autonomia). A idade58 permanece0.
Motives finais brutos, em ordem0–15:
`[0,0,0,0,0,100,99,99,99,78,0,0,0,0,99,23]`.
Não se substituem esses resultados pelo pipeline posterior da referência,
que aplica BodySTR e outra inicialização de motivos depois do base.Init.
O JSON agora expõe todo o banco PersonData101 e MotiveData16 para comparação.

Main separado permanece11 instruções/1 tick, fault no opcode31 NeighborId em
semiglobal8211 pc3. Ainda requer NBRS, output StackObjectID, scope32 e relações;
o probe de Main não recebeu silenciosamente o estado do Init separado.

Um probe **isolado**, ainda no construtor parcial, de privado Load4098 para
após10 instruções em semiglobal8244 pc15, leitura de global20 GameEdition.
Esse resultado não valida Load após Init e não autoriza preencher o global
com um default da referência. Load é entrada2, não entrada8. A entrada8 é
DynamicMultiTileUpdate (`VMEntity.cs:450,456`). O construtor da referência
executa Init0 e atualização8, empilha Main e depois Avatar.Init aplica BodySTR
(`VMEntity.cs:494–495`, `VMAvatar.cs:401`). A próxima integração deve preservar
essa ordem e estabelecer quando a entrada Load2 é usada na restauração;
o probe isolado não estabelece que Load pertence à sequência do construtor.

Relatórios locais ignorados: `reports/person-indirect-init.json`,
`reports/person-indirect-main.json` e `reports/person-indirect-load-isolated.json`.
Não houve gravação em casas, recursos comerciais ou saves originais.

## Verificação e limites

311 checks sintéticos usam a VM e World reais: ambos os scopes, índices0/19
de temp e58/100 de PersonData, zero/negativos/wrap, duas entidades distintas,
nested calls com owners, remapeamento recebido pelo callback, erros sem
mutação e ranges/destinos indiretos do RNG. Fixtures CLI cobrem escrita/leitura
de idade por temp, RNG indireto e snapshots intactos em faults.

Debug e ASan/UBSan:47 testes CTest passaram;1 X11 foi pulado. LeakSanitizer
desativado devido ao ambiente ptrace. JSONs nativos Main/Init coincidem byte
a byte entre os builds. O Init privado retornar true é um gate de execução
ODH sobre BHAV nativo; não é validação do construtor inteiro, do jogo original,
de um lote jogável ou de consoles.
