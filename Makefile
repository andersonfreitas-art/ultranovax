# Makefile para o driver Ultranova
#
# Comandos:
#   make          - Compila o programa (cria o 'midi_bridge')
#   make install  - Instala tudo. Pedirá sudo para partes do sistema.
#   make uninstall- Remove tudo do sistema. Pedirá sudo.
#   make clean    - Limpa os arquivos compilados.
#

# --- Variáveis ---
TARGET = midi_bridge
SOURCE = midi_bridge.c

# Locais de instalação
PREFIX = /usr/local
BIN_DIR = $(PREFIX)/bin
RULES_DIR = /etc/udev/rules.d
SERVICE_DIR = $(HOME)/.config/systemd/user

# Compilador e flags
CC = gcc
CFLAGS = -Wall -O2
LDLIBS = -lusb-1.0 -lasound

# --- Regras de Compilação ---
all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

clean:
	rm -f $(TARGET)

# --- Regras de Instalação ---
install: all
	@echo "Instalando executável e regra udev (necessário sudo)..."
	sudo install -Dm755 $(TARGET) $(DESTDIR)$(BIN_DIR)/$(TARGET)
	sudo install -Dm644 50-ultranova.rules $(DESTDIR)$(RULES_DIR)/50-ultranova.rules
	
	@echo "Recarregando regras udev..."
	sudo udevadm control --reload-rules
	
	@echo "Instalando serviço systemd (como usuário)..."
	@mkdir -p $(SERVICE_DIR)
	install -Dm644 ultranova.service $(DESTDIR)$(SERVICE_DIR)/ultranova.service
	
	@echo "Recarregando daemon systemd..."
	systemctl --user daemon-reload
	
	@echo "Instalação concluída!"
	@echo "Por favor, desconecte e reconecte seu Ultranova para iniciar o driver."

uninstall:
	@echo "Parando o serviço (se estiver rodando)..."
	-systemctl --user stop ultranova.service
	
	@echo "Removendo executável e regra udev (necessário sudo)..."
	sudo rm -f $(BIN_DIR)/$(TARGET)
	sudo rm -f $(RULES_DIR)/50-ultranova.rules
	
	@echo "Recarregando regras udev..."
	sudo udevadm control --reload-rules
	
	@echo "Removendo serviço systemd (como usuário)..."
	rm -f $(SERVICE_DIR)/ultranova.service
	
	@echo "Recarregando daemon systemd..."
	systemctl --user daemon-reload
	
	@echo "Desinstalação concluída."

.PHONY: all clean install uninstall