# Roadmap do RedBaron_Recomp

## Decisões atuais

**Prioridade atual: vídeo/GPU do launcher.** Em 28/10/2026, o usuário adiou multiplayer para quatro jogadores e online. O launcher simples em tema escuro foi implementado com Install, Video, Controls e Online, Save/Play e abertura direta. Ver [funções, testes e limites](../launcher/README.md).

Preservar o gameplay original: o movimento do mouse não deve controlar câmera ou voo neste momento. Teclado, cliques e controles físicos devem coexistir. O layout atual serve como referência; os comandos são configuráveis no launcher.

O Xbox Series do usuário é o controle de referência. A compatibilidade é ampliada pelo SDL e pela base de mapeamentos, com confirmação por modelo conforme os testes.

## Sequência

1. **Licença e execução:** desbloqueio confirmado no menu e renderização normal com ROV. Continuar a validação de conteúdo e estabilidade da campanha.
2. **Entrada:** validar o layout no voo, uso simultâneo com controle, vibração, reconexão e outros modelos. Manter câmera pelo mouse desligada.
3. **Multiplayer (adiado):** roteamento separado de teclado e controles implementado, mantendo o padrão compartilhado. O SDK só reconhece um perfil e compartilha sua identidade nas operações de saves; implementar perfis e armazenamento por usuário antes de validar o co-op de dois jogadores identificado nos textos locais. Quatro jogadores e online dependem da retomada desta etapa. Ver [diagnóstico e validação](MULTIPLAYER.md).
4. **Vídeo (prioridade atual):** FSR 1/CAS, VSync do monitor, resolução física até 8K, escala interna e filtros implementados. Validar missões em resoluções maiores; concluir FPS com tempo de simulação correto e integrar DLSS. Ver [implementação e medições](VIDEO.md).
5. **Launcher com instalador embutido (prioridade promovida):** versão inicial implementada. Próximos passos: validação do usuário, extração de pacotes locais, preparação de dependências e futuras abas Som/Conquistas.
6. **Splash screens:** corrigir o pipeline 2D de apresentação das imagens de abertura.

## Launcher: estado e evolução

| Aba | Escopo previsto |
|---|---|
| Vídeo / GPU | Adaptador, backend, resolução, janela/tela cheia, proporção, VSync, limite de FPS, filtros e opções disponíveis |
| Controles | Dispositivos, mapeamento de teclado/mouse e gamepad, perfis, zonas mortas e teste de entradas |
| Som | Dispositivo de saída e volumes suportados |
| Conquistas | Lista, descrições e progresso local do runtime; serviços externos dependem de integração futura |
| Online / perfil | Nome online, perfil local e conexão; recursos de rede dependem da implementação do multiplayer |
| Geral | Caminho dos dados, iniciar jogo, restaurar padrões e acesso aos logs |
| Instalação | Selecionar cópia local do jogo, verificar arquivos, extrair/importar os dados e preparar os componentes necessários localmente |

O launcher preserva preferências, oferece restauração do mapa e usa o TOML atual como ponto de partida. Nome online fica reservado no JSON, sem conexão aos serviços originais do Xbox Live. UPnP está desabilitado. FSR 1/CAS disponíveis no runtime atualizado. DLSS, FSR temporal e desbloqueio de FPS continuam pendentes. A janela do launcher agora centraliza, ajusta-se à área útil e oferece Fullscreen/Restore e Close.

O repositório poderá tornar-se público posteriormente, sem arquivos do jogo nem código gerado. O instalador já usa uma pasta extraída fornecida pelo usuário e compila em destino separado. Extração de pacotes e instalação das ferramentas ainda não foram implementadas.

Interface nativa Windows em WPF/.NET Framework 4.8. Os quatro mapas de entrada não criam perfis adicionais no jogo. O estado jogável atual continua sendo um jogador.

O usuário autorizou empacotar e publicar o launcher em Releases quando o instalador estiver completo e validado. Essa condição ainda não foi atendida; nenhuma Release oficial foi publicada nesta etapa.
