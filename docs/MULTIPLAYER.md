# Multiplayer local: investigação e primeira etapa

O roteamento independente de teclado/mouse e controles foi implementado. **Uma partida com dois jogadores ainda não está habilitada nem validada.** A próxima dependência são os perfis locais do runtime.

## Roteamento disponível

`redbaron_keyboard_player` é uma opção do TOML ao lado do EXE e aceita valores de 0 a 4. Precisa reiniciar o jogo. Se a configuração existente não contiver a opção, o valor padrão é 0.

| Valor | Teclado/mouse | Controles físicos |
|---|---|---|
| 0 | Compartilha jogador 1 | Jogadores 1, 2, 3 e 4 |
| 1 | Exclusivo no jogador 1 | Jogadores 2, 3 e 4 |
| 2 | Exclusivo no jogador 2 | Jogadores 1, 3 e 4 |
| 3 | Exclusivo no jogador 3 | Jogadores 1, 2 e 4 |
| 4 | Exclusivo no jogador 4 | Jogadores 1, 2 e 3 |

O limite de quatro entradas pertence ao runtime; não comprova quatro jogadores simultâneos no jogo. Reservar teclado/mouse deixa três entradas para controles, sem duplicar o controle excedente em outro jogador. Com `mnk_mode=false`, a reserva é ignorada. O movimento do mouse continua desligado.

A associação usa os ordinais estáveis fornecidos pelo SDK. Desconectar um controle não promove os outros; uma nova conexão recebe o menor ordinal livre no SDK. O log registra os dispositivos e seus jogadores quando o conjunto de dispositivos muda. A política usa os mesmos drivers, leitura de estado, mistura de comandos e vibração do runtime.

Para jogar normalmente nesta etapa, mantenha **0**. Os valores separados servem à preparação e ao diagnóstico do multiplayer; não criam perfis adicionais.

## Bloqueios encontrados

| Camada | Evidência local | Consequência |
|---|---|---|
| Modo do jogo | Textos de localização identificam missões cooperativas locais para dois participantes | Começar validando dois; investigar quatro separadamente |
| Entrada | SDK `src/input/device_assignment.cpp` e `input_system.cpp` distribuem até quatro slots | O número de dispositivos não equivale ao número de perfis |
| Identidade | `src/kernel/xam/xam_user.cpp`: XUID, nome, estado e informações de login só reconhecem o índice 0 | O segundo controle sozinho não produz um segundo jogador autenticado |
| Configurações de perfil | Leituras e gravações em `xam_user.cpp` restringem usuários adicionais | Implementar o conjunto de APIs de perfil de forma consistente |
| Saves | `src/kernel/xam/xam_content.cpp` usa o XUID do perfil único, inclusive em operações com índice de usuário | Separar armazenamento antes de permitir múltiplos perfis persistentes |
| Sessões e rede | `src/kernel/xam/apps/xgi_app.cpp` contém operações de sessão incompletas; `xam_net.cpp` mantém stubs de chaves XNet | Login ou nome online não tornam o multiplayer de rede funcional |

As consultas foram feitas nos dados locais e no SDK experimental. Nenhum arquivo de jogo, localização ou fonte gerado foi adicionado ao repositório. Não foram alterados os imports XAM nem as DLLs nesta etapa.

## Próxima implementação

1. Criar identidades locais distintas e definir a persistência de cada perfil, mantendo o XUID e os saves atuais do jogador 1.
2. Atender de forma consistente às consultas de login, XUID, nome, leitura/gravação de configurações e conteúdo por usuário; tratar notificações de login e operações assíncronas.
3. Testar ingresso, início e saída de uma missão cooperativa com teclado + controle e depois com dois controles. Conferir divisão de tela, comandos independentes, vibração e preservação de saves.
4. Investigar o limite real de jogadores nos outros modos. Implementar e testar sessões de rede em uma etapa posterior.

## Validação desta etapa

O teste `redbaron_input_routing` verifica modo compartilhado, cada slot reservado, dispositivo excedente, enumeração fora de ordem, desconexão, reconexão, remoção de todos os dispositivos e índices inválidos. Usa cabeçalhos do SDK, sem carregar jogo ou GPU:

```powershell
cmake -S port -B build_cpp_release -DREDBARON_BUILD_INPUT_TESTS=ON
cmake --build build_cpp_release --target redbaron_input_tests --parallel 4
ctest --test-dir build_cpp_release --output-on-failure
```

O build do jogo passou. A abertura foi exercitada com o controle Microsoft conectado nos modos compartilhado, teclado reservado e teclado desabilitado. Logs e backups ficam em `prioridade3_multiplayer/`, fora do Git. Isso valida a inicialização e o roteamento observado; não substitui uma partida local ou testes físicos com vários controles.
