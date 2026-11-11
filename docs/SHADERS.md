# Shaders experimentais

O menu Display > Shaders oferece Gauss Scanlines, HQ2, CRT (Hyllian), CRT 3D (Lottes) e ScaleFX, além de None. Os presets usam arquivos da coleção [common-shaders do Libretro](https://github.com/libretro/common-shaders), com as adaptações já usadas no R2NESV2 e no R2BOY; as informações de autoria e licença permanecem nos arquivos de origem.

No Windows a pipeline usa Direct3D 11, com WARP como alternativa quando a GPU não está disponível. O resultado é copiado para uma textura SDL; essa transferência e os múltiplos passes podem afetar o desempenho. O menu fica disponível somente no Windows enquanto não houver backend para Linux.

Os pixels RGBA da apresentação são convertidos para a pipeline sem alterar o quadro do núcleo. O recorte e as scanlines existentes são aplicados antes do shader; a escala e as coordenadas do Light Phaser continuam determinadas pela área apresentada. O Gauss usa o preset de 256 pixels nos consoles de mesa e o de 160 pixels no Game Gear. A saída é opaca, inclusive quando o ScaleFX retorna alfa zero. Os arquivos necessários são copiados para a pasta shaders junto ao executável durante a compilação.

Os testes de pipeline abrangem os cinco presets e as duas larguras do NTSC. O teste do menu verifica os shaders e a transparência nas duas resoluções. A aparência, o desempenho e a compatibilidade com a GPU ainda devem ser avaliados em jogos reais, inclusive em tela cheia.
