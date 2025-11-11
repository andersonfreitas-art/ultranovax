# Driver Plug-and-Play para Novation Ultranova no Linux

Este projeto é um "driver" de espaço de usuário simples que torna o sintetizador Novation Ultranova (ID USB `1235:0011`) totalmente funcional no Linux (PipeWire/ALSA).

O Ultranova não é "Class Compliant", então ele não funciona nativamente no Linux. Este driver lê os dados MIDI brutos do dispositivo usando `libusb` e os encaminha para uma porta MIDI virtual criada com `libasound`, tornando-o visível para qualquer DAW ou software de música.

O driver é composto por:
* `midi_bridge`: O programa em C que faz a ponte.
* `50-ultranova.rules`: Uma regra `udev` para dar permissões e acionar o driver.
* `ultranova.service`: Um serviço `systemd` que inicia o driver automaticamente.

## Pré-requisitos

Você precisará das bibliotecas de desenvolvimento para compilar o driver.

**No Arch Linux (e derivados):**
```bash
sudo pacman -S base-devel libusb alsa-lib
```

**No Debian/Ubuntu (e derivados):**
```bash
sudo apt install build-essential libusb-1.0-0-dev libasound2-dev
```

## Instalação

A instalação é automatizada com o `Makefile`.

1.  **Clone este repositório:**
    ```bash
    git clone [https://github.com/SEU-USUARIO/SEU-REPOSITORIO.git](https://github.com/SEU-USUARIO/SEU-REPOSITORIO.git)
    cd SEU-REPOSITORIO
    ```

2.  **Compile:**
    ```bash
    make
    ```

3.  **Instale:**
    ```bash
    make install
    ```
    *(O script pedirá sua senha `sudo` para instalar os arquivos de sistema, como a regra `udev`.)*

4.  **Pronto!**
    Desconecte e reconecte seu Ultranova. O driver será iniciado automaticamente.

    Você pode verificar se ele está funcionando abrindo um terminal e digitando:
    ```bash
    aconnect -l
    ```
    Você deve ver uma nova porta chamada "Ultranova Driver". Seu DAW agora também verá esta porta.

## Desinstalação

Para remover completamente o driver:
```bash
make uninstall
```
*(Isso também pedirá sua senha `sudo` para remover os arquivos do sistema.)*

## Como Funciona

1.  Uma **regra `udev`** (`50-ultranova.rules`) detecta o Ultranova (ID `1235:0011`) quando ele é conectado.
2.  A regra concede permissões de usuário (`uaccess`) e aciona o **serviço `systemd`** (`ultranova.service`).
3.  O serviço systemd inicia o programa **`midi_bridge`**.
4.  O `midi_bridge` usa `libusb` para "reivindicar" (claim) o dispositivo e começa a escutar no Endpoint `0x83` (IN).
5.  Ele usa `libasound` para criar uma porta MIDI virtual no ALSA chamada "Ultranova".
6.  Quando dados MIDI chegam pela USB (possivelmente em pacotes fragmentados), o programa os analisa, remonta e os envia para a porta ALSA virtual, onde qualquer aplicativo (DAW, PipeWire) pode lê-los.