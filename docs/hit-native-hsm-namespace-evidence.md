# Namespace HSM nativo

Investigação estática de 2026-10-17 sobre `../referencias/The Sims/Sims.exe`,
SHA256 `6e598c22512a24659cbd0e533306abf3129733d67b1d120916b1b6b1581e29f2`.
O recibo reproduzível ignorado pelo Git está em
registro arquivado não distribuído, com `collect.py`, manifesto
PE, hashes e 18 recortes pontuais de assembly. O executável não foi executado
nem modificado.

O loader `0x57d3bd` insere as linhas HSM na tabela `manager+0xb4`; o parser
HOT resolve `@label` por `0x57d4ab` contra a mesma tabela. O token HSM e o
trecho após `@` são copiados sem conversão de caixa. Hash e igualdade usam
bytes exatos. Quando o nome já existe, o loader sobrescreve seu valor, de
modo que a última linha processada vence entre arquivos e dentro deles. A
resolução de `@label` ocorre durante o parse HOT e guarda a chave numérica
resultante no Track. A tabela e suas cópias de nomes pertencem ao manager.
Um HSM que falhe ao abrir deixa símbolos anteriores na tabela nesse caminho,
pois o caller não testa o retorno antes de prosseguir para HIT/HOT. Falhas de
entrada não definem uma política segura para ODH.

ODH expõe essa regra apenas com `HsmReadProfile::native_namespace` e
`HotReadProfile::native_entry_map`: nomes HSM preservam a grafia, duplicatas
exatas são substituídas e `@` exige busca HSM exata, inclusive se o nome
contiver só dígitos. CSV7 sem `@` pode ser chave numérica. Os perfis padrão
mantêm a política Simitone-local de nomes HSM ASCII-folded e primeira
ocorrência. O banco de recursos deve acumular símbolos em ordem de grupos
fornecida pelo caller e manter a identidade do HIT junto aos destinos
TrackData, conforme [hit-native-entry-map-evidence.md](hit-native-entry-map-evidence.md).

Os testes de recursos cobrem grafias distintas, duplicata exata, `@` ausente,
`@` com nome numérico, CSV7 numérico sem `@`, perfil inválido antes da leitura
e regressão do perfil padrão. Passaram 106 verificações em Linux Debug. Isto
não é prova de execução original nem determina a ordem de enumeração Win32.
