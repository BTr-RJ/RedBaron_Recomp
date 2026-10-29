# Multiplayer local, LAN e salas online experimentais

## Salas online em desenvolvimento

O launcher pode criar uma sala ou entrar usando um código de 16 caracteres. Configure
o endereço em **Online → Room server**; o anfitrião cria a sala, compartilha o código,
inicia o jogo e abre **Online → Player Match → Find Game**. Os demais entram na sala,
abrem o jogo e procuram o mesmo modo. Mantenha o launcher aberto durante a partida.

O serviço de salas e a configuração para Caddy/Coturn estão em [server](../server/README.md).
**Ainda não há servidor público hospedado.** WebRTC estabelece conexões criptografadas
entre anfitrião e participantes, com relay TURN quando configurado. O anfitrião controla
a entrada e retransmite tráfego entre clientes; o jogo conserva seu protocolo original.
Isso não comprova autoridade completa da simulação nem implementa rollback.

Oito instâncias no mesmo PC passaram pelo lobby, terminaram uma rodada de Team Dog Fight
e iniciaram outra no mapa seguinte. Uma tentativa anterior de entrada de vários clientes
durante a partida terminou em desconexões; após corrigir o fechamento de streams, um novo
teste iniciou com dois e recebeu mais seis durante a partida, mantendo os oito após mais
de um minuto. Os testes
de transporte cobrem oito processos, transferência TCP, UDP, saída do anfitrião e rejeição
de certificado inválido. São necessários testes entre computadores, sob CGNAT, com relay
público e em co-op antes de declarar o online pronto. Não há garantia de ausência de lag.

O anfitrião remove uma tentativa que não conclui a conexão em 30 segundos, liberando
a vaga. O teste de integração confirmou que os demais clientes permanecem conectados
e continuam transferindo dados enquanto essa tentativa é descartada.

Online e quatro jogadores locais voltaram a ser a prioridade do usuário em 29/10/2026. **Dois jogadores em tela dividida estão funcionando; quatro locais ainda não.** A primeira partida LAN foi exercitada entre dois processos no mesmo PC, usando o modo original Team DogFight. Isso ainda não comprova conexão entre PCs diferentes ou pela Internet.

## Jogar localmente

Em Controls, atribua controles diferentes aos perfis Player 1 e Player 2. Para controle + teclado, escolha **Exclusive · player 2** em Keyboard and mouse e **No controller** ou **Automatic** no dispositivo de Player 2. Reinicie o jogo, abra **Enlist Now → Local Multiplayer** e pressione A no segundo controle, ou a tecla de Confirm do teclado (Space no mapa padrão).

O teclado reservado para Player 3 ou Player 4 não entra no segundo slot nativo. O launcher preserva escolhas anteriores, inclusive essa reserva; ajuste-a explicitamente. Um teclado/mouse atende apenas um jogador. Movimento de câmera pelo mouse permanece desligado.

**Game Setup → Bots** mantém os bots originais e sua dificuldade. Foram testados dois jogadores locais com quatro bots em Dog Fight: a tela dividida renderizou as duas câmeras e o placar registrou os seis participantes, com pontuação dos bots.

O runtime agora tem quatro identidades locais, consultas XAM indexadas, armazenamento separado e notificações assíncronas de login. O XUID de armazenamento do jogador 1 foi preservado. A identidade LAN é separada dos saves e persistida em `redbaron-network-id.bin` no diretório de usuário; não é uma conta Xbox Live. O nome do launcher altera o nome exibido do jogador 1, sem renomear sua pasta de saves.

Os testes de ingresso usaram o driver de diagnóstico com entradas independentes. Roteamento de teclado/controles, desconexão e reconexão têm testes automatizados. Ainda é necessário validar uma sessão prolongada com dois controles físicos, persistência de progresso dos perfis secundários e vibração. Conquistas dos perfis secundários ainda não persistem; não são gravadas no perfil 1 por engano.

## LAN experimental

1. Use a mesma versão do runtime/jogo em cada instalação. Em Online no launcher, marque **Enable experimental LAN play**, escolha um nome e salve.
2. `255.255.255.255` tenta descobrir salas na rede local. Se necessário, configure o IPv4 do PC anfitrião em **Host IPv4 address** nos PCs que vão entrar.
3. No jogo, abra **Online → Player Match → Find Game**. Escolha o mesmo modo e opções. Uma busca sem resultados cria uma partida pública; os próximos jogadores encontram a sala e o jogo controla o início.

A troca de **Xbox Live** por **Online** altera somente a entrada de localização `XBOX Live[30]`, mantendo o layout e a fonte originais. `scripts/prepare-game.ps1` aplica a mudança aos arquivos locais com backup e sem redistribuí-los. Os outros textos e os modos originais permanecem.

O transporte implementa criação, entrada/saída, busca e detalhes de sessão, contextos/propriedades, descoberta UDP e sondagens QoS, além das correções de sockets exigidas pelo protocolo original. Os tamanhos de buffer de SearchEx/GetDetails na mensagem XGI são valores, não ponteiros; os testes verificam essa ABI e seus limites. A busca do título trata a propriedade de capacidade da party como limite inferior, conforme o comportamento observado no jogo.

O teste entre dois processos percorreu descoberta, conexão, lobby, contagem regressiva e voo, com os dois nomes no mesmo placar. A configuração padrão de interface também criou uma sala pública. Ainda faltam testes em dois PCs, Wi-Fi, perda/latência, queda do anfitrião e sessões longas.

Portas locais usadas: **UDP 43821** para descoberta/QoS e **TCP/UDP 21000–21002** para o tráfego que o jogo originalmente direciona a 1000–1002. A tradução evita conflito com serviços do Windows. Não há alteração automática de firewall, roteador ou UPnP. O transporte não adiciona autenticação ou criptografia: use a prévia em uma rede confiável, sem expor essas portas à Internet.

## O que falta

- Quatro jogadores locais: entradas e perfis existem, mas o fluxo nativo só admitiu dois. É preciso ampliar gerenciamento de jogadores, câmeras/viewports e UI, além de testar os modos. Aumentar apenas um limite de login não resolve.
- Paridade com Xbox Live: convites, amigos, matchmaking pela Internet, serviços de ranking, estatísticas remotas, voz e migração de host não estão completos. A conectividade de partidas privadas/co-op online também não está validada.
- Rollback: ainda não implementado. O jogo usa sua replicação original de rede. O estudo de [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp) mostra captura/restauração de estado específica da engine; o [sm64coopdx](https://github.com/coop-deluxe/sm64coopdx) usa compensação de lag com histórico de jogadores/objetos, que não equivale a rollback completo. PhysX, threads, áudio e efeitos externos deste jogo precisam de uma estratégia própria de determinismo e snapshots.
- Bots foram preservados e testados no modo local. A prévia LAN não muda as regras dos modos públicos para inserir bots onde o jogo original não os oferece.

## Build e validação

O patch autoral `patches/rexglue-local-profiles-lan.patch` é aplicado por `scripts/build-video-sdk.ps1` em uma cópia isolada do SDK. O padrão `redbaron_lan=false` mantém LAN desativada. `redbaron_lan_bind` é uma opção de diagnóstico de interface; não precisa ser alterada no uso normal.

```powershell
cmake -S port -B build_cpp_release -DREDBARON_BUILD_INPUT_TESTS=ON -DREDBARON_BUILD_RUNTIME_TESTS=ON -DREDBARON_INPUT_FEED=OFF
cmake --build build_cpp_release --target snoopyflyingace redbaron_probe redbaron_input_tests redbaron_runtime_tests --parallel 4
ctest --test-dir build_cpp_release --output-on-failure
```

`redbaron_input_routing` verifica distribuição e remapeamento; `redbaron_runtime_services` verifica perfis/identidade de saves, capacidade e estados de sessão, ABI de busca/detalhes, TCP/UDP e descoberta/QoS com pacotes reais, sem XEX. O driver `REDBARON_INPUT_FEED` fica desligado no build normal. Evidências, binários e backups permanecem em `prioridade3_multiplayer/`, fora do Git.
