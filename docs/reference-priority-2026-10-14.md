# Prioridade de referência: Simitone / The Sims Complete Collection

Decisão humana de 2026-10-14: priorizar Simitone para o alvo The Sims Complete
Collection. A ordem de consulta é frontend Simitone, núcleo FreeSO fixado por
ele e caminhos TS1 ativados nessa combinação. O FreeSO principal fica como
referência secundária. O comportamento original continua sendo o critério de
fidelidade; bugs, adaptações e limitações do Simitone não se tornam requisitos
do ODH por serem encontrados nessa referência.

Revisões locais verificadas, sem atualizar ou executar os projetos:

- Simitone: `4906db3156616bfaffc10ce0963cf08062975fef`.
- Seu submódulo FreeSO: `f86bbd34b2112296ed786741f640a0535886d180`.
- FreeSO principal comparativo: `4c6b3e8f5835b228723caea3c9f683c62f244f73`.

O README local do Simitone identifica o alvo The Sims1 / Complete Collection.
No frontend, `Client/Simitone/Simitone.Client/SimitoneGame.cs:131–132` define
`Content.Target = FSOEngineMode.TS1` e o caminho da instalação TS1. Na mesma
classe, linha157 inicializa `HITVM` e linha299 avança essa VM no update. Esses
call sites são a rota inicial para a próxima investigação de áudio; não se
infere daí execução, compatibilidade Linux ou sincronização completa do ODH.

O arquivo `TSOClient/tso.sound/HITInterpreter.cs` do submódulo foi comparado
byte a byte com o do FreeSO principal. Ambos têm SHA256
`51268c1743ed7b736f46451d6330c07d3b1a94cce9dcf59df97d9c01bd21b748`.
Portanto, a heurística de Jump identificada no checkpoint147051c também existe
nesse arquivo da revisão usada pelo Simitone. A mudança de prioridade não
resolve sozinha a divergência com o `Sims.exe` distinto. Isso não demonstra
igualdade dos runtimes completos ou dos demais arquivos.

Os perfis implementados e os documentos históricos mantêm sua proveniência
FreeSO real. Novos contratos serão consultados na combinação Simitone/TS1;
diferenças relevantes serão comparadas e verificadas antes de mudar o código.
A alteração deste checkpoint é de orientação e documentação, sem mudanças
de runtime. Verificação: revisões Git locais, README/call sites, `cmp`, hashes
e `git diff --check`. Não foi necessário repetir testes de código.
