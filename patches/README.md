# Patches do ReXGlue

Atualizações incrementais de 30/10/2026, aplicadas pelo build isolado:

- `rexglue-eos-matchmaking.patch`, após o social: consulta Quick Match observada
  no menu e preservação de filtros de versão/conteúdo, sem igualdade de rank.
- `rexglue-native-leaderboards.patch`, após matchmaking: enumerador de ranking,
  ABI de resultados e encaminhamento das estatísticas ao provedor do port.
- `rexglue-temporal-stability.patch`, quando FSR temporal/DLSS está incluído:
  estabilidade de recursos/histórico, escala do jitter e amostragem estável no
  DLSS enquanto o HUD ainda está composto na imagem de entrada.

Os patches incrementais foram revertidos/reaplicados em cópia de teste e
reproduziram o texto dos arquivos atuais. Veja [EOS](../docs/EOS.md),
[rankings](../docs/LEADERBOARDS.md) e [vídeo](../docs/VIDEO.md) para limites.

O patch de vídeo `rexglue-spatial-fsr-vsync.patch` habilita FSR 1/CAS espaciais,
VSync do monitor e tamanho físico da janela. Use `scripts/build-video-sdk.ps1`
para compilar uma cópia isolada, sem modificar o SDK de origem. Consulte
[configuração, testes e limitações](../docs/VIDEO.md). Esse patch é separado
da correção de codegen abaixo.

`rexglue-snoopy-fallback-v2.patch` registra o diff atual de `src/codegen/builders/context.cpp` no SDK experimental utilizado.

`rexglue-codegen-repro.patch` contém todas as nove alterações de codegen do SDK
experimental sobre o commit indicado abaixo, incluindo o fallback acima. O script
`build-video-sdk.ps1` aplica o conjunto automaticamente em uma cópia isolada ou
confere que já está aplicado. Não aplique os dois patches de codegen juntos.

- Commit base do SDK: `7236893039fac7e6ce8a21351a430933899f9320`.
- Inclui resolução de destinos mesclados e fallback restrito aos três saltos cross-chunk verificados.
- Preserva saltos locais e exige as condições de agrupamento e exceções do patch.

Os locais corrigidos são `0x82362B18`, `0x82362BB4` e `0x82362C20`. A validação histórica registrou três fallbacks aplicados, zero ignorados e zero labels ausentes restantes.

Em um checkout compatível, confira primeiro:

```powershell
git -C CAMINHO_DO_SDK apply --check CAMINHO_DO_PROJETO/patches/rexglue-snoopy-fallback-v2.patch
```

Faça backup antes de aplicar. Se o teste passar, aplique o patch, recompile e instale o SDK conforme as instruções dessa versão. O SDK experimental desta máquina já contém o ajuste; não o aplique novamente nele.

Este patch é específico para os agrupamentos e endereços do XEX utilizado. Não é uma correção genérica para outros jogos ou versões. Os avisos e a licença do SDK original continuam aplicáveis.
