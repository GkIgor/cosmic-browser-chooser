# Escolher navegador para COSMIC

Uma janela simples que aparece ao abrir links HTTP/HTTPS ou arquivos HTML. Ela detecta os navegadores registrados no sistema (inclusive entradas exportadas por Flatpak) e passa o endereço ao navegador escolhido usando GIO. A janela é um executável nativo em C/GTK 4 e roda na sessão COSMIC; Python não participa da abertura dos links.

A janela inicia em 600 × 800 pixels e acompanha a preferência de tema claro/escuro exposta ao GTK pelo sistema. No COSMIC, ela também usa a paleta GTK gerada pelo ambiente em `~/.config/gtk-4.0/`.

Requisitos para compilar: Pop!_OS COSMIC com compilador C, `make`, `pkg-config` e headers GTK 4/GIO. Para instalar/remover: Python 3 e `xdg-mime`. Depois de instalado, o seletor só depende das bibliotecas GTK 4/GIO.

## Usar

```bash
make
./browser-chooser --list-browsers
./browser-chooser 'https://example.org/'
```

Para instalar e tornar o seletor o navegador padrão apenas para o seu usuário:

```bash
./manage.py install
```

Teste após instalar com `xdg-open 'https://example.org/'`. Para verificar o estado, execute `./manage.py status`. Para restaurar os padrões anteriores e remover o seletor instalado, execute `./manage.py uninstall`. O mesmo comando fica disponível em `~/.local/share/browser-chooser/manage.py`, caso você apague esta pasta do projeto.

O instalador salva as associações anteriores em `~/.config/browser-chooser/previous-defaults.json` e registra o seletor em `~/.config/cosmic-mimeapps.list`. Ele não altera as configurações de sistema. O script `manage.py` só roda nas operações de instalação, estado e remoção; não fica residente. Aplicativos que chamam diretamente o executável de um navegador não passam pelo seletor.
