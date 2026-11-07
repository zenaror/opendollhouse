# Migração dos sistemas TS1

O motor é C/C++. Simitone e a revisão correspondente do FreeSO são referências,
com as [origens e licenças](../references.md) preservadas. Código exclusivo de
The Sims Online não define automaticamente o comportamento desejado para TS1.
Não se mantém outro motor gerenciado como produto paralelo.

## Relação entre os sistemas

| Referência | Área nativa | Situação |
| --- | --- | --- |
| SimAntics, primitivas e scheduler | VM, scripts, filas e ciclo dos objetos em `src/` | Parcial, com consumidores no aplicativo. |
| TS1Platform e providers | Famílias, recursos, necessidades e destinos | Parcial; viagens e progresso pessoal exigem cobertura integrada. |
| Arquitetura e roteamento | Geometria preparada, cômodos, posicionamento e circulação | Parcial; compra/construção completas permanecem abertas. |
| Marshals e estado de sessão | Codecs, restauração e serviços de sessão | Parcial; não confundir uma reabertura com todos os estados suportados. |
| Vitaboy e mundo | Aparência, poses, animação e composição | Parcial; apresentação completa ainda não aceita. |
| HIT e áudio TS1 | Controle de áudio, decodificação e reprodução | Parcial; backend e uso real precisam de verificação própria. |
| Interface e aplicativo Simitone | Navegação e consumidores nativos em `tools/` | Parcial; aparência e comportamento seguem o original. |

A existência de uma classe equivalente não prova migração completa. Portar um
serviço inclui regras, dependências, consumidores, estado e uso no aplicativo.
Os [contratos](../engine-contracts.md) explicam decisões duradouras; o
[plano](../native-port-plan.md) define entregas jogáveis, sem uma fila por função.
Lacunas da referência que impeçam fidelidade exigem investigação delimitada dos
dados ou do original, sem inventar resultados.
