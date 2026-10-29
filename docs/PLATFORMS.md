# Linux, Android e crossplay

O pedido de ports Linux e Android está em investigação. O projeto atual entrega
somente Windows; não há builds jogáveis nem crossplay validados para as outras
plataformas. A primeira versão pública de testes pretendida é 0.10, após cumprir
e testar o escopo solicitado.

O [ReXGlue](https://github.com/rexglue/rexglue-sdk/wiki/Getting-Started) oferece
configuração de build Linux e o [libdatachannel](https://github.com/paullouisageneau/libdatachannel)
suporta Linux e Android. Isso não torna automaticamente portáveis as adaptações
locais: o transporte usa WinHTTP e Winsock, o launcher é WPF e partes do vídeo,
instalador e ferramentas auxiliares dependem de Windows. O SDK modificado precisa
ser compilado e testado com Vulkan no Linux. Android exige também integração com
ciclo de vida, armazenamento, entrada e renderização móvel; não existe build
Android neste repositório.

Crossplay deve manter o mesmo protocolo e regras da sessão em todas as plataformas,
com arquivos do jogo fornecidos pelo jogador em cada dispositivo. Sua validação
exige clientes reais nas plataformas participantes, além de uma sala e relay
disponíveis pela Internet. Este computador de desenvolvimento não tem WSL
instalado, e a ferramenta ADB não foi encontrada para testar um dispositivo Android.
