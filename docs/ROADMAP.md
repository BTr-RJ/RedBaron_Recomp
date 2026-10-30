# Roadmap do RedBaron_Recomp

Atualizado em 30/10/2026. A versão prevista para testes públicos é **0.10**.
O repositório e os instaladores de teste continuam privados.

## Online e serviços Epic

- Provedor EOS dentro do jogo, descoberta de salas públicas, P2P direto/relay,
  login Epic, overlay, amigos e fluxo de convites implementados. O launcher
  mantém apenas o nome online; não precisa ficar aberto durante a partida.
- Dois PCs em redes distintas já chegaram ao lobby e ao voo de Team Dog Fight.
  Abertura de sala/overlay foi verificada em Private Match, Player Match público
  e co-op; co-op conserva duas vagas. Isso não valida partidas completas em todos
  os modos ou o aceite de convite entre duas contas.
- Quick Match foi corrigido a partir da consulta do menu real. Considera modos
  multiplayer compatíveis e escolhe o menor ping medido entre candidatos; busca
  Custom preserva o modo escolhido. Co-op conserva sua categoria própria.
- As 12 conquistas nativas são reconhecidas no produto. Sincronização implementada;
  ainda falta confirmar um novo desbloqueio legítimo e sua restauração em outro PC.
- Ponte de leaderboard e estatísticas implementada no menu original. O EOS
  reconheceu 17/17 definições e respondeu à leitura real de um ranking vazio.
  Pontuação obtida em partida, filtro de amigos e renovação semanal precisam de
  validação. As semanas futuras exigem configuração administrativa.

Veja [EOS](EOS.md) e [leaderboards](LEADERBOARDS.md). Priorizar convite/entrada de
outra conta, rodada até o placar, co-op completo e múltiplos hosts em redes reais.
Oito jogadores reais e reconexão continuam sem validação completa.

## Vídeo e entrada

- ROV, resoluções maiores, filtros, VSync, alvos de FPS com correção de timing,
  contador de FPS e saída HDR10 implementados. HDR converte conteúdo SDR; não é
  iluminação HDR nativa. Preservar configurações pessoais e saves.
- FSR temporal/DLSS permanecem experimentais. A correção atual estabiliza a
  amostragem e os recursos do DLSS; HUD separado e movimento de objetos continuam
  pendentes. Não anunciar ausência total de flickering ou ghosting.
- A imagem de abertura defeituosa é ignorada pelo ponto de entrada direto;
  captura de inicialização confirmou sua ausência. Outros filmes originais de
  abertura e o aviso de carregamento permanecem.
- Teclado, botões do mouse e controle SDL configuráveis; câmera pelo mouse adiada.
  Quatro mapas de entrada não equivalem a quatro câmeras locais. Dois jogadores
  locais foram validados; quatro locais ainda não estão concluídos.

Veja [vídeo](VIDEO.md) e [multiplayer](MULTIPLAYER.md).

## Instalador e publicação

O instalador reúne jogo e DLC opcional numa operação, aceita pasta extraída,
`default.xex` ou STFS, detecta ferramentas, mostra progresso e limpa arquivos
intermediários após sucesso. Existe versão com ferramentas e versão que baixa
somente as ausentes. O usuário confirmou instalação em outro Windows 11; o novo
pacote sem build tools inclui o runtime atualizado e passou no self-test.

Antes de publicar: validar os fluxos acima, resolver acesso social público do EAS
fora da organização, revisar redistribuição e todo o histórico Git. Não incluir
jogo, DLCs, fontes derivados, metadados, saves ou credenciais. AGENTS.md continua
local; sua remoção do histórico público ainda exige auditoria final.

Ports completos para Linux/Android, crossplay desses ports, rollback e quatro
câmeras locais permanecem trabalho futuro. O suporte multiplataforma do SDK EOS
não comprova que esses ports existam.
