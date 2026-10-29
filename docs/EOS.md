# Epic Online Services: integração planejada

O EOS foi escolhido para investigação e integração do online, mas **ainda não
está implementado**. O jogo continua usando o transporte experimental existente.
Não anunciar salas, amigos, crossplay ou conectividade EOS como funcionais até
serem testados com o produto e o SDK oficiais.

## Experiência pretendida

- O anfitrião cria uma partida no jogo. Partidas públicas podem ser encontradas
  no jogo sem convite; as privadas usam convite ou entrada por amigos.
- A simulação continua no jogo do anfitrião. O EOS fornece serviços de sessão,
  identificação e comunicação; não converte automaticamente a lógica original
  em uma simulação autoritativa e não implementa rollback para o port.
- O acesso à lista de amigos Epic exige a integração e autenticação da conta
  Epic. O Social Overlay é uma opção no Windows; a documentação da Epic informa
  que ele não está disponível no Linux ou Android. Nessas plataformas, a
  interface correspondente precisará ser própria e validada.
- O SDK existe para Windows, Linux e Android, mas isso não conclui os ports do
  jogo nem comprova crossplay entre eles. Testar as versões e funções exatas
  das plataformas antes de anunciar suporte.

## Preparação pelo responsável pelo projeto

1. No [Epic Developer Portal](https://dev.epicgames.com/portal/), configurar
   organização e produto `RedBaron_Recomp`. Revisar e aceitar pessoalmente os
   acordos que a Epic exigir; não registrar aceites em nome do responsável.
2. Em Downloads & Release Notes → EOS SDK, baixar e extrair o SDK oficial para
   Windows. Manter o pacote local, fora do Git, até revisar sua redistribuição.
3. Configurar o produto, sandbox, deployment e um cliente com política limitada
   aos serviços necessários. Para recursos sociais com contas Epic, configurar
   também Epic Account Services e seguir as exigências de revisão da Epic.
4. Informar ao desenvolvedor apenas o caminho local do SDK. Não enviar senhas,
   tokens nem segredos em issues, conversas, logs ou commits. As credenciais e
   configurações de acesso devem ser tratadas conforme a política do cliente e
   a documentação oficial da Epic.

## Integração e validação

Integrar inicialização, ciclo de chamadas e encerramento do SDK; autenticação;
criação, busca e entrada nas sessões; conexão P2P; e mapeamento das sessões
para as estruturas XAM usadas pelo jogo. A camada `port/network/wire.h` separa
o adaptador de sockets do provedor de rede, mantendo o WebRTC como referência
para testes de regressão. Será necessário adaptar limites de mensagem,
confiabilidade e ciclos de vida do EOS ao protocolo real do jogo.

Testar criação e entrada públicas no menu, salas privadas, até oito jogadores,
limite de dois no co-op, bots, desconexão e retomada, além de conexões diretas
e via relay em redes diferentes. Validar contas Epic e a experiência social
separadamente. Antes de uma release, revisar licenças, redistribuíveis,
políticas de acesso e todos os arquivos incluídos no pacote e no histórico Git.

Referências oficiais: [Lobbies e sessões](https://dev.epicgames.com/docs/epic-online-services/multiplayer/lobbies-and-sessions/lobbies-and-sessions-introduction),
[P2P](https://dev.epicgames.com/docs/epic-online-services/multiplayer/nat-p2p-interface/p2p-guide/p2p-intro),
[Social Overlay e crossplay](https://dev.epicgames.com/docs/epic-online-services/accounts-and-social/crossplay/crossplay-technical-overview),
[configuração de clientes e políticas](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy/client-policy-guide).
