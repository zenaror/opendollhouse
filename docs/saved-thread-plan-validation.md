# Plano de leitura das pilhas salvas

`odh-saved-world` agora mostra como os frames de cada personagem salvo podem
ser representados seguindo a conversão do ativador TS1 do Simitone. O campo
`simitone_thread_plan` é diagnóstico: `executable` permanece `false`, e a
resposta geral continua com `saved_threads_restored:false`.

Para cada frame, o plano resolve o índice de tipo do dono pela tabela OBJT e
retém o GUID, árvore, instrução, StackObject, parâmetros, locais e estado
primitivo bruto. Parâmetros são completados até quatro palavras, e os oito
registradores salvos são colocados nos primeiros oito de um banco de vinte;
os demais começam em zero. O dono e o chamado começam como o próprio Sim.
`ObjectUses` troca o chamado dos frames a partir da profundidade indicada,
como faz o adaptador do Simitone. Um dono sem GUID e uma profundidade inválida
fazem o plano falhar, em vez de inventar um valor.

## Referência e limites

Consulta local ao submódulo Simitone fixado em
`f86bbd34b2112296ed786741f640a0535886d180`:
`refs/upstream/Simitone/FreeSO/TSOClient/tso.simantics/Utils/VMTS1ActivatorNew.cs`,
`ConvertThread` (linhas 212–323). A rotina usa o índice OBJT para obter o GUID,
faz padding dos parâmetros, cria fila/frames e reconstrói parte dos chamados
com `ObjectUses`. As notas dessa implementação marcam essas regras como
adaptações próprias do FreeSO/Simitone. Elas orientam o plano, mas não provam
que o executável original usa os mesmos detalhes.

O plano ainda não verifica se a rotina e o endereço existem nos arquivos BHAV
carregados, não restaura o estado especial de retorno entre frames e não
retoma o relógio, a fila de interações ou os serviços de rota. O Simitone tem
regras adicionais para esses casos. Portanto esta etapa prepara e exibe dados;
não é continuação da partida nem teste de gameplay.

## House07 da instalação local

Com `../referencias/The Sims/UserData/Houses/House07.iff`, `Objects.far` e os
dois recursos de personagem externos, o leitor restaurou estruturalmente 182
entidades e gerou:

| Sim salvo | ID | Quadros | Dono de rotina | Fila salva |
|---|---:|---:|---|---|
| Bob | 105 | 6 | OBJT type 81, GUID 850010198 | vazia |
| membro47 | 137 | 6 | OBJT type 82, GUID 844195932 | vazia |

Cada pilha contém rotinas 4096, 8193, 8283, 8227, 8216 e 8222, com endereços
salvos preservados; nenhum `ObjectUses` estava presente. Os dois Sims mantêm
registradores salvos zerados. O resultado real é uma inspeção do save e dos
recursos comerciais, não uma execução do original ou do Simitone.

Reprodução a partir do checkout, sem escrever nos dados comerciais:

```sh
cmake -S . -B /tmp/odh-saved-thread-plan-build -DBUILD_TESTING=ON
cmake --build /tmp/odh-saved-thread-plan-build --target odh-saved-world odh-saved-thread-plan-tests -j2
ctest --test-dir /tmp/odh-saved-thread-plan-build -R saved_thread_restore_plan --output-on-failure
/tmp/odh-saved-thread-plan-build/odh-saved-world \
  '../referencias/The Sims/UserData/Houses/House07.iff' \
  --object-definitions '../referencias/The Sims/GameData/Objects/Objects.far' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00000.iff' \
  --object-definition-file '../referencias/The Sims/UserData/Characters/User00001.iff'
```

O teste sintético cobre resolução do dono, padding, `ObjectUses` e recusa de
índices, IDs ou GUIDs inválidos. A build Linux limpa em `/tmp` concluiu, e
CTest passou **164/164**; `linux_window_backend` foi pulado por falta de
display. A conferência do diff (`git diff --check`) também passou. Esses testes
não executam as pilhas salvas nem provam gameplay.
